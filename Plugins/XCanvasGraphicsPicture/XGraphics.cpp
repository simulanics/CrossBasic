/*

  XGraphics.cpp
  CrossBasic Plugin: XGraphics                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
#include <gdiplus.h>
#include <mutex>
#include <unordered_map>
#include <random>
#include <string>
#include <iostream>

#pragma comment(lib,"gdiplus.lib")
#define XPLUGIN_API __declspec(dllexport)
#define strdup _strdup

using namespace Gdiplus;

/* ────── GDI+ bootstrap ─────────────────────────────────────────── */
static ULONG_PTR gGdiToken;
static bool      gGdiInit = false;
static void EnsureGdiPlus()
{
    if (!gGdiInit) {
        GdiplusStartupInput in;
        if (GdiplusStartup(&gGdiToken,&in,nullptr)==Ok) gGdiInit = true;
        else std::cerr<<"XGraphics ERROR: GdiplusStartup failed\n";
    }
}

/* ────── optional linkage to XPicture.dll ───────────────────────── */
static HMODULE  gPicLib = nullptr;
typedef int (__cdecl *PicGfxGetFn)(int);
static PicGfxGetFn pPic_Graphics_GET=nullptr;
static void EnsurePicBindings()
{
    if (pPic_Graphics_GET) return;
    gPicLib = LoadLibraryA("XPicture.dll");
    if (gPicLib)
        pPic_Graphics_GET=(PicGfxGetFn)GetProcAddress(gPicLib,"XPicture_Graphics_GET");
}


/* ────── optional linkage to XPoints.dll ───────────────────────── */
static HMODULE  gPtsLib = nullptr;
typedef int (__cdecl *PtsCountFn)(int);
typedef const PointF* (__cdecl *PtsDataFn)(int);
static PtsCountFn pPts_Count_GET = nullptr;
static PtsDataFn  pPts_DataPointer_GET = nullptr;

static void EnsurePtsBindings()
{
    if (pPts_Count_GET && pPts_DataPointer_GET) return;
    gPtsLib = LoadLibraryA("XPoints.dll");
    if (gPtsLib) {
        pPts_Count_GET = (PtsCountFn)GetProcAddress(gPtsLib, "XPoints_Count_GET");
        pPts_DataPointer_GET = (PtsDataFn)GetProcAddress(gPtsLib, "XPoints_DataPointer_GET");
    }
}

/* ────── per-instance data ───────────────────────────────────────── */
struct GfxInst {
    bool        isBitmap;
    HWND        hwnd;
    HDC         hdc;
    Bitmap*     bmp;
    Graphics*   gfx;
    Color       color;

    std::wstring fontName;
    int          fontSize;
    bool         bold,italic,underline;

    int          width,height;
    int          penSize;
    bool         antialias;
};

static std::mutex                       gMx;
static std::unordered_map<int,GfxInst*> gInst;
static std::mt19937                     gRng{std::random_device{}()};
static std::uniform_int_distribution<int> gDist(10000000,99999999);

static void ApplyAA(GfxInst* inst)
{
    inst->gfx->SetSmoothingMode(
        inst->antialias ? SmoothingModeHighQuality : SmoothingModeNone);

    inst->gfx->SetTextRenderingHint(
        inst->antialias ? TextRenderingHintClearTypeGridFit
                        : TextRenderingHintSingleBitPerPixel);
}

static void CleanupAll()
{
    std::lock_guard<std::mutex> lk(gMx);
    for (auto& kv: gInst) {
        auto p = kv.second;
        delete p->gfx;
        if (p->isBitmap) delete p->bmp;
        else ReleaseDC(p->hwnd,p->hdc);
        delete p;
    }
    gInst.clear();
    if (gGdiInit){ GdiplusShutdown(gGdiToken); gGdiInit=false; }
    if (gPicLib)  FreeLibrary(gPicLib);
    if (gPtsLib)  FreeLibrary(gPtsLib);
}

/* ────── PUBLIC API ─────────────────────────────────────────────── */
extern "C" {

/* ---- constructor: window-backed --------------------------------- */
XPLUGIN_API int XGraphics_Constructor(int hwndInt)
{
    EnsureGdiPlus(); std::lock_guard<std::mutex> lk(gMx);
    int h; do{ h=gDist(gRng);}while(gInst.count(h));

    auto i = new GfxInst{};
    i->isBitmap=false;
    i->hwnd=(HWND)(intptr_t)hwndInt;
    i->hdc =GetDC(i->hwnd);
    i->bmp =nullptr;
    i->gfx =new Graphics(i->hdc);

    i->color=Color(255,0,0,0);
    i->fontName=L"Segoe UI"; i->fontSize=12;
    i->bold=i->italic=i->underline=false;
    RECT rc; GetClientRect(i->hwnd,&rc);
    i->width=rc.right-rc.left; i->height=rc.bottom-rc.top;
    i->penSize=1; i->antialias=true;

    ApplyAA(i);

    gInst[h]=i; return h;
}

/* ---- constructor: bitmap-backed --------------------------------- */
XPLUGIN_API int XGraphics_ConstructorPicture(int w,int hgt,int depthBits)
{
    EnsureGdiPlus(); std::lock_guard<std::mutex> lk(gMx);

    PixelFormat pf=PixelFormat32bppARGB;
    if (depthBits==24) pf=PixelFormat24bppRGB;
    else if (depthBits==8) pf=PixelFormat8bppIndexed;

    int h; do{ h=gDist(gRng);}while(gInst.count(h));
    if (w==0) w=1; if (hgt==0) hgt=1;

    auto i=new GfxInst{};
    i->isBitmap=true; i->hwnd=nullptr; i->hdc=nullptr;
    i->bmp=new Bitmap(w,hgt,pf); i->gfx=new Graphics(i->bmp);

    i->color=Color(255,0,0,0);
    i->fontName=L"Segoe UI"; i->fontSize=12;
    i->bold=i->italic=i->underline=false;
    i->width=w; i->height=hgt; i->penSize=1; i->antialias=true;

    ApplyAA(i);

    gInst[h]=i; return h;
}


// NOTE: CrossBasic plugin-class constructors are invoked with *zero* arguments.
// Provide a safe 0-arg constructor for "New XGraphics" scenarios.
XPLUGIN_API int XGraphics_Constructor0()
{
    // Create a tiny offscreen surface by default. Consumers can still
    // use XCanvas.Graphics or other APIs that return a window-attached graphics.
    return XGraphics_ConstructorPicture(1, 1, 32);
}

/* ---- destructor -------------------------------------------------- */
XPLUGIN_API void XGraphics_Close(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    auto i=it->second;
    delete i->gfx; if(i->isBitmap) delete i->bmp;
    else ReleaseDC(i->hwnd,i->hdc);
    delete i; gInst.erase(it);
}

/* ---- Antialias property ----------------------------------------- */
XPLUGIN_API void XGraphics_Antialias_SET(int h,bool v)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->antialias=v; ApplyAA(it->second);
}
XPLUGIN_API bool XGraphics_Antialias_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); return it!=gInst.end() && it->second->antialias;
}

/* ---- clear (transparent) ---------------------------------------- */
XPLUGIN_API void XGraphics_Clear(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->gfx->Clear(Color::MakeARGB(0,0,0,0));
}

/* ---- color ------------------------------------------------------- */
XPLUGIN_API void XGraphics_DrawingColor_SET(int h,unsigned int rgb)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->color=Color(255,(rgb>>16)&0xFF,(rgb>>8)&0xFF,rgb&0xFF);
}
XPLUGIN_API unsigned int XGraphics_DrawingColor_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return 0;
    auto& c=it->second->color;
    return (c.GetR()<<16)|(c.GetG()<<8)|c.GetB();
}

/* ---- primitive drawing ----------------------------------------- */
XPLUGIN_API void XGraphics_DrawLine(int h,int x1,int y1,int x2,int y2)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    Pen p(it->second->color,(REAL)it->second->penSize);
    it->second->gfx->DrawLine(&p,x1,y1,x2,y2);
}
XPLUGIN_API void XGraphics_DrawRect(int h,int x,int y,int w,int hgt)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    Pen p(it->second->color,(REAL)it->second->penSize);
    it->second->gfx->DrawRectangle(&p,x,y,w,hgt);
}
XPLUGIN_API void XGraphics_FillRect(int h,int x,int y,int w,int hgt)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    SolidBrush b(it->second->color);
    it->second->gfx->FillRectangle(&b,x,y,w,hgt);
}
XPLUGIN_API void XGraphics_DrawOval(int h,int x,int y,int w,int hgt)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    Pen p(it->second->color,(REAL)it->second->penSize);
    it->second->gfx->DrawEllipse(&p,x,y,w,hgt);
}
XPLUGIN_API void XGraphics_FillOval(int h,int x,int y,int w,int hgt)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    SolidBrush b(it->second->color);
    it->second->gfx->FillEllipse(&b,x,y,w,hgt);
}

/* ---- polygon drawing (XPoints) ----------------------------------- */
/*
    XPoints is a small helper object that owns an internal `PointF[]` buffer.
    This avoids the ambiguity of passing raw numeric arrays (x,y,x,y,...) and
    needing a separate count parameter.
*/
XPLUGIN_API void XGraphics_DrawPolygon(int h, int ptsH)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;

    EnsurePtsBindings();
    if (!pPts_Count_GET || !pPts_DataPointer_GET) return;

    int count = pPts_Count_GET(ptsH);
    if (count < 2) return;

    const PointF* pts = pPts_DataPointer_GET(ptsH);
    if (!pts) return;

    Pen p(it->second->color, (REAL)it->second->penSize);
    it->second->gfx->DrawPolygon(&p, pts, count);
}

XPLUGIN_API void XGraphics_FillPolygon(int h, int ptsH)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;

    EnsurePtsBindings();
    if (!pPts_Count_GET || !pPts_DataPointer_GET) return;

    int count = pPts_Count_GET(ptsH);
    if (count < 2) return;

    const PointF* pts = pPts_DataPointer_GET(ptsH);
    if (!pts) return;

    SolidBrush b(it->second->color);
    it->second->gfx->FillPolygon(&b, pts, count);
}


/* ---- draw picture ----------------------------------------------- */
XPLUGIN_API void XGraphics_DrawPicture(int dstH,int picH,int x,int y,int w,int hgt)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto dit=gInst.find(dstH); if(dit==gInst.end()) return;

    int ph=picH;
    auto pit=gInst.find(ph);
    if(pit==gInst.end()){
        EnsurePicBindings();
        if(pPic_Graphics_GET){ ph=pPic_Graphics_GET(picH); pit=gInst.find(ph);}
    }
    if(pit==gInst.end()||!pit->second->isBitmap) return;

    Bitmap* bmp=pit->second->bmp;
    if(w<=0) w=bmp->GetWidth(); if(hgt<=0) hgt=bmp->GetHeight();
    dit->second->gfx->DrawImage(bmp,Rect(x,y,w,hgt),
                                0,0,bmp->GetWidth(),bmp->GetHeight(),UnitPixel);
}

/* ---- font-name property (missing earlier) ----------------------- */
XPLUGIN_API void XGraphics_FontName_SET(int h, const char* name)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()||!name) return;
    int len = MultiByteToWideChar(CP_UTF8,0,name,-1,nullptr,0);
    if(len>0){
        std::wstring wname(len,L'\0');
        MultiByteToWideChar(CP_UTF8,0,name,-1,&wname[0],len);
        if(!wname.empty() && wname.back()==L'\0') wname.pop_back();
        it->second->fontName = wname;
    }
}
XPLUGIN_API const char* XGraphics_FontName_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return nullptr;
    const std::wstring& w = it->second->fontName;
    int len = WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string s; s.resize(len);
    WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,&s[0],len,nullptr,nullptr);
    return strdup(s.c_str());
}

/* ---- font-size -------------------------------------------------- */
XPLUGIN_API void XGraphics_FontSize_SET(int h,int v)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->fontSize = v>0 ? v : 1;
}
XPLUGIN_API int XGraphics_FontSize_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); return it!=gInst.end() ? it->second->fontSize : 12;
}

/* ---- bold ------------------------------------------------------- */
XPLUGIN_API void XGraphics_Bold_SET(int h,bool v)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->bold = v;
}
XPLUGIN_API bool XGraphics_Bold_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); return it!=gInst.end() && it->second->bold;
}

/* ---- italic ----------------------------------------------------- */
XPLUGIN_API void XGraphics_Italic_SET(int h,bool v)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->italic = v;
}
XPLUGIN_API bool XGraphics_Italic_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); return it!=gInst.end() && it->second->italic;
}

/* ---- underline -------------------------------------------------- */
XPLUGIN_API void XGraphics_Underline_SET(int h,bool v)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    it->second->underline = v;
}
XPLUGIN_API bool XGraphics_Underline_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); return it!=gInst.end() && it->second->underline;
}

/* ---- text -------------------------------------------------------- */
XPLUGIN_API void XGraphics_DrawText(int h,const char* s,int x,int y)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    auto* inst=it->second;

    std::wstring txt = std::wstring(s, s+strlen(s));
    FontFamily fam(inst->fontName.c_str());
    INT style=FontStyleRegular;
    if(inst->bold)      style|=FontStyleBold;
    if(inst->italic)    style|=FontStyleItalic;
    if(inst->underline) style|=FontStyleUnderline;

    Font f(&fam,(REAL)inst->fontSize,style,UnitPixel);
    SolidBrush b(inst->color); PointF pt((REAL)x,(REAL)y);
    inst->gfx->DrawString(txt.c_str(),-1,&f,pt,&b);
}

/* ---- blit -------------------------------------------------------- */
XPLUGIN_API void XGraphics_Blit(int h,HDC hdc)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it=gInst.find(h); if(it==gInst.end()) return;
    auto* inst=it->second;
    if(!inst->isBitmap||!inst->bmp) return;
    inst->gfx->Flush(FlushIntentionFlush);
    Graphics g(hdc);
    g.DrawImage(inst->bmp,0,0,inst->bmp->GetWidth(),inst->bmp->GetHeight());
}

/* ---- pensize ----------------------------------------------------- */
XPLUGIN_API void XGraphics_PenSize_SET(int h,int v)
{ std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it!=gInst.end()) it->second->penSize=v>0?v:1; }
XPLUGIN_API int  XGraphics_PenSize_GET(int h)
{ std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); return it!=gInst.end()?it->second->penSize:1; }

/* ---- geometry read-only ----------------------------------------- */
XPLUGIN_API int XGraphics_Width_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it==gInst.end()) return 0;
    auto* i=it->second;
    if(i->isBitmap && i->bmp)    return (int)i->bmp->GetWidth();
    if(i->hwnd){ RECT rc; GetClientRect(i->hwnd,&rc); return rc.right-rc.left; }
    return 0;
}
XPLUGIN_API int XGraphics_Height_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it==gInst.end()) return 0;
    auto* i=it->second;
    if(i->isBitmap && i->bmp)    return (int)i->bmp->GetHeight();
    if(i->hwnd){ RECT rc; GetClientRect(i->hwnd,&rc); return rc.bottom-rc.top; }
    return 0;
}

/* ---- Save & Load bitmap ----------------------------------------- */
XPLUGIN_API bool XGraphics_SaveToFile(int h,const char* p)
{
    std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h);
    if(it==gInst.end()||!it->second->bmp) return false;

    CLSID clsid; UINT n=0,sz=0; GetImageEncodersSize(&n,&sz); if(!sz) return false;
    auto* info=(ImageCodecInfo*)malloc(sz); GetImageEncoders(n,sz,info);
    bool ok=false;
    for(UINT i=0;i<n;++i) if(wcscmp(info[i].MimeType,L"image/png")==0){ clsid=info[i].Clsid; ok=true; break;}
    free(info); if(!ok) return false;

    std::wstring w(p,p+strlen(p));
    return it->second->bmp->Save(w.c_str(),&clsid,nullptr)==Ok;
}
XPLUGIN_API bool XGraphics_LoadFromFile(int h,const char* p)
{
    std::lock_guard<std::mutex> lk(gMx); auto it=gInst.find(h); if(it==gInst.end()) return false;

    delete it->second->gfx; delete it->second->bmp;
    Bitmap* bmp = Bitmap::FromFile(std::wstring(p,p+strlen(p)).c_str());
    if(!bmp||bmp->GetLastStatus()!=Ok){ delete bmp; return false; }

    it->second->bmp=bmp; it->second->gfx=new Graphics(bmp); it->second->isBitmap=true;
    it->second->width=bmp->GetWidth(); it->second->height=bmp->GetHeight();
    ApplyAA(it->second);
    return true;
}

/* ────── CrossBasic / Xojo glue tables ──────────────────────────── */
typedef struct{const char* n;const char* t;void* g;void* s;} Property;
typedef struct{const char* n;void* f;int a;const char* p[10];const char* r;} Method;
typedef struct{const char* cls;size_t sz;void* ctor;Property* props;size_t pCnt;Method* meths;size_t mCnt;Method* consts;size_t cCnt;} ClassDef;

/* properties */
static Property props[]={
  { "DrawingColor","color",   (void*)XGraphics_DrawingColor_GET,(void*)XGraphics_DrawingColor_SET },
  { "FontName",    "string",  (void*)XGraphics_FontName_GET,    (void*)XGraphics_FontName_SET     },
  { "FontSize",    "integer", (void*)XGraphics_FontSize_GET,    (void*)XGraphics_FontSize_SET     },
  { "Bold",        "boolean", (void*)XGraphics_Bold_GET,        (void*)XGraphics_Bold_SET         },
  { "Italic",      "boolean", (void*)XGraphics_Italic_GET,      (void*)XGraphics_Italic_SET       },
  { "Underline",   "boolean", (void*)XGraphics_Underline_GET,   (void*)XGraphics_Underline_SET    },
  { "PenSize",     "integer", (void*)XGraphics_PenSize_GET,     (void*)XGraphics_PenSize_SET      },
  { "Antialias",   "boolean", (void*)XGraphics_Antialias_GET,   (void*)XGraphics_Antialias_SET    },
  { "Width",       "integer", (void*)XGraphics_Width_GET,       nullptr                           },
  { "Height",      "integer", (void*)XGraphics_Height_GET,      nullptr                           }
};

/* methods */
static Method meths[]={
  { "Clear",        (void*)XGraphics_Clear,        1, {"integer"},                                                           "void"    },
  { "DrawLine",     (void*)XGraphics_DrawLine,     5, {"integer","integer","integer","integer","integer"},                   "void"    },
  { "DrawRect",     (void*)XGraphics_DrawRect,     5, {"integer","integer","integer","integer","integer"},                   "void"    },
  { "FillRect",     (void*)XGraphics_FillRect,     5, {"integer","integer","integer","integer","integer"},                   "void"    },
  { "DrawOval",     (void*)XGraphics_DrawOval,     5, {"integer","integer","integer","integer","integer"},                   "void"    },
  { "FillOval",     (void*)XGraphics_FillOval,     5, {"integer","integer","integer","integer","integer"},                   "void"    },
  { "DrawPolygon",  (void*)XGraphics_DrawPolygon,  2, {"integer","XPoints"},                                                "void"    },
  { "FillPolygon",  (void*)XGraphics_FillPolygon,  2, {"integer","XPoints"},                                                "void"    },
  { "DrawPicture",  (void*)XGraphics_DrawPicture,  6, {"integer","XPicture","integer","integer","integer","integer"},        "void"    },
  { "DrawText",     (void*)XGraphics_DrawText,     4, {"integer","string","integer","integer"},                              "void"    },
  { "SaveToFile",   (void*)XGraphics_SaveToFile,   2, {"integer","string"},                                                  "boolean" },
  { "LoadFromFile", (void*)XGraphics_LoadFromFile, 2, {"integer","string"},                                                  "boolean" }
};

static ClassDef classDef={
    "XGraphics", sizeof(GfxInst),
    (void*)XGraphics_Constructor0,
    props, sizeof(props)/sizeof(props[0]),
    meths, sizeof(meths)/sizeof(meths[0]),
    nullptr,0
};

XPLUGIN_API ClassDef* GetClassDefinition(){ return &classDef; }

/* ────── DLL main: automatic cleanup ─────────────────────────────── */
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if(reason==DLL_PROCESS_DETACH) CleanupAll();
    return TRUE;
}

} // extern "C"
