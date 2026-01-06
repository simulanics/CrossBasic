/*

  XTextField.cpp
  CrossBasic Plugin: XTextField                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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

#ifdef _WIN32
  #define UNICODE
  #define _UNICODE
  #include <windows.h>
  #include <commctrl.h>
  #include <dwmapi.h>
  #include <richedit.h>
  #pragma comment(lib, "dwmapi.lib")
  #pragma comment(lib, "comctl32.lib")
  #define XPLUGIN_API __declspec(dllexport)
  #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
    #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
  #endif
  #define strdup _strdup
#else
  #define XPLUGIN_API __attribute__((visibility("default")))

// -----------------------------------------------------------------------------
// Cross-platform implementation (non-Windows)
// -----------------------------------------------------------------------------
#if !defined(_WIN32)

typedef void* (*XWindow_GetNativeHandle_Proc)(int);
static XWindow_GetNativeHandle_Proc gGetParentNativeHandle = nullptr;

static void* resolveParentNativeHandle(int windowId) {
  if (gGetParentNativeHandle) return gGetParentNativeHandle(windowId);

#if defined(__APPLE__)
  const char* candidates[] = {"libXWindow.dylib","XWindow.dylib","XWindow.bundle","XWindow"};
#else
  const char* candidates[] = {"libXWindow.so","XWindow.so","XWindow"};
#endif

  void* lib = nullptr;
  for (auto* name : candidates) {
    lib = dlopen(name, RTLD_LAZY);
    if (lib) break;
  }
  if (!lib) lib = dlopen(nullptr, RTLD_LAZY);
  if (lib) gGetParentNativeHandle = (XWindow_GetNativeHandle_Proc)dlsym(lib, "XWindow_GetNativeHandle");
  if (!gGetParentNativeHandle) return nullptr;
  return gGetParentNativeHandle(windowId);
}

static void platformApplyGeom(const std::shared_ptr<XTextField>& tf) {
  if (!tf || !tf->created) return;
#if defined(__linux__)
  if (!tf->widget) return;
  if (tf->parentGtk && GTK_IS_FIXED(tf->parentGtk)) gtk_fixed_move(GTK_FIXED(tf->parentGtk), tf->widget, tf->x, tf->y);
  gtk_widget_set_size_request(tf->widget, tf->width, tf->height);
#elif defined(__APPLE__)
  if (!tf->view || !tf->parentCocoa) return;
  NSView* p = tf->parentCocoa;
  NSRect pb = [p bounds];
  CGFloat y = pb.size.height - tf->y - tf->height;
  [tf->view setFrame:NSMakeRect(tf->x, y, tf->width, tf->height)];
#endif
}

static void platformSetVisible(const std::shared_ptr<XTextField>& tf, bool vis) {
  if (!tf || !tf->created) return;
#if defined(__linux__)
  if (!tf->widget) return;
  if (vis) gtk_widget_show(tf->widget); else gtk_widget_hide(tf->widget);
#elif defined(__APPLE__)
  if (!tf->view) return;
  [tf->view setHidden:(!vis)];
#endif
}

static void platformSetEnabled(const std::shared_ptr<XTextField>& tf, bool en) {
  if (!tf || !tf->created) return;
#if defined(__linux__)
  if (!tf->widget) return;
  gtk_widget_set_sensitive(tf->widget, en ? TRUE : FALSE);
#elif defined(__APPLE__)
  if (!tf->view) return;
  if ([tf->view respondsToSelector:@selector(setEnabled:)]) [(id)tf->view setEnabled:en ? YES : NO];
#endif
}

static void platformSetText(const std::shared_ptr<XTextField>& tf, const char* txt) {
  if (!tf || !tf->created) return;
  const char* s = txt ? txt : "";
#if defined(__linux__)
  if (!tf->widget) return;
  gtk_entry_set_text(GTK_ENTRY(tf->widget), s);
#elif defined(__APPLE__)
  if (!tf->view) return;
  NSString* ns = [NSString stringWithUTF8String:s];
  if ([tf->view isKindOfClass:[NSTextField class]]) [(NSTextField*)tf->view setStringValue:ns];
#endif
}

static std::string platformGetText(const std::shared_ptr<XTextField>& tf) {
  if (!tf || !tf->created) return "";
#if defined(__linux__)
  if (!tf->widget) return "";
  const char* s = gtk_entry_get_text(GTK_ENTRY(tf->widget));
  return std::string(s ? s : "");
#elif defined(__APPLE__)
  if (!tf->view) return "";
  if ([tf->view isKindOfClass:[NSTextField class]]) {
    NSString* ns = [(NSTextField*)tf->view stringValue];
    return ns ? std::string([ns UTF8String]) : "";
  }
  return "";
#else
  return "";
#endif
}

static void dispatchEventJSON(int handle, const char* eventName) {
  if (!eventName) return;
  void* cb = nullptr;
  {
    std::lock_guard<std::mutex> lk(gEventsMx);
    auto hit = gEvents.find(handle);
    if (hit != gEvents.end()) {
      auto eit = hit->second.find(eventName);
      if (eit != hit->second.end()) cb = eit->second;
    }
  }
  if (!cb) return;
  std::string payload = std::string("{\"event\":\"") + eventName + "\",\"handle\":" + std::to_string(handle) + "}";
  using EventFn = void(*)(const char*);
  ((EventFn)cb)(payload.c_str());
}

#if defined(__linux__)
static void onGtkChanged(GtkEditable*, gpointer userData) {
  int handle = (int)(intptr_t)userData;
  dispatchEventJSON(handle, "TextChanged");
}
#endif

#if defined(__APPLE__)
@interface CBTextFieldObserver : NSObject<NSTextFieldDelegate>
@property(nonatomic,assign) int handle;
@end
@implementation CBTextFieldObserver
- (void)controlTextDidChange:(NSNotification*)note {
  (void)note;
  dispatchEventJSON(self.handle, "TextChanged");
}
@end
#endif

#endif // !defined(_WIN32)

#endif

#include <mutex>
#include <unordered_map>
#include <atomic>
#include <string>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <algorithm>    // std::find

//-----------------------------------------------------------------------------
// Dark-mode colors & brush
static COLORREF gDarkBkg      = RGB(32,32,32);
static COLORREF gDarkText     = RGB(255,255,255);
static COLORREF gDisabledBkg  = RGB(48,48,48);
static COLORREF gDisabledText = RGB(128,128,128);
#ifdef _WIN32
static HBRUSH   gDarkBrush      = nullptr;
static bool     gRichEditLoaded = false;
static const UINT WM_NOTIFY_TEXTCHANGE = WM_APP + 1;

// Apply Windows 10+ dark mode attribute
static void ApplyDarkMode(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (!gDarkBrush)
        gDarkBrush = CreateSolidBrush(gDarkBkg);
}

// UTF-8 ↔ UTF-16 helpers
static std::wstring utf8_to_wstring(const char* utf8) {
    if (!utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], len);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
static std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
#endif

//-----------------------------------------------------------------------------
// XTextField instance
class XTextField {
public:
    int handle;
    int x{0}, y{0}, width{100}, height{23};
    bool HasBorder{true};
    bool HasBevel{false};
    unsigned int TextColor{ 0xFFFFFF };
    std::string BackgroundColor{""};
    std::string BorderColor{""};

    // ── anchoring fields:
    bool LockTop{false}, LockLeft{false},
         LockBottom{false}, LockRight{false};
    int  rightOffset{0}, bottomOffset{0};

    int parentHandle{0};
#ifdef _WIN32
    HWND hwnd{nullptr};
    bool created{false};
#endif

    explicit XTextField(int h) : handle(h) {}
    ~XTextField() {
#ifdef _WIN32
        if (created && hwnd) DestroyWindow(hwnd);
#endif
    }
};

//-----------------------------------------------------------------------------
// Globals
static std::mutex gInstancesMx;
static std::unordered_map<int, XTextField*> gInstances;
static std::mt19937 rng(std::random_device{}());
static std::uniform_int_distribution<int> dist(10000000, 99999999);

static std::mutex gEventsMx;
static std::unordered_map<int, std::unordered_map<std::string, void*>> gEvents;

#ifdef _WIN32
static std::atomic<int> gNextCtrlId{1000};

// Helper: get parent HWND from XWindow.dll
static HWND GetParentHWND(int parentHandle) {
    using Fn = HWND(*)(int);
    static Fn fn = nullptr;
    if (!fn) {
        HMODULE m = GetModuleHandleA("XWindow.dll");
        if (m) fn = (Fn)GetProcAddress(m, "XWindow_GetHWND");
    }
    return fn ? fn(parentHandle) : nullptr;
}

//-----------------------------------------------------------------------------
// ── Anchoring support ──────────────────────────────────────────────────────
struct AnchorSet {
    HWND                       parent{};
    std::vector<XTextField*>   children;
};
static std::unordered_map<HWND,AnchorSet> gAnchors;

// Parent subclass to catch WM_SIZE
static LRESULT CALLBACK ParentSizeProc(HWND hWnd, UINT msg,
                                       WPARAM wp, LPARAM lp,
                                       UINT_PTR, DWORD_PTR)
{
    if (msg == WM_SIZE) {
        RECT prc; GetClientRect(hWnd, &prc);
        auto ait = gAnchors.find(hWnd);
        if (ait != gAnchors.end()) {
            for (auto *tf : ait->second.children) {
                if (!tf->created) continue;

                int nx = tf->x, ny = tf->y,
                    nw = tf->width, nh = tf->height;

                // HORIZONTAL
                if (tf->LockLeft && tf->LockRight)
                    nw = prc.right - tf->rightOffset - tf->x;
                else if (tf->LockRight)
                    nx = prc.right - tf->rightOffset - tf->width;

                // VERTICAL
                if (tf->LockTop && tf->LockBottom)
                    nh = prc.bottom - tf->bottomOffset - tf->y;
                else if (tf->LockBottom)
                    ny = prc.bottom - tf->bottomOffset - tf->height;

                MoveWindow(tf->hwnd, nx, ny, nw, nh, TRUE);
            }
        }
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}

// Helper: calculate offsets & register in the anchor table
static void registerAnchor(XTextField *tf, HWND parent) {
    if (!(tf->LockTop||tf->LockLeft||tf->LockBottom||tf->LockRight))
        return;

    RECT prc, crc;
    GetClientRect(parent,&prc);
    GetWindowRect(tf->hwnd,&crc);
    MapWindowPoints(HWND_DESKTOP,parent,(POINT*)&crc,2);

    if (tf->LockRight)  tf->rightOffset  = prc.right  - crc.right;
    if (tf->LockBottom) tf->bottomOffset = prc.bottom - crc.bottom;

    auto &aset = gAnchors[parent];
    aset.parent = parent;
    if (std::find(aset.children.begin(),
                  aset.children.end(), tf) == aset.children.end())
        aset.children.push_back(tf);

    if (aset.children.size()==1)
        SetWindowSubclass(parent, ParentSizeProc, 0xFEED, 0);
}

//-----------------------------------------------------------------------------
// Subclass proc for the text-field itself (dark-mode, border, events)
static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR idSubclass, DWORD_PTR refData)
{
    int h = (int)refData;
    switch (msg) {
      case WM_CREATE:
        ApplyDarkMode(hWnd);
        SendMessageW(hWnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), MAKELPARAM(TRUE,0));
        break;

      case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it = gInstances.find(h);
        COLORREF bg = gDarkBkg;
        if (it != gInstances.end() && !it->second->BackgroundColor.empty()) {
            std::string hex = it->second->BackgroundColor;
            if (hex.rfind("&h",0)==0||hex.rfind("&H",0)==0) hex=hex.substr(2);
            unsigned long v = strtoul(hex.c_str(), nullptr, 16);
            bg = RGB((v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
        }
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
      }

      case WM_PAINT: {
        LRESULT lr = DefSubclassProc(hWnd, msg, wParam, lParam);
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it = gInstances.find(h);
        if (it != gInstances.end() && it->second->HasBorder) {
            COLORREF bc = GetSysColor(COLOR_WINDOWFRAME);
            if (!it->second->BorderColor.empty()) {
                std::string hex = it->second->BorderColor;
                if (hex.rfind("&h",0)==0||hex.rfind("&H",0)==0) hex=hex.substr(2);
                unsigned long v = strtoul(hex.c_str(), nullptr, 16);
                bc = RGB((v>>16)&0xFF, (v>>8)&0xFF, v&0xFF);
            }
            HDC hdc = GetDC(hWnd);
            HPEN pen = CreatePen(PS_SOLID,1,bc);
            auto oldPen = SelectObject(hdc,pen);
            auto oldBrush = SelectObject(hdc,GetStockObject(NULL_BRUSH));
            RECT rc; GetClientRect(hWnd,&rc);
            Rectangle(hdc,rc.left,rc.top,rc.right,rc.bottom);
            SelectObject(hdc,oldBrush);
            SelectObject(hdc,oldPen);
            DeleteObject(pen);
            ReleaseDC(hWnd,hdc);
        }
        return lr;
      }

      case WM_CHAR:
      case WM_PASTE:
        PostMessageW(hWnd, WM_NOTIFY_TEXTCHANGE, 0, 0);
        break;

      case WM_NOTIFY_TEXTCHANGE: {
        int len = GetWindowTextLengthW(hWnd);
        std::wstring wstr; wstr.resize(len);
        GetWindowTextW(hWnd, &wstr[0], len+1);
        std::string utf8 = wstring_to_utf8(wstr);
        // invoke XTextField_TextChanged
        void* cb = nullptr;
        {
            std::lock_guard<std::mutex> lk(gEventsMx);
            auto git = gEvents.find(h);
            if (git != gEvents.end()) {
                auto cit = git->second.find("TextChanged");
                if (cit != git->second.end()) cb = cit->second;
            }
        }
        if (cb) {
            char* data = strdup(utf8.c_str());
            ((void(__stdcall*)(const char*))cb)(data);
            free(data);
        }
        return 0;
      }

      default:
        break;
    }
    return DefSubclassProc(hWnd,msg,wParam,lParam);
}

// Cleanup on unload
static void CleanupInstances() {
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        for(auto& kv:gInstances) delete kv.second;
        gInstances.clear();
    }
    {
        std::lock_guard<std::mutex> lk(gEventsMx);
        gEvents.clear();
    }
}

#endif // _WIN32

// --- Platform UI headers ---
#if defined(__linux__)
  #include <dlfcn.h>
  #include <gtk/gtk.h>
#elif defined(__APPLE__)
  #include <dlfcn.h>
  #include <Cocoa/Cocoa.h>
#endif

#ifndef _WIN32
  // Windows defines COLORREF/RGB; we provide minimal equivalents for parsing colors.
  typedef uint32_t COLORREF;
  static inline constexpr COLORREF RGB(int r,int g,int b) {
    return (COLORREF)((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16));
  }
#endif


//-----------------------------------------------------------------------------
// Exposed API
extern "C" {

// Constructor / Destructor
XPLUGIN_API int Constructor() {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    int h; do { h = dist(rng); } while (gInstances.count(h));
    gInstances[h] = new XTextField(h);
    return h;
}

// XPLUGIN_API void Close(int h) {
//     std::lock_guard<std::mutex> lk(gInstancesMx);
//     auto it = gInstances.find(h);
//     if (it != gInstances.end()) {
//         delete it->second;
//         gInstances.erase(it);
//     }
// }

// Geometry properties
#define PROP_IMPL_INT(NAME, member) \
XPLUGIN_API void XTextField_##NAME##_SET(int h,int v){ \
    std::lock_guard<std::mutex> lk(gInstancesMx); \
    auto it=gInstances.find(h); \
    if(it!=gInstances.end()){ it->second->member=v; \
        if(it->second->created) \
            MoveWindow(it->second->hwnd, \
                       it->second->x, it->second->y, \
                       it->second->width, it->second->height, TRUE); \
    } \
} \
XPLUGIN_API int XTextField_##NAME##_GET(int h){ \
    std::lock_guard<std::mutex> lk(gInstancesMx); \
    auto it=gInstances.find(h); \
    return it!=gInstances.end()?it->second->member:0; \
}

PROP_IMPL_INT(Left,   x)
PROP_IMPL_INT(Top,    y)
PROP_IMPL_INT(Width,  width)
PROP_IMPL_INT(Height, height)
#undef PROP_IMPL_INT

// Parent property
XPLUGIN_API void XTextField_Parent_SET(int h,int ph){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()) return;
#ifdef _WIN32
    XTextField &TF=*it->second;
    // destroy old
    if(TF.created && TF.hwnd) {
        DestroyWindow(TF.hwnd);
        TF.created=false;
    }

    HWND p = GetParentHWND(ph);
    if(!p) return;

    // (load rich-edit once)
    if(!gRichEditLoaded) {
        LoadLibraryW(L"Msftedit.dll");
        gRichEditLoaded=true;
    }

    TF.parentHandle=ph;
    int id = gNextCtrlId++;
    TF.hwnd = CreateWindowExW(
        0, L"RICHEDIT50W", L"",
        WS_CHILD|WS_VISIBLE|
        ES_AUTOHSCROLL|ES_LEFT|
        (TF.HasBevel?WS_BORDER:0),
        TF.x,TF.y,TF.width,TF.height,
        p, (HMENU)(intptr_t)id,
        GetModuleHandleW(NULL), NULL
    );
    if(!TF.hwnd) return;
    TF.created=true;

    ApplyDarkMode(TF.hwnd);

    // background color
    COLORREF bg = gDarkBkg;
    if(!TF.BackgroundColor.empty()) {
        std::string hex=TF.BackgroundColor;
        if(hex.rfind("&h",0)==0||hex.rfind("&H",0)==0) hex=hex.substr(2);
        unsigned long v=strtoul(hex.c_str(),nullptr,16);
        bg=RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF);
    }
    SendMessageW(TF.hwnd, EM_SETBKGNDCOLOR, FALSE, bg);

    // text color
    {
        unsigned long v = TF.TextColor & 0xFFFFFF;
        CHARFORMAT2 cf{}; cf.cbSize=sizeof(cf);
        cf.dwMask=CFM_COLOR;
        cf.crTextColor = RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF);
        SendMessageW(TF.hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }

    SetWindowSubclass(TF.hwnd, SubclassProc, id, (DWORD_PTR)h);

    // register for anchoring
    registerAnchor(&TF,p);
#endif
}
XPLUGIN_API int XTextField_Parent_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?it->second->parentHandle:0;
}

// Text
XPLUGIN_API void XTextField_Text_SET(int h,const char* text){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        auto w = utf8_to_wstring(text?text:"");
        SetWindowTextW(it->second->hwnd, w.c_str());
#endif
    }
}
XPLUGIN_API const char* XTextField_Text_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        int len = GetWindowTextLengthW(it->second->hwnd);
        std::wstring w; w.resize(len);
        GetWindowTextW(it->second->hwnd,&w[0],len+1);
        auto s = wstring_to_utf8(w);
        return strdup(s.c_str());
#endif
    }
    return strdup("");
}

// TextColor
XPLUGIN_API void XTextField_TextColor_SET(int h, unsigned int rgb) {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
    it->second->TextColor = rgb & 0xFFFFFF;
#ifdef _WIN32
    unsigned long v=rgb&0xFFFFFF;
    CHARFORMAT2 cf{}; cf.cbSize=sizeof(cf);
    cf.dwMask=CFM_COLOR;
    cf.crTextColor=RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF);
    SendMessageW(it->second->hwnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
#endif
}
XPLUGIN_API unsigned int XTextField_TextColor_GET(int h) {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return 0x000000;
    return it->second->TextColor;
}

// BackgroundColor
XPLUGIN_API void XTextField_BackgroundColor_SET(int h,const char* hex){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()) return;
    it->second->BackgroundColor = hex?hex:"";
#ifdef _WIN32
    if(it->second->created) {
        // background
        COLORREF bg=gDarkBkg;
        if(!it->second->BackgroundColor.empty()){
            std::string hx=it->second->BackgroundColor;
            if(hx.rfind("&h",0)==0||hx.rfind("&H",0)==0) hx=hx.substr(2);
            unsigned long v=strtoul(hx.c_str(),nullptr,16);
            bg=RGB((v>>16)&0xFF,(v>>8)&0xFF,v&0xFF);
        }
        SendMessageW(it->second->hwnd,EM_SETBKGNDCOLOR,FALSE,bg);
        // reapply text color
        unsigned long v2=it->second->TextColor;
        CHARFORMAT2 cf2{}; cf2.cbSize=sizeof(cf2);
        cf2.dwMask=CFM_COLOR;
        cf2.crTextColor=RGB((v2>>16)&0xFF,(v2>>8)&0xFF,v2&0xFF);
        SendMessageW(it->second->hwnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&cf2);
        InvalidateRect(it->second->hwnd,nullptr,TRUE);
    }
#endif
}
XPLUGIN_API const char* XTextField_BackgroundColor_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?strdup(it->second->BackgroundColor.c_str()):strdup("");
}

// BorderColor
XPLUGIN_API void XTextField_BorderColor_SET(int h,const char* hex){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()) return;
    it->second->BorderColor = hex?hex:"";
#ifdef _WIN32
    if(it->second->created)
        InvalidateRect(it->second->hwnd,nullptr,TRUE);
#endif
}
XPLUGIN_API const char* XTextField_BorderColor_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?strdup(it->second->BorderColor.c_str()):strdup("");
}

// HasBorder / HasBevel / Invalidate
XPLUGIN_API void XTextField_HasBorder_SET(int h,bool b){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()){
        it->second->HasBorder=b;
#ifdef _WIN32
        if(it->second->created)
            InvalidateRect(it->second->hwnd,nullptr,TRUE);
#endif
    }
}
XPLUGIN_API bool XTextField_HasBorder_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?it->second->HasBorder:false;
}
XPLUGIN_API void XTextField_HasBevel_SET(int h,bool b){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()){
        it->second->HasBevel=b;
#ifdef _WIN32
        if(it->second->created){
            LONG st=GetWindowLongW(it->second->hwnd,GWL_STYLE);
            if(b) st|=WS_BORDER; else st&=~WS_BORDER;
            SetWindowLongW(it->second->hwnd,GWL_STYLE,st);
            SetWindowPos(it->second->hwnd,nullptr,0,0,0,0,
                         SWP_NOMOVE|SWP_NOSIZE|SWP_FRAMECHANGED);
        }
#endif
    }
}
XPLUGIN_API bool XTextField_HasBevel_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?it->second->HasBevel:false;
}
XPLUGIN_API void XTextField_Invalidate(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created)
        InvalidateRect(it->second->hwnd,nullptr,TRUE);
}

// FontName / FontSize
XPLUGIN_API void XTextField_FontName_SET(int h,const char* name){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        HFONT of=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
        LOGFONTA lf={}; if(of) GetObjectA(of,sizeof(lf),&lf);
        strncpy_s(lf.lfFaceName,LF_FACESIZE,name?name:lf.lfFaceName,_TRUNCATE);
        HFONT f=CreateFontIndirectA(&lf);
        SendMessageW(it->second->hwnd,WM_SETFONT,(WPARAM)f,MAKELPARAM(TRUE,0));
#endif
    }
}
XPLUGIN_API const char* XTextField_FontName_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        HFONT of=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
        LOGFONTA lf={}; if(of) GetObjectA(of,sizeof(lf),&lf);
        return strdup(lf.lfFaceName);
#endif
    }
    return strdup("");
}
XPLUGIN_API void XTextField_FontSize_SET(int h,int size){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        HFONT of=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
        LOGFONTA lf={}; if(of) GetObjectA(of,sizeof(lf),&lf);
        lf.lfHeight = -size;
        HFONT f=CreateFontIndirectA(&lf);
        SendMessageW(it->second->hwnd,WM_SETFONT,(WPARAM)f,MAKELPARAM(TRUE,0));
#endif
    }
}
XPLUGIN_API int XTextField_FontSize_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        HFONT of=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
        LOGFONTA lf={}; if(of) GetObjectA(of,sizeof(lf),&lf);
        return -lf.lfHeight;
#endif
    }
    return 0;
}

// Enabled / Visible
XPLUGIN_API void XTextField_Enabled_SET(int h,bool e){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        EnableWindow(it->second->hwnd,e);
        InvalidateRect(it->second->hwnd,nullptr,TRUE);
#endif
    }
}
XPLUGIN_API bool XTextField_Enabled_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        return IsWindowEnabled(it->second->hwnd)!=0;
#endif
    }
    return false;
}
XPLUGIN_API void XTextField_Visible_SET(int h,bool v){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        ShowWindow(it->second->hwnd,v?SW_SHOW:SW_HIDE);
#endif
    }
}
XPLUGIN_API bool XTextField_Visible_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end() && it->second->created){
#ifdef _WIN32
        return IsWindowVisible(it->second->hwnd)!=0;
#endif
    }
    return false;
}

// TextChanged token
XPLUGIN_API const char* XTextField_TextChanged_GET(int h){
    char buf[64];
    sprintf_s(buf,sizeof(buf),"xtextfield:%d:TextChanged",h);
    return strdup(buf);
}

//------------------------------------------------------------------------------
// Destructor
XPLUGIN_API void Close(int h) {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if (it != gInstances.end()) {
#ifdef _WIN32
        // if we've already created the HWND, destroy it
        if (it->second->created && it->second->hwnd)
            DestroyWindow(it->second->hwnd);
#endif
        delete it->second;
        gInstances.erase(it);
    }
}


// Event hookup
XPLUGIN_API bool XTextField_SetEventCallback(int h,const char* ev,void* cb){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    if(!gInstances.count(h)) return false;
    std::string key = ev?ev:"";
    if(auto p=key.rfind(':'); p!=std::string::npos) key.erase(0,p+1);
    std::lock_guard<std::mutex> lk2(gEventsMx);
    gEvents[h][key] = cb;
    return true;
}

//-----------------------------------------------------------------------------
// lock-boolean setters/getters
#define BOOL_PROP(NAME, field)                                              \
XPLUGIN_API void XTextField_##NAME##_SET(int h,bool v){                     \
    std::lock_guard<std::mutex> lk(gInstancesMx);                           \
    auto it=gInstances.find(h); if(it==gInstances.end()) return;            \
    XTextField *tf = it->second; bool old=tf->field; tf->field=v;           \
    if (tf->created && v!=old) {                                            \
        HWND p = GetParentHWND(tf->parentHandle);                           \
        if (p) registerAnchor(tf,p);                                        \
    }                                                                       \
}                                                                           \
XPLUGIN_API bool XTextField_##NAME##_GET(int h){                            \
    std::lock_guard<std::mutex> lk(gInstancesMx);                           \
    auto it=gInstances.find(h); return it!=gInstances.end()?it->second->field:false; \
}

BOOL_PROP(LockTop,    LockTop)
BOOL_PROP(LockLeft,   LockLeft)
BOOL_PROP(LockRight,  LockRight)
BOOL_PROP(LockBottom, LockBottom)
#undef BOOL_PROP

//-----------------------------------------------------------------------------
// Class definition
typedef struct { const char* name; const char* type; void* getter; void* setter; } ClassProperty;
typedef struct { const char* name; void* funcPtr; int arity; const char* paramTypes[10]; const char* retType; } ClassEntry;
typedef struct { const char* declaration; } ClassConstant;
typedef struct {
    const char*     className;
    size_t          classSize;
    void*           constructor;
    ClassProperty*  properties;
    size_t          propertiesCount;
    ClassEntry*     methods;
    size_t          methodsCount;
    ClassConstant*  constants;
    size_t          constantsCount;
} ClassDefinition;

static ClassProperty props[] = {
    { "Left","integer",(void*)XTextField_Left_GET,(void*)XTextField_Left_SET },
    { "Top","integer",(void*)XTextField_Top_GET,(void*)XTextField_Top_SET },
    { "Width","integer",(void*)XTextField_Width_GET,(void*)XTextField_Width_SET },
    { "Height","integer",(void*)XTextField_Height_GET,(void*)XTextField_Height_SET },
    { "Parent","integer",(void*)XTextField_Parent_GET,(void*)XTextField_Parent_SET },
    { "Text","string",(void*)XTextField_Text_GET,(void*)XTextField_Text_SET },
    { "TextColor","color",(void*)XTextField_TextColor_GET,(void*)XTextField_TextColor_SET },
    { "BackgroundColor","string",(void*)XTextField_BackgroundColor_GET,(void*)XTextField_BackgroundColor_SET },
    { "BorderColor","string",(void*)XTextField_BorderColor_GET,(void*)XTextField_BorderColor_SET },
    { "HasBorder","boolean",(void*)XTextField_HasBorder_GET,(void*)XTextField_HasBorder_SET },
    { "HasBevel","boolean",(void*)XTextField_HasBevel_GET,(void*)XTextField_HasBevel_SET },
    { "FontName","string",(void*)XTextField_FontName_GET,(void*)XTextField_FontName_SET },
    { "FontSize","integer",(void*)XTextField_FontSize_GET,(void*)XTextField_FontSize_SET },
    { "Enabled","boolean",(void*)XTextField_Enabled_GET,(void*)XTextField_Enabled_SET },
    { "Visible","boolean",(void*)XTextField_Visible_GET,(void*)XTextField_Visible_SET },
    { "TextChanged","string",(void*)XTextField_TextChanged_GET,nullptr },

    // ── anchoring properties
    { "LockTop","boolean",(void*)XTextField_LockTop_GET,(void*)XTextField_LockTop_SET },
    { "LockLeft","boolean",(void*)XTextField_LockLeft_GET,(void*)XTextField_LockLeft_SET },
    { "LockRight","boolean",(void*)XTextField_LockRight_GET,(void*)XTextField_LockRight_SET },
    { "LockBottom","boolean",(void*)XTextField_LockBottom_GET,(void*)XTextField_LockBottom_SET }
};

static ClassEntry methods[] = {
    { "XTextField_SetEventCallback",(void*)XTextField_SetEventCallback,3,
      {"integer","string","pointer"},"boolean" },
    { "Invalidate",(void*)XTextField_Invalidate,1,{"integer"},"void" },
    { "Close", (void*)Close, 1, {"integer"}, "void"    }
};

static ClassDefinition classDef = {
    "XTextField",
    sizeof(XTextField),
    (void*)Constructor,
    props, sizeof(props)/sizeof(props[0]),
    methods, sizeof(methods)/sizeof(methods[0]),
    nullptr, 0
};

XPLUGIN_API ClassDefinition* GetClassDefinition(){
    return &classDef;
}

#ifdef _WIN32
// DLL entry
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) CleanupInstances();
    return TRUE;
}
#else
__attribute__((destructor))
static void onUnload() {
    CleanupInstances();
}
#endif

} // extern "C"
