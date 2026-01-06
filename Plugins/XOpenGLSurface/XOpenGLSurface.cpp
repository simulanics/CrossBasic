/*

  XOpenGLSurface.cpp
  CrossBasic Plugin: XOpenGLSurface                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
// CrossBasic plugin wrapping OpenGL for generic rendering support
// NOTE: Ensure in your CrossBasic script that you invoke the Array.LastIndex property (not the bound method) when computing buffer sizes.  E.g.:
//       gl.BufferData(GL_ARRAY_BUFFER, verts, verts.LastIndex * 3 * SizeOf(Double), GL_STATIC_DRAW)

#ifdef _WIN32
  #define UNICODE
  #define _UNICODE
  #include <windows.h>
  #pragma comment(lib, "opengl32.lib")
  #define XPLUGIN_API __declspec(dllexport)
#else
  #define XPLUGIN_API __attribute__((visibility("default")))
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <random>
#include <iostream>
#include <cstdint>
#include <cstring>

// Plugin instance holding context
class XOpenGLSurface {
public:
    int handle;
    GLFWwindow* window = nullptr;
    XOpenGLSurface(int h) : handle(h) {}
    ~XOpenGLSurface() {
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
    }
};

static std::unordered_map<int, XOpenGLSurface*> gInstances;
static std::mutex gMutex;
static std::atomic<bool> gGlfwInit{false};

extern "C" {

// Constructor
XPLUGIN_API int Constructor() {
    std::lock_guard<std::mutex> lock(gMutex);
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<int> dist(10000000,99999999);
    int h;
    do { h = dist(rng); } while (gInstances.count(h));
    gInstances[h] = new XOpenGLSurface(h);
    return h;
}

// Destructor
XPLUGIN_API void Close(int h) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gInstances.find(h);
    if (it != gInstances.end()) {
        delete it->second;
        gInstances.erase(it);
    }
}

// Initialize OpenGL context
XPLUGIN_API bool Init(int h,int width,int height,const char* title) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gInstances.find(h); if (it == gInstances.end()) return false;
    if (!gGlfwInit) {
        if (!glfwInit()) return false;
        gGlfwInit = true;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);
#endif
    auto* inst = it->second;
    inst->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!inst->window) return false;
    glfwMakeContextCurrent(inst->window);
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) return false;
    glEnable(GL_DEPTH_TEST);
    return true;
}

// Viewport
XPLUGIN_API void SetViewport(int, int x,int y,int w,int h) { glViewport(x,y,w,h); }
// Clear
XPLUGIN_API void Clear(int, int mask) { glClear(mask); }
// Shader compile
XPLUGIN_API unsigned int CompileShader(int, unsigned int type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s,512,nullptr,log); std::cerr<<log<<std::endl; }
    return s;
}
// Program link
XPLUGIN_API unsigned int LinkProgram(int, unsigned int vs, unsigned int fs) {
    unsigned int p = glCreateProgram();
    glAttachShader(p,vs);
    glAttachShader(p,fs);
    glLinkProgram(p);
    int ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if (!ok) { char log[512]; glGetProgramInfoLog(p,512,nullptr,log); std::cerr<<log<<std::endl; }
    return p;
}
// Use
XPLUGIN_API void UseProgram(int, unsigned int p) { glUseProgram(p); }
// Uniform location
XPLUGIN_API int GetUniformLocation(int, int program, const char* name) { return glGetUniformLocation(program,name); }
// Vertex array
XPLUGIN_API unsigned int CreateVertexArray(int) { unsigned int id; glGenVertexArrays(1,&id); return id; }
XPLUGIN_API void BindVertexArray(int, unsigned int id) { glBindVertexArray(id); }
// Buffer
XPLUGIN_API unsigned int CreateBuffer(int) { unsigned int id; glGenBuffers(1,&id); return id; }
XPLUGIN_API void BindBuffer(int, unsigned int t, unsigned int id) { glBindBuffer(t,id); }
// BufferData: target, data pointer, size in bytes, usage
XPLUGIN_API void BufferData(int handle, unsigned int target, const void* data, int sizeBytes, unsigned int usage) {
    // Raw pointer/bytes version (advanced use).
    if (!gInstances.count(handle)) return;
    glBufferData(target, sizeBytes, data, usage);
}

XPLUGIN_API void BufferDataF(int handle, unsigned int target, const double* values, int count, unsigned int usage) {
    // Accepts CrossBasic numeric arrays via VM type 'array' (marshalled as double*).
    if (!gInstances.count(handle)) return;
    if (!values || count <= 0) return;
    std::vector<float> tmp;
    tmp.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) tmp.push_back(static_cast<float>(values[i]));
    glBufferData(target, static_cast<GLsizeiptr>(tmp.size() * sizeof(float)), tmp.data(), usage);
}

XPLUGIN_API void BufferDataUI(int handle, unsigned int target, const double* values, int count, unsigned int usage) {
    // Accepts CrossBasic numeric arrays via VM type 'array' (marshalled as double*).
    if (!gInstances.count(handle)) return;
    if (!values || count <= 0) return;
    std::vector<unsigned int> tmp;
    tmp.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) tmp.push_back(static_cast<unsigned int>(values[i]));
    glBufferData(target, static_cast<GLsizeiptr>(tmp.size() * sizeof(unsigned int)), tmp.data(), usage);
}
// Attributes
XPLUGIN_API void VertexAttribPointer(int, unsigned int idx,int sz,unsigned int t,bool n,int st,int off) {
    glVertexAttribPointer(idx,sz,t,n,st,reinterpret_cast<void*>(static_cast<intptr_t>(off)));
}
XPLUGIN_API void EnableVertexAttrib(int, unsigned int idx) { glEnableVertexAttribArray(idx); }
// Uniform setters
XPLUGIN_API void UniformMatrix4fv(int, int loc,int cnt,bool n,const float* v) { glUniformMatrix4fv(loc,cnt,n,v); }
XPLUGIN_API void Uniform3f(int, int loc,double x,double y,double z) { glUniform3f(loc,(float)x,(float)y,(float)z); }
// Draw
XPLUGIN_API void DrawElements(int, unsigned int m,int cnt,unsigned int t,int off) { glDrawElements(m,cnt,t,reinterpret_cast<void*>(static_cast<intptr_t>(off))); }
// Swap and poll
XPLUGIN_API void SwapGLBuffers(int h) {
    XOpenGLSurface* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(h);
        if (it == gInstances.end()) return;
        inst = it->second;
    }
    glfwSwapBuffers(inst->window);
}
XPLUGIN_API void PollEvents(int) { glfwPollEvents(); }
// Should close
XPLUGIN_API bool ShouldClose(int h) {
    XOpenGLSurface* inst = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        auto it = gInstances.find(h);
        if (it == gInstances.end()) return true;
        inst = it->second;
    }
    return glfwWindowShouldClose(inst->window);
}
// Time
XPLUGIN_API double GetTime(int) { return glfwGetTime(); }

// Helper functions for matrix operations
// Identity matrix
XPLUGIN_API float* IdentityMatrix4(int) {
    static float buffer[16]; glm::mat4 m(1.0f);
    std::memcpy(buffer, glm::value_ptr(m), sizeof(buffer)); return buffer;
}
// Rotate matrix
XPLUGIN_API float* RotateMatrix4(int, const float* inMat, double angle, double x, double y, double z) {
    static float buffer[16]; glm::mat4 m;
    for (int i = 0; i < 16; ++i) m[i/4][i%4] = inMat[i];
    glm::mat4 r = glm::rotate(m, static_cast<float>(angle), glm::vec3(static_cast<float>(x),static_cast<float>(y),static_cast<float>(z)));
    std::memcpy(buffer, glm::value_ptr(r), sizeof(buffer)); return buffer;
}
// LookAt matrix
XPLUGIN_API float* LookAtMatrix4(int, double ex,double ey,double ez, double cx,double cy,double cz, double ux,double uy,double uz) {
    static float buffer[16];
    glm::mat4 v = glm::lookAt(glm::vec3(ex,ey,ez), glm::vec3(cx,cy,cz), glm::vec3(ux,uy,uz));
    std::memcpy(buffer, glm::value_ptr(v), sizeof(buffer)); return buffer;
}
// Perspective matrix
XPLUGIN_API float* PerspectiveMatrix4(int, double fovy,double aspect,double zn,double zf) {
    static float buffer[16];
    glm::mat4 p = glm::perspective(glm::radians(static_cast<float>(fovy)), static_cast<float>(aspect), static_cast<float>(zn), static_cast<float>(zf));
    std::memcpy(buffer, glm::value_ptr(p), sizeof(buffer)); return buffer;
}

} // extern "C"

// CrossBasic binding
#include <cstddef>
struct ClassProperty {const char* name; const char* type; void* get; void* set;};
struct ClassEntry    {const char* name; void* func; int arity; const char* params[10]; const char* ret;};
struct ClassConstant {const char* decl;};
struct ClassDefinition {const char* className; size_t classSize; void* ctor;
    ClassProperty* props; size_t propsCount;
    ClassEntry* methods; size_t methodsCount;
    ClassConstant* consts; size_t constCount;
};
static ClassProperty props[] = {};
static ClassEntry methods[] = {
    {"Init",               (void*)Init,               4,{"integer","integer","integer","string"},     "boolean"},
    {"SetViewport",        (void*)SetViewport,        5,{"integer","integer","integer","integer","integer"},"void"},
    {"Clear",              (void*)Clear,              2,{"integer","integer"},                            "void"},
    {"CompileShader",      (void*)CompileShader,      3,{"integer","integer","string"},                 "integer"},
    {"LinkProgram",        (void*)LinkProgram,        3,{"integer","integer","integer"},                "integer"},
    {"UseProgram",         (void*)UseProgram,         2,{"integer","integer"},                            "void"},
    {"GetUniformLocation", (void*)GetUniformLocation, 3,{"integer","integer","string"},                 "integer"},
    {"CreateVertexArray",  (void*)CreateVertexArray,  1,{"integer"},                                      "integer"},
    {"BindVertexArray",    (void*)BindVertexArray,    2,{"integer","integer"},                            "void"},
    {"CreateBuffer",       (void*)CreateBuffer,       1,{"integer"},                                      "integer"},
    {"BindBuffer",         (void*)BindBuffer,         3,{"integer","integer","integer"},                "void"},
    {"BufferData",         (void*)BufferData,         5,{"integer","integer","pointer","integer","integer"},"void"},
    {"BufferDataF",        (void*)BufferDataF,        5,{"integer","integer","array","integer","integer"},"void"},
    {"BufferDataUI",       (void*)BufferDataUI,       5,{"integer","integer","array","integer","integer"},"void"},
    {"VertexAttribPointer",(void*)VertexAttribPointer,7,{"integer","integer","integer","integer","boolean","integer","integer"},"void"},
    {"EnableVertexAttrib", (void*)EnableVertexAttrib, 2,{"integer","integer"},                            "void"},
    {"UniformMatrix4fv",   (void*)UniformMatrix4fv,   5,{"integer","integer","integer","boolean","pointer"},"void"},
    {"Uniform3f",          (void*)Uniform3f,          5,{"integer","integer","double","double","double"},"void"},
    {"DrawElements",       (void*)DrawElements,       5,{"integer","integer","integer","integer","integer"},"void"},
    {"SwapGLBuffers",      (void*)SwapGLBuffers,      1,{"integer"},                                      "void"},
    {"PollEvents",         (void*)PollEvents,         1,{"integer"},                                      "void"},
    {"ShouldClose",        (void*)ShouldClose,        1,{"integer"},                                      "boolean"},
    {"GetTime",            (void*)GetTime,            1,{"integer"},                                      "double"},
    {"IdentityMatrix4",    (void*)IdentityMatrix4,    1,{"integer"},                                      "pointer"},
    {"RotateMatrix4",      (void*)RotateMatrix4,      6,{"integer","pointer","double","double","double","double"},"pointer"},
    {"LookAtMatrix4",      (void*)LookAtMatrix4,     10,{"integer","double","double","double","double","double","double","double","double","double"},"pointer"},
    {"PerspectiveMatrix4", (void*)PerspectiveMatrix4, 5,{"integer","double","double","double","double"},           "pointer"}
};
static ClassConstant consts[] = {
    {"kGL_COLOR_BUFFER_BIT=0x00004000"},
    {"kGL_DEPTH_BUFFER_BIT=0x00000100"},
    {"kGL_TRIANGLES=0x0004"},
    {"kGL_STATIC_DRAW=0x88E4"},
    {"kGL_ARRAY_BUFFER=0x8892"},
    {"kGL_ELEMENT_ARRAY_BUFFER=0x8893"},
    {"kGL_FLOAT=0x1406"},
    {"kGL_UNSIGNED_INT=0x1405"}
};
static ClassDefinition classDef = {
    "XOpenGLSurface",
    sizeof(XOpenGLSurface),
    (void*)Constructor,
    props,
    sizeof(props)/sizeof(props[0]),
    methods,
    sizeof(methods)/sizeof(methods[0]),
    consts,
    sizeof(consts)/sizeof(consts[0])
};
extern "C" XPLUGIN_API ClassDefinition* GetClassDefinition() { return &classDef; }
