/*

  XTextArea.cpp
  CrossBasic Plugin: XTextArea                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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


// --- Platform UI headers ---
#if defined(__linux__)
  #include <dlfcn.h>
  #include <gtk/gtk.h>
#elif defined(__APPLE__)
  #include <dlfcn.h>
  #include <Cocoa/Cocoa.h>
#endif

#ifndef _WIN32
  typedef uint32_t COLORREF;
  static inline constexpr COLORREF RGB(int r,int g,int b) {
    return (COLORREF)((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16));
  }
#endif

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

  // marshalled UI messages (sent with PostMessage from non-UI threads)
  #define WM_XTEXTAREA_ADDTEXT       (WM_APP + 0x100)
  #define WM_XTEXTAREA_SETTEXT       (WM_APP + 0x101)
  #define WM_XTEXTAREA_SCROLL        (WM_APP + 0x102)
  #define WM_XTEXTAREA_SETTEXTCOLOR  (WM_APP + 0x103)
  #define WM_XTEXTAREA_SETBACKCOLOR  (WM_APP + 0x104)
  #define WM_XTEXTAREA_SETBORDER     (WM_APP + 0x105)
  #define WM_XTEXTAREA_INVALIDATE    (WM_APP + 0x106)
  #define WM_XTEXTAREA_SETFONTNAME   (WM_APP + 0x107)
  #define WM_XTEXTAREA_SETFONTSIZE   (WM_APP + 0x108)
  #define WM_XTEXTAREA_SETENABLED    (WM_APP + 0x109)
  #define WM_XTEXTAREA_SETVISIBLE    (WM_APP + 0x10A)
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
  for (auto* name : candidates) { lib = dlopen(name, RTLD_LAZY); if (lib) break; }
  if (!lib) lib = dlopen(nullptr, RTLD_LAZY);
  if (lib) gGetParentNativeHandle = (XWindow_GetNativeHandle_Proc)dlsym(lib, "XWindow_GetNativeHandle");
  if (!gGetParentNativeHandle) return nullptr;
  return gGetParentNativeHandle(windowId);
}

static void platformApplyGeom(const std::shared_ptr<XTextArea>& ta) {
  if (!ta || !ta->created) return;
#if defined(__linux__)
  if (!ta->scroller) return;
  if (ta->parentGtk && GTK_IS_FIXED(ta->parentGtk)) gtk_fixed_move(GTK_FIXED(ta->parentGtk), ta->scroller, ta->x, ta->y);
  gtk_widget_set_size_request(ta->scroller, ta->width, ta->height);
#elif defined(__APPLE__)
  if (!ta->scroll || !ta->parentCocoa) return;
  NSRect pb = [ta->parentCocoa bounds];
  CGFloat y = pb.size.height - ta->y - ta->height;
  [ta->scroll setFrame:NSMakeRect(ta->x, y, ta->width, ta->height)];
#endif
}

static void platformSetVisible(const std::shared_ptr<XTextArea>& ta, bool vis) {
  if (!ta || !ta->created) return;
#if defined(__linux__)
  if (!ta->scroller) return;
  if (vis) gtk_widget_show(ta->scroller); else gtk_widget_hide(ta->scroller);
#elif defined(__APPLE__)
  if (!ta->scroll) return;
  [ta->scroll setHidden:(!vis)];
#endif
}

static void platformSetEnabled(const std::shared_ptr<XTextArea>& ta, bool en) {
  if (!ta || !ta->created) return;
#if defined(__linux__)
  if (!ta->scroller) return;
  gtk_widget_set_sensitive(ta->scroller, en ? TRUE : FALSE);
#elif defined(__APPLE__)
  if (!ta->textView) return;
  [ta->textView setEditable:(en ? YES : NO)];
#endif
}

static void platformSetText(const std::shared_ptr<XTextArea>& ta, const char* txt) {
  if (!ta || !ta->created) return;
  const char* s = txt ? txt : "";
#if defined(__linux__)
  if (!ta->textView) return;
  GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ta->textView));
  gtk_text_buffer_set_text(buf, s, -1);
#elif defined(__APPLE__)
  if (!ta->textView) return;
  NSString* ns = [NSString stringWithUTF8String:s];
  [[ta->textView textStorage] setAttributedString:[[NSAttributedString alloc] initWithString:ns]];
#endif
}

static void platformAppendText(const std::shared_ptr<XTextArea>& ta, const char* txt) {
  if (!ta || !ta->created) return;
  const char* s = txt ? txt : "";
#if defined(__linux__)
  if (!ta->textView) return;
  GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ta->textView));
  GtkTextIter endIter;
  gtk_text_buffer_get_end_iter(buf, &endIter);
  gtk_text_buffer_insert(buf, &endIter, s, -1);
#elif defined(__APPLE__)
  if (!ta->textView) return;
  NSString* ns = [NSString stringWithUTF8String:s];
  [[ta->textView textStorage] appendAttributedString:[[NSAttributedString alloc] initWithString:ns]];
#endif
}

static std::string platformGetText(const std::shared_ptr<XTextArea>& ta) {
  if (!ta || !ta->created) return "";
#if defined(__linux__)
  if (!ta->textView) return "";
  GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ta->textView));
  GtkTextIter startIter, endIter;
  gtk_text_buffer_get_bounds(buf, &startIter, &endIter);
  gchar* s = gtk_text_buffer_get_text(buf, &startIter, &endIter, TRUE);
  std::string out = s ? std::string(s) : "";
  if (s) g_free(s);
  return out;
#elif defined(__APPLE__)
  if (!ta->textView) return "";
  NSString* ns = [[ta->textView textStorage] string];
  return ns ? std::string([ns UTF8String]) : "";
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
static void onGtkBufferChanged(GtkTextBuffer*, gpointer userData) {
  int handle = (int)(intptr_t)userData;
  dispatchEventJSON(handle, "TextChanged");
}
#endif

#if defined(__APPLE__)
@interface CBTextAreaObserver : NSObject<NSTextViewDelegate>
@property(nonatomic,assign) int handle;
@end
@implementation CBTextAreaObserver
- (void)textDidChange:(NSNotification*)note {
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
#include <sstream>
#include <vector>
#include <algorithm> 

//-----------------------------------------------------------------------------
// Dark-mode colours
static COLORREF gDarkBkg      = RGB(32,32,32);
static COLORREF gDarkText     = RGB(255,255,255);
static COLORREF gDisabledBkg  = RGB(48,48,48);
static COLORREF gDisabledText = RGB(128,128,128);

#ifdef _WIN32
static bool gRichEditLoaded = false;

//─────────────────────────────────────────────────────────────────────────────
//  Windows helpers
//─────────────────────────────────────────────────────────────────────────────
static void ApplyDarkMode(HWND hwnd)
{
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

static std::wstring utf8_to_wstring(const char* utf8)
{
    if (!utf8) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], len);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}
static std::string wstring_to_utf8(const std::wstring& wstr)
{
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &s[0], len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// exported helper in XWindow.dll
static HWND GetParentHWND(int parentHandle)
{
    using Fn = HWND(*)(int);
    static Fn fn = nullptr;
    if (!fn)
    {
        HMODULE m = GetModuleHandleA("XWindow.dll");
        if (m) fn = (Fn)GetProcAddress(m, "XWindow_GetHWND");
    }
    return fn ? fn(parentHandle) : nullptr;
}
#endif // _WIN32

// forward-declare event invoker
static void triggerEvent(int handle, const std::string& eventName, const char* param);

//-----------------------------------------------------------------------------
// XTextArea instance
class XTextArea
{
public:
    int  handle;
    int  x{0}, y{0}, width{200}, height{100};

    // anchoring
    bool LockTop   {false};
    bool LockLeft  {false};
    bool LockRight {false};
    bool LockBottom{false};
    int  rightOffset  {0};
    int  bottomOffset {0};

    bool HasBorder{true};
    unsigned int TextColor       {0xFFFFFF};   // 0xRRGGBB
    unsigned int BackgroundColor {0x000000};
    unsigned int BorderColor     {0x000000};
    int  parentHandle{0};

#ifdef _WIN32
    HWND hwnd{nullptr};
    bool created{false};
#endif

    explicit XTextArea(int h) : handle(h) {}
    ~XTextArea()
    {
#ifdef _WIN32
        if (created && hwnd) DestroyWindow(hwnd);
#endif
    }
};

//-----------------------------------------------------------------------------
// Global bookkeeping
static std::mutex gInstancesMx;
static std::unordered_map<int, XTextArea*> gInstances;
static std::mt19937 rng(std::random_device{}());
static std::uniform_int_distribution<int> dist(10000000, 99999999);

static std::mutex gEventsMx;
static std::unordered_map<int, std::unordered_map<std::string, void*>> gEvents;

#ifdef _WIN32
static std::atomic<int> gNextCtrlId{1000};

//─────────────────────────────────────────────────────────────────────────────
//  Anchoring support (same scheme as XButton / XTextField)
//─────────────────────────────────────────────────────────────────────────────
struct AnchorSet
{
    HWND                        parent {};
    std::vector<XTextArea*>     children;
};
static std::unordered_map<HWND, AnchorSet> gAnchors;

static LRESULT CALLBACK ParentSizeProc(HWND hWnd, UINT msg,
                                       WPARAM wp, LPARAM lp,
                                       UINT_PTR /*idSub*/, DWORD_PTR /*ref*/)
{
    if (msg == WM_SIZE)
    {
        RECT prc; GetClientRect(hWnd, &prc);

        auto it = gAnchors.find(hWnd);
        if (it != gAnchors.end())
        {
            for (auto *ta : it->second.children)
            {
                if (!ta->created) continue;

                int nx = ta->x, ny = ta->y,
                    nw = ta->width,
                    nh = ta->height;

                // ── horizontal
                if (ta->LockLeft && ta->LockRight)
                    nw = prc.right - ta->rightOffset - ta->x;
                else if (ta->LockRight)
                    nx = prc.right - ta->rightOffset - ta->width;

                // ── vertical
                if (ta->LockTop && ta->LockBottom)
                    nh = prc.bottom - ta->bottomOffset - ta->y;
                else if (ta->LockBottom)
                    ny = prc.bottom - ta->bottomOffset - ta->height;

                SetWindowPos(ta->hwnd, nullptr, nx, ny, nw, nh,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                ta->x = nx;  ta->y = ny;
                ta->width = nw; ta->height = nh;
            }
        }
    }
    return DefSubclassProc(hWnd, msg, wp, lp);
}
#endif // _WIN32

//=============================================================================
//  RichEdit subclass proc   (paint, custom border, marshalled UI, TextChanged)
//=============================================================================
#ifdef _WIN32
static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam,
                                     UINT_PTR /*idSub*/, DWORD_PTR refData)
{
    int h = (int)refData;

    switch (msg)
    {
        case WM_CREATE:
            ApplyDarkMode(hWnd);
            break;

        case WM_CHAR:
        case WM_PASTE:
            triggerEvent(h, "TextChanged", nullptr);
            break;

        case WM_PAINT:
        {
            LRESULT lr = DefSubclassProc(hWnd, msg, wParam, lParam);

            std::lock_guard<std::mutex> lk(gInstancesMx);
            auto it = gInstances.find(h);
            if (it != gInstances.end() && it->second->HasBorder)
            {
                COLORREF bc = GetSysColor(COLOR_WINDOWFRAME);
                if (it->second->BorderColor != 0)
                {
                    unsigned int col = it->second->BorderColor;
                    bc = RGB((col>>16)&0xFF, (col>>8)&0xFF, col&0xFF);
                }

                HDC hdc = GetDC(hWnd);
                HPEN pen = CreatePen(PS_SOLID, 1, bc);
                HGDIOBJ oldPen = SelectObject(hdc, pen);
                HGDIOBJ oldBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                RECT rc; GetClientRect(hWnd, &rc);
                rc.right--; rc.bottom--;
                Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
                SelectObject(hdc, oldBr);
                SelectObject(hdc, oldPen);
                DeleteObject(pen);
                ReleaseDC(hWnd, hdc);
            }
            return lr;
        }

        //———— marshalled UI messages ————————————————————————————————
        case WM_XTEXTAREA_ADDTEXT:
        {
            wchar_t* pw = (wchar_t*)lParam;
            if (pw)
            {
                int len = GetWindowTextLengthW(hWnd);
                SendMessageW(hWnd, EM_SETSEL, len, len);
                SendMessageW(hWnd, EM_REPLACESEL, FALSE, (LPARAM)pw);
                SendMessageW(hWnd, EM_SCROLLCARET, 0, 0);
                free(pw);
            }
            return 0;
        }
        case WM_XTEXTAREA_SETTEXT:
        {
            wchar_t* pw = (wchar_t*)lParam;
            if (pw)
            {
                SetWindowTextW(hWnd, pw);
                free(pw);
            }
            return 0;
        }
        case WM_XTEXTAREA_SCROLL:
        {
            int pos   = (int)wParam;
            int total = (int)SendMessageW(hWnd, EM_GETLINECOUNT, 0, 0);
            if (pos >= total)
                SendMessageW(hWnd, WM_VSCROLL, MAKEWPARAM(SB_BOTTOM,0), 0);
            else if (pos <= 0)
                SendMessageW(hWnd, WM_VSCROLL, MAKEWPARAM(SB_TOP,0), 0);
            else
            {
                int first = (int)SendMessageW(hWnd, EM_GETFIRSTVISIBLELINE,0,0);
                SendMessageW(hWnd, EM_LINESCROLL, 0, pos - first);
            }
            return 0;
        }
        case WM_XTEXTAREA_SETTEXTCOLOR:
        {
            CHARFORMAT2 cf{}; cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR;
            cf.crTextColor = (COLORREF)wParam;
            SendMessageW(hWnd, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
            return 0;
        }
        case WM_XTEXTAREA_SETBACKCOLOR:
            SendMessageW(hWnd, EM_SETBKGNDCOLOR, FALSE, lParam);
            return 0;
        case WM_XTEXTAREA_SETBORDER:
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        case WM_XTEXTAREA_INVALIDATE:
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        case WM_XTEXTAREA_SETFONTNAME:
        {
            char* nm = (char*)lParam;
            if (nm)
            {
                HFONT oldF = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
                LOGFONTA lf = {};
                if (oldF) GetObjectA(oldF, sizeof(lf), &lf);
                strncpy_s(lf.lfFaceName, LF_FACESIZE, nm, _TRUNCATE);
                HFONT f = CreateFontIndirectA(&lf);
                SendMessageW(hWnd, WM_SETFONT, (WPARAM)f, MAKELPARAM(TRUE,0));
                free(nm);
            }
            return 0;
        }
        case WM_XTEXTAREA_SETFONTSIZE:
        {
            int sz = (int)wParam;
            HFONT oldF = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
            LOGFONTA lf = {};
            if (oldF) GetObjectA(oldF, sizeof(lf), &lf);
            lf.lfHeight = -sz;
            HFONT f = CreateFontIndirectA(&lf);
            SendMessageW(hWnd, WM_SETFONT, (WPARAM)f, MAKELPARAM(TRUE,0));
            return 0;
        }
        case WM_XTEXTAREA_SETENABLED:
            EnableWindow(hWnd, (BOOL)wParam); return 0;
        case WM_XTEXTAREA_SETVISIBLE:
            ShowWindow(hWnd, (BOOL)wParam ? SW_SHOW : SW_HIDE); return 0;
        //———— end marshalled messages ————————————————————————————————

        default: break;
    }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}
#endif // _WIN32

//─────────────────────────────────────────────────────────────────────────────
//  Helpers
//────────────────────────────────────────────────────────────────────────────-
static void triggerEvent(int handle, const std::string& eventName, const char* param)
{
    void* cb = nullptr;
    {
        std::lock_guard<std::mutex> lk(gEventsMx);
        auto it = gEvents.find(handle);
        if (it != gEvents.end())
        {
            auto jt = it->second.find(eventName);
            if (jt != it->second.end()) cb = jt->second;
        }
    }
    if (!cb) return;
    char* dup = strdup(param ? param : "");
#ifdef _WIN32
    using CB = void(__stdcall*)(const char*);
#else
    using CB = void(*)(const char*);
#endif
    ((CB)cb)(dup);
    free(dup);
}

// cleanup when DLL unloads
static void CleanupInstances()
{
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        for (auto &kv : gInstances) delete kv.second;
        gInstances.clear();
    }
    {
        std::lock_guard<std::mutex> lk(gEventsMx);
        gEvents.clear();
    }
}

//=============================================================================
//  Exported C interface
//=============================================================================
extern "C" {

//------------------------------------------------------------------------------
// Constructor  /  Close
//------------------------------------------------------------------------------
XPLUGIN_API int Constructor()
{
    int h;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        do { h = dist(rng); } while (gInstances.count(h));
        gInstances[h] = new XTextArea(h);
    }
    return h;
}

XPLUGIN_API void Close(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if (it != gInstances.end())
    {
#ifdef _WIN32
        if (it->second->created && it->second->hwnd)
            DestroyWindow(it->second->hwnd);
#endif
        delete it->second;
        gInstances.erase(it);
    }
}

//------------------------------------------------------------------------------
// Geometry
//------------------------------------------------------------------------------
#define PROP_INT(NAME, MEMBER)                                                    \
    XPLUGIN_API void XTextArea_##NAME##_SET(int h,int v){                         \
        std::lock_guard<std::mutex> lk(gInstancesMx);                             \
        auto it=gInstances.find(h);                                               \
        if(it!=gInstances.end()){                                                 \
            it->second->MEMBER=v;                                                 \
            if(it->second->created)                                               \
                MoveWindow(it->second->hwnd,it->second->x,it->second->y,          \
                           it->second->width,it->second->height,TRUE);            \
        }}                                                                        \
    XPLUGIN_API int XTextArea_##NAME##_GET(int h){                                \
        std::lock_guard<std::mutex> lk(gInstancesMx);                             \
        auto it=gInstances.find(h);                                               \
        return it!=gInstances.end()?it->second->MEMBER:0; }

PROP_INT(Left,x)
PROP_INT(Top,y)
PROP_INT(Width,width)
PROP_INT(Height,height)
#undef PROP_INT

//------------------------------------------------------------------------------
// LockTop / LockLeft / LockRight / LockBottom
//------------------------------------------------------------------------------
#ifdef _WIN32
#define BOOL_PROP(NAME, MEMBER)                                                   \
XPLUGIN_API void XTextArea_##NAME##_SET(int h,bool v){                            \
    std::lock_guard<std::mutex> lk(gInstancesMx);                                 \
    auto it=gInstances.find(h);                                                   \
    if(it==gInstances.end()) return;                                              \
    XTextArea* ta=it->second;                                                     \
    bool old=ta->MEMBER;                                                          \
    ta->MEMBER=v;                                                                 \
    if(ta->created && v!=old){                                                    \
        HWND p=GetParentHWND(ta->parentHandle);                                   \
        if(!p) return;                                                            \
        auto &aset=gAnchors[p];                                                   \
        if(std::find(aset.children.begin(),aset.children.end(),ta)==aset.children.end()){\
            RECT prc,crc; GetClientRect(p,&prc); GetWindowRect(ta->hwnd,&crc);    \
            MapWindowPoints(HWND_DESKTOP,p,(POINT*)&crc,2);                       \
            ta->rightOffset  = prc.right  - crc.right;                            \
            ta->bottomOffset = prc.bottom - crc.bottom;                           \
            aset.parent=p; aset.children.push_back(ta);                           \
            if(aset.children.size()==1)                                           \
                SetWindowSubclass(p,ParentSizeProc,0xFEED,0);                     \
        }                                                                         \
    }}                                                                            \
XPLUGIN_API bool XTextArea_##NAME##_GET(int h){                                   \
    std::lock_guard<std::mutex> lk(gInstancesMx);                                 \
    auto it=gInstances.find(h);                                                   \
    return it!=gInstances.end()?it->second->MEMBER:false; }
#else
#define BOOL_PROP(NAME,MEMBER)                                                    \
    XPLUGIN_API void XTextArea_##NAME##_SET(int,int){}                            \
    XPLUGIN_API bool XTextArea_##NAME##_GET(int){return false;}
#endif

BOOL_PROP(LockTop,    LockTop)
BOOL_PROP(LockLeft,   LockLeft)
BOOL_PROP(LockRight,  LockRight)
BOOL_PROP(LockBottom, LockBottom)
#undef BOOL_PROP

//------------------------------------------------------------------------------
// Parent  (creates HWND, registers anchors)
//------------------------------------------------------------------------------
XPLUGIN_API void XTextArea_Parent_SET(int h,int ph)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()) return;

#ifdef _WIN32
    if(it->second->created && it->second->hwnd){
        DestroyWindow(it->second->hwnd);
        it->second->created=false;
    }

    HWND p=GetParentHWND(ph);
    if(!p) return;

    if(!gRichEditLoaded){
        LoadLibraryW(L"Msftedit.dll");
        gRichEditLoaded=true;
    }

    auto &TA=*it->second;
    TA.parentHandle=ph;
    int id=gNextCtrlId++;

    TA.hwnd=CreateWindowExW(
        0,L"RICHEDIT50W",L"",
        WS_CHILD|WS_VISIBLE|
        ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,
        TA.x,TA.y,TA.width,TA.height,
        p,(HMENU)(intptr_t)id,
        GetModuleHandleW(NULL),NULL);
    if(!TA.hwnd) return;

    TA.created=true;
    ApplyDarkMode(TA.hwnd);
    SendMessageW(TA.hwnd,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),MAKELPARAM(TRUE,0));

    // colours
    PostMessageW(TA.hwnd,WM_XTEXTAREA_SETBACKCOLOR,(WPARAM)RGB((TA.BackgroundColor>>16)&0xFF,(TA.BackgroundColor>>8)&0xFF,TA.BackgroundColor&0xFF),0);
    PostMessageW(TA.hwnd,WM_XTEXTAREA_SETTEXTCOLOR,(WPARAM)RGB((TA.TextColor>>16)&0xFF,(TA.TextColor>>8)&0xFF,TA.TextColor&0xFF),0);

    SetWindowSubclass(TA.hwnd,SubclassProc,id,(DWORD_PTR)h);

    // ---------- anchor registration ----------
    bool anchored=TA.LockTop||TA.LockLeft||TA.LockRight||TA.LockBottom;
    if(anchored){
        RECT prc,crc; GetClientRect(p,&prc); GetWindowRect(TA.hwnd,&crc);
        MapWindowPoints(HWND_DESKTOP,p,(POINT*)&crc,2);
        TA.rightOffset  = prc.right  - crc.right;
        TA.bottomOffset = prc.bottom - crc.bottom;

        auto &aset=gAnchors[p];
        aset.parent=p;
        aset.children.push_back(&TA);
        if(aset.children.size()==1)
            SetWindowSubclass(p,ParentSizeProc,0xFEED,0);
    }
#endif
}

XPLUGIN_API int XTextArea_Parent_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return it!=gInstances.end()?it->second->parentHandle:0;
}

//------------------------------------------------------------------------------
// Text (set / get)  – uses marshalled message for thread-safety
//------------------------------------------------------------------------------
XPLUGIN_API void XTextArea_Text_SET(int h,const char* text)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
#ifdef _WIN32
    std::wstring w=utf8_to_wstring(text?text:"");
    size_t cb=(w.size()+1)*sizeof(wchar_t);
    wchar_t* pw=(wchar_t*)malloc(cb);
    memcpy(pw,w.c_str(),cb);
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETTEXT,0,(LPARAM)pw);
#endif
}

XPLUGIN_API const char* XTextArea_Text_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()&&it->second->created){
#ifdef _WIN32
        int len=GetWindowTextLengthW(it->second->hwnd);
        std::wstring w; w.resize(len);
        GetWindowTextW(it->second->hwnd,&w[0],len+1);
        std::string s=wstring_to_utf8(w);
        return strdup(s.c_str());
#endif
    }
    return strdup("");
}

//------------------------------------------------------------------------------
// Colour / border / font / misc  (unchanged from earlier file)
//------------------------------------------------------------------------------
XPLUGIN_API void XTextArea_TextColor_SET(int h,unsigned int rgb)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
    it->second->TextColor=rgb;
#ifdef _WIN32
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETTEXTCOLOR,
                 (WPARAM)RGB((rgb>>16)&0xFF,(rgb>>8)&0xFF,rgb&0xFF),0);
#endif
}
XPLUGIN_API unsigned int XTextArea_TextColor_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return (it!=gInstances.end())?it->second->TextColor:0x000000;
}

XPLUGIN_API void XTextArea_BackgroundColor_SET(int h,unsigned int rgb)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
    it->second->BackgroundColor=rgb;
#ifdef _WIN32
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETBACKCOLOR,
                 (WPARAM)RGB((rgb>>16)&0xFF,(rgb>>8)&0xFF,rgb&0xFF),0);
#endif
}
XPLUGIN_API unsigned int XTextArea_BackgroundColor_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return (it!=gInstances.end())?it->second->BackgroundColor:0x000000;
}

XPLUGIN_API void XTextArea_HasBorder_SET(int h,bool b)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
    it->second->HasBorder=b;
#ifdef _WIN32
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETBORDER,(WPARAM)b,0);
#endif
}
XPLUGIN_API bool XTextArea_HasBorder_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    return (it!=gInstances.end())?it->second->HasBorder:false;
}

XPLUGIN_API void XTextArea_Invalidate(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()&&it->second->created){
#ifdef _WIN32
        PostMessageW(it->second->hwnd,WM_XTEXTAREA_INVALIDATE,0,0);
#endif
    }
}

XPLUGIN_API void XTextArea_FontName_SET(int h,const char* name)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
#ifdef _WIN32
    char* cp=_strdup(name?name:"");
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETFONTNAME,0,(LPARAM)cp);
#endif
}
XPLUGIN_API const char* XTextArea_FontName_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return strdup("");
#ifdef _WIN32
    HFONT f=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
    LOGFONTA lf={};
    if(f) GetObjectA(f,sizeof(lf),&lf);
    return strdup(lf.lfFaceName);
#else
    return strdup("");
#endif
}

XPLUGIN_API void XTextArea_FontSize_SET(int h,int sz)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return;
#ifdef _WIN32
    PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETFONTSIZE,(WPARAM)sz,0);
#endif
}
XPLUGIN_API int XTextArea_FontSize_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return 0;
#ifdef _WIN32
    HFONT f=(HFONT)SendMessageW(it->second->hwnd,WM_GETFONT,0,0);
    LOGFONTA lf={};
    if(f) GetObjectA(f,sizeof(lf),&lf);
    return -lf.lfHeight;
#else
    return 0;
#endif
}

// Enabled / Visible
XPLUGIN_API void XTextArea_Enabled_SET(int h,bool e)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()&&it->second->created){
#ifdef _WIN32
        PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETENABLED,(WPARAM)e,0);
#endif
    }
}
XPLUGIN_API bool XTextArea_Enabled_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return false;
#ifdef _WIN32
    return IsWindowEnabled(it->second->hwnd)!=0;
#else
    return false;
#endif
}
XPLUGIN_API void XTextArea_Visible_SET(int h,bool v)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()&&it->second->created){
#ifdef _WIN32
        PostMessageW(it->second->hwnd,WM_XTEXTAREA_SETVISIBLE,(WPARAM)v,0);
#endif
    }
}
XPLUGIN_API bool XTextArea_Visible_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return false;
#ifdef _WIN32
    return IsWindowVisible(it->second->hwnd)!=0;
#else
    return false;
#endif
}

// ScrollPosition
XPLUGIN_API void XTextArea_ScrollPosition_SET(int h,int pos)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it!=gInstances.end()&&it->second->created){
#ifdef _WIN32
        PostMessageW(it->second->hwnd,WM_XTEXTAREA_SCROLL,(WPARAM)pos,0);
#endif
    }
}
XPLUGIN_API int XTextArea_ScrollPosition_GET(int h)
{
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    if(it==gInstances.end()||!it->second->created) return 0;
#ifdef _WIN32
    return (int)SendMessageW(it->second->hwnd,EM_GETFIRSTVISIBLELINE,0,0);
#else
    return 0;
#endif
}

// TextChanged token
XPLUGIN_API const char* XTextArea_TextChanged_GET(int h)
{
    std::ostringstream os; os<<"xtextarea:"<<h<<":TextChanged";
    return strdup(os.str().c_str());
}

// AddText (append)
XPLUGIN_API void XTextArea_AddText(int h,const char* text)
{
    HWND hwnd=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it==gInstances.end()||!it->second->created) return;
        hwnd=it->second->hwnd;
    }
#ifdef _WIN32
    std::wstring w=utf8_to_wstring(text?text:"");
    size_t cb=(w.size()+1)*sizeof(wchar_t);
    wchar_t* pw=(wchar_t*)malloc(cb);
    memcpy(pw,w.c_str(),cb);
    PostMessageW(hwnd,WM_XTEXTAREA_ADDTEXT,0,(LPARAM)pw);
#endif
}

// Event hookup
XPLUGIN_API bool XTextArea_SetEventCallback(int h,const char* ev,void* cb)
{
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        if(!gInstances.count(h)) return false;
    }
    std::string key=ev?ev:"";
    if(auto p=key.rfind(':');p!=std::string::npos) key.erase(0,p+1);
    std::lock_guard<std::mutex> lk2(gEventsMx);
    gEvents[h][key]=cb;
    return true;
}

//------------------------------------------------------------------------------
// Class definition for Xojo / CrossBasic
//------------------------------------------------------------------------------
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

// properties array (includes anchor flags!)
static ClassProperty props[] = {
    { "Left","integer",(void*)XTextArea_Left_GET,(void*)XTextArea_Left_SET },
    { "Top","integer",(void*)XTextArea_Top_GET,(void*)XTextArea_Top_SET },
    { "Width","integer",(void*)XTextArea_Width_GET,(void*)XTextArea_Width_SET },
    { "Height","integer",(void*)XTextArea_Height_GET,(void*)XTextArea_Height_SET },
    { "Parent","integer",(void*)XTextArea_Parent_GET,(void*)XTextArea_Parent_SET },
    { "LockTop","boolean",(void*)XTextArea_LockTop_GET,(void*)XTextArea_LockTop_SET },
    { "LockLeft","boolean",(void*)XTextArea_LockLeft_GET,(void*)XTextArea_LockLeft_SET },
    { "LockRight","boolean",(void*)XTextArea_LockRight_GET,(void*)XTextArea_LockRight_SET },
    { "LockBottom","boolean",(void*)XTextArea_LockBottom_GET,(void*)XTextArea_LockBottom_SET },
    { "Text","string",(void*)XTextArea_Text_GET,(void*)XTextArea_Text_SET },
    { "TextColor","color",(void*)XTextArea_TextColor_GET,(void*)XTextArea_TextColor_SET },
/*  { "BackgroundColor","color",(void*)XTextArea_BackgroundColor_GET,(void*)XTextArea_BackgroundColor_SET },
    { "BorderColor","color",(void*)XTextArea_BorderColor_GET,(void*)XTextArea_BorderColor_SET },
    { "HasBorder","boolean",(void*)XTextArea_HasBorder_GET,(void*)XTextArea_HasBorder_SET }, */
    { "FontName","string",(void*)XTextArea_FontName_GET,(void*)XTextArea_FontName_SET },
    { "FontSize","integer",(void*)XTextArea_FontSize_GET,(void*)XTextArea_FontSize_SET },
    { "Enabled","boolean",(void*)XTextArea_Enabled_GET,(void*)XTextArea_Enabled_SET },
    { "Visible","boolean",(void*)XTextArea_Visible_GET,(void*)XTextArea_Visible_SET },
    { "ScrollPosition","integer",(void*)XTextArea_ScrollPosition_GET,(void*)XTextArea_ScrollPosition_SET },
    { "TextChanged","string",(void*)XTextArea_TextChanged_GET,nullptr }
};

// methods array  (now includes Close)
static ClassEntry methods[] = {
    { "XTextArea_SetEventCallback",(void*)XTextArea_SetEventCallback,3,{"integer","string","pointer"},"boolean" },
    { "AddText",(void*)XTextArea_AddText,2,{"integer","string"},"void" },
    { "Invalidate",(void*)XTextArea_Invalidate,1,{"integer"},"void" },

    { "Close",(void*)Close,1,{"integer"},"void" }
};

static ClassDefinition classDef = {
    "XTextArea",
    sizeof(XTextArea),
    (void*)Constructor,
    props,   sizeof(props)/sizeof(props[0]),
    methods, sizeof(methods)/sizeof(methods[0]),
    nullptr, 0
};

XPLUGIN_API ClassDefinition* GetClassDefinition(){ return &classDef; }

//------------------------------------------------------------------------------
// DLL entry-point (Windows) / destructor (POSIX)
//------------------------------------------------------------------------------
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE, DWORD why, LPVOID)
{
    if(why==DLL_PROCESS_DETACH) CleanupInstances();
    return TRUE;
}
#else
__attribute__((destructor)) static void onUnload(){ CleanupInstances(); }
#endif

} // extern "C"
