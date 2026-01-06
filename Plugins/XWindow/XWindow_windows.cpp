/*

  XWindow.cpp
  CrossBasic Plugin: XWindow                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
/*  ── Build commands ────────────────────────────────────────────────────────────
   windres XWindow.rc -O coff -o XWindow.res
   g++ -std=c++17 -shared -m64 -static -static-libgcc -static-libstdc++ \
       -o XWindow.dll XWindow.cpp XWindow.res -pthread -s \
       -lcomctl32 -ldwmapi -lgdi32 -luser32 -luxtheme -ole32
*/
#include <mutex>
#include <deque>
#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <string>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <random>

#ifdef _WIN32
  #include <windows.h>
  #include <windowsx.h>
  #include <CommCtrl.h>
  #include <Shlwapi.h>
  #include <ShObjIdl.h>
  #include <shellapi.h>
  #include <shobjidl.h>
  #include <dwmapi.h>
  #pragma comment(lib, "shlwapi.lib")
  #pragma comment(lib, "Comctl32.lib")
  #pragma comment(lib, "Dwmapi.lib")
  #define XPLUGIN_API __declspec(dllexport)
#elif defined(__APPLE__)
  // Compile on macOS as Objective-C++:
  //   clang++ -std=c++17 -x objective-c++ -dynamiclib -o XWindow.dylib XWindow.cpp -framework Cocoa -framework CoreGraphics
  #include <Cocoa/Cocoa.h>
  #include <CoreGraphics/CoreGraphics.h>
  #define XPLUGIN_API __attribute__((visibility("default")))
#elif defined(__linux__)
  #include <X11/Xlib.h>
  #include <X11/Xatom.h>
  #include <X11/Xutil.h>
  #include <unistd.h>
  #define XPLUGIN_API __attribute__((visibility("default")))
#else
  #define XPLUGIN_API __attribute__((visibility("default")))
#endif

#ifdef _WIN32
  #ifndef WM_XWINDOW_OPEN
  #define WM_XWINDOW_OPEN   (WM_APP + 0x100)
  #endif
  #ifndef WM_XWINDOW_CLOSE
  #define WM_XWINDOW_CLOSE  (WM_APP + 0x101)
  #endif
#else
  // Not used on non-Windows platforms (we use native event loops).
  #ifndef WM_XWINDOW_OPEN
  #define WM_XWINDOW_OPEN   0
  #endif
  #ifndef WM_XWINDOW_CLOSE
  #define WM_XWINDOW_CLOSE  0
  #endif
#endif

#ifdef _WIN32
  #include <gdiplus.h>
  #pragma comment(lib, "gdiplus.lib")
  using namespace Gdiplus;
  static ULONG_PTR gdiPlusToken = 0;
#endif

#define DBG_PREFIX "XWindow DEBUG: "
#define DBG(msg)  do { std::cout<<DBG_PREFIX<<msg<<std::endl; } while(0)

static void triggerEvent(int, const std::string&, const char*);
#ifdef _WIN32
static LRESULT CALLBACK XWindow_WndProc(HWND, UINT, WPARAM, LPARAM);
#endif

class XWindow
{
public:
    int                 handle;
  std::mutex          readyMutex;
  std::condition_variable readyCv;
  bool                ready{false};

    int                 winType{0};
    std::string         titleCache{"CrossBasic Window"};
    unsigned int        bgColorVal{0x202020};

    // Style toggles (best-effort on non-Windows).
    bool HasCloseButton{true};
    bool HasMinimizeButton{true};
    bool HasMaximizeButton{true};
    bool HasFullScreenButton{false};
    bool HasTitleBar{true};
    bool Resizable{true};


#ifdef _WIN32
    HWND                hwnd{nullptr};
    std::thread         msgThread;
    COLORREF            bgColor{RGB(32,32,32)};
    HBRUSH              bgBrush{nullptr};


    void updateWindowStyles()
    {
        DWORD style = WS_OVERLAPPED;
        if (HasTitleBar)    style |= WS_CAPTION;
        if (HasCloseButton) style |= WS_SYSMENU;
        if (HasMinimizeButton) style |= WS_MINIMIZEBOX;
        if (HasMaximizeButton || HasFullScreenButton) style |= WS_MAXIMIZEBOX;
        if (Resizable)      style |= WS_THICKFRAME;

        ::SetWindowLongPtrA(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0,0,0,0,
                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
    }
#endif

#if defined(__linux__)
  // X11 backend (Linux/BSD with X11).
  Display* x11Display = nullptr;
  ::Window x11Window = 0;
  Atom x11WmDelete = None;
  int x11Screen = 0;
  bool x11Mapped = false;
  std::mutex x11Mutex;

  // Cached geometry (updated in event loop).
  int x11X = 0;
  int x11Y = 0;
  int x11W = 0;
  int x11H = 0;
#elif defined(__APPLE__)
  // Cocoa backend (macOS).
  NSApplication* cocoaApp = nil;
  NSWindow* cocoaWindow = nil;
  id cocoaDelegate = nil;
  std::mutex cocoaQueueMutex;
  std::deque<std::function<void()>> cocoaQueue;
  bool cocoaReady = false;

  // Cached geometry (updated in event loop).
  int cocoaX = 0;
  int cocoaY = 0;
  int cocoaW = 0;
  int cocoaH = 0;
#endif

#ifndef _WIN32
    void runNonWindowsLoop();
#if defined(__APPLE__)
    void enqueueCocoa(std::function<void()> fn);
#endif
#endif

    std::atomic<bool>   running{false};
    std::thread        windowThread;

    bool                openedFired{false};
    bool                inSizeMove{false};

    explicit XWindow(int h) : handle(h) {
        running = true;
#ifdef _WIN32
        registerClass();
        createWindowHidden();
        bgBrush = CreateSolidBrush(bgColor);
        msgThread = std::thread([this]{ messageLoop(); });
#else
        windowThread = std::thread([this]{ runNonWindowsLoop(); });
        std::unique_lock<std::mutex> lk(readyMutex);
        readyCv.wait_for(lk, std::chrono::milliseconds(500), [this]{ return ready; });
#endif
    }

    ~XWindow() {
        running = false;
#ifdef _WIN32
        if (hwnd) PostMessageA(hwnd, WM_CLOSE, 0, 0);
        if (msgThread.joinable()) msgThread.join();
        if (bgBrush) DeleteObject(bgBrush);
#else
        // Ask the UI loop to quit and wait for it.
  #if defined(__linux__)
        {
            std::lock_guard<std::mutex> lk(x11Mutex);
            if (x11Display && x11Window) {
                // Trigger a polite close via WM_DELETE_WINDOW if possible.
                if (x11WmDelete != None) {
                    XEvent ev{};
                    ev.xclient.type = ClientMessage;
                    ev.xclient.window = x11Window;
                    ev.xclient.message_type = XInternAtom(x11Display, "WM_PROTOCOLS", True);
                    ev.xclient.format = 32;
                    ev.xclient.data.l[0] = (long)x11WmDelete;
                    ev.xclient.data.l[1] = CurrentTime;
                    XSendEvent(x11Display, x11Window, False, NoEventMask, &ev);
                    XFlush(x11Display);
                } else {
                    XDestroyWindow(x11Display, x11Window);
                    XFlush(x11Display);
                }
            }
        }
  #elif defined(__APPLE__)
        enqueueCocoa([this]{
            if (cocoaWindow) [cocoaWindow close];
        });
  #endif
        if (windowThread.joinable()) windowThread.join();
#endif
    }

#ifdef _WIN32
    void setBackgroundColor(unsigned int rgb) {
        bgColorVal = rgb & 0xFFFFFFu;
        BYTE r=(bgColorVal>>16)&0xFF, g=(bgColorVal>>8)&0xFF, b=bgColorVal&0xFF;
        bgColor = RGB(r,g,b);
        if(bgBrush) DeleteObject(bgBrush);
        bgBrush = CreateSolidBrush(bgColor);
        if(hwnd) {
            InvalidateRect(hwnd,nullptr,TRUE);
            UpdateWindow(hwnd);
        }
    }
    unsigned int getBackgroundColor() const { return bgColorVal; }

private:
  void runNonWindowsLoop();
#if defined(__APPLE__)
  void enqueueCocoa(std::function<void()> fn);
#endif

    static void registerClass() {
        static std::once_flag once;
        std::call_once(once, []{
            WNDCLASSEXA wc{};
            wc.cbSize      = sizeof(wc);
            wc.lpfnWndProc = XWindow_WndProc;
            wc.hInstance   = GetModuleHandleA(NULL);
            wc.hCursor     = LoadCursorA(NULL, IDC_ARROW);
            wc.lpszClassName = "XWindowClass";
            RegisterClassExA(&wc);
        });
    }

    void createWindowHidden() {
        DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        hwnd = CreateWindowExA(
            0, "XWindowClass", "XWindow",
            style, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            NULL, NULL, GetModuleHandleA(NULL),
            reinterpret_cast<LPVOID>(this)
        );
        BOOL dark=TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    }

    void messageLoop() {
        MSG msg;
        while(running && GetMessageA(&msg,NULL,0,0)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

public:
    static void applyStyle(HWND hwnd, int t);
#endif
};

#ifdef _WIN32
static const struct{DWORD s,ex;} gStyles[6] = {
    {WS_OVERLAPPEDWINDOW,0},
    {WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,WS_EX_TOPMOST},
    {WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,WS_EX_DLGMODALFRAME},
    {WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,WS_EX_TOOLWINDOW},
    {WS_OVERLAPPED|WS_CAPTION,0},
    {WS_OVERLAPPED|WS_CAPTION,WS_EX_TOOLWINDOW|WS_EX_DLGMODALFRAME|WS_EX_LAYERED}
};
void XWindow::applyStyle(HWND hwnd,int t){
    if(t<0||t>5) t=0;
    SetWindowLongPtrA(hwnd,GWL_STYLE,gStyles[t].s);
    SetWindowLongPtrA(hwnd,GWL_EXSTYLE,gStyles[t].ex);
    SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
}
#endif

static std::mutex g_instancesMtx;

// ------------------------------
// Non-Windows backends
// ------------------------------
#if defined(__linux__)

static unsigned long x11AllocARGB(Display* dpy, int screen, uint32_t argb) {
    XColor color{};
    color.red   = (unsigned short)(((argb >> 16) & 0xFF) * 257);
    color.green = (unsigned short)(((argb >> 8)  & 0xFF) * 257);
    color.blue  = (unsigned short)(((argb >> 0)  & 0xFF) * 257);
    color.flags = DoRed | DoGreen | DoBlue;
    Colormap cmap = DefaultColormap(dpy, screen);
    if (XAllocColor(dpy, cmap, &color)) return color.pixel;
    return BlackPixel(dpy, screen);
}

static void x11SendNetWmState(Display* dpy, int screen, Window win, bool add, Atom state1, Atom state2 = None) {
    Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = win;
    e.xclient.message_type = wmState;
    e.xclient.format = 32;
    e.xclient.data.l[0] = add ? 1 : 0; // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE
    e.xclient.data.l[1] = (long)state1;
    e.xclient.data.l[2] = (long)state2;
    e.xclient.data.l[3] = 1; // source indication
    e.xclient.data.l[4] = 0;
    Window root = RootWindow(dpy, screen);
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(dpy);
}

#elif defined(__APPLE__)

@interface XWindowCocoaDelegate : NSObject<NSWindowDelegate>
@property(nonatomic, assign) void* ownerPtr;
@end

@implementation XWindowCocoaDelegate
- (BOOL)windowShouldClose:(id)sender {
    // The C++ owner will observe 'running' and shut down the loop.
    return YES;
}
- (void)windowWillClose:(NSNotification*)notification {
    // Nothing here; closing is handled by the C++ loop.
}
@end

static NSColor* cocoaColorFromARGB(uint32_t argb) {
    CGFloat r = ((argb >> 16) & 0xFF) / 255.0;
    CGFloat g = ((argb >> 8)  & 0xFF) / 255.0;
    CGFloat b = ((argb >> 0)  & 0xFF) / 255.0;
    CGFloat a = ((argb >> 24) & 0xFF) / 255.0;
    return [NSColor colorWithCalibratedRed:r green:g blue:b alpha:a];
}

#endif

#ifndef _WIN32
void XWindow::runNonWindowsLoop() {
#if defined(__linux__)
    XInitThreads();
    x11Display = XOpenDisplay(nullptr);
    if (!x11Display) {
        std::lock_guard<std::mutex> lk(readyMutex);
        ready = true;
        readyCv.notify_all();
        return;
    }

    x11Screen = DefaultScreen(x11Display);

    // Defaults, can be changed later through setters.
    x11X = 100;
    x11Y = 100;
    x11W = 800;
    x11H = 600;

    {
        std::lock_guard<std::mutex> lk(x11Mutex);
        x11Window = XCreateSimpleWindow(
            x11Display,
            RootWindow(x11Display, x11Screen),
            x11X, x11Y, (unsigned int)x11W, (unsigned int)x11H,
            0,
            BlackPixel(x11Display, x11Screen),
            x11AllocARGB(x11Display, x11Screen, bgColorVal));

        XSelectInput(x11Display, x11Window,
                     ExposureMask | StructureNotifyMask |
                     KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask);

        x11WmDelete = XInternAtom(x11Display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(x11Display, x11Window, &x11WmDelete, 1);

        XStoreName(x11Display, x11Window, titleCache.c_str());

        // Best-effort style flags.
        if (!Resizable) {
            XSizeHints hints{};
            hints.flags = PMinSize | PMaxSize;
            hints.min_width = x11W; hints.max_width = x11W;
            hints.min_height = x11H; hints.max_height = x11H;
            XSetWMNormalHints(x11Display, x11Window, &hints);
        }

        XMapWindow(x11Display, x11Window);
        XFlush(x11Display);
        x11Mapped = true;
    }

    {
        std::lock_guard<std::mutex> lk(readyMutex);
        ready = true;
    }
    readyCv.notify_all();

    triggerEvent(handle, "Opening", nullptr);

    while (running) {
        while (true) {
            XEvent ev{};
            {
                std::lock_guard<std::mutex> lk(x11Mutex);
                if (!x11Display || !x11Window) break;
                if (XPending(x11Display) <= 0) break;
                XNextEvent(x11Display, &ev);
            }

            switch (ev.type) {
                case Expose: {
                    std::lock_guard<std::mutex> lk(x11Mutex);
                    if (x11Display && x11Window) {
                        XClearWindow(x11Display, x11Window);
                        XFlush(x11Display);
                    }
                } break;

                case ConfigureNotify: {
                    x11X = ev.xconfigure.x;
                    x11Y = ev.xconfigure.y;
                    x11W = ev.xconfigure.width;
                    x11H = ev.xconfigure.height;
                } break;

                case ClientMessage: {
                    if ((Atom)ev.xclient.data.l[0] == x11WmDelete) {
                        running = false;
                    }
                } break;

                default: break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    triggerEvent(handle, "Closing", nullptr);

    {
        std::lock_guard<std::mutex> lk(x11Mutex);
        if (x11Display && x11Window) {
            XDestroyWindow(x11Display, x11Window);
            x11Window = 0;
        }
        if (x11Display) {
            XCloseDisplay(x11Display);
            x11Display = nullptr;
        }
        x11Mapped = false;
    }

#elif defined(__APPLE__)
    @autoreleasepool {
        cocoaApp = [NSApplication sharedApplication];
        [cocoaApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSUInteger style =
            (HasTitleBar ? NSWindowStyleMaskTitled : 0) |
            (HasCloseButton ? NSWindowStyleMaskClosable : 0) |
            (HasMinimizeButton ? NSWindowStyleMaskMiniaturizable : 0) |
            (Resizable ? NSWindowStyleMaskResizable : 0);

        NSRect rect = NSMakeRect(100, 100, 800, 600);
        cocoaWindow = [[NSWindow alloc] initWithContentRect:rect
                                                  styleMask:style
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        [cocoaWindow setTitle:[NSString stringWithUTF8String:titleCache.c_str()]];
        [cocoaWindow setBackgroundColor:cocoaColorFromARGB(bgColorVal)];
        [cocoaWindow setReleasedWhenClosed:NO];

        cocoaDelegate = [[XWindowCocoaDelegate alloc] init];
        [cocoaWindow setDelegate:(id<NSWindowDelegate>)cocoaDelegate];

        [cocoaWindow makeKeyAndOrderFront:nil];
        [cocoaApp activateIgnoringOtherApps:YES];

        cocoaReady = true;

        {
            std::lock_guard<std::mutex> lk(readyMutex);
            ready = true;
        }
        readyCv.notify_all();

        triggerEvent(handle, "Opening", nullptr);

        while (running) {
            // Run queued UI tasks.
            std::deque<std::function<void()>> q;
            {
                std::lock_guard<std::mutex> lk(cocoaQueueMutex);
                q.swap(cocoaQueue);
            }
            for (auto &fn : q) fn();

            // Pump events (manual run loop).
            NSEvent* event = [cocoaApp nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
            if (event) {
                [cocoaApp sendEvent:event];
                [cocoaApp updateWindows];
            }

            // Detect close.
            if (cocoaWindow && ![cocoaWindow isVisible]) {
                // If the user closes the window, we may end up here.
                // We'll treat this as a close request.
                running = false;
            }

            if (cocoaWindow) {
                NSRect f = [cocoaWindow frame];
                cocoaX = (int)f.origin.x;
                cocoaY = (int)f.origin.y;
                cocoaW = (int)f.size.width;
                cocoaH = (int)f.size.height;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        triggerEvent(handle, "Closing", nullptr);

        if (cocoaWindow) {
            [cocoaWindow close];
            cocoaWindow = nil;
        }
        cocoaApp = nil;
        cocoaReady = false;
    }
#endif
}

#if defined(__APPLE__)
void XWindow::enqueueCocoa(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(cocoaQueueMutex);
    cocoaQueue.push_back(std::move(fn));
}
#endif
#endif // !_WIN32
static std::unordered_map<int,XWindow*> g_instances;
static std::mt19937 g_rng(std::random_device{}());
static std::uniform_int_distribution<int> g_handleDist(10000000,99999999);
static std::mutex g_eventMtx;
static std::unordered_map<int,std::unordered_map<std::string,void*>> g_eventCallbacks;

static void CleanupInstances(){
    {
        std::lock_guard<std::mutex> lk(g_instancesMtx);
        for(auto &p:g_instances) delete p.second;
        g_instances.clear();
    }
    {
        std::lock_guard<std::mutex> lk(g_eventMtx);
        g_eventCallbacks.clear();
    }
}

static void triggerEvent(int h,const std::string& e,const char* p){
    void* cb=nullptr;
    {
        std::lock_guard<std::mutex> lk(g_eventMtx);
        auto it=g_eventCallbacks.find(h);
        if(it!=g_eventCallbacks.end()){
            auto eit=it->second.find(e);
            if(eit!=it->second.end()) cb=eit->second;
        }
    }
    if(!cb) return;
    using CB=void(*)(const char*);
    char* dup=strdup(p?p:"");
    reinterpret_cast<CB>(cb)(dup);
    free(dup);
}

#ifdef _WIN32
static LRESULT CALLBACK XWindow_WndProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    XWindow* self=nullptr;
    if(m==WM_NCCREATE){
        auto cs=reinterpret_cast<CREATESTRUCTA*>(lp);
        self=reinterpret_cast<XWindow*>(cs->lpCreateParams);
        SetWindowLongPtrA(w,GWLP_USERDATA,(LONG_PTR)self);
    } else {
        self=reinterpret_cast<XWindow*>(GetWindowLongPtrA(w,GWLP_USERDATA));
    }
    switch(m){
        case WM_ERASEBKGND:{
            HDC hdc=(HDC)wp; RECT rc;GetClientRect(w,&rc);
            HBRUSH br=self?self->bgBrush:CreateSolidBrush(RGB(32,32,32));
            FillRect(hdc,&rc,br);
            if(!self) DeleteObject(br);
            return 1;
        }
        case WM_ENTERSIZEMOVE: if(self) self->inSizeMove=true; break;
        case WM_EXITSIZEMOVE:
            if(self){
                self->inSizeMove=false;
                InvalidateRect(w,NULL,TRUE);
                UpdateWindow(w);
            }
            break;
        // case WM_SHOWWINDOW:
        //     if(self&&wp&&!self->openedFired){
        //         self->openedFired=true;
        //         triggerEvent(self->handle,"Opening",NULL);
        //     }
        //     break;
        case WM_SHOWWINDOW:
            if (self && wp && !self->openedFired)
            {
                self->openedFired = true;              // only once
                /*  Post a custom message so the event fires AFTER the window is
                    visible and the message queue returns to idle.                 */
                PostMessageW(w, WM_XWINDOW_OPEN, 0, 0);
            }
            break;

        case WM_XWINDOW_OPEN:
            if (self) triggerEvent(self->handle, "Opening", nullptr);
            return 0;          // already handled – don’t fall through

        case WM_SIZE: case WM_SIZING: case WM_MOVE: case WM_WINDOWPOSCHANGED:
            if(self&&!self->inSizeMove){
                InvalidateRect(w,NULL,TRUE);
                UpdateWindow(w);
            }
            break;
        case WM_CLOSE: case WM_DESTROY:
            if(self) triggerEvent(self->handle,"Closing",NULL);
            DestroyWindow(w);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(w,m,wp,lp);
}
#endif

extern "C"{

XPLUGIN_API int Constructor(){
    int h;
    {
        std::lock_guard<std::mutex> lk(g_instancesMtx);
        do{h=g_handleDist(g_rng);}while(g_instances.count(h));
        g_instances[h]=new XWindow(h);
    }
    return h;
}
XPLUGIN_API void Close(int h){
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end()){
        delete it->second;
        g_instances.erase(it);
    }
}

#ifdef _WIN32
XPLUGIN_API HWND XWindow_GetHWND(int h){
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end())
        return it->second->hwnd;
    return NULL;
}
#endif

XPLUGIN_API int XWindow_Handle_GET(int h){ return h; }
XPLUGIN_API const char* Opening_GET(int h){ return strdup(("XWindow:"+std::to_string(h)+":Opening").c_str()); }
XPLUGIN_API const char* Closing_GET(int h){ return strdup(("XWindow:"+std::to_string(h)+":Closing").c_str()); }

XPLUGIN_API bool XWindow_SetEventCallback(int h,const char* n,void* cb){
    {
        std::lock_guard<std::mutex> lk(g_instancesMtx);
        if(!g_instances.count(h)) return false;
    }
    std::string key(n?n:"");
    if(auto p=key.rfind(':');p!=std::string::npos) key.erase(0,p+1);
    std::lock_guard<std::mutex> lk(g_eventMtx);
    g_eventCallbacks[h][key]=cb;
    return true;
}

//-- HasCloseButton
XPLUGIN_API void XWindow_HasCloseButton_SET(int h, bool v) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end()) return;
    it->second->HasCloseButton = v;
    it->second->updateWindowStyles();
#endif
}
XPLUGIN_API bool XWindow_HasCloseButton_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->HasCloseButton : false;
}

//-- HasMinimizeButton
XPLUGIN_API void XWindow_HasMinimizeButton_SET(int h, bool v) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end()) return;
    it->second->HasMinimizeButton = v;
    it->second->updateWindowStyles();
#endif
}
XPLUGIN_API bool XWindow_HasMinimizeButton_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->HasMinimizeButton : false;
}

//-- HasMaximizeButton
XPLUGIN_API void XWindow_HasMaximizeButton_SET(int h, bool v) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end()) return;
    it->second->HasMaximizeButton = v;
    it->second->updateWindowStyles();
#endif
}
XPLUGIN_API bool XWindow_HasMaximizeButton_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->HasMaximizeButton : false;
}

//-- HasFullScreenButton
XPLUGIN_API void XWindow_HasFullScreenButton_SET(int h, bool v) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end()){
        it->second->HasFullScreenButton = v;
#ifdef _WIN32
        it->second->updateWindowStyles();
#endif
    }
}
XPLUGIN_API bool XWindow_HasFullScreenButton_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->HasFullScreenButton : false;
}

//-- HasTitleBar
XPLUGIN_API void XWindow_HasTitleBar_SET(int h, bool v) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end()) return;
    it->second->HasTitleBar = v;
    it->second->updateWindowStyles();
#endif
}
XPLUGIN_API bool XWindow_HasTitleBar_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->HasTitleBar : false;
}

//-- Resizable
XPLUGIN_API void XWindow_Resizable_SET(int h, bool v) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end()) return;
    it->second->Resizable = v;
    it->second->updateWindowStyles();
#endif
}
XPLUGIN_API bool XWindow_Resizable_GET(int h) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    return (it != g_instances.end()) ? it->second->Resizable : false;
}

// Explicit Top/Left/Width/Height implementations:

XPLUGIN_API void XWindow_Top_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd)
        SetWindowPos(win->hwnd, nullptr, win->getLeft(), value, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#elif defined(__linux__)
    win->x11Y = value;
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        XMoveWindow(win->x11Display, win->x11Window, win->x11X, win->x11Y);
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->cocoaY = value;
    win->enqueueCocoa([win]{
        if (!win->cocoaWindow) return;
        NSRect f = [win->cocoaWindow frame];
        f.origin.y = win->cocoaY;
        [win->cocoaWindow setFrame:f display:YES];
    });
#endif
}

XPLUGIN_API int XWindow_Top_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    XWindow *win = it->second;
#ifdef _WIN32
    return win->getTop();
#elif defined(__linux__)
    return win->x11Y;
#elif defined(__APPLE__)
    return win->cocoaY;
#else
    return 0;
#endif
}



XPLUGIN_API void XWindow_Left_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd)
        SetWindowPos(win->hwnd, nullptr, value, win->getTop(), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#elif defined(__linux__)
    win->x11X = value;
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        XMoveWindow(win->x11Display, win->x11Window, win->x11X, win->x11Y);
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->cocoaX = value;
    win->enqueueCocoa([win]{
        if (!win->cocoaWindow) return;
        NSRect f = [win->cocoaWindow frame];
        f.origin.x = win->cocoaX;
        [win->cocoaWindow setFrame:f display:YES];
    });
#endif
}

XPLUGIN_API int XWindow_Left_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    XWindow *win = it->second;
#ifdef _WIN32
    return win->getLeft();
#elif defined(__linux__)
    return win->x11X;
#elif defined(__APPLE__)
    return win->cocoaX;
#else
    return 0;
#endif
}



XPLUGIN_API void XWindow_Width_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd)
        SetWindowPos(win->hwnd, nullptr, 0, 0, value, win->getHeight(),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#elif defined(__linux__)
    win->x11W = value;
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        XResizeWindow(win->x11Display, win->x11Window, (unsigned int)win->x11W, (unsigned int)win->x11H);
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->cocoaW = value;
    win->enqueueCocoa([win]{
        if (!win->cocoaWindow) return;
        NSRect f = [win->cocoaWindow frame];
        f.size.width = win->cocoaW;
        [win->cocoaWindow setFrame:f display:YES];
    });
#endif
}

XPLUGIN_API int XWindow_Width_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    XWindow *win = it->second;
#ifdef _WIN32
    return win->getWidth();
#elif defined(__linux__)
    return win->x11W;
#elif defined(__APPLE__)
    return win->cocoaW;
#else
    return 0;
#endif
}



XPLUGIN_API void XWindow_Height_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd)
        SetWindowPos(win->hwnd, nullptr, 0, 0, win->getWidth(), value,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#elif defined(__linux__)
    win->x11H = value;
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        XResizeWindow(win->x11Display, win->x11Window, (unsigned int)win->x11W, (unsigned int)win->x11H);
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->cocoaH = value;
    win->enqueueCocoa([win]{
        if (!win->cocoaWindow) return;
        NSRect f = [win->cocoaWindow frame];
        f.size.height = win->cocoaH;
        [win->cocoaWindow setFrame:f display:YES];
    });
#endif
}

XPLUGIN_API int XWindow_Height_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    XWindow *win = it->second;
#ifdef _WIN32
    return win->getHeight();
#elif defined(__linux__)
    return win->x11H;
#elif defined(__APPLE__)
    return win->cocoaH;
#else
    return 0;
#endif
}


XPLUGIN_API void XWindow_Title_SET(int handle, const char *title)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
    win->titleCache = title ? title : "";

#ifdef _WIN32
    if (win->hwnd) SetWindowTextA(win->hwnd, win->titleCache.c_str());
#elif defined(__linux__)
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        XStoreName(win->x11Display, win->x11Window, win->titleCache.c_str());
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->enqueueCocoa([win]{
        if (win->cocoaWindow)
            [win->cocoaWindow setTitle:[NSString stringWithUTF8String:win->titleCache.c_str()]];
    });
#endif
}

XPLUGIN_API const char* XWindow_Title_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return strdup("");
    XWindow *win = it->second;

#ifdef _WIN32
    char buf[512];
    buf[0] = '\0';
    if (win->hwnd) GetWindowTextA(win->hwnd, buf, 511);
    else strncpy(buf, win->titleCache.c_str(), 511);
    buf[511] = '\0';
    return strdup(buf);
#else
    return strdup(win->titleCache.c_str());
#endif
}


XPLUGIN_API void XWindow_Enabled_SET(int h,bool v){
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end())
        EnableWindow(it->second->hwnd,v);
#endif
}
XPLUGIN_API bool XWindow_Enabled_GET(int h){
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end())
        return IsWindowEnabled(it->second->hwnd)!=0;
#endif
    return false;
}

XPLUGIN_API void XWindow_Visible_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
    bool vis = (value != 0);

#ifdef _WIN32
    if (win->hwnd) ShowWindow(win->hwnd, vis ? SW_SHOW : SW_HIDE);
#elif defined(__linux__)
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        if (vis) XMapWindow(win->x11Display, win->x11Window);
        else XUnmapWindow(win->x11Display, win->x11Window);
        XFlush(win->x11Display);
        win->x11Mapped = vis;
    }
#elif defined(__APPLE__)
    win->enqueueCocoa([win, vis]{
        if (!win->cocoaWindow) return;
        if (vis) [win->cocoaWindow makeKeyAndOrderFront:nil];
        else [win->cocoaWindow orderOut:nil];
    });
#endif
}

XPLUGIN_API int XWindow_Visible_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    XWindow *win = it->second;

#ifdef _WIN32
    return (win->hwnd && IsWindowVisible(win->hwnd)) ? 1 : 0;
#elif defined(__linux__)
    return win->x11Mapped ? 1 : 0;
#elif defined(__APPLE__)
    return (win->cocoaWindow && [win->cocoaWindow isVisible]) ? 1 : 0;
#else
    return 0;
#endif
}


XPLUGIN_API void XWindow_Type_SET(int h,int v){
#ifdef _WIN32
    if(v<0||v>5) return;
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end()){
        it->second->winType=v;
        XWindow::applyStyle(it->second->hwnd,v);
    }
#endif
}
XPLUGIN_API int XWindow_Type_GET(int h){
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end())
        return it->second->winType;
    return 0;
}

XPLUGIN_API void XWindow_BackgroundColor_SET(int handle, int value)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;

    win->bgColorVal = (unsigned int)value;

#ifdef _WIN32
    win->bgColor = RGB((win->bgColorVal >> 16) & 0xFF, (win->bgColorVal >> 8) & 0xFF, win->bgColorVal & 0xFF);
    if (win->bgBrush) DeleteObject(win->bgBrush);
    win->bgBrush = CreateSolidBrush(win->bgColor);
    if (win->hwnd) InvalidateRect(win->hwnd, nullptr, TRUE);
#elif defined(__linux__)
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        unsigned long px = x11AllocARGB(win->x11Display, win->x11Screen, win->bgColorVal);
        XSetWindowBackground(win->x11Display, win->x11Window, px);
        XClearWindow(win->x11Display, win->x11Window);
        XFlush(win->x11Display);
    }
#elif defined(__APPLE__)
    win->enqueueCocoa([win]{
        if (win->cocoaWindow) [win->cocoaWindow setBackgroundColor:cocoaColorFromARGB(win->bgColorVal)];
    });
#endif
}

XPLUGIN_API int XWindow_BackgroundColor_GET(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return 0;
    return (int)it->second->bgColorVal;
}


XPLUGIN_API void XWindow_Minimize(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd) ShowWindow(win->hwnd, SW_MINIMIZE);
#elif defined(__linux__)
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window)
        XIconifyWindow(win->x11Display, win->x11Window, win->x11Screen);
#elif defined(__APPLE__)
    win->enqueueCocoa([win]{ if (win->cocoaWindow) [win->cocoaWindow miniaturize:nil]; });
#endif
}

XPLUGIN_API void XWindow_Maximize(int handle)
{
    std::lock_guard<std::mutex> lock(g_instancesMtx);
    auto it = g_instances.find(handle);
    if (it == g_instances.end()) return;
    XWindow *win = it->second;
#ifdef _WIN32
    if (win->hwnd) ShowWindow(win->hwnd, SW_MAXIMIZE);
#elif defined(__linux__)
    std::lock_guard<std::mutex> lk(win->x11Mutex);
    if (win->x11Display && win->x11Window) {
        Atom maxH = XInternAtom(win->x11Display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        Atom maxV = XInternAtom(win->x11Display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        x11SendNetWmState(win->x11Display, win->x11Screen, win->x11Window, true, maxH, maxV);
    }
#elif defined(__APPLE__)
    win->enqueueCocoa([win]{ if (win->cocoaWindow) [win->cocoaWindow zoom:nil]; });
#endif
}

XPLUGIN_API void XWindow_Show(int h){
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end()){
        ShowWindow(it->second->hwnd,SW_SHOWNORMAL);
        UpdateWindow(it->second->hwnd);
    }
#endif
}
XPLUGIN_API void XWindow_Hide(int h){
#ifdef _WIN32
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    if(auto it=g_instances.find(h);it!=g_instances.end())
        ShowWindow(it->second->hwnd,SW_HIDE);
#endif
}

//------------------------------------------------------------------------------  
// SetIcon: load a PNG from disk and apply it to the window  
//------------------------------------------------------------------------------
#ifdef _WIN32
XPLUGIN_API void XWindow_SetIcon(int h, const char* utf8Path) {
    std::lock_guard<std::mutex> lk(g_instancesMtx);
    auto it = g_instances.find(h);
    if (it == g_instances.end() || !it->second->hwnd) return;

    std::wstring wpath;
    {
        int len = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, nullptr, 0);
        if (len > 0) {
            wpath.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, &wpath[0], len);
        }
    }
    Bitmap* bmp = Bitmap::FromFile(wpath.c_str());
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return;
    }

    HICON hIcon = nullptr;
    if (bmp->GetHICON(&hIcon) == Ok && hIcon) {
        HWND wnd = it->second->hwnd;
        SendMessageW(wnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessageW(wnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
    delete bmp;
}
#endif

#ifndef _WIN32
XPLUGIN_API void XWindow_SetIcon(int handle, const char* utf8Path) { (void)handle; (void)utf8Path; }
#endif

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
//------------------------------------------------------------------------------
// ShowModal: disable parent, show this window, run a modal loop, then re-enable parent
//------------------------------------------------------------------------------
XPLUGIN_API void XWindow_ShowModal(int handle)
{
    // Best-effort: show the window (if hidden) and block until it's closed (handle destroyed).
    {
        std::lock_guard<std::mutex> lock(g_instancesMtx);
        auto it = g_instances.find(handle);
        if (it != g_instances.end()) {
            XWindow *win = it->second;
#ifdef _WIN32
            if (win->hwnd) ShowWindow(win->hwnd, SW_SHOW);
#elif defined(__linux__)
            std::lock_guard<std::mutex> lk(win->x11Mutex);
            if (win->x11Display && win->x11Window) {
                XMapWindow(win->x11Display, win->x11Window);
                XFlush(win->x11Display);
                win->x11Mapped = true;
            }
#elif defined(__APPLE__)
            win->enqueueCocoa([win]{ if (win->cocoaWindow) [win->cocoaWindow makeKeyAndOrderFront:nil]; });
#endif
        }
    }

    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_instancesMtx);
            if (g_instances.find(handle) == g_instances.end()) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}



//------------------------------------------------------------------------------
// MessageBox: display a modal message box disabling parent until dismissed
//------------------------------------------------------------------------------
XPLUGIN_API int XWindow_MessageBox(int handle, const char* title, const char* message, int flags)
{
#ifdef _WIN32
    HWND owner = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_instancesMtx);
        auto it = g_instances.find(handle);
        if (it != g_instances.end()) owner = it->second->hwnd;
    }
    return MessageBoxA(owner, message ? message : "", title ? title : "", (UINT)flags);
#elif defined(__APPLE__)
    (void)flags;
    XWindow* win = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_instancesMtx);
        auto it = g_instances.find(handle);
        if (it != g_instances.end()) win = it->second;
    }
    if (!win) return 0;

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int result = 0;

    std::string titleStr = title ? title : "Message";
    std::string msgStr = message ? message : "";

    win->enqueueCocoa([&]{
        NSAlert* alert = [[NSAlert alloc] init];
        [alert setMessageText:[NSString stringWithUTF8String:titleStr.c_str()]];
        [alert setInformativeText:[NSString stringWithUTF8String:msgStr.c_str()]];
        [alert addButtonWithTitle:@"OK"];
        NSModalResponse r = [alert runModal];
        {
            std::lock_guard<std::mutex> lk(mtx);
            result = (int)r;
            done = true;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&]{ return done; });
    return result;
#elif defined(__linux__)
    // Try zenity if present; otherwise print to stderr and return 0.
    std::string t = title ? title : "Message";
    std::string m = message ? message : "";
    std::string cmd = "zenity --info --title=\"" + t + "\" --text=\"" + m + "\" >/dev/null 2>&1";
    int rc = system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "[XWindow] " << t << ": " << m << std::endl;
    }
    return 0;
#else
    (void)handle; (void)title; (void)message; (void)flags;
    return 0;
#endif
}


#endif

//──────────────────────────────────────── Class definition ───────────────────
typedef struct { const char* n; const char* t; void* g; void* s; } ClassProperty;
typedef struct { const char* n; void* f; int a; const char* p[10]; const char* r; } ClassEntry;
typedef struct { const char* d; } ClassConstant;
typedef struct
{
    const char*     className;
    size_t          classSize;
    void*           ctor;
    ClassProperty*  props;
    size_t          propCount;
    ClassEntry*     methods;
    size_t          methCount;
    ClassConstant*  constants;
    size_t          constCount;
} ClassDefinition;

// properties
static ClassProperty props[] =
{
    { "Handle",             "integer", (void*)XWindow_Handle_GET,              nullptr },
    { "Opening",            "string",  (void*)Opening_GET,                     nullptr },
    { "Closing",            "string",  (void*)Closing_GET,                     nullptr },
    { "Top",                "integer", (void*)XWindow_Top_GET,                 (void*)XWindow_Top_SET },
    { "Left",               "integer", (void*)XWindow_Left_GET,                (void*)XWindow_Left_SET },
    { "Width",              "integer", (void*)XWindow_Width_GET,               (void*)XWindow_Width_SET },
    { "Height",             "integer", (void*)XWindow_Height_GET,              (void*)XWindow_Height_SET },
    { "Title",              "string",  (void*)XWindow_Title_GET,               (void*)XWindow_Title_SET },
    { "Enabled",            "boolean", (void*)XWindow_Enabled_GET,             (void*)XWindow_Enabled_SET },
    { "Visible",            "boolean", (void*)XWindow_Visible_GET,             (void*)XWindow_Visible_SET },
    { "ViewType",           "integer", (void*)XWindow_Type_GET,                (void*)XWindow_Type_SET },
    { "BackgroundColor",    "color",   (void*)XWindow_BackgroundColor_GET,     (void*)XWindow_BackgroundColor_SET },
    { "HasCloseButton",     "boolean", (void*)XWindow_HasCloseButton_GET,      (void*)XWindow_HasCloseButton_SET },
    { "HasMinimizeButton",  "boolean", (void*)XWindow_HasMinimizeButton_GET,   (void*)XWindow_HasMinimizeButton_SET },
    { "HasMaximizeButton",  "boolean", (void*)XWindow_HasMaximizeButton_GET,   (void*)XWindow_HasMaximizeButton_SET },
    { "HasFullScreenButton","boolean", (void*)XWindow_HasFullScreenButton_GET, (void*)XWindow_HasFullScreenButton_SET },
    { "HasTitleBar",        "boolean", (void*)XWindow_HasTitleBar_GET,         (void*)XWindow_HasTitleBar_SET },
    { "Resizable",          "boolean", (void*)XWindow_Resizable_GET,           (void*)XWindow_Resizable_SET }
};

// methods
static ClassEntry methods[] =
{
    { "XWindow_SetEventCallback", (void*)XWindow_SetEventCallback, 3, { "integer","string","pointer" }, "boolean" },
    { "Minimize",                 (void*)XWindow_Minimize,         1, { "integer" },                    "void" },
    { "Maximize",                 (void*)XWindow_Maximize,         1, { "integer" },                    "void" },
    { "Show",                     (void*)XWindow_Show,             1, { "integer" },                    "void" },
    { "ShowModal",                (void*)XWindow_ShowModal,        2, { "integer","integer" },          "void" },
    { "Hide",                     (void*)XWindow_Hide,             1, { "integer" },                    "void" },
    { "Close",                    (void*)Close,                    1, { "integer" },                    "void" },
    { "SetIcon",                  (void*)XWindow_SetIcon,          2, { "integer","string" },           "void" },
    { "MessageBox",               (void*)XWindow_MessageBox,       2, { "integer","string" },           "void" }
};

// final class definition
static ClassDefinition classDef =
{
    "XWindow",
    sizeof(XWindow),
    (void*)Constructor,
    props,   sizeof(props)/sizeof(props[0]),
    methods, sizeof(methods)/sizeof(methods[0]),
    nullptr, 0
};

XPLUGIN_API ClassDefinition* GetClassDefinition() { return &classDef; }

} // extern "C"

//==============================================================================
//  Unload hook
//==============================================================================
#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE, DWORD why, LPVOID)
{
   if (why == DLL_PROCESS_ATTACH) {
     GdiplusStartupInput gdiplusStartupInput;
     GdiplusStartup(&gdiPlusToken, &gdiplusStartupInput, nullptr);
   }
   if(why==DLL_PROCESS_DETACH) CleanupInstances();
   if (why == DLL_PROCESS_DETACH && gdiPlusToken) {
     GdiplusShutdown(gdiPlusToken);
   }
   return TRUE;
}
#else
__attribute__((destructor)) static void onUnload() { CleanupInstances(); }
#endif