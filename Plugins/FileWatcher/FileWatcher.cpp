// FileWatcher.cpp
//
// CrossBasic Plugin: FileWatcher
// Watches a directory (optionally recursively) and raises events when files
// are created, changed, or deleted. Cross-platform using std::filesystem
// polling in a worker thread.
//
// Events (via SetEventCallback):
//   "Created" -> payload: "filename|fullpath"
//   "Changed" -> payload: "filename|fullpath"
//   "Deleted" -> payload: "filename|fullpath"
//   "Error"   -> payload: error message
//
// Properties:
//   Left, Top, Width, Height       : generic geometry integers (unused by logic)
//   LockTop, LockLeft, LockRight, LockBottom : generic anchors (unused by logic)
//   Directory (string)             : directory path to watch
//   Filter (string)                : wildcard filter, e.g. "*", "*.txt", "data*.log"
//   IncludeSubdirs (boolean)       : recurse into subdirectories if true
//   PollIntervalMs (integer)       : polling interval in milliseconds
//   Enabled (boolean)              : start/stop watcher
//   Running (boolean, read-only)   : true if watcher thread is active
//
// Sample Windows build:
// g++ -std=c++17 -shared -m64 -static -static-libgcc -static-libstdc++ -s \
//     -o FileWatcher.dll FileWatcher.cpp -I. -pthread \
//     -luser32 -lole32 -loleaut32 -luuid -comdlg32 -lcomctl32 -ldwmapi \
//     -lgdi32 -ladvapi32 -lshell32 -lshlwapi -lversion

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #define UNICODE
  #define _UNICODE
  #include <windows.h>
  #define XPLUGIN_API __declspec(dllexport)
  #define strdup _strdup
#else
  #define XPLUGIN_API __attribute__((visibility("default")))
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <filesystem>
#include <system_error>

static void debug_log(const char* tag, const std::string& msg) {
#ifdef _WIN32
    std::string line = std::string("[FileWatcherPlugin] ") + tag + ": " + msg + "\r\n";
    OutputDebugStringA(line.c_str());
#endif
    std::fprintf(stderr, "[FileWatcherPlugin] %s: %s\n", tag, msg.c_str());
    std::fflush(stderr);
}
#define DBG(TAG, MSG) debug_log(TAG, (MSG))

namespace fs = std::filesystem;

// Forward declarations for structures used by CrossBasic runtime
typedef struct { const char* n; const char* t; void* g; void* s; } PropDef;
typedef struct { const char* n; void* f; int a; const char* p[10]; const char* r; } MethDef;
typedef struct { const char* d; } ConstDef;
typedef struct {
    const char* name;
    size_t      size;
    void*       ctor;
    PropDef*    props; size_t propsCount;
    MethDef*    meths; size_t methCount;
    ConstDef*   csts; size_t cstCount;
} ClassDef;

// -----------------------------------------------------------------------------
// Event callback registry
// -----------------------------------------------------------------------------

static std::mutex gEventsMx;
static std::unordered_map<int, std::unordered_map<std::string, void*>> gEvents;

static void triggerEvent(int h, const std::string& ev, const char* param) {
    void* cb = nullptr;
    {
        std::lock_guard<std::mutex> lk(gEventsMx);
        auto it = gEvents.find(h);
        if (it != gEvents.end()) {
            auto jt = it->second.find(ev);
            if (jt != it->second.end()) cb = jt->second;
        }
    }
    if (!cb) return;
    char* data = strdup(param ? param : "");
#ifdef _WIN32
    using CB = void(__stdcall*)(const char*);
#else
    using CB = void(*)(const char*);
#endif
    ((CB)cb)(data);
    std::free(data);
}

// -----------------------------------------------------------------------------
// Wildcard matching: pattern with '*' and '?' against filename
// -----------------------------------------------------------------------------

static bool wildcardMatch(const char* str, const char* pat) {
    // Simple non-recursive wildcard matcher: '*' matches any sequence, '?' one char
    const char* s = nullptr;
    const char* p = nullptr;

    while (*str) {
        if (*pat == '*') {
            // skip consecutive '*'
            while (*pat == '*') ++pat;
            if (!*pat) return true; // trailing '*' matches all
            s = str;
            p = pat;
        } else if (*pat == '?' || *pat == *str) {
            ++str;
            ++pat;
        } else if (s) {
            ++s;
            str = s;
            pat = p;
        } else {
            return false;
        }
    }
    // skip remaining '*'
    while (*pat == '*') ++pat;
    return *pat == '\0';
}

static bool matchFilter(const std::string& name, const std::string& filter) {
    if (filter.empty() || filter == "*")
        return true;
    return wildcardMatch(name.c_str(), filter.c_str());
}

// -----------------------------------------------------------------------------
// FileWatcher class
// -----------------------------------------------------------------------------

class FileWatcher {
public:
    int   handle;
    int   parentHandle; // unused but kept for consistency
    int   x, y, width, height;
    bool  LockTop, LockLeft, LockRight, LockBottom;

    // Watching configuration/state
    std::string directory;
    std::string filter;
    bool        includeSubdirs;
    int         pollIntervalMs;

    std::thread              worker;
    std::atomic<bool>        running;
    std::atomic<bool>        stopRequested;
    std::mutex               stateMx;
    std::unordered_map<std::string, fs::file_time_type> knownFiles;

    explicit FileWatcher(int h)
        : handle(h),
          parentHandle(0),
          x(0), y(0), width(100), height(30),
          LockTop(false), LockLeft(false), LockRight(false), LockBottom(false),
          directory(""),
          filter("*"),
          includeSubdirs(false),
          pollIntervalMs(1000),
          running(false),
          stopRequested(false)
    {
        DBG("Ctor", "FileWatcher instance created handle=" + std::to_string(h));
    }

    ~FileWatcher() {
        DBG("Dtor", "FileWatcher instance destroyed handle=" + std::to_string(handle));
        stop();
    }

    void emitError(const std::string& msg) {
        triggerEvent(handle, "Error", msg.c_str());
    }

    void buildSnapshot(const std::string& dir,
                       const std::string& filt,
                       bool recurse,
                       std::unordered_map<std::string, fs::file_time_type>& out) {
        std::error_code ec;
        if (dir.empty()) return;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            emitError("Directory does not exist or is not a directory: " + dir);
            return;
        }

        auto options = fs::directory_options::skip_permission_denied;
        try {
            if (recurse) {
                for (fs::recursive_directory_iterator it(dir, options, ec), end; it != end && !ec; it.increment(ec)) {
                    if (ec) break;
                    if (fs::is_regular_file(it->path(), ec) && !ec) {
                        const fs::path& p = it->path();
                        std::string fname = p.filename().string();
                        if (!matchFilter(fname, filt)) continue;
                        auto ftime = fs::last_write_time(p, ec);
                        if (!ec) {
                            out[p.string()] = ftime;
                        }
                    }
                }
            } else {
                for (fs::directory_iterator it(dir, options, ec), end; it != end && !ec; it.increment(ec)) {
                    if (ec) break;
                    if (fs::is_regular_file(it->path(), ec) && !ec) {
                        const fs::path& p = it->path();
                        std::string fname = p.filename().string();
                        if (!matchFilter(fname, filt)) continue;
                        auto ftime = fs::last_write_time(p, ec);
                        if (!ec) {
                            out[p.string()] = ftime;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            emitError(std::string("Exception while scanning directory: ") + e.what());
        } catch (...) {
            emitError("Unknown error while scanning directory.");
        }
    }

    void detectChangesOnce() {
        std::string dir;
        std::string filt;
        bool        recurse;
        {
            std::lock_guard<std::mutex> lk(stateMx);
            dir     = directory;
            filt    = filter;
            recurse = includeSubdirs;
        }

        if (dir.empty()) {
            // nothing to do
            return;
        }

        std::unordered_map<std::string, fs::file_time_type> newMap;
        buildSnapshot(dir, filt, recurse, newMap);

        std::unordered_map<std::string, fs::file_time_type> oldMap;
        {
            std::lock_guard<std::mutex> lk(stateMx);
            oldMap = knownFiles;
            knownFiles = newMap;
        }

        // Compare old vs new and fire events outside lock
        for (const auto& kv : newMap) {
            const std::string& fullPath = kv.first;
            auto it = oldMap.find(fullPath);
            const fs::file_time_type& tNew = kv.second;
            if (it == oldMap.end()) {
                // Created
                fs::path p(fullPath);
                std::string payload = p.filename().string() + "|" + fullPath;
                triggerEvent(handle, "Created", payload.c_str());
            } else {
                const fs::file_time_type& tOld = it->second;
                if (tNew != tOld) {
                    // Changed
                    fs::path p(fullPath);
                    std::string payload = p.filename().string() + "|" + fullPath;
                    triggerEvent(handle, "Changed", payload.c_str());
                }
            }
        }

        for (const auto& kv : oldMap) {
            const std::string& fullPath = kv.first;
            if (newMap.find(fullPath) == newMap.end()) {
                // Deleted
                fs::path p(fullPath);
                std::string payload = p.filename().string() + "|" + fullPath;
                triggerEvent(handle, "Deleted", payload.c_str());
            }
        }
    }

    void watchLoop() {
        DBG("Watcher", "Thread started for handle=" + std::to_string(handle));
        try {
            // Initial snapshot without firing events
            std::string dir;
            std::string filt;
            bool        recurse;
            {
                std::lock_guard<std::mutex> lk(stateMx);
                dir     = directory;
                filt    = filter;
                recurse = includeSubdirs;
            }

            std::unordered_map<std::string, fs::file_time_type> snap;
            if (!dir.empty()) {
                buildSnapshot(dir, filt, recurse, snap);
                {
                    std::lock_guard<std::mutex> lk(stateMx);
                    knownFiles = snap;
                }
            }

            while (!stopRequested.load()) {
                int intervalMs;
                {
                    std::lock_guard<std::mutex> lk(stateMx);
                    intervalMs = pollIntervalMs;
                }
                if (intervalMs < 100) intervalMs = 100;
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                if (stopRequested.load()) break;
                detectChangesOnce();
            }
        } catch (const std::exception& e) {
            emitError(std::string("Watcher thread exception: ") + e.what());
        } catch (...) {
            emitError("Watcher thread unknown exception.");
        }
        running.store(false);
        DBG("Watcher", "Thread exiting for handle=" + std::to_string(handle));
    }

    void start() {
        if (running.load()) {
            DBG("Watcher", "start() called but already running");
            return;
        }
        stopRequested.store(false);
        running.store(true);
        try {
            worker = std::thread(&FileWatcher::watchLoop, this);
        } catch (const std::exception& e) {
            running.store(false);
            emitError(std::string("Failed to start watcher thread: ") + e.what());
        } catch (...) {
            running.store(false);
            emitError("Failed to start watcher thread: unknown error.");
        }
    }

    void stop() {
        if (!running.load()) return;
        stopRequested.store(true);
        if (worker.joinable()) {
            try {
                worker.join();
            } catch (...) {
                // ignore
            }
        }
        running.store(false);
    }

    bool isRunning() const {
        return running.load();
    }
};

// -----------------------------------------------------------------------------
// Global instance management
// -----------------------------------------------------------------------------

static std::mutex gInstancesMx;
static std::unordered_map<int, FileWatcher*> gInstances;
static std::mt19937 rng(std::random_device{}());
static std::uniform_int_distribution<int> dist(10000000, 99999999);

extern "C" {

XPLUGIN_API int Constructor() {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    int h;
    do { h = dist(rng); } while (gInstances.count(h));
    gInstances[h] = new FileWatcher(h);
    DBG("API", "Constructor: handle=" + std::to_string(h));
    return h;
}

XPLUGIN_API void Close(int h) {
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if (it != gInstances.end()) {
        DBG("API", "Close: handle=" + std::to_string(h));
        delete it->second;
        gInstances.erase(it);
        // Remove any registered events for this handle
        std::lock_guard<std::mutex> lkEv(gEventsMx);
        gEvents.erase(h);
    } else {
        DBG("API", "Close: invalid handle");
    }
}

} // extern "C"

// -----------------------------------------------------------------------------
// Property helpers
// -----------------------------------------------------------------------------

#define PROP_INT(NAME, FIELD) \
XPLUGIN_API void FileWatcher_##NAME##_SET(int h,int v){ \
  FileWatcher* self=nullptr; { \
    std::lock_guard<std::mutex> lk(gInstancesMx); \
    auto it=gInstances.find(h); \
    if(it!=gInstances.end()) self=it->second; \
  } \
  if(self){ \
    std::lock_guard<std::mutex> lk(self->stateMx); \
    self->FIELD=v; \
    DBG("API", #NAME "_SET ok"); \
  } else { \
    DBG("API", #NAME "_SET: bad handle"); \
  } \
} \
XPLUGIN_API int FileWatcher_##NAME##_GET(int h){ \
  std::lock_guard<std::mutex> lk(gInstancesMx); \
  auto it=gInstances.find(h); \
  int rv = 0; \
  if(it!=gInstances.end()){ \
    FileWatcher* self = it->second; \
    std::lock_guard<std::mutex> lk2(self->stateMx); \
    rv = self->FIELD; \
  } \
  DBG("API", std::string(#NAME "_GET -> ")+std::to_string(rv)); \
  return rv; \
}

#define BOOL_PROP(NAME, FIELD) \
XPLUGIN_API void FileWatcher_##NAME##_SET(int h,bool v){ \
  FileWatcher* self=nullptr; { \
    std::lock_guard<std::mutex> lk(gInstancesMx); \
    auto it=gInstances.find(h); \
    if(it!=gInstances.end()) self=it->second; \
  } \
  if(!self){ DBG("API", #NAME "_SET: bad handle"); return; } \
  { \
    std::lock_guard<std::mutex> lk(self->stateMx); \
    self->FIELD = v; \
  } \
  DBG("API", std::string(#NAME "_SET -> ") + (v ? "true":"false")); \
} \
XPLUGIN_API bool FileWatcher_##NAME##_GET(int h){ \
  std::lock_guard<std::mutex> lk(gInstancesMx); \
  auto it=gInstances.find(h); \
  bool rv = false; \
  if(it!=gInstances.end()){ \
    FileWatcher* self = it->second; \
    std::lock_guard<std::mutex> lk2(self->stateMx); \
    rv = self->FIELD; \
  } \
  DBG("API", std::string(#NAME "_GET -> ") + (rv ? "true":"false")); \
  return rv; \
}

// Geometry properties (generic)
PROP_INT(Left,  x)
PROP_INT(Top,   y)
PROP_INT(Width, width)
PROP_INT(Height,height)

// Lock flags
BOOL_PROP(LockTop,    LockTop)
BOOL_PROP(LockLeft,   LockLeft)
BOOL_PROP(LockRight,  LockRight)
BOOL_PROP(LockBottom, LockBottom)

// Directory property (string)
XPLUGIN_API void FileWatcher_Directory_SET(int h,const char* utf8){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if(it==gInstances.end()){
        DBG("API","Directory_SET: bad handle");
        return;
    }
    FileWatcher* self = it->second;
    std::lock_guard<std::mutex> lk2(self->stateMx);
    self->directory = utf8 ? utf8 : "";
    DBG("API","Directory_SET -> " + self->directory);
}

XPLUGIN_API const char* FileWatcher_Directory_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if(it==gInstances.end()){
        DBG("API","Directory_GET: bad handle");
        return strdup("");
    }
    FileWatcher* self = it->second;
    std::lock_guard<std::mutex> lk2(self->stateMx);
    DBG("API","Directory_GET -> " + self->directory);
    return strdup(self->directory.c_str());
}

// Filter property (string)
XPLUGIN_API void FileWatcher_Filter_SET(int h,const char* utf8){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if(it==gInstances.end()){
        DBG("API","Filter_SET: bad handle");
        return;
    }
    FileWatcher* self = it->second;
    std::lock_guard<std::mutex> lk2(self->stateMx);
    self->filter = utf8 ? utf8 : "*";
    if (self->filter.empty()) self->filter = "*";
    DBG("API","Filter_SET -> " + self->filter);
}

XPLUGIN_API const char* FileWatcher_Filter_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it = gInstances.find(h);
    if(it==gInstances.end()){
        DBG("API","Filter_GET: bad handle");
        return strdup("*");
    }
    FileWatcher* self = it->second;
    std::lock_guard<std::mutex> lk2(self->stateMx);
    DBG("API","Filter_GET -> " + self->filter);
    return strdup(self->filter.c_str());
}

// IncludeSubdirs property (boolean)
BOOL_PROP(IncludeSubdirs, includeSubdirs)

// PollIntervalMs property (integer, clamped to >=100)
XPLUGIN_API void FileWatcher_PollIntervalMs_SET(int h,int v){
    FileWatcher* self=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it!=gInstances.end()) self=it->second;
    }
    if(!self){
        DBG("API","PollIntervalMs_SET: bad handle");
        return;
    }
    if (v < 10) v = 10;
    {
        std::lock_guard<std::mutex> lk(self->stateMx);
        self->pollIntervalMs = v;
    }
    DBG("API", "PollIntervalMs_SET -> " + std::to_string(v));
}

XPLUGIN_API int FileWatcher_PollIntervalMs_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    int rv = 0;
    if(it!=gInstances.end()){
        FileWatcher* self = it->second;
        std::lock_guard<std::mutex> lk2(self->stateMx);
        rv = self->pollIntervalMs;
    }
    DBG("API", std::string("PollIntervalMs_GET -> ") + std::to_string(rv));
    return rv;
}

// Enabled property (boolean) -> start/stop watcher
XPLUGIN_API void FileWatcher_Enabled_SET(int h,bool v){
    FileWatcher* self=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it!=gInstances.end()) self=it->second;
    }
    if(!self){
        DBG("API","Enabled_SET: bad handle");
        return;
    }
    if (v) {
        self->start();
    } else {
        self->stop();
    }
    DBG("API", std::string("Enabled_SET -> ") + (v ? "true":"false"));
}

XPLUGIN_API bool FileWatcher_Enabled_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    bool rv = false;
    if(it!=gInstances.end()){
        FileWatcher* self = it->second;
        rv = self->isRunning();
    }
    DBG("API", std::string("Enabled_GET -> ") + (rv ? "true":"false"));
    return rv;
}

// Running property (read-only boolean)
XPLUGIN_API bool FileWatcher_Running_GET(int h){
    std::lock_guard<std::mutex> lk(gInstancesMx);
    auto it=gInstances.find(h);
    bool rv = false;
    if(it!=gInstances.end()){
        rv = it->second->isRunning();
    }
    DBG("API", std::string("Running_GET -> ") + (rv ? "true":"false"));
    return rv;
}

#undef PROP_INT
#undef BOOL_PROP

// -----------------------------------------------------------------------------
// Methods
// -----------------------------------------------------------------------------

extern "C" {

// SetEventCallback(handle, eventName, callbackPtr)
XPLUGIN_API bool FileWatcher_SetEventCallback(int h,const char* ev,void* cb){
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        if(!gInstances.count(h)){
            DBG("API","SetEventCallback: bad handle");
            return false;
        }
    }
    std::string key = ev ? ev : "";
    auto p = key.rfind(':');
    if (p != std::string::npos) key.erase(0, p + 1);
    std::lock_guard<std::mutex> lk2(gEventsMx);
    gEvents[h][key] = cb;
    DBG("API","SetEventCallback: handle=" + std::to_string(h) + " event=" + key);
    return true;
}

// Start(handle)
XPLUGIN_API void FileWatcher_Start(int h){
    FileWatcher* self=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it!=gInstances.end()) self=it->second;
    }
    if(!self){
        DBG("API","Start: bad handle");
        return;
    }
    self->start();
}

// Stop(handle)
XPLUGIN_API void FileWatcher_Stop(int h){
    FileWatcher* self=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it!=gInstances.end()) self=it->second;
    }
    if(!self){
        DBG("API","Stop: bad handle");
        return;
    }
    self->stop();
}

// Refresh(handle) - runs an immediate change detection pass
XPLUGIN_API void FileWatcher_Refresh(int h){
    FileWatcher* self=nullptr;
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        auto it=gInstances.find(h);
        if(it!=gInstances.end()) self=it->second;
    }
    if(!self){
        DBG("API","Refresh: bad handle");
        return;
    }
    self->detectChangesOnce();
}

} // extern "C"

// -----------------------------------------------------------------------------
// Class definition for CrossBasic
// -----------------------------------------------------------------------------

static PropDef gProps[] = {
    {"Left",          "integer", (void*)FileWatcher_Left_GET,          (void*)FileWatcher_Left_SET},
    {"Top",           "integer", (void*)FileWatcher_Top_GET,           (void*)FileWatcher_Top_SET},
    {"Width",         "integer", (void*)FileWatcher_Width_GET,         (void*)FileWatcher_Width_SET},
    {"Height",        "integer", (void*)FileWatcher_Height_GET,        (void*)FileWatcher_Height_SET},
    {"LockTop",       "boolean", (void*)FileWatcher_LockTop_GET,       (void*)FileWatcher_LockTop_SET},
    {"LockLeft",      "boolean", (void*)FileWatcher_LockLeft_GET,      (void*)FileWatcher_LockLeft_SET},
    {"LockRight",     "boolean", (void*)FileWatcher_LockRight_GET,     (void*)FileWatcher_LockRight_SET},
    {"LockBottom",    "boolean", (void*)FileWatcher_LockBottom_GET,    (void*)FileWatcher_LockBottom_SET},
    {"Directory",     "string",  (void*)FileWatcher_Directory_GET,     (void*)FileWatcher_Directory_SET},
    {"Filter",        "string",  (void*)FileWatcher_Filter_GET,        (void*)FileWatcher_Filter_SET},
    {"IncludeSubdirs","boolean", (void*)FileWatcher_IncludeSubdirs_GET,(void*)FileWatcher_IncludeSubdirs_SET},
    {"PollIntervalMs","integer", (void*)FileWatcher_PollIntervalMs_GET,(void*)FileWatcher_PollIntervalMs_SET},
    {"Enabled",       "boolean", (void*)FileWatcher_Enabled_GET,       (void*)FileWatcher_Enabled_SET},
    {"Running",       "boolean", (void*)FileWatcher_Running_GET,       nullptr}
};

static MethDef gMeths[] = {
    {"Close",           (void*)Close,                       1, {"integer"},                       "void"},
    {"Filewatcher_SetEventCallback",(void*)FileWatcher_SetEventCallback,3, {"integer","string","pointer"},    "boolean"},
    {"Start",           (void*)FileWatcher_Start,           1, {"integer"},                       "void"},
    {"Stop",            (void*)FileWatcher_Stop,            1, {"integer"},                       "void"},
    {"Refresh",         (void*)FileWatcher_Refresh,         1, {"integer"},                       "void"}
};

static ClassDef gClassDef = {
    "FileWatcher",
    sizeof(FileWatcher),
    (void*)Constructor,
    gProps, sizeof(gProps)/sizeof(gProps[0]),
    gMeths, sizeof(gMeths)/sizeof(gMeths[0]),
    nullptr, 0
};

// -----------------------------------------------------------------------------
// CrossBasic entry points (export both for maximum compatibility)
// -----------------------------------------------------------------------------
extern "C" {

// Primary single-class entry point (what you expected)
XPLUGIN_API ClassDef* GetClassDefinition() {
    DBG("API","GetClassDefinition");
    return &gClassDef;
}

// Multi-class entry point (some CrossBasic builds prefer this)
// Signature chosen to be safe: returns pointer to first ClassDef and an out-count.
// If CrossBasic only checks for symbol existence, this alone fixes your error.
// If it actually calls it, this signature is the most common pattern.
// XPLUGIN_API ClassDef** GetPluginEntries(int* outCount) {
//     DBG("API","GetPluginEntries");
//     static ClassDef* entries[1] = { &gClassDef };
//     if (outCount) *outCount = 1;
//     return entries;
// }

} // extern "C"


// -----------------------------------------------------------------------------
// DLL cleanup
// -----------------------------------------------------------------------------

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        DBG("DLL", "PROCESS_DETACH cleanup");
        {
            std::lock_guard<std::mutex> lk(gInstancesMx);
            for (auto& kv : gInstances) {
                delete kv.second;
            }
            gInstances.clear();
        }
        {
            std::lock_guard<std::mutex> lkEv(gEventsMx);
            gEvents.clear();
        }
    }
    return TRUE;
}
#else
__attribute__((destructor))
static void onUnload(){
    DBG("DLL", "destructor cleanup");
    {
        std::lock_guard<std::mutex> lk(gInstancesMx);
        for (auto& kv : gInstances) {
            delete kv.second;
        }
        gInstances.clear();
    }
    {
        std::lock_guard<std::mutex> lkEv(gEventsMx);
        gEvents.clear();
    }
}
#endif
