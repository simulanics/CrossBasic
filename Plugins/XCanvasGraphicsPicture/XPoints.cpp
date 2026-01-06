/*

  XPoints.cpp
  CrossBasic Plugin: XPoints

  A small helper container for polygon/polyline APIs that need an array of (x,y) points.
  This avoids ambiguity with plain numeric arrays and lets XGraphics call Draw/FillPolygon
  without a separate "count" argument.

  Copyright (c) 2025 Simulanics Technologies – Matthew Combatti
  All rights reserved.

  Licensed under the CrossBasic Source License (CBSL-1.1).
  You may not use this file except in compliance with the License.
  You may obtain a copy of the License at:
  https://www.crossbasic.com/license

  SPDX-License-Identifier: CBSL-1.1

*/

#include <windows.h>
#include <gdiplus.h>
#include <mutex>
#include <unordered_map>
#include <random>
#include <vector>
#include <string>

#define XPLUGIN_API __declspec(dllexport)
#define strdup _strdup

using namespace Gdiplus;

struct PointsInst {
    std::vector<PointF> pts;
};

static std::mutex gMx;
static std::unordered_map<int, PointsInst*> gInst;
static std::mt19937 gRng{ std::random_device{}() };
static std::uniform_int_distribution<int> gDist(10000000, 99999999);

static void CleanupAll()
{
    std::lock_guard<std::mutex> lk(gMx);
    for (auto &kv : gInst) delete kv.second;
    gInst.clear();
}

extern "C" {

XPLUGIN_API int XPoints_Constructor()
{
    std::lock_guard<std::mutex> lk(gMx);
    int h; do { h = gDist(gRng); } while (gInst.count(h));
    gInst[h] = new PointsInst{};
    return h;
}

XPLUGIN_API void XPoints_Close(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;
    delete it->second;
    gInst.erase(it);
}

XPLUGIN_API void XPoints_Clear(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;
    it->second->pts.clear();
}

XPLUGIN_API void XPoints_Add(int h, double x, double y)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return;
    it->second->pts.emplace_back((REAL)x, (REAL)y);
}

XPLUGIN_API int XPoints_Count_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return 0;
    return (int)it->second->pts.size();
}

/*
    Returns a stable pointer to the internal PointF buffer.
    NOTE: The pointer remains valid until the XPoints object is modified (Add/Clear/Close).
*/
XPLUGIN_API const PointF* XPoints_DataPointer_GET(int h)
{
    std::lock_guard<std::mutex> lk(gMx);
    auto it = gInst.find(h);
    if (it == gInst.end()) return nullptr;
    if (it->second->pts.empty()) return nullptr;
    return it->second->pts.data();
}

/* ────── CrossBasic glue tables ─────────────────────────────────── */
typedef struct { const char* n; const char* t; void* g; void* s; } Property;
typedef struct { const char* n; void* f; int a; const char* p[10]; const char* r; } Method;
typedef struct { const char* cls; size_t sz; void* ctor; Property* props; size_t pCnt; Method* meths; size_t mCnt; Method* consts; size_t cCnt; } ClassDef;

static Property props[] = {
    { "Count", "integer", (void*)XPoints_Count_GET, nullptr },
    { "DataPointer", "pointer", (void*)XPoints_DataPointer_GET, nullptr }
};

static Method meths[] = {
    { "Clear", (void*)XPoints_Clear, 1, { "integer" }, "void" },
    { "Add",   (void*)XPoints_Add,   3, { "integer", "double", "double" }, "void" }
};

static ClassDef classDef = {
    "XPoints",
    sizeof(PointsInst),
    (void*)XPoints_Constructor,
    props, sizeof(props)/sizeof(props[0]),
    meths, sizeof(meths)/sizeof(meths[0]),
    nullptr, 0
};

XPLUGIN_API ClassDef* GetClassDefinition() { return &classDef; }

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_DETACH) CleanupAll();
    return TRUE;
}

} // extern "C"
