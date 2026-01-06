// XWindow_cocoa.mm
// CrossBasic XWindow plugin - Cocoa host implementation (macOS).
// Provides an NSWindow with an NSView content host for embedding.
//
// Build (macOS):
//   clang++ -shared -fPIC -O2 -o libXWindow.dylib XWindow_cocoa.mm -framework Cocoa -std=c++17
//
// Notes:
// - Cocoa UI must run on the main thread. We bootstrap NSApplication on a dedicated UI thread and
//   marshal calls onto that thread synchronously.
// - XWindow_GetNativeHandle returns the NSView* content view (host).

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
  #error "This file is macOS-only."
#endif
#if !defined(__APPLE__)
  #error "This file targets macOS."
#endif

#import <Cocoa/Cocoa.h>

#ifndef XPLUGIN_API
  #define XPLUGIN_API extern "C" __attribute__((visibility("default")))
#endif

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

// ----------------------- Cocoa thread bootstrap / marshal ----------------------
static std::once_flag g_cocoaOnce;
static std::thread g_uiThread;
static std::atomic<bool> g_uiReady{false};

static void ensureCocoa() {
  std::call_once(g_cocoaOnce, [](){
    g_uiThread = std::thread([](){
      @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        g_uiReady.store(true);
        [NSApp run];
      }
    });
    g_uiThread.detach();
  });
  while (!g_uiReady.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

template <class Fn>
auto cocoaSync(Fn&& fn) -> decltype(fn()) {
  ensureCocoa();
  using R = decltype(fn());
  std::promise<R> prom;
  auto fut = prom.get_future();
  auto block = ^{
    @autoreleasepool {
      try {
        if constexpr (std::is_void<R>::value) {
          fn();
          prom.set_value();
        } else {
          prom.set_value(fn());
        }
      } catch (...) {
        if constexpr (!std::is_void<R>::value) prom.set_value(R());
        else prom.set_value();
      }
    }
  };
  dispatch_sync(dispatch_get_main_queue(), block);
  return fut.get();
}

// --------------------------------- XWindow ------------------------------------
@interface XWindowDelegate : NSObject<NSWindowDelegate>
@property(nonatomic, assign) int handle;
@end
@implementation XWindowDelegate
- (BOOL)windowShouldClose:(id)sender {
  (void)sender;
  triggerEvent(self.handle, "Closing", std::string("{\"handle\":") + std::to_string(self.handle) + "}");
  return YES;
}
- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  triggerEvent(self.handle, "Closed", std::string("{\"handle\":") + std::to_string(self.handle) + "}");
}
@end

struct XWindow {
  int handle = 0;
  NSWindow* window = nil;
  NSView* content = nil;
  XWindowDelegate* delegate = nil;

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

XPLUGIN_API int XWindow_Create() {
  int h = g_nextHandle.fetch_add(1);
  auto obj = std::make_unique<XWindow>();
  obj->handle = h;

  cocoaSync([&](){
    NSRect frame = NSMakeRect(obj->left, obj->top, obj->width, obj->height);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    if (obj->resizable) style |= NSWindowStyleMaskResizable;
    obj->window = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
    [obj->window setTitle:[NSString stringWithUTF8String:obj->title.c_str()]];
    obj->content = [obj->window contentView];

    obj->delegate = [XWindowDelegate new];
    obj->delegate.handle = h;
    [obj->window setDelegate:obj->delegate];
  });

  {
    std::lock_guard<std::mutex> lock(g_winMutex);
    g_windows[h] = std::move(obj);
  }

  triggerEvent(h, "Opening", std::string("{\"handle\":") + std::to_string(h) + "}");
  return h;
}

XPLUGIN_API void XWindow_Destroy(int h) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w) return;
    if (w->window) {
      [w->window close];
      w->window = nil;
      w->content = nil;
      w->delegate = nil;
    }
    std::lock_guard<std::mutex> lock(g_winMutex);
    g_windows.erase(h);
  });
}

XPLUGIN_API void XWindow_Show(int h) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    [w->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    w->visible = true;
  });
}

XPLUGIN_API void XWindow_Hide(int h) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    [w->window orderOut:nil];
    w->visible = false;
  });
}

XPLUGIN_API void XWindow_Close(int h) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    [w->window close];
  });
}

XPLUGIN_API int XWindow_Left_GET(int h) { XWindow* w = getWin(h); return w ? w->left : 0; }
XPLUGIN_API void XWindow_Left_SET(int h, int v) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    w->left = v;
    NSRect fr = [w->window frame];
    fr.origin.x = v;
    [w->window setFrame:fr display:YES];
  });
}

XPLUGIN_API int XWindow_Top_GET(int h) { XWindow* w = getWin(h); return w ? w->top : 0; }
XPLUGIN_API void XWindow_Top_SET(int h, int v) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    w->top = v;
    NSRect fr = [w->window frame];
    fr.origin.y = v;
    [w->window setFrame:fr display:YES];
  });
}

XPLUGIN_API int XWindow_Width_GET(int h) { XWindow* w = getWin(h); return w ? w->width : 0; }
XPLUGIN_API void XWindow_Width_SET(int h, int v) {
  if (v < 1) v = 1;
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    w->width = v;
    NSRect fr = [w->window frame];
    fr.size.width = v;
    [w->window setFrame:fr display:YES];
  });
}

XPLUGIN_API int XWindow_Height_GET(int h) { XWindow* w = getWin(h); return w ? w->height : 0; }
XPLUGIN_API void XWindow_Height_SET(int h, int v) {
  if (v < 1) v = 1;
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w || !w->window) return;
    w->height = v;
    NSRect fr = [w->window frame];
    fr.size.height = v;
    [w->window setFrame:fr display:YES];
  });
}

XPLUGIN_API bool XWindow_Visible_GET(int h) { XWindow* w = getWin(h); return w ? w->visible : false; }
XPLUGIN_API void XWindow_Visible_SET(int h, bool v) { if (v) XWindow_Show(h); else XWindow_Hide(h); }

XPLUGIN_API const char* XWindow_Title_GET(int h) {
  static thread_local std::string tmp;
  XWindow* w = getWin(h);
  tmp = w ? w->title : std::string();
  return tmp.c_str();
}
XPLUGIN_API void XWindow_Title_SET(int h, const char* t) {
  cocoaSync([&](){
    XWindow* w = getWin(h);
    if (!w) return;
    w->title = (t ? t : "");
    if (w->window) [w->window setTitle:[NSString stringWithUTF8String:w->title.c_str()]];
  });
}

XPLUGIN_API void* XWindow_GetNativeHandle(int h) {
  XWindow* w = getWin(h);
  return w ? (void*)w->content : nullptr;
}

// menu bar: set as app mainMenu
XPLUGIN_API bool XWindow_SetMenuBarNative(int, void* menuPtr) {
  return cocoaSync([&]() -> bool {
    if (!menuPtr) return false;
    NSMenu* menu = (NSMenu*)menuPtr;
    [NSApp setMainMenu:menu];
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
