/*
  XWebView.cpp
  CrossBasic Plugin: XWebView

  Copyright (c) 2025 Simulanics Technologies – Matthew Combatti
  All rights reserved.

  Licensed under the CrossBasic Source License (CBSL-1.1).
  You may not use this file except in compliance with the License.
  You may obtain a copy of the License at:
  https://www.crossbasic.com/license

  SPDX-License-Identifier: CBSL-1.1

  Author:
    The AI Team under direction of Matthew Combatti <mcombatti@crossbasic.com>
*/

#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <fstream>
#include <sstream>
#include <string>
#include <random>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstring>
#include <cctype>   // isspace

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #define UNICODE
  #define _UNICODE
  #include <windows.h>
  #include <uxtheme.h>
  #include <commctrl.h>
  #include <windowsx.h>
  #include <CommCtrl.h>
  #include <vssym32.h>
  #include <strsafe.h>
  #include <commdlg.h>
  #include <richedit.h>
  #define XPLUGIN_API __declspec(dllexport)
  #define strdup _strdup
#else
  #define XPLUGIN_API __attribute__((visibility("default")))
#endif

// ─── webview single-header ───────────────────────────────────────────────────
#define WEBVIEW_DEBUG 0
#define WEBVIEW_IMPLEMENTATION
#include "webview.h"

// Normalize success code across webview variants
#ifndef WEBVIEW_SUCCESS
#define WEBVIEW_SUCCESS 0
#endif

// ----- Debug logging helper -----
static void debug_log(const char* tag, const std::string& msg) {
// #ifdef _WIN32
//   std::string line = std::string("[XWebView] ") + tag + ": " + msg + "\r\n";
//   OutputDebugStringA(line.c_str());
// #endif
//   fprintf(stderr, "[XWebView] %s: %s\n", tag, msg.c_str());
//   fflush(stderr);
}
#define DBG(TAG, MSG) debug_log(TAG, (MSG))

// ═════════ helper: read file into string ════════════════════════════════════
static std::string readFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream os;
  os << f.rdbuf();
  return os.str();
}

// ═════════ tiny Base64 decoder (ASCII, ignores whitespace) ═════════════════
static inline unsigned b64v(int c){
  if (c>='A' && c<='Z') return c-'A';
  if (c>='a' && c<='z') return c-'a'+26;
  if (c>='0' && c<='9') return c-'0'+52;
  if (c=='+') return 62;
  if (c=='/') return 63;
  return 0xFFFFFFFFu;
}
static std::string base64_decode(const std::string& s){
  std::string out;
  out.reserve(s.size()*3/4);
  unsigned val=0; int valb=-8;
  for (unsigned char ch : s){
    if (ch=='=' || ch=='\r' || ch=='\n' || ch==' ' || ch=='\t') continue;
    unsigned v = b64v(ch);
    if (v==0xFFFFFFFFu) continue;
    val = (val<<6) | v;
    valb += 6;
    if (valb>=0){
      out.push_back(char((val>>valb)&0xFF));
      valb -= 8;
    }
  }
  return out;
}

#ifdef _WIN32
// Forward-declare XWebView so our anchor structures can mention it
class XWebView;

// One AnchorSet per parent window, holds pointers to all anchored children
struct AnchorSet {
  HWND                   parent{};
  std::vector<XWebView*> children;
};
static std::unordered_map<HWND, AnchorSet> gAnchors;

// Forward-declare ParentSizeProc so XWebView can refer to it
static LRESULT CALLBACK ParentSizeProc(
  HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
  UINT_PTR, DWORD_PTR
);

// Helper: get the HWND of the XWindow parent
static HWND GetParentHWND(int xojoHandle) {
  using Fn = HWND(*)(int);
  static Fn fn = nullptr;
  if (!fn) {
    HMODULE m = GetModuleHandleA("XWindow.dll");
    if (m) fn = (Fn)GetProcAddress(m, "XWindow_GetHWND");
  }
  return fn ? fn(xojoHandle) : nullptr;
}
#endif

// ═════════ XWebView class ═══════════════════════════════════════════════════
class XWebView {
public:
  int           handle{};
  int           x{0}, y{0}, w{800}, h{600};
  int           parentHandle{0};

  // UI thread state
  std::thread         ui;
  std::atomic<bool>   running{false};
  std::atomic<bool>   ready{false};
  std::mutex          readyMx;
  std::condition_variable readyCv;

  webview_t     wv{nullptr};
  std::string   currentURL;

  // anchoring flags + offsets
  bool LockTop{false}, LockLeft{false}, LockBottom{false}, LockRight{false};
  int  rightOffset{0}, bottomOffset{0};

  // plugin-owned buffer for last sync result (PLAIN STRING now)
  std::string   lastSyncResultJSON;

  explicit XWebView(int hdl) : handle(hdl) {
    DBG("Ctor", "creating XWebView; spinning UI thread");
    running = true;
    ui = std::thread([this]{
      this->wv = webview_create(0, nullptr);
      if (!this->wv) {
        DBG("UI", "webview_create failed");
      } else {
        webview_set_size(this->wv, this->w, this->h, WEBVIEW_HINT_NONE);
      }
      {
        std::lock_guard<std::mutex> lk(this->readyMx);
        this->ready = true;
      }
      this->readyCv.notify_all();

      DBG("RunLoop", "enter webview_run");
      if (this->wv) webview_run(this->wv);
      DBG("RunLoop", "exit webview_run");
    });
    waitReady(5000);
  }

  ~XWebView() {
    DBG("Dtor", "destroying webview instance");
    running = false;
    if (wv) webview_terminate(wv);
    if (ui.joinable()) ui.join();
    if (wv) { webview_destroy(wv); wv=nullptr; }
  }

  void waitReady(int ms) {
    if (ready.load()) return;
    std::unique_lock<std::mutex> lk(readyMx);
    readyCv.wait_for(lk, std::chrono::milliseconds(ms), [&]{ return ready.load(); });
    DBG("Init", ready.load() ? "UI ready" : "UI not ready (timeout)");
  }

  // Apply geometry immediately (UI thread task)
  void applyGeom() {
#ifdef _WIN32
    if (!wv) { DBG("Geom", "no wv yet"); return; }
    webview_dispatch(wv, [](webview_t, void* arg){
      auto* self = static_cast<XWebView*>(arg);
      HWND hwnd = (HWND)webview_get_window(self->wv);
      if (hwnd) {
        SetWindowPos(hwnd, nullptr, self->x, self->y, self->w, self->h,
                     SWP_NOZORDER | SWP_SHOWWINDOW);
        DBG("Geom", "SetWindowPos applied");
      } else {
        DBG("Geom", "no hwnd yet");
      }
    }, this);
#else
    if (wv) webview_set_size(wv, w, h, WEBVIEW_HINT_NONE);
#endif
  }

  // Parent/anchoring logic
  void applyParent() {
#ifdef _WIN32
    if (!parentHandle) { DBG("Parent", "no parentHandle set"); return; }
    if (!wv) { DBG("Parent", "wv not ready"); return; }
    webview_dispatch(wv, [](webview_t, void* arg){
      auto* self = static_cast<XWebView*>(arg);
      HWND hwndParent = GetParentHWND(self->parentHandle);
      HWND hwnd       = (HWND)webview_get_window(self->wv);
      if (hwnd && hwndParent) {
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style &= ~(WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_BORDER|WS_DLGFRAME);
        style |= WS_CHILD;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);

        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

        SetParent(hwnd, hwndParent);
        SetWindowPos(hwnd, nullptr, self->x, self->y, self->w, self->h,
                     SWP_NOZORDER|SWP_SHOWWINDOW|SWP_FRAMECHANGED);

        DBG("Parent", "embedded into parent and positioned");
        self->registerAnchor(hwndParent);
      } else {
        DBG("Parent", "missing parent hwnd or child hwnd");
      }
    }, this);
#endif
  }

#ifdef _WIN32
  // Declared inside class so &XWebView::registerAnchor is valid on Windows
  void registerAnchor(HWND parent);
#endif

  // ── Synchronous JS evaluation returning ONLY the result string ───────────
  std::string executeSync(const std::string& js, int timeout_ms = 1500) {
    if (!wv) { DBG("ExecuteSync", "wv not ready"); return ""; }

    struct SyncState {
      XWebView*               self{};
      std::mutex              m;
      std::condition_variable cv;
      bool                    done{false};
      std::string             payload;     // plain decoded string result
      std::string             bindName;
      std::string             script;
      std::chrono::steady_clock::time_point t0;
      std::atomic<int>        refs{2};     // UI + waiter
      bool                    finalSeen{false};
    };

    auto *st = new SyncState();
    st->self     = this;
    st->bindName = "_xwv_sync_" + std::to_string(handle) + "_" +
                   std::to_string((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    st->t0       = std::chrono::steady_clock::now();
    DBG("ExecuteSync", "begin; binding=" + st->bindName);

    // Wrapper: stage pings as objects, final as Base64 string
    std::ostringstream ss;
    ss
      << "(function(){"
      << "  const __name=" << st->bindName << ";"
      << "  const __fire=(obj)=>{ try{console.log('[XWV] fire',obj);}catch(_e){}; " << st->bindName << "(obj); };"
      << "  const __emitFinal=(s)=>{"
      << "    var b64='';"
      << "    try{ b64=btoa(unescape(encodeURIComponent(String(s)))) }"
      << "    catch(e){ try{ b64=btoa(String(s)) }catch(e2){ b64='' } }"
      << "    __name(b64);"
      << "  };"
      << "  const __run=()=>{"
      << "    try{ var __val=(function(){ " << js << " })(); __emitFinal(__val); }"
      << "    catch(e){ __emitFinal('Error: '+String(e)); }"
      << "  };"
      << "  try {"
      << "    if (document.readyState==='complete'||document.readyState==='interactive'){"
      << "      __fire({ok:true,stage:'ready',final:false}); __run();"
      << "    } else {"
      << "      __fire({ok:true,stage:'waiting',final:false});"
      << "      window.addEventListener('DOMContentLoaded',()=>{"
      << "        __fire({ok:true,stage:'domcontentloaded',final:false}); __run();"
      << "      },{once:true});"
      << "    }"
      << "  } catch(e){ __emitFinal('Error: '+String(e)); }"
      << "})();";
    st->script = ss.str();

    // Bind + eval on UI thread
    webview_dispatch(wv,
      [](webview_t w, void* arg){
        auto* st_ = static_cast<SyncState*>(arg);

        DBG("Dispatch", "UI: binding " + st_->bindName);
        webview_error_t e1 = webview_bind(
          w, st_->bindName.c_str(),
          [](const char* call_id, const char* req, void* argInner){
            auto* st2 = static_cast<SyncState*>(argInner);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - st2->t0).count();
            DBG("BindCB", std::string("fired @")+std::to_string(ms)+"ms; id="+
                          (call_id?call_id:"<null>")+" req="+(req?req:"<null>"));

            // Extract Base64: handle both "b64" and ["b64"]
            auto extract_b64 = [](const char* s)->std::string{
              if (!s) return {};
              // skip leading ws
              while (*s && std::isspace((unsigned char)*s)) ++s;
              if (*s=='"') {
                const char* q = s+1;
                while (*q && *q!='"') ++q;
                if (*q=='"') return std::string(s+1, q-(s+1));
                return {};
              }
              if (*s=='[') {
                ++s; while (*s && std::isspace((unsigned char)*s)) ++s;
                if (*s=='"') {
                  const char* q = s+1;
                  while (*q && *q!='"') ++q;
                  if (*q=='"') return std::string(s+1, q-(s+1));
                }
              }
              return {};
            };

            std::string b64 = extract_b64(req);
            if (!b64.empty()){
              std::string decoded = base64_decode(b64);
              {
                std::lock_guard<std::mutex> lk(st2->m);
                st2->payload = decoded;
                st2->done = true;
              }
              st2->cv.notify_one();
              webview_return(st2->self->wv, call_id, 0, "null");
              if (!st2->finalSeen) {
                st2->finalSeen = true;
                webview_unbind(st2->self->wv, st2->bindName.c_str());
                DBG("Unbind", "removed " + st2->bindName);
              }
              if (st2->refs.fetch_sub(1) == 1) delete st2;
              return;
            }

            // Non-final stage pings
            webview_return(st2->self->wv, call_id, 0, "null");
          },
          st_
        );
        if (e1 != WEBVIEW_SUCCESS) DBG("Dispatch", "webview_bind failed");

        DBG("Dispatch", "UI: eval (" + std::to_string(st_->script.size()) + " bytes)");
        webview_error_t e2 = webview_eval(w, st_->script.c_str());
        if (e2 != WEBVIEW_SUCCESS) DBG("Dispatch", "webview_eval failed");
      },
      st
    );

    // Wait briefly
    {
      std::unique_lock<std::mutex> lk(st->m);
      if (!st->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return st->done; })) {
        DBG("ExecuteSync", "timeout (no final)");
        if (st->refs.fetch_sub(1) == 1) delete st;
        return std::string("");
      }
    }
    std::string out = st->payload;
    if (st->refs.fetch_sub(1) == 1) delete st;
    return out;
  }
};

#ifdef _WIN32
// Internal: register this instance in the parent's anchor list
void XWebView::registerAnchor(HWND parent) {
  bool anchored = LockTop || LockLeft || LockBottom || LockRight;
  if (!anchored) { DBG("Anchor", "no anchor flags set"); return; }

  RECT prc, crc;
  GetClientRect(parent, &prc);

  HWND hwnd = (HWND)webview_get_window(wv);
  if (!hwnd) { DBG("Anchor", "no hwnd"); return; }
  GetWindowRect(hwnd, &crc);
  MapWindowPoints(HWND_DESKTOP, parent, (POINT*)&crc, 2);

  if (LockRight)  rightOffset  = prc.right  - crc.right;
  if (LockBottom) bottomOffset = prc.bottom - crc.bottom;

  auto &aset = gAnchors[parent];
  aset.parent = parent;

  // avoid duplicates
  if (std::find(aset.children.begin(), aset.children.end(), this) ==
      aset.children.end())
  {
    aset.children.push_back(this);
    if (aset.children.size() == 1) {
      SetWindowSubclass(parent, ParentSizeProc, 0xFEED, 0);
      DBG("Anchor", "installed parent subclass proc");
    } else {
      DBG("Anchor", "added child to existing anchor set");
    }
  } else {
    DBG("Anchor", "already anchored on this parent");
  }
}
#endif

// ═════════ ParentSizeProc implementation (after XWebView) ═══════════════════
#ifdef _WIN32
static LRESULT CALLBACK ParentSizeProc(
  HWND hWnd, UINT msg, WPARAM wp, LPARAM lp,
  UINT_PTR, DWORD_PTR
) {
  if (msg == WM_SIZE) {
    RECT prc;
    GetClientRect(hWnd, &prc);
    auto it = gAnchors.find(hWnd);
    if (it != gAnchors.end()) {
      for (auto *vw : it->second.children) {
        HWND ctl = (HWND)webview_get_window(vw->wv);
        if (!ctl) continue;

        int nx = vw->x, ny = vw->y;
        int nw = vw->w, nh = vw->h;

        // Horizontal anchoring
        if (vw->LockLeft && vw->LockRight) {
          nw = prc.right - vw->rightOffset - vw->x;
        } else if (vw->LockRight) {
          nx = prc.right - vw->rightOffset - vw->w;
        }

        // Vertical anchoring
        if (vw->LockTop && vw->LockBottom) {
          nh = prc.bottom - vw->bottomOffset - vw->y;
        } else if (vw->LockBottom) {
          ny = prc.bottom - vw->bottomOffset - vw->h;
        }

        SetWindowPos(ctl, nullptr, nx, ny, nw, nh, SWP_NOZORDER|SWP_SHOWWINDOW);
        vw->x = nx; vw->y = ny;
        vw->w = nw; vw->h = nh;
      }
    }
  }
  return DefSubclassProc(hWnd, msg, wp, lp);
}
#endif

// ═════════ global bookkeeping ═══════════════════════════════════════════════
static std::mutex gMx;
static std::unordered_map<int, XWebView*> gInst;
static std::mt19937 rng(std::random_device{}());
static std::uniform_int_distribution<int> dist(10000000, 99999999);

// ═════════ exported C interface ═════════════════════════════════════════════
extern "C" {

// Constructor / Destructor
XPLUGIN_API int Constructor() {
  std::lock_guard<std::mutex> lk(gMx);
  int h;
  do { h = dist(rng); } while (gInst.count(h));
  gInst[h] = new XWebView(h);
  DBG("API", "Constructor: handle=" + std::to_string(h));
  return h;
}
XPLUGIN_API void Close(int h) {
  std::lock_guard<std::mutex> lk(gMx);
  auto it = gInst.find(h);
  if (it != gInst.end()) {
    DBG("API", "Close: handle=" + std::to_string(h));
    delete it->second;
    gInst.erase(it);
  } else {
    DBG("API", "Close: invalid handle");
  }
}

// ── geometry ────────────────────────────────────────────────────────────────
#define PROP_INT(NAME,FIELD) \
XPLUGIN_API void XWebView_##NAME##_SET(int h,int v){ \
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; } \
  if(self){ self->FIELD=v; self->applyGeom(); DBG("API", #NAME " _SET ok"); } \
  else { DBG("API", #NAME " _SET: bad handle"); } } \
XPLUGIN_API int XWebView_##NAME##_GET(int h){ \
  std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); \
  int rv = it!=gInst.end()?it->second->FIELD:0; \
  DBG("API", std::string(#NAME " _GET -> ")+std::to_string(rv)); \
  return rv; }

PROP_INT(Left,  x)
PROP_INT(Top,   y)
PROP_INT(Width, w)
PROP_INT(Height,h)
#undef PROP_INT

// ── Parent ─────────────────────────────────────────────────────────────────
XPLUGIN_API void XWebView_Parent_SET(int h,int ph){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if(self){
    self->parentHandle = ph;
    DBG("API", "Parent_SET: " + std::to_string(ph));
    self->applyParent();
  } else {
    DBG("API", "Parent_SET: bad handle");
  }
}
XPLUGIN_API int XWebView_Parent_GET(int h){
  std::lock_guard<std::mutex> lk(gMx);
  auto it=gInst.find(h);
  int rv = it!=gInst.end()?it->second->parentHandle:0;
  DBG("API", "Parent_GET -> " + std::to_string(rv));
  return rv;
}

// ── navigation ─────────────────────────────────────────────────────────────
XPLUGIN_API void XWebView_LoadURL(int h,const char* url){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && url) {
    DBG("API", std::string("LoadURL: ") + url);
    self->currentURL = url;
    if (self->wv) {
      char* u = strdup(url);
      webview_dispatch(self->wv, [](webview_t w, void* p){
        char* s = (char*)p; webview_navigate(w, s); free(s);
      }, u);
    }
  } else {
    DBG("API", "LoadURL: bad handle or null url");
  }
}
XPLUGIN_API void XWebView_LoadHTML(int h,const char* html){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && html) {
    std::string data = std::string("data:text/html,") + html;
    DBG("API", "LoadHTML (len=" + std::to_string(data.size()) + ")");
    self->currentURL = data;
    if (self->wv) {
      char* u = strdup(data.c_str());
      webview_dispatch(self->wv, [](webview_t w, void* p){
        char* s = (char*)p; webview_navigate(w, s); free(s);
      }, u);
    }
  } else {
    DBG("API", "LoadHTML: bad handle or null html");
  }
}
XPLUGIN_API void XWebView_LoadPage(int h,const char* path){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && path) {
    std::string html = readFile(path);
    if (!html.empty()) {
      std::string data = std::string("data:text/html,") + html;
      DBG("API", "LoadPage: " + std::string(path));
      self->currentURL = std::string("file://") + path;
      if (self->wv) {
        char* u = strdup(data.c_str());
        webview_dispatch(self->wv, [](webview_t w, void* p){
          char* s = (char*)p; webview_navigate(w, s); free(s);
        }, u);
      }
    } else {
      DBG("API", "LoadPage: empty file or read error");
    }
  } else {
    DBG("API", "LoadPage: bad handle or null path");
  }
}
XPLUGIN_API void XWebView_Refresh(int h){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && self->wv){
    DBG("API", "Refresh");
    webview_dispatch(self->wv, [](webview_t w, void*){ webview_eval(w, "location.reload()"); }, nullptr);
  } else {
    DBG("API", "Refresh: bad handle or not ready");
  }
}
XPLUGIN_API void XWebView_GoBack(int h){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && self->wv){
    DBG("API", "GoBack");
    webview_dispatch(self->wv, [](webview_t w, void*){ webview_eval(w, "history.back()"); }, nullptr);
  } else {
    DBG("API", "GoBack: bad handle or not ready");
  }
}
XPLUGIN_API void XWebView_GoForward(int h){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && self->wv){
    DBG("API", "GoForward");
    webview_dispatch(self->wv, [](webview_t w, void*){ webview_eval(w, "history.forward()"); }, nullptr);
  } else {
    DBG("API", "GoForward: bad handle or not ready");
  }
}
XPLUGIN_API void XWebView_ExecuteJavaScript(int h,const char* js){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (self && js && self->wv){
    DBG("API", "ExecuteJavaScript (len=" + std::to_string(std::strlen(js)) + ")");
    char* s = strdup(js);
    webview_dispatch(self->wv, [](webview_t w, void* p){
      char* code = (char*)p; webview_eval(w, code); free(code);
    }, s);
  } else {
    DBG("API", "ExecuteJavaScript: bad handle/null js/not ready");
  }
}
XPLUGIN_API const char* XWebView_ExecuteJavaScriptSync(int h,const char* js){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (!self || !js) {
    DBG("API", "ExecuteJavaScriptSync: bad handle or null js");
    static const char* kEmpty = "";
    return kEmpty;
  }
  DBG("API", std::string("ExecuteJavaScriptSync: js='") + js + "'");
  if (!self->ready.load()) self->waitReady(2000);
  self->lastSyncResultJSON = self->executeSync(js, /*timeout_ms*/1500);
  return self->lastSyncResultJSON.c_str(); // stable until next sync call
}

// Optional helper: copy last sync result into caller buffer (safe ownership)
XPLUGIN_API int XWebView_CopyLastSyncResult(int h, char* outBuf, int outCap){
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; }
  if (!self || outCap<0) return 0;
  const std::string& s = self->lastSyncResultJSON;
  int n = (int)s.size();
  if (!outBuf || outCap==0) return n; // query required size
  int cpy = (n < outCap-1) ? n : (outCap-1);
  if (cpy > 0) std::memcpy(outBuf, s.data(), cpy);
  if (outCap>0) outBuf[cpy] = '\0';
  return cpy;
}

XPLUGIN_API const char* XWebView_URL_GET(int h){
  std::lock_guard<std::mutex> lk(gMx);
  auto it=gInst.find(h);
  const char* rv = it!=gInst.end()
    ? strdup(it->second->currentURL.c_str())
    : strdup("");
  DBG("API", std::string("URL_GET -> '") + rv + "'");
  return rv;
}

// ── anchoring boolean props ────────────────────────────────────────────────
// IMPORTANT: this is the ONLY behavior change from your original working version:
// when a lock flag transitions from false -> true and a parent exists,
// we re-register anchors so WM_SIZE tracking works even if flags are set later.
#define BOOL_PROP(NAME,FIELD) \
XPLUGIN_API void XWebView_##NAME##_SET(int h,bool v){ \
  XWebView* self=nullptr; { std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) self=it->second; } \
  if(!self){ DBG("API", #NAME "_SET: bad handle"); return; } \
  bool old=self->FIELD; \
  self->FIELD = v; \
  DBG("API", std::string(#NAME "_SET -> ") + (v ? "true":"false")); \
  /* If flag turned on after parent is set, (re)register anchors */ \
  if (self->parentHandle && v && !old) { \
    HWND p = GetParentHWND(self->parentHandle); \
    if (p) self->registerAnchor(p); \
  } \
} \
XPLUGIN_API bool XWebView_##NAME##_GET(int h){ \
  std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); \
  bool rv = it!=gInst.end() ? it->second->FIELD : false; \
  DBG("API", std::string(#NAME "_GET -> ") + (rv ? "true":"false")); \
  return rv; \
}

BOOL_PROP(LockTop,   LockTop)
BOOL_PROP(LockLeft,  LockLeft)
BOOL_PROP(LockRight, LockRight)
BOOL_PROP(LockBottom,LockBottom)
#undef BOOL_PROP

// ── Xojo class-definition tables ───────────────────────────────────────────
typedef struct{const char* n;const char* t;void* g;void* s;} PropDef;
typedef struct{const char* n;void* f;int a;const char* p[10];const char* r;} MethDef;
typedef struct{const char* d;} ConstDef;
typedef struct{ const char* name; size_t size; void* ctor;
                PropDef* props; size_t propsCount;
                MethDef* meths; size_t methCount;
                ConstDef* csts; size_t cstCount; } ClassDef;

static PropDef props[] = {
  {"Left","integer",(void*)XWebView_Left_GET,(void*)XWebView_Left_SET},
  {"Top","integer",(void*)XWebView_Top_GET,(void*)XWebView_Top_SET},
  {"Width","integer",(void*)XWebView_Width_GET,(void*)XWebView_Width_SET},
  {"Height","integer",(void*)XWebView_Height_GET,(void*)XWebView_Height_SET},
  {"Parent","integer",(void*)XWebView_Parent_GET,(void*)XWebView_Parent_SET},
  {"URL","string",(void*)XWebView_URL_GET,nullptr},
  {"LockTop","boolean",(void*)XWebView_LockTop_GET,(void*)XWebView_LockTop_SET},
  {"LockLeft","boolean",(void*)XWebView_LockLeft_GET,(void*)XWebView_LockLeft_SET},
  {"LockRight","boolean",(void*)XWebView_LockRight_GET,(void*)XWebView_LockRight_SET},
  {"LockBottom","boolean",(void*)XWebView_LockBottom_GET,(void*)XWebView_LockBottom_SET},
};
static MethDef meths[] = {
  {"LoadURL",(void*)XWebView_LoadURL,2,{"integer","string"},"void"},
  {"LoadHTML",(void*)XWebView_LoadHTML,2,{"integer","string"},"void"},
  {"LoadPage",(void*)XWebView_LoadPage,2,{"integer","string"},"void"},
  {"Refresh",(void*)XWebView_Refresh,1,{"integer"},"void"},
  {"GoBack",(void*)XWebView_GoBack,1,{"integer"},"void"},
  {"GoForward",(void*)XWebView_GoForward,1,{"integer"},"void"},
  {"ExecuteJavaScript",(void*)XWebView_ExecuteJavaScript,2,{"integer","string"},"void"},
  {"ExecuteJavaScriptSync",(void*)XWebView_ExecuteJavaScriptSync,2,{"integer","string"},"string"},
  {"CopyLastSyncResult",(void*)XWebView_CopyLastSyncResult,3,{"integer","ptr","integer"},"integer"}
};
static ClassDef cls = {
  "XWebView",
  sizeof(XWebView),
  (void*)Constructor,
  props, sizeof(props)/sizeof(props[0]),
  meths, sizeof(meths)/sizeof(meths[0]),
  nullptr, 0
};

XPLUGIN_API ClassDef* GetClassDefinition() { DBG("API","GetClassDefinition"); return &cls; }

// ── cleanup on unload ──────────────────────────────────────────────────────
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_DETACH) {
    DBG("DLL", "PROCESS_DETACH cleanup");
    std::lock_guard<std::mutex> lk(gMx);
    for (auto& kv : gInst) delete kv.second;
    gInst.clear();
  }
  return TRUE;
}
#else
__attribute__((destructor))
static void onUnload(){
  DBG("DLL", "destructor cleanup");
  std::lock_guard<std::mutex> lk(gMx);
  for (auto& kv : gInst) delete kv.second;
  gInst.clear();
}
#endif

} // extern "C"
