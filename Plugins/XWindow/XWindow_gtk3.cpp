// XWindow_gtk3.cpp
// CrossBasic XWindow plugin - GTK3 host implementation (Linux).
// Builds a top-level GtkWindow with a GtkBox (for optional menubar) and a GtkFixed content host.
// Other GTK-based plugins can embed by calling XWindow_GetNativeHandle(handle) to retrieve the GtkFixed*.
//
// Build (Linux, GTK3):
//   g++ -shared -fPIC -O2 -o libXWindow.so XWindow_gtk3.cpp -pthread -ldl `pkg-config --cflags --libs gtk+-3.0`
//
// Notes:
// - GTK runs in its own thread; all GTK operations are marshalled onto that thread synchronously.
// - Events are delivered via XWindow_SetEventCallback(handle, eventName, callbackPtr) with a JSON payload string.
//

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
  #error "This file is Linux-only. Use XWindow_windows.cpp for Windows."
#endif

#if !defined(__linux__)
  #error "This file targets __linux__ with GTK3."
#endif

#include <gtk/gtk.h>

// ------------------------- CrossBasic plugin ABI types -------------------------
#ifndef XPLUGIN_API
  #define XPLUGIN_API extern "C" __attribute__((visibility("default")))
#endif

// A very small ABI subset used by CrossBasic for reflection.
// These match the pattern used by your other plugins.
struct FunctionDefinition {
  const char* name;
  void* fn_ptr;
  int arg_count;
  const char* arg_types[8];
  const char* return_type;
};

struct PropertyDefinition {
  const char* name;
  const char* type;
  void* getter;
  void* setter;
};

struct ClassDefinition {
  const char* name;
  void* constructor;
  void* destructor;
  FunctionDefinition* methods;
  int method_count;
  PropertyDefinition* properties;
  int property_count;
};

// ------------------------------ Event plumbing --------------------------------
static std::mutex g_eventMutex;
static std::unordered_map<int, std::unordered_map<std::string, void(*)(const char*)>> g_eventCallbacks;

static void triggerEvent(int h, const std::string& evt, const std::string& jsonPayload) {
  void(*cb)(const char*) = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    auto itH = g_eventCallbacks.find(h);
    if (itH != g_eventCallbacks.end()) {
      auto itE = itH->second.find(evt);
      if (itE != itH->second.end()) cb = itE->second;
    }
  }
  if (!cb) return;

  char* payload = (char*)malloc(jsonPayload.size() + 1);
  if (!payload) return;
  std::memcpy(payload, jsonPayload.c_str(), jsonPayload.size() + 1);
  cb(payload);
  free(payload);
}

XPLUGIN_API bool XWindow_SetEventCallback(int h, const char* eventName, void* cbPtr) {
  if (!eventName) return false;
  auto cb = (void(*)(const char*))cbPtr;
  if (!cb) return false;
  std::lock_guard<std::mutex> lock(g_eventMutex);
  g_eventCallbacks[h][eventName] = cb;
  return true;
}

// ---------------------------- GTK thread marshal ------------------------------
static std::once_flag g_gtkOnce;
static std::thread g_gtkThread;
static std::atomic<bool> g_gtkReady{false};
static GMainContext* g_gtkCtx = nullptr;

static void ensureGtkThread() {
  std::call_once(g_gtkOnce, [](){
    g_gtkThread = std::thread([](){
      int argc = 0;
      char** argv = nullptr;
      gtk_init(&argc, &argv);
      g_gtkCtx = g_main_context_default();
      g_gtkReady.store(true);
      gtk_main();
    });
    // Detach so the process can exit cleanly without join.
    g_gtkThread.detach();
  });

  while (!g_gtkReady.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

template <class Fn>
auto gtkSync(Fn&& fn) -> decltype(fn()) {
  ensureGtkThread();
  using R = decltype(fn());

  // If we're already on the GTK thread, run directly.
  if (g_gtkCtx && g_main_context_is_owner(g_gtkCtx)) {
    return fn();
  }

  std::promise<R> prom;
  auto fut = prom.get_future();

  struct Payload {
    std::promise<R> prom;
    std::function<R()> fn;
  };

  auto* p = new Payload{std::move(prom), std::function<R()>(std::forward<Fn>(fn))};

  g_main_context_invoke(g_gtkCtx, [](gpointer data) -> gboolean {
    auto* p = static_cast<Payload*>(data);
    try {
      if constexpr (std::is_void<R>::value) {
        p->fn();
        p->prom.set_value();
      } else {
        p->prom.set_value(p->fn());
      }
    } catch (...) {
      // best effort: return default
      if constexpr (!std::is_void<R>::value) {
        p->prom.set_value(R());
      } else {
        p->prom.set_value();
      }
    }
    delete p;
    return G_SOURCE_REMOVE;
  }, p);

  return fut.get();
}

// ------------------------------- XWindow model --------------------------------
struct XWindow {
  int handle = 0;

  GtkWidget* window = nullptr;     // GtkWindow
  GtkWidget* vbox = nullptr;       // GtkBox (vertical)
  GtkWidget* fixed = nullptr;      // GtkFixed (content host)
  GtkWidget* menuBar = nullptr;    // GtkMenuBar (optional)

  int left = 100;
  int top = 100;
  int width = 800;
  int height = 600;
  bool visible = false;
  bool resizable = true;
  bool decorated = true;
  std::string title = "XWindow";
};

static std::mutex g_winMutex;
static std::unordered_map<int, std::unique_ptr<XWindow>> g_windows;
static std::atomic<int> g_nextHandle{1};

static XWindow* getWin(int h) {
  std::lock_guard<std::mutex> lock(g_winMutex);
  auto it = g_windows.find(h);
  return (it == g_windows.end()) ? nullptr : it->second.get();
}

static void withWin(int h, const std::function<void(XWindow&)>& fn) {
  std::unique_ptr<XWindow>* ptr = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_winMutex);
    auto it = g_windows.find(h);
    if (it == g_windows.end()) return;
    ptr = &it->second;
  }
  if (!ptr || !ptr->get()) return;
  fn(*ptr->get());
}

static gboolean on_delete_event(GtkWidget*, GdkEvent*, gpointer user_data) {
  int h = (int)(intptr_t)user_data;
  triggerEvent(h, "Closing", std::string("{\"handle\":") + std::to_string(h) + "}");
  // Returning FALSE allows the window to be destroyed.
  return FALSE;
}

static void on_destroy(GtkWidget*, gpointer user_data) {
  int h = (int)(intptr_t)user_data;
  triggerEvent(h, "Closed", std::string("{\"handle\":") + std::to_string(h) + "}");
  // Remove instance from map
  std::lock_guard<std::mutex> lock(g_winMutex);
  g_windows.erase(h);
}

// ------------------------------- Core API -------------------------------------
XPLUGIN_API int XWindow_Create() {
  int h = g_nextHandle.fetch_add(1);

  auto winObj = std::make_unique<XWindow>();
  winObj->handle = h;

  gtkSync([&](){
    winObj->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    winObj->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    winObj->fixed = gtk_fixed_new();

    gtk_container_add(GTK_CONTAINER(winObj->window), winObj->vbox);
    gtk_box_pack_end(GTK_BOX(winObj->vbox), winObj->fixed, TRUE, TRUE, 0);

    gtk_window_set_title(GTK_WINDOW(winObj->window), winObj->title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(winObj->window), winObj->width, winObj->height);
    gtk_window_move(GTK_WINDOW(winObj->window), winObj->left, winObj->top);
    gtk_window_set_resizable(GTK_WINDOW(winObj->window), winObj->resizable ? TRUE : FALSE);
    gtk_window_set_decorated(GTK_WINDOW(winObj->window), winObj->decorated ? TRUE : FALSE);

    g_signal_connect(G_OBJECT(winObj->window), "delete-event", G_CALLBACK(on_delete_event), (gpointer)(intptr_t)h);
    g_signal_connect(G_OBJECT(winObj->window), "destroy", G_CALLBACK(on_destroy), (gpointer)(intptr_t)h);
  });

  {
    std::lock_guard<std::mutex> lock(g_winMutex);
    g_windows[h] = std::move(winObj);
  }

  triggerEvent(h, "Opening", std::string("{\"handle\":") + std::to_string(h) + "}");
  return h;
}

XPLUGIN_API void XWindow_Destroy(int h) {
  gtkSync([&](){
    XWindow* w = getWin(h);
    if (!w) return;
    if (w->window) {
      gtk_widget_destroy(w->window);
      w->window = nullptr;
      w->vbox = nullptr;
      w->fixed = nullptr;
      w->menuBar = nullptr;
    }
  });
}

XPLUGIN_API void XWindow_Show(int h) {
  gtkSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    gtk_widget_show_all(w->window);
    w->visible = true;
  });
}

XPLUGIN_API void XWindow_Hide(int h) {
  gtkSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    gtk_widget_hide(w->window);
    w->visible = false;
  });
}

XPLUGIN_API void XWindow_Close(int h) {
  gtkSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    gtk_widget_destroy(w->window);
  });
}

XPLUGIN_API int XWindow_Left_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->left : 0;
}
XPLUGIN_API void XWindow_Left_SET(int h, int v) {
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.left = v;
      if (w.window) gtk_window_move(GTK_WINDOW(w.window), w.left, w.top);
    });
  });
}

XPLUGIN_API int XWindow_Top_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->top : 0;
}
XPLUGIN_API void XWindow_Top_SET(int h, int v) {
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.top = v;
      if (w.window) gtk_window_move(GTK_WINDOW(w.window), w.left, w.top);
    });
  });
}

XPLUGIN_API int XWindow_Width_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->width : 0;
}
XPLUGIN_API void XWindow_Width_SET(int h, int v) {
  if (v < 1) v = 1;
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.width = v;
      if (w.window) gtk_window_resize(GTK_WINDOW(w.window), w.width, w.height);
    });
  });
}

XPLUGIN_API int XWindow_Height_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->height : 0;
}
XPLUGIN_API void XWindow_Height_SET(int h, int v) {
  if (v < 1) v = 1;
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.height = v;
      if (w.window) gtk_window_resize(GTK_WINDOW(w.window), w.width, w.height);
    });
  });
}

XPLUGIN_API bool XWindow_Visible_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->visible : false;
}
XPLUGIN_API void XWindow_Visible_SET(int h, bool v) {
  if (v) XWindow_Show(h);
  else XWindow_Hide(h);
}

XPLUGIN_API const char* XWindow_Title_GET(int h) {
  static thread_local std::string tmp;
  XWindow* w = getWin(h);
  tmp = w ? w->title : std::string();
  return tmp.c_str();
}
XPLUGIN_API void XWindow_Title_SET(int h, const char* t) {
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.title = (t ? t : "");
      if (w.window) gtk_window_set_title(GTK_WINDOW(w.window), w.title.c_str());
    });
  });
}

XPLUGIN_API bool XWindow_Resizable_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->resizable : true;
}
XPLUGIN_API void XWindow_Resizable_SET(int h, bool v) {
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.resizable = v;
      if (w.window) gtk_window_set_resizable(GTK_WINDOW(w.window), v ? TRUE : FALSE);
    });
  });
}

XPLUGIN_API bool XWindow_HasTitleBar_GET(int h) {
  XWindow* w = getWin(h);
  return w ? w->decorated : true;
}
XPLUGIN_API void XWindow_HasTitleBar_SET(int h, bool v) {
  gtkSync([&](){
    withWin(h, [&](XWindow& w){
      w.decorated = v;
      if (w.window) gtk_window_set_decorated(GTK_WINDOW(w.window), v ? TRUE : FALSE);
    });
  });
}

// The critical function: returns the native host where child widgets should be placed.
XPLUGIN_API void* XWindow_GetNativeHandle(int h) {
  XWindow* w = getWin(h);
  return w ? (void*)w->fixed : nullptr;
}

// Optional helper for menu bar plugin: attach/replace a GtkMenuBar.
XPLUGIN_API bool XWindow_SetMenuBarNative(int h, void* menuBarWidget) {
  return gtkSync([&]() -> bool {
    XWindow* w = getWin(h);
    if (!w || !w->vbox || !w->window) return false;

    GtkWidget* mb = (GtkWidget*)menuBarWidget;
    if (!mb) return false;

    if (w->menuBar) {
      gtk_container_remove(GTK_CONTAINER(w->vbox), w->menuBar);
      w->menuBar = nullptr;
    }

    w->menuBar = mb;
    gtk_box_pack_start(GTK_BOX(w->vbox), w->menuBar, FALSE, FALSE, 0);
    gtk_widget_show_all(w->window);
    return true;
  });
}

// ------------------------- Reflection (minimal subset) -------------------------
static FunctionDefinition g_functions[] = {
  { "XWindow_Create", (void*)XWindow_Create, 0, {0}, "integer" },
  { "XWindow_Destroy", (void*)XWindow_Destroy, 1, {"integer"}, "void" },
  { "XWindow_Show", (void*)XWindow_Show, 1, {"integer"}, "void" },
  { "XWindow_Hide", (void*)XWindow_Hide, 1, {"integer"}, "void" },
  { "XWindow_Close", (void*)XWindow_Close, 1, {"integer"}, "void" },
  { "XWindow_GetNativeHandle", (void*)XWindow_GetNativeHandle, 1, {"integer"}, "pointer" },
  { "XWindow_SetMenuBarNative", (void*)XWindow_SetMenuBarNative, 2, {"integer","pointer"}, "boolean" },
  { "XWindow_SetEventCallback", (void*)XWindow_SetEventCallback, 3, {"integer","string","pointer"}, "boolean" },
};

static PropertyDefinition g_props[] = {
  { "Left", "integer", (void*)XWindow_Left_GET, (void*)XWindow_Left_SET },
  { "Top", "integer", (void*)XWindow_Top_GET, (void*)XWindow_Top_SET },
  { "Width", "integer", (void*)XWindow_Width_GET, (void*)XWindow_Width_SET },
  { "Height", "integer", (void*)XWindow_Height_GET, (void*)XWindow_Height_SET },
  { "Visible", "boolean", (void*)XWindow_Visible_GET, (void*)XWindow_Visible_SET },
  { "Title", "string", (void*)XWindow_Title_GET, (void*)XWindow_Title_SET },
  { "Resizable", "boolean", (void*)XWindow_Resizable_GET, (void*)XWindow_Resizable_SET },
  { "HasTitleBar", "boolean", (void*)XWindow_HasTitleBar_GET, (void*)XWindow_HasTitleBar_SET },
};

static ClassDefinition g_classDef = {
  "XWindow",
  (void*)XWindow_Create,
  (void*)XWindow_Destroy,
  nullptr,
  0,
  g_props,
  (int)(sizeof(g_props)/sizeof(g_props[0]))
};

XPLUGIN_API FunctionDefinition* GetFunctionDefinitions(int* count) {
  if (count) *count = (int)(sizeof(g_functions)/sizeof(g_functions[0]));
  return g_functions;
}

XPLUGIN_API ClassDefinition* XWindow_GetClassDefinition() {
  return &g_classDef;
}
