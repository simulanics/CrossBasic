/*

  XPicture.cpp
  CrossBasic Plugin: XPicture                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
#include <mutex>
#include <unordered_map>
#include <random>
#include <string>
#include <iostream>

#define XPLUGIN_API __declspec(dllexport)
#define strdup _strdup

//----------------------------------------------------------------------
// We dynamically load XGraphics.dll and bind the few methods we need.
static HMODULE            gGfxLib       = nullptr;
typedef int   (__cdecl *GfxCtorFn)(int,int,int);
typedef void  (__cdecl *GfxCloseFn)(int);
typedef bool  (__cdecl *GfxSaveFn)(int, const char*);
typedef bool  (__cdecl *GfxLoadFn)(int, const char*);
typedef int (__cdecl *GfxIntFn)(int);       // ← one typedef for both


static GfxIntFn pGfx_Width_GET  = nullptr;
static GfxIntFn pGfx_Height_GET = nullptr;
static GfxCtorFn   pGfx_ConstructorPicture = nullptr;
static GfxCloseFn  pGfx_Close               = nullptr;
static GfxSaveFn   pGfx_SaveToFile          = nullptr;
static GfxLoadFn   pGfx_LoadFromFile        = nullptr;


static bool EnsureGfxBindings() {
    if (gGfxLib) return true;
    //gGfxLib = LoadLibraryA("XGraphics.dll");
    gGfxLib = GetModuleHandleA("XGraphics.dll");
    if (!gGfxLib) {
       /*  std::cerr << "XPicture ERROR: could not load XGraphics.dll\n";
        return false;
    } */
        gGfxLib = LoadLibraryA("XGraphics.dll");

    if (!gGfxLib) { std::cerr << "...cannot load XGraphics.dll\n"; return false; }
    
    }

    pGfx_ConstructorPicture = (GfxCtorFn)GetProcAddress(gGfxLib, "XGraphics_ConstructorPicture");
    pGfx_Close               = (GfxCloseFn)GetProcAddress(gGfxLib, "XGraphics_Close");
    pGfx_SaveToFile          = (GfxSaveFn)GetProcAddress(gGfxLib, "XGraphics_SaveToFile");
    pGfx_LoadFromFile        = (GfxLoadFn)GetProcAddress(gGfxLib, "XGraphics_LoadFromFile");
    pGfx_Width_GET  = (GfxIntFn)GetProcAddress(gGfxLib,"XGraphics_Width_GET");
    pGfx_Height_GET = (GfxIntFn)GetProcAddress(gGfxLib,"XGraphics_Height_GET");
    if (!pGfx_ConstructorPicture || !pGfx_Close || !pGfx_SaveToFile || !pGfx_LoadFromFile) {
        std::cerr << "XPicture ERROR: missing exports in XGraphics.dll\n";
        return false;
    }
    return true;
}

//----------------------------------------------------------------------
// Per‐instance data for pictures
struct PicInst {
    int   gfxHandle;
    int   width, height;
};

static std::mutex                          gMx;
static std::unordered_map<int, PicInst*>  gInst;
static std::mt19937                        gRng((std::random_device())());
static std::uniform_int_distribution<int>  gDist(10000000, 99999999);

//----------------------------------------------------------------------
// Destructor helper
static void CleanupAll() {
    std::lock_guard<std::mutex> lk(gMx);
    for (auto &kv : gInst) {
        // close the graphics
        pGfx_Close(kv.second->gfxHandle);
        delete kv.second;
    }
    gInst.clear();
    if (gGfxLib) {
        FreeLibrary(gGfxLib);
        gGfxLib = nullptr;
    }
}

//----------------------------------------------------------------------
// Plugin exports
extern "C" {

//------------------------------------------------------------------------------
// // Constructor(width, height) – makes a blank picture
// XPLUGIN_API int XPicture_Constructor(int width, int height) 
// {
//     if (!EnsureGfxBindings()) return 0;
//     std::lock_guard<std::mutex> lk(gMx);

//     int picHandle;
//     do { picHandle = gDist(gRng); } while (gInst.count(picHandle));

//     // ask XGraphics for an off‐screen bitmap
//     int gfxH = pGfx_ConstructorPicture(width, height);
//     if (gfxH == 0) {
//         std::cerr << "XPicture ERROR: XGraphics_ConstructorPicture failed\n";
//         return 0;
//     }

//     PicInst* inst = new PicInst{ gfxH, width, height };
//     gInst[picHandle] = inst;
//     return picHandle;
// }

//------------------------------------------------------------------------------
// Constructor(width, height, depthBits)
XPLUGIN_API int XPicture_Constructor(int width,int height,int depthBits)
{
    if(!EnsureGfxBindings()) return 0;
    std::lock_guard<std::mutex> lk(gMx);

    if (width==0)  width  = 1;
    if (height==0) height = 1;

    int picH; do{ picH=gDist(gRng);}while(gInst.count(picH));

    int gfxH = pGfx_ConstructorPicture(width,height,depthBits);
    if(gfxH==0){
        std::cerr<<"XPicture ERROR: XGraphics_ConstructorPicture failed\n";
        return 0;
    }
    auto* inst = new PicInst{ gfxH,width,height };
    gInst[picH]=inst;
    return picH;
}


// NOTE: CrossBasic plugin-class constructors are invoked with *zero* arguments.
// Provide a safe 0-arg constructor for "New XPicture" scenarios.
XPLUGIN_API int XPicture_Constructor0()
{
    // Default to a small 1x1 32-bit surface. The picture can be resized via Load/Draw or future APIs.
    return XPicture_Constructor(1, 1, 32);
}

//------------------------------------------------------------------------------
// Close
XPLUGIN_API void XPicture_Close(int h) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;
    pGfx_Close(it->second->gfxHandle);
    delete it->second;
    gInst.erase(it);
}

//------------------------------------------------------------------------------
// Save(filepath) – writes out the off‐screen bitmap
XPLUGIN_API bool XPicture_Save(int h, const char* filepath) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return false;
    return pGfx_SaveToFile(it->second->gfxHandle, filepath);
}

//------------------------------------------------------------------------------
// Load(filepath) – replaces contents of the picture
XPLUGIN_API bool XPicture_Load(int h, const char* filepath) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return false;
    bool ok = pGfx_LoadFromFile(it->second->gfxHandle, filepath);
    if (ok) {
        // if the loaded image has a new size, update our cached width/height
        // (we assume XGraphics_LoadFromFile updated the off‐screen bitmap)
        // you could expose getters; here we just trust it
    }
    return ok;
}

//------------------------------------------------------------------------------
// Graphics property – get the underlying graphics handle
XPLUGIN_API int XPicture_Graphics_GET(int h) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return 0;
    return it->second->gfxHandle;
}

//------------------------------------------------------------------------------
// Width / Height getters
/* XPLUGIN_API int XPicture_Width_GET(int h) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return 0;
    return it->second->width;
}
XPLUGIN_API int XPicture_Height_GET(int h) {
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return 0;
    return it->second->height;
} */

XPLUGIN_API int XPicture_Width_GET (int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it==gInst.end()) ? 0 : pGfx_Width_GET(it->second->gfxHandle);
}

XPLUGIN_API int XPicture_Height_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    return (it==gInst.end()) ? 0 : pGfx_Height_GET(it->second->gfxHandle);
}


//------------------------------------------------------------------------------
// Class definition for Xojo
typedef struct { const char* name; const char* type; void* getter; void* setter; } ClassProperty;
typedef struct { const char* name; void* funcPtr; int arity; const char* paramTypes[10]; const char* retType; } ClassEntry;
typedef struct {
    const char*     className;
    size_t          classSize;
    void*           constructor;
    ClassProperty*  properties;
    size_t          propertiesCount;
    ClassEntry*     methods;
    size_t          methodsCount;
    ClassEntry*     constants;
    size_t          constantsCount;
} ClassDefinition;

static ClassProperty props[] = {
    { "Graphics", "XGraphics", (void*)XPicture_Graphics_GET, nullptr },
    { "Width",    "integer", (void*)XPicture_Width_GET,    nullptr },
    { "Height",   "integer", (void*)XPicture_Height_GET,   nullptr }
};

static ClassEntry methods[] = {
    { "Save", (void*)XPicture_Save, 2, {"integer","string"}, "boolean" },
    { "Load", (void*)XPicture_Load, 2, {"integer","string"}, "boolean" }
};

static ClassDefinition classDef = {
    "XPicture",
    sizeof(PicInst),
    (void*)XPicture_Constructor0,
    props, sizeof(props)/sizeof(props[0]),
    methods, sizeof(methods)/sizeof(methods[0]),
    nullptr, 0
};

XPLUGIN_API ClassDefinition* GetClassDefinition() {
    return &classDef;
}

//------------------------------------------------------------------------------
// DLL unload
BOOL APIENTRY DllMain(HMODULE /*h*/, DWORD reason, LPVOID /*lpv*/) {
    if (reason == DLL_PROCESS_DETACH) {
        CleanupAll();
    }
    return TRUE;
}

} // extern "C"
