/*

  XCanvas.cpp
  CrossBasic Plugin: XCanvas                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <mutex>
#include <unordered_map>
#include <random>
#include <string>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define XPLUGIN_API __declspec(dllexport)
#define strdup _strdup

/* ------------------------------------------------------------------------
   Dynamic bindings
------------------------------------------------------------------------ */
static HMODULE             gGfxLib = nullptr;
typedef void  (__cdecl* GfxCloseFn)(int);
typedef void  (__cdecl* GfxBlitFn)(int,HDC);
typedef void  (__cdecl* GfxDrawPicFn)(int,int,int,int,int,int);
typedef int   (__cdecl* GfxCtorWndFn)(int);
typedef int   (__cdecl* GfxCtorPicFn)(int,int,int);

static GfxCloseFn          pGfx_Close             = nullptr;
static GfxBlitFn           pGfx_Blit              = nullptr;
static GfxDrawPicFn        pGfx_DrawPicture       = nullptr;
static GfxCtorWndFn        pGfx_Constructor       = nullptr;
static GfxCtorPicFn        pGfx_ConstructorPicture= nullptr;   // single definition!

/* --- XPicture bridge --- */
static HMODULE  gPicLib           = nullptr;
typedef int (__cdecl* PicGfxGetFn)(int);
static PicGfxGetFn pPic_Graphics_GET = nullptr;

/* -------------------------------------------------------------------- */
static bool EnsureGfxBindings()
{
    if (gGfxLib) return true;
    // gGfxLib = LoadLibraryA("XGraphics.dll");
    // if (!gGfxLib) { std::cerr<<"XCanvas: cannot load XGraphics.dll\n"; return false; }
    gGfxLib = GetModuleHandleA("XGraphics.dll");
    if (!gGfxLib)                       // not yet? then load it
        gGfxLib = LoadLibraryA("XGraphics.dll");

    if (!gGfxLib) { std::cerr << "...cannot load XGraphics.dll\n"; return false; }

    pGfx_Close              = (GfxCloseFn) GetProcAddress(gGfxLib,"XGraphics_Close");
    pGfx_Blit               = (GfxBlitFn)  GetProcAddress(gGfxLib,"XGraphics_Blit");
    pGfx_DrawPicture        = (GfxDrawPicFn)GetProcAddress(gGfxLib,"XGraphics_DrawPicture");
    pGfx_Constructor        = (GfxCtorWndFn)GetProcAddress(gGfxLib,"XGraphics_Constructor");
    pGfx_ConstructorPicture = (GfxCtorPicFn)GetProcAddress(gGfxLib,"XGraphics_ConstructorPicture");

    if(!pGfx_Close||!pGfx_Blit||!pGfx_DrawPicture||!pGfx_Constructor||!pGfx_ConstructorPicture){
        std::cerr<<"XCanvas: missing exports in XGraphics.dll\n"; return false;
    }
    return true;
}
static bool EnsurePicBindings()
{
    if (gPicLib) return true;
    gPicLib = LoadLibraryA("XPicture.dll");
    if (!gPicLib) { std::cerr<<"XCanvas: cannot load XPicture.dll\n"; return false; }
    pPic_Graphics_GET = (PicGfxGetFn)GetProcAddress(gPicLib,"XPicture_Graphics_GET");
    return (bool)pPic_Graphics_GET;
}

/* -------------------------------------------------------------------- */
/*                     Per-instance state & helpers                     */
struct CanvasInst{
    int     gfxHandle   = 0;         // our double-buffer
    int     backdropPic  = 0;         // optional background picture (XPicture handle)
    HWND    hwnd        = nullptr;
    int     x=0,y=0,w=0,h=0,parent=0;
    bool    created     = false;
};

static std::mutex                                      gMx;
static std::unordered_map<int,CanvasInst*>             gInst;
static std::mt19937                                    gRng{std::random_device{}()};
static std::uniform_int_distribution<int>              gDist(10000000,99999999);

/* -------- event dispatch -------- */
static std::mutex gEvMx;
static std::unordered_map<int,std::unordered_map<std::string,void*>> gCallbacks;

// Used to synthesize double-clicks on window classes that don't deliver WM_LBUTTONDBLCLK.
struct ClickState { DWORD lastDown=0; int lastX=0; int lastY=0; };
static std::unordered_map<int, ClickState> gClick;

static void fire(int h,const std::string& ev,const char* param)
{
    void* fp=nullptr;
    { std::lock_guard<std::mutex> lk(gEvMx);
      auto it=gCallbacks.find(h);
      if(it!=gCallbacks.end()){
        auto jt=it->second.find(ev);
        if(jt!=it->second.end()) fp=jt->second;
      }}
    if(!fp) return;

    // Match XButton's callback contract: pass a stable, heap-backed UTF-8 string
    // for the duration of the callback, then free it.
    char* data = strdup(param ? param : "");
#ifdef _WIN32
    using CB = void(__stdcall*)(const char*);
#else
    using CB = void(*)(const char*);
#endif
    ((CB)fp)(data);
    free(data);
}

/* -------------------------------------------------------------------- */
/*                    Win32 subclass – real painting                    */
#ifdef _WIN32
static LRESULT CALLBACK CanvasProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,
                                   UINT_PTR, DWORD_PTR ref)
{
    int h=(int)ref;
    CanvasInst* ci=nullptr;
    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it != gInst.end()) ci = it->second;
    }
    if (!ci) return DefSubclassProc(hwnd,msg,wp,lp);
    switch(msg){
    /* case WM_SIZE:{
        ci->w=LOWORD(lp); ci->h=HIWORD(lp);
        if(ci->gfxHandle) pGfx_Close(ci->gfxHandle);
        ci->gfxHandle = pGfx_ConstructorPicture(ci->w?ci->w:1,ci->h?ci->h:1,32);
        break;
    } */
    /* case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);           // <-- always valid
        ci->w = rc.right  - rc.left;
        ci->h = rc.bottom - rc.top;

        if (ci->gfxHandle) pGfx_Close(ci->gfxHandle);
        ci->gfxHandle =
            pGfx_ConstructorPicture(ci->w ? ci->w : 1,
                                    ci->h ? ci->h : 1,
                                    32);
        break;
    } */
   case WM_SIZE:
    {
        /* Always ask the control itself – this is never 0 × 0 */
        RECT rc;
        GetClientRect(hwnd, &rc);

        ci->w = rc.right  - rc.left;
        ci->h = rc.bottom - rc.top;

        /* rebuild back-buffer */
        if (ci->gfxHandle) pGfx_Close(ci->gfxHandle);
        ci->gfxHandle = pGfx_ConstructorPicture(
                            ci->w ? ci->w : 1,
                            ci->h ? ci->h : 1,
                            32);
        break;
    }

    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);

        /* draw optional backdrop */
        if(ci->backdropPic)
            // Pass the XPicture handle; XGraphics_DrawPicture will resolve it to a bitmap-backed XGraphics.
            pGfx_DrawPicture(ci->gfxHandle,ci->backdropPic,0,0,ci->w,ci->h);

        /* user Paint event */
        char buf[32]; sprintf(buf,"%d",ci->gfxHandle);
        fire(h,"paint",buf);

        pGfx_Blit(ci->gfxHandle,dc);
        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);

        char b[64];
        std::snprintf(b, sizeof(b), "%d,%d", mx, my);
        fire(h, "mousedown", b);

        // Capture so the script reliably receives MouseUp even if the user drags outside the control.
        SetCapture(hwnd);

        // Synthesize DoubleClick if the underlying window class doesn't deliver WM_LBUTTONDBLCLK.
        DWORD now = GetTickCount();
        ClickState &cs = gClick[h];
        UINT dblTime = GetDoubleClickTime();
        int maxDx = GetSystemMetrics(SM_CXDOUBLECLK);
        int maxDy = GetSystemMetrics(SM_CYDOUBLECLK);

        if (cs.lastDown != 0 &&
            (now - cs.lastDown) <= dblTime &&
            std::abs(mx - cs.lastX) <= maxDx &&
            std::abs(my - cs.lastY) <= maxDy)
        {
            fire(h, "doubleclick", b);
            cs.lastDown = 0;
        }
        else
        {
            cs.lastDown = now;
            cs.lastX = mx;
            cs.lastY = my;
        }
        break;
    }
    case WM_LBUTTONUP: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);

        char b[64];
        std::snprintf(b, sizeof(b), "%d,%d", mx, my);
        fire(h, "mouseup", b);

        if (GetCapture() == hwnd) ReleaseCapture();
        break;
    }
    case WM_MOUSEMOVE: {
        char b[64];
        std::snprintf(b, sizeof(b), "%d,%d", GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        fire(h, "mousemove", b);
        break;
    }
    case WM_LBUTTONDBLCLK: {
        // If the class supports true WM_LBUTTONDBLCLK, fire the event and reset our synthesizer.
        char b[64];
        std::snprintf(b, sizeof(b), "%d,%d", GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        fire(h, "doubleclick", b);
        gClick[h].lastDown = 0;
        break;
    }
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
#endif

/* -------------------------------------------------------------------- */
/*                              API                                     */
extern "C"{

XPLUGIN_API int  XCanvas_Constructor()
{
    std::lock_guard<std::mutex> lk(gMx);
    int h; do{h=gDist(gRng);}while(gInst.count(h));
    gInst[h]=new CanvasInst;
    return h;
}
XPLUGIN_API void XCanvas_Close(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end())return;
    if(pGfx_Close && it->second->gfxHandle) pGfx_Close(it->second->gfxHandle);
    delete it->second; gInst.erase(it);
}

/* ---- simple properties (Left/Top/Width/Height) ----
   IMPORTANT: never hold gMx while calling Win32 APIs like MoveWindow/DestroyWindow/CreateWindowEx,
   because those APIs can synchronously send messages (WM_SIZE/WM_PAINT/etc) back into CanvasProc,
   which also locks gMx → deadlock.
*/

XPLUGIN_API int XCanvas_X_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it != gInst.end()) ? it->second->x : 0;
}

XPLUGIN_API void XCanvas_X_SET(int h, int v)
{
    HWND hwnd = nullptr;
    bool created = false;
    int x = 0, y = 0, w = 0, hh = 0;

    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) return;
        CanvasInst* ci = it->second;
        ci->x = v;

        created = ci->created;
        hwnd = ci->hwnd;
        x = ci->x; y = ci->y; w = ci->w; hh = ci->h;
    }

    if (created && hwnd) {
        MoveWindow(hwnd, x, y, w, hh, TRUE);
    }
}

XPLUGIN_API int XCanvas_Y_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it != gInst.end()) ? it->second->y : 0;
}

XPLUGIN_API void XCanvas_Y_SET(int h, int v)
{
    HWND hwnd = nullptr;
    bool created = false;
    int x = 0, y = 0, w = 0, hh = 0;

    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) return;
        CanvasInst* ci = it->second;
        ci->y = v;

        created = ci->created;
        hwnd = ci->hwnd;
        x = ci->x; y = ci->y; w = ci->w; hh = ci->h;
    }

    if (created && hwnd) {
        MoveWindow(hwnd, x, y, w, hh, TRUE);
    }
}

XPLUGIN_API int XCanvas_Width_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it != gInst.end()) ? it->second->w : 0;
}

XPLUGIN_API void XCanvas_Width_SET(int h, int v)
{
    HWND hwnd = nullptr;
    bool created = false;
    int x = 0, y = 0, w = 0, hh = 0;

    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) return;
        CanvasInst* ci = it->second;
        ci->w = v;

        created = ci->created;
        hwnd = ci->hwnd;
        x = ci->x; y = ci->y; w = ci->w; hh = ci->h;
    }

    if (created && hwnd) {
        MoveWindow(hwnd, x, y, w, hh, TRUE);
    }
}

XPLUGIN_API int XCanvas_Height_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it != gInst.end()) ? it->second->h : 0;
}

XPLUGIN_API void XCanvas_Height_SET(int h, int v)
{
    HWND hwnd = nullptr;
    bool created = false;
    int x = 0, y = 0, w = 0, hh = 0;

    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) return;
        CanvasInst* ci = it->second;
        ci->h = v;

        created = ci->created;
        hwnd = ci->hwnd;
        x = ci->x; y = ci->y; w = ci->w; hh = ci->h;
    }

    if (created && hwnd) {
        MoveWindow(hwnd, x, y, w, hh, TRUE);
    }
}

/* ---- Parent (creates the real HWND) -------------------------------- */
XPLUGIN_API void XCanvas_Parent_SET(int h, int parent)
{
    // Grab instance and detach any existing HWND/back-buffer under the lock
    CanvasInst* ci = nullptr;
    HWND oldHwnd = nullptr;
    int oldGfx = 0;

    int x = 0, y = 0, w = 0, hh = 0;

    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) return;
        ci = it->second;

        if (ci->created) {
            oldHwnd = ci->hwnd;
            oldGfx  = ci->gfxHandle;

            ci->created = false;
            ci->hwnd = nullptr;
            ci->gfxHandle = 0;
        }

        ci->parent = parent;

        x = ci->x;
        y = ci->y;
        w = ci->w;
        hh = ci->h;
    }

    // Tear down old resources OUTSIDE the lock (avoids message re-entrancy deadlocks)
    if (oldHwnd) {
        DestroyWindow(oldHwnd);
    }
    if (oldGfx && pGfx_Close) {
        pGfx_Close(oldGfx);
    }

    // Resolve the parent HWND from XWindow
    using Fn = HWND(*)(int);
    static Fn getHWND = nullptr;
    if (!getHWND) {
        HMODULE m = GetModuleHandleA("XWindow.dll");
        if (m) getHWND = (Fn)GetProcAddress(m, "XWindow_GetHWND");
    }

    HWND phwnd = getHWND ? getHWND(parent) : nullptr;
    if (!phwnd) return;

    // Create the control window OUTSIDE the lock (CreateWindowEx can synchronously send messages)
    HWND hwnd = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        x, y, w, hh,
        phwnd,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    if (!hwnd) return;

    EnsureGfxBindings();
    int gfxHandle = pGfx_ConstructorPicture(w ? w : 1, hh ? hh : 1, 32);

    // Publish state under lock
    {
        std::lock_guard<std::mutex> lk(gMx);
        auto it = gInst.find(h);
        if (it == gInst.end()) {
            // Extremely unlikely; clean up to avoid leaks
            if (gfxHandle && pGfx_Close) pGfx_Close(gfxHandle);
            DestroyWindow(hwnd);
            return;
        }
        it->second->hwnd = hwnd;
        it->second->created = true;
        it->second->gfxHandle = gfxHandle;
    }

    // Install subclass after state is valid
    SetWindowSubclass(hwnd, CanvasProc, 0, (DWORD_PTR)h);
}

XPLUGIN_API int XCanvas_Parent_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return it != gInst.end() ? it->second->parent : 0;
}

/* ---- exposed handles ----------------------------------------------- */
XPLUGIN_API int  XCanvas_Graphics_GET (int h){std::lock_guard<std::mutex>lk(gMx);auto it=gInst.find(h);return it!=gInst.end()?it->second->gfxHandle:0;}
XPLUGIN_API void XCanvas_Backdrop_SET(int h,int picH)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h);
    if(it==gInst.end()) return;
    it->second->backdropPic = picH;
    if(it->second->created) InvalidateRect(it->second->hwnd,nullptr,TRUE);
}
XPLUGIN_API int  XCanvas_Backdrop_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h);
    return it!=gInst.end()?it->second->backdropPic:0;
}

/* ---- misc ----------------------------------------------------------- */
XPLUGIN_API void XCanvas_Refresh   (int h){std::lock_guard<std::mutex>lk(gMx);auto it=gInst.find(h);if(it!=gInst.end()&&it->second->created)InvalidateRect(it->second->hwnd,nullptr,TRUE);}
XPLUGIN_API void XCanvas_Invalidate(int h){XCanvas_Refresh(h);}

/* ---- event token getters ------------------------------------------- */
#define TOKEN_GETTER(name,evt) XPLUGIN_API const char* XCanvas_##name##_GET(int h){std::string s="xcanvas:"+std::to_string(h)+":"+evt;return strdup(s.c_str());}
TOKEN_GETTER(Paint,"paint") TOKEN_GETTER(MouseDown,"mousedown") TOKEN_GETTER(MouseUp,"mouseup")
TOKEN_GETTER(MouseMove,"mousemove") TOKEN_GETTER(DoubleClick,"doubleclick")

XPLUGIN_API bool XCanvas_SetEventCallback(int h,const char* token,void* fp)
{
    { std::lock_guard<std::mutex> lk(gMx); if(!gInst.count(h)) return false; }
    std::string key=token?token:"";
    if(auto p=key.rfind(':');p!=std::string::npos) key.erase(0,p+1);
    std::lock_guard<std::mutex>lk(gEvMx); gCallbacks[h][key]=fp; return true;
}

/* -------------------------------------------------------------------- */
/*         CrossBasic registration – now publishes event constants      */
//------------------------------------------------------------------------------
// ClassDefinition + DllMain
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

static ClassConstant kConsts[] = {
};


static ClassProperty kProps[]={
    {"Left","integer",(void*)XCanvas_X_GET,(void*)XCanvas_X_SET},
    {"Top","integer",(void*)XCanvas_Y_GET,(void*)XCanvas_Y_SET},
    {"Width","integer",(void*)XCanvas_Width_GET,(void*)XCanvas_Width_SET},
    {"Height","integer",(void*)XCanvas_Height_GET,(void*)XCanvas_Height_SET},
    {"Parent","integer",(void*)XCanvas_Parent_GET,(void*)XCanvas_Parent_SET},
    {"Graphics","XGraphics",(void*)XCanvas_Graphics_GET,nullptr},
    {"Backdrop","XPicture",(void*)XCanvas_Backdrop_GET,(void*)XCanvas_Backdrop_SET},
    {"Paint","string",(void*)XCanvas_Paint_GET,nullptr},
    {"MouseDown","string",(void*)XCanvas_MouseDown_GET,nullptr},
    {"MouseUp","string",(void*)XCanvas_MouseUp_GET,nullptr},
    {"MouseMove","string",(void*)XCanvas_MouseMove_GET,nullptr},
    {"DoubleClick","string",(void*)XCanvas_DoubleClick_GET,nullptr}
};
static ClassEntry kMethods[]={
    {"Refresh",   (void*)XCanvas_Refresh,    1,{"integer"},"void"},
    {"Invalidate",(void*)XCanvas_Invalidate, 1,{"integer"},"void"},
    {"XCanvas_SetEventCallback",(void*)XCanvas_SetEventCallback,3,{"integer","string","pointer"},"boolean"}
};
typedef struct{
    const char*     className;
    size_t          classSize;
    void*           ctor;
    ClassProperty*         props; size_t propsCnt;
    ClassEntry*        meths; size_t methCnt;
    ClassConstant*         consts;size_t constCnt;
} CBClassDef;

static ClassDefinition gDef={
    "XCanvas",sizeof(CanvasInst),
    (void*)XCanvas_Constructor,
    kProps,   sizeof(kProps  )/sizeof(kProps[0]),
    kMethods, sizeof(kMethods)/sizeof(kMethods[0]),
    kConsts,  sizeof(kConsts )/sizeof(kConsts[0])
};

XPLUGIN_API ClassDefinition* GetClassDefinition(){ return &gDef; }

/* ---- DLL unload ---- */
static void CleanupAll(){
    std::lock_guard<std::mutex> lk(gMx);
    for(auto&kv:gInst){ if(pGfx_Close && kv.second->gfxHandle) pGfx_Close(kv.second->gfxHandle); delete kv.second; }
    gInst.clear();
    { std::lock_guard<std::mutex> lk2(gEvMx); gCallbacks.clear(); gClick.clear(); }
    if(gGfxLib) FreeLibrary(gGfxLib); if(gPicLib) FreeLibrary(gPicLib);
}
BOOL APIENTRY DllMain(HMODULE, DWORD r, LPVOID){ if(r==DLL_PROCESS_DETACH) CleanupAll(); return TRUE; }

} // extern "C"
