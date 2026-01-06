// ============================================================================
// CrossBasic Bytecode Compiler and Virtual Machine
// Created by The Simulanics AI Team under direction of Matthew A. Combatti
// https://www.crossbasic.com
// DISCLAIMER: Simulanics Technologies and CrossBasic are not affiliated with Xojo, Inc.
// -----------------------------------------------------------------------------
/*

  crossbasic.cpp
  Application: CrossBasic                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
// -----------------------------------------------------------------------------  
// Build: g++ -o crossbasic.exe crossbasic.cpp -lffi -O3 -march=native -mtune=native -flto
// Library: g++ -shared -DBUILD_SHARED -s -o crossbasic.dll crossbasic.cpp -lffi -O3 -march=native -mtune=native
// Full Example: g++ -s -static -m64 -o crossbasic.exe crossbasic.cpp crossbasic.res -Lc:/xojodevkit/x86_64-w64-mingw32/lib/libffix64 -lffi -static-libgcc -static-libstdc++ -O3 -march=native -mtune=native

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <cmath>
#include <random>
#include <iomanip>
#include <cstring>
#include <typeinfo>
#include <cstdint>
#include <streambuf>
#include <unordered_set>
#include <mutex> 
#include <queue>
#include <thread>
#include <cerrno>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <dirent.h>
#endif

#include <ffi.h>

// ============================================================================  
// Debugging and Time globals  
// ============================================================================
bool DEBUG_MODE = false; // set to true for debug logging
void debugLog(const std::string& msg) {
    if (DEBUG_MODE)
        std::cout << "[DEBUG] " << msg << std::endl;
}
std::chrono::steady_clock::time_point startTime;

// ---------------------------------------------------------------------------  
// Global random engine used by built-in rnd (and random class)
// Global RNG and mutex.
std::mt19937 global_rng(std::chrono::steady_clock::now().time_since_epoch().count());
std::mutex rngMutex;

// Forward declaration of VM struct for use in callbacks.
struct VM;
VM* globalVM = nullptr;

// Add a global mutex to protect VM state:
std::recursive_mutex vmMutex;


// ============================================================================  
// Forward declarations for object types
// ============================================================================
struct ObjFunction;
struct ObjClass;
struct ObjInstance;
struct ObjArray;
struct ObjBoundMethod;
struct ObjModule;
struct ObjRef;

// ============================================================================  
// Color type  
// In Xojo a color literal is written as &cRRGGBB (hexadecimal).
// ============================================================================
struct Color {
    unsigned int value;
};

// ============================================================================  
// NEW: Enum object definition
// ============================================================================
struct ObjEnum {
    std::string name;
    std::unordered_map<std::string, int> members;
};

// ============================================================================  
// Built-in function type
// ============================================================================
using BuiltinFn = std::function<struct Value(const std::vector<struct Value>&)>;

// ----------------------------------------------------------------------------  
// For class property defaults we use a property map
// ----------------------------------------------------------------------------
using PropertiesType = std::vector<std::pair<std::string, struct Value>>;

// ============================================================================  
// Dynamic Value type – a wrapper around std::variant
// Now includes pointer type (void*) for pointer variables.
// ============================================================================
// By Design - Not a duplication mistake: The duplicate "using" declaration 
// inherits std::variant's constructors so Value can be directly constructed 
// from any of its alternatives.
struct Value : public std::variant<
    std::monostate,
    int,
    double,
    bool,
    std::string,
    Color,
    std::shared_ptr<ObjFunction>,
    std::shared_ptr<ObjClass>,
    std::shared_ptr<ObjInstance>,
    std::shared_ptr<ObjArray>,
    std::shared_ptr<ObjBoundMethod>,
    BuiltinFn,
    PropertiesType,
    std::vector<std::shared_ptr<ObjFunction>>,
    std::shared_ptr<ObjModule>,
    std::shared_ptr<ObjEnum>,
    std::shared_ptr<ObjRef>,
    void* // Pointer type
> {
    using std::variant<
        std::monostate,
        int,
        double,
        bool,
        std::string,
        Color,
        std::shared_ptr<ObjFunction>,
        std::shared_ptr<ObjClass>,
        std::shared_ptr<ObjInstance>,
        std::shared_ptr<ObjArray>,
        std::shared_ptr<ObjBoundMethod>,
        BuiltinFn,
        PropertiesType,
        std::vector<std::shared_ptr<ObjFunction>>,
        std::shared_ptr<ObjModule>,
        std::shared_ptr<ObjEnum>,
        std::shared_ptr<ObjRef>,
        void*
    >::variant;
};





// ============================================================================  
// ObjRef – internal reference wrapper used for ByRef parameters.
// Holds a pointer to the ultimate Value cell in an Environment chain.
// ============================================================================
struct ObjRef {
    Value* target = nullptr;
};

// Forward declaration for invokeScriptCallback:
void invokeScriptCallback(const Value& funcVal, const char* param);

struct CallbackRequest {
    Value funcVal;
    std::string param;
};

std::queue<CallbackRequest> callbackQueue;
std::mutex callbackQueueMutex;

std::thread::id mainThreadId;

// ---------------------------------------------------------------------------
//  processPendingCallbacks   – Drain queue without blocking the VM (non-blocking main thread/threads)
// ---------------------------------------------------------------------------
void processPendingCallbacks()
{
    for (;;) {
        CallbackRequest req;

        // 1) Grab one pending callback under the lock
        {
            std::lock_guard<std::mutex> lk(callbackQueueMutex);
            if (callbackQueue.empty())
                break;                // nothing to do
            req = std::move(callbackQueue.front());
            callbackQueue.pop();
        }   // <-- mutex released here

        // 2) Now it’s safe to run script code
        invokeScriptCallback(req.funcVal, req.param.c_str());
    }
}


// ----------------------------------------------------------------------------  
// Helper templates for type queries and access.
// ----------------------------------------------------------------------------
template<typename T>
bool holds(const Value& v) {
    return std::holds_alternative<T>(v); 
}
template<typename T>
T getVal(const Value& v) {
    return std::get<T>(v);
}
template <typename T>
std::string getTypeName(const T& var) {
    return typeid(var).name();
}

// ----------------------------------------------------------------------------  
// Helper: Convert a string to lowercase
// ----------------------------------------------------------------------------
std::string toLower(const std::string& s) {
    std::string ret = s;
    std::transform(ret.begin(), ret.end(), ret.begin(), ::tolower);
    return ret;
}

// ============================================================================  
// Parameter structure for functions/methods
// ============================================================================
struct Param {
    std::string name;
    std::string type;
    bool optional;
    bool isAssigns = false;
    Value defaultValue;
    bool byRef = false;
};

// ============================================================================  
// Enum for Access Modifiers (for module members)
// ============================================================================
enum class AccessModifier { PUBLIC, PRIVATE };

// ============================================================================  
// Object definitions
// ============================================================================
struct ObjFunction {
    std::string name;
    int arity = 0; // Parameter initialization.
    std::vector<Param> params; // Full parameter list
    struct CodeChunk {
        std::vector<int> code;
        std::vector<Value> constants;
    } chunk;
};

struct ObjClass {
    std::string name;
    std::unordered_map<std::string, Value> methods;
    PropertiesType properties;
    bool isPlugin = false;
    BuiltinFn pluginConstructor;
    std::unordered_map<std::string, std::pair<BuiltinFn, BuiltinFn>> pluginProperties;
};

struct ObjInstance {
    std::shared_ptr<ObjClass> klass;
    std::unordered_map<std::string, Value> fields;
    void* pluginInstance = nullptr;
};

struct ObjArray {
    std::vector<Value> elements;
};

struct ObjBoundMethod {
    Value receiver;
    std::string name;
};

struct ObjModule {
    std::string name;
    std::unordered_map<std::string, Value> publicMembers;
};

// ============================================================================  
// valueToString – visitor for Value conversion (with trailing zero trimming)
// ============================================================================
// valueToString converts a Value to a string
std::string valueToString(const Value& val) {
    struct Visitor {
        std::string operator()(std::monostate) const { return "nil"; }
        std::string operator()(int i) const { return std::to_string(i); }
        std::string operator()(double d) const {
            std::string s = std::to_string(d);
            size_t pos = s.find('.');
            if (pos != std::string::npos) {
                while (!s.empty() && s.back() == '0')
                    s.pop_back();
                if (!s.empty() && s.back() == '.')
                    s.pop_back();
            }
            return s;
        }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(const std::string& s) const { return s; }
        std::string operator()(const Color& col) const {
            char buf[10];
            std::snprintf(buf, sizeof(buf), "&h%06X", col.value & 0xFFFFFF);
            return std::string(buf);
        }
        std::string operator()(const std::shared_ptr<ObjFunction>& fn) const { return "<function " + fn->name + ">"; }
        std::string operator()(const std::shared_ptr<ObjClass>& cls) const { return "<class " + cls->name + ">"; }
        std::string operator()(const std::shared_ptr<ObjInstance>& inst) const { return "<instance of " + inst->klass->name + ">"; }
        std::string operator()(const std::shared_ptr<ObjArray>& arr) const { return "Array(" + std::to_string(arr->elements.size()) + ")"; }
        std::string operator()(const std::shared_ptr<ObjBoundMethod>& bm) const { return "<bound method " + bm->name + ">"; }
        std::string operator()(const BuiltinFn&) const { return "<builtin fn>"; }
        std::string operator()(const PropertiesType&) const { return "<properties>"; }
        std::string operator()(const std::vector<std::shared_ptr<ObjFunction>>&) const { return "<overloaded functions>"; }
        std::string operator()(const std::shared_ptr<ObjModule>& mod) const { return "<module " + mod->name + ">"; }
        std::string operator()(const std::shared_ptr<ObjEnum>& e) const { return "<enum " + e->name + ">"; }
        std::string operator()(const std::shared_ptr<ObjRef>&) const { return "<byref>"; }
        std::string operator()(void* ptr) const {
            if(ptr == nullptr) return "nil";
            char buf[20];
            std::snprintf(buf, sizeof(buf), "ptr(%p)", ptr);
            return std::string(buf);
        } // Pointer type
    } visitor;
    return std::visit(visitor, val);
}

// ============================================================================  
// Token and XTokenType Definitions
// ============================================================================
enum class XTokenType {
    LEFT_PAREN, RIGHT_PAREN, COMMA, PLUS, MINUS, STAR, SLASH, EQUAL,
    DOT, LEFT_BRACKET, RIGHT_BRACKET, COLON,
    IDENTIFIER, STRING, NUMBER, COLOR, BOOLEAN_TRUE, BOOLEAN_FALSE,
    FUNCTION, SUB, END, RETURN, CLASS, NEW, DIM, AS, XOPTIONAL, PUBLIC, PRIVATE,
    XCONST, PRINT, GOTO,
    IF, THEN, ELSE, ELSEIF,
    FOR, TO, DOWNTO, STEP, NEXT,
    WHILE, WEND,
    NOT, AND, OR, XOR,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL, NOT_EQUAL,
    EOF_TOKEN,
    CARET,  // '^'
    MOD,   // "mod" keyword/operator
    MODULE, EXTENDS, // Module declaration
    DECLARE, // Module declaration
    SELECT,  // Select keyword for Select Case statement
    CASE,   // Case keyword for Select Case statement
    ENUM,    // Enum keyword
    PLUS_EQUAL,    // +=
    MINUS_EQUAL,   // -=
    STAR_EQUAL,    // *=
    SLASH_EQUAL,   // /=
    ASSIGNS,
    BYREF
};

struct Token {
    XTokenType type;
    std::string lexeme;
    int line;
};

// ============================================================================  
// Lexer
// ============================================================================
class Lexer {
public:
    Lexer(const std::string& source) : source(source) {}
    std::vector<Token> scanTokens() {
        while (!isAtEnd()) {
            start = current;
            scanToken();
        }
        tokens.push_back({ XTokenType::EOF_TOKEN, "", line });
        return tokens;
    }
private:
    const std::string source;
    std::vector<Token> tokens;
    int start = 0, current = 0, line = 1;

    bool isAtEnd() { return current >= source.size(); }
    char advance() { return source[current++]; }
    void addToken(XTokenType type) {
        tokens.push_back({ type, source.substr(start, current - start), line });
    }
    bool match(char expected) {
        if (isAtEnd() || source[current] != expected) return false;
        current++;
        return true;
    }
    char peek() { return isAtEnd() ? '\0' : source[current]; }
    char peekNext() { return (current + 1 >= source.size()) ? '\0' : source[current + 1]; }
    void scanToken() {
        char c = advance();
        switch (c) {
        case '(': addToken(XTokenType::LEFT_PAREN); break;
        case ')': addToken(XTokenType::RIGHT_PAREN); break;
        case ',': addToken(XTokenType::COMMA); break;
        case '+':
            if (match('=')) addToken(XTokenType::PLUS_EQUAL);
            else              addToken(XTokenType::PLUS);
            break;

        case '-':
            if (match('=')) addToken(XTokenType::MINUS_EQUAL);
            else              addToken(XTokenType::MINUS);
            break;

        case '*':
            if (match('=')) addToken(XTokenType::STAR_EQUAL);
            else              addToken(XTokenType::STAR);
            break;

        case '/':
            // you already look for '//'‐style comments here…
            if (peek() == '=')
            {
                advance();
                addToken(XTokenType::SLASH_EQUAL);
            }
            else if (peek() == '/' || peek() == '\'')
            {  // comment…
                while (peek()!='\n' && !isAtEnd()) advance();
            }
            else
            {
                addToken(XTokenType::SLASH);
            }
            break;
        case '^': addToken(XTokenType::CARET); break;
        case '=': addToken(XTokenType::EQUAL); break;
        case '<':
            if (match('=')) addToken(XTokenType::LESS_EQUAL);
            else if (match('>')) addToken(XTokenType::NOT_EQUAL);
            else addToken(XTokenType::LESS);
            break;
        case '>':
            if (match('=')) addToken(XTokenType::GREATER_EQUAL);
            else addToken(XTokenType::GREATER);
            break;
        case '.': addToken(XTokenType::DOT); break;
        case '[': addToken(XTokenType::LEFT_BRACKET); break;
        case ']': addToken(XTokenType::RIGHT_BRACKET); break;
        case ':': addToken(XTokenType::COLON); break;
        case '&': {
            if (peek() == 'c' || peek() == 'C') {
                advance();
                std::string hex;
                while (isxdigit(peek())) { hex.push_back(advance()); }
                tokens.push_back({ XTokenType::COLOR, "&c" + hex, line });
            }
            else {
                std::cerr << "Unexpected '&' token at line " << line << std::endl;
                exit(1);
            }
            break;
        }
        case ' ':
        case '\r':
        case '\t': break;
        case '\n': line++; break;
        case '"': string(); break;
        case '\'':
            while (peek() != '\n' && !isAtEnd()) advance();
            break;
        default:
            if (isdigit(c)) { number(); }
            else if (isalpha(c)) { identifier(); }
            break;
        }
    }
    void string() {
        while (peek() != '"' && !isAtEnd()) {
            if (peek() == '\n') line++;
            advance();
        }
        if (isAtEnd()) return;
        advance(); // closing "
        addToken(XTokenType::STRING);
    }
    void number() {
        while (isdigit(peek())) advance();
        if (peek() == '.' && isdigit(peekNext())) {
            advance();
            while (isdigit(peek())) advance();
        }
        addToken(XTokenType::NUMBER);
    }
    void identifier() {
        while (isalnum(peek()) || peek() == '_') advance();
        std::string text = source.substr(start, current - start);
        std::string lowerText = toLower(text);
        XTokenType type = XTokenType::IDENTIFIER;
        if (lowerText == "function") type = XTokenType::FUNCTION;
        else if (lowerText == "sub")      type = XTokenType::SUB;
        else if (lowerText == "end")      type = XTokenType::END;
        else if (lowerText == "return")   type = XTokenType::RETURN;
        else if (lowerText == "class")    type = XTokenType::CLASS;
        else if (lowerText == "new")      type = XTokenType::NEW;
        else if (lowerText == "dim" || lowerText == "var") type = XTokenType::DIM;
        else if (lowerText == "const")    type = XTokenType::XCONST;
        else if (lowerText == "as")       type = XTokenType::AS;
        else if (lowerText == "optional") type = XTokenType::XOPTIONAL;
        else if (lowerText == "public")   type = XTokenType::PUBLIC;
        else if (lowerText == "private")  type = XTokenType::PRIVATE;
        else if (lowerText == "print")    type = XTokenType::PRINT;
        else if (lowerText == "if")       type = XTokenType::IF;
        else if (lowerText == "then")     type = XTokenType::THEN;
        else if (lowerText == "else")     type = XTokenType::ELSE;
        else if (lowerText == "elseif")   type = XTokenType::ELSEIF;
        else if (lowerText == "for")      type = XTokenType::FOR;
        else if (lowerText == "to")       type = XTokenType::TO;
        else if (lowerText == "downto")   type = XTokenType::DOWNTO;
        else if (lowerText == "step")     type = XTokenType::STEP;
        else if (lowerText == "next")     type = XTokenType::NEXT;
        else if (lowerText == "while")    type = XTokenType::WHILE;
        else if (lowerText == "wend")     type = XTokenType::WEND;
        else if (lowerText == "not")      type = XTokenType::NOT;
        else if (lowerText == "and")      type = XTokenType::AND;
        else if (lowerText == "or")       type = XTokenType::OR;
        else if (lowerText == "xor")      type = XTokenType::XOR;
        else if (lowerText == "mod")      type = XTokenType::MOD;
        else if (lowerText == "true")     type = XTokenType::BOOLEAN_TRUE;
        else if (lowerText == "false")    type = XTokenType::BOOLEAN_FALSE;
        else if (lowerText == "module")   type = XTokenType::MODULE;
        else if (lowerText == "extends") type = XTokenType::EXTENDS;
        else if (lowerText == "declare")  type = XTokenType::DECLARE;
        else if (lowerText == "select")   type = XTokenType::SELECT;
        else if (lowerText == "case")     type = XTokenType::CASE;
        else if (lowerText == "goto")     type = XTokenType::GOTO;
        else if (lowerText == "enum")     type = XTokenType::ENUM;
        else if (lowerText == "assigns")  type = XTokenType::ASSIGNS;
        else if (lowerText == "byref")   type = XTokenType::BYREF;
        addToken(type);
    }
};

// ----------------------------------------------------------------------------  
// Helper function to trim trailing whitespace
// ----------------------------------------------------------------------------
std::string rtrim(const std::string& s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) return "";
    return s.substr(0, end + 1);
}

// ============================================================================
// Environment (case–insensitive for variable names)
// Notes:
//  - ByRef parameters are implemented by storing an ObjRef in the callee's environment.
//  - Environment::get() transparently dereferences ObjRef.
//  - Environment::assign() writes through ObjRef.
// ============================================================================
[[noreturn]] void runtimeError(const std::string& msg);

struct Environment {
    std::unordered_map<std::string, Value> values;
    std::shared_ptr<Environment> enclosing;

    Environment(std::shared_ptr<Environment> enclosing = nullptr)
        : enclosing(enclosing) { }

    void define(const std::string& name, const Value& value) {
        values[toLower(name)] = value;
    }

    // Non-fatal lookup used by the compiler and a few runtime helpers.
    bool tryGetRaw(const std::string& name, Value& out) const {
        std::string key = toLower(name);
        auto it = values.find(key);
        if (it != values.end()) {
            out = it->second;
            return true;
        }
        if (enclosing) return enclosing->tryGetRaw(name, out);
        return false;
    }

    // Return a pointer to the ultimate storage cell for a variable name.
    // If the variable is an ObjRef, this resolves and returns the referenced cell.
    Value* getCell(const std::string& name) {
        std::string key = toLower(name);

        auto it = values.find(key);
        if (it != values.end()) {
            if (holds<std::shared_ptr<ObjRef>>(it->second)) {
                auto r = getVal<std::shared_ptr<ObjRef>>(it->second);
                if (!r || !r->target)
                    runtimeError("ByRef: dangling reference for variable: " + name);
                return r->target;
            }
            return &it->second;
        }

        // Support instance fields via implicit "self"
        auto selfIt = values.find("self");
        if (selfIt != values.end()) {
            Value selfVal = selfIt->second;
            if (holds<std::shared_ptr<ObjInstance>>(selfVal)) {
                auto instance = getVal<std::shared_ptr<ObjInstance>>(selfVal);
                auto fit = instance->fields.find(key);
                if (fit != instance->fields.end())
                    return &fit->second;
            }
        }

        if (enclosing) return enclosing->getCell(name);

        std::cerr << "NilObjectException for variable: " << name << std::endl;
        exit(1);
        return nullptr;
    }

    // Standard get: transparently dereference ObjRef.
    Value get(const std::string& name) {
        Value* cell = getCell(name);
        if (!cell) return Value(std::monostate{});
        // If the cell itself happens to hold an ObjRef (nested), resolve once more.
        if (holds<std::shared_ptr<ObjRef>>(*cell)) {
            auto r = getVal<std::shared_ptr<ObjRef>>(*cell);
            if (!r || !r->target)
                runtimeError("ByRef: dangling nested reference for variable: " + name);
            return *(r->target);
        }
        return *cell;
    }

    // Standard assign: write-through ObjRef when applicable.
    void assign(const std::string& name, const Value& value) {
        std::string key = toLower(name);

        auto it = values.find(key);
        if (it != values.end()) {
            if (holds<std::shared_ptr<ObjRef>>(it->second)) {
                auto r = getVal<std::shared_ptr<ObjRef>>(it->second);
                if (!r || !r->target)
                    runtimeError("ByRef: dangling reference assignment for variable: " + name);
                *(r->target) = value;
            } else {
                it->second = value;
            }
            return;
        }

        // Support instance fields via implicit "self"
        auto selfIt = values.find("self");
        if (selfIt != values.end()) {
            Value selfVal = selfIt->second;
            if (holds<std::shared_ptr<ObjInstance>>(selfVal)) {
                auto instance = getVal<std::shared_ptr<ObjInstance>>(selfVal);
                auto fit = instance->fields.find(key);
                if (fit != instance->fields.end()) {
                    if (holds<std::shared_ptr<ObjRef>>(fit->second)) {
                        auto r = getVal<std::shared_ptr<ObjRef>>(fit->second);
                        if (!r || !r->target)
                            runtimeError("ByRef: dangling field reference assignment for variable: " + name);
                        *(r->target) = value;
                    } else {
                        fit->second = value;
                    }
                    return;
                }
            }
        }

        if (enclosing) {
            enclosing->assign(name, value);
            return;
        }

        std::cerr << "NilObjectException for variable: " << name << std::endl;
        exit(1);
    }
};



// ============================================================================  
// Runtime error helper
// Runtime error helper
// ============================================================================
[[noreturn]] void runtimeError(const std::string& msg) {
    std::cerr << "Runtime Error: " << msg << std::endl;
    exit(1);
}

// ============================================================================  
// Preprocessing: Remove line continuations and comments
// ============================================================================
std::string preprocessSource(const std::string& source) {
    std::istringstream iss(source);
    std::string line, result;
    while (std::getline(iss, line)) {
        bool inString = false;
        std::string newline;
        for (size_t i = 0; i < line.size(); i++) {
            char c = line[i];
            // Toggle inString flag on unescaped double quotes.
            if (c == '"' && (i == 0 || line[i - 1] != '\\')) {
                inString = !inString;
            }
            // If not inside a string literal, check for comment markers.
            if (!inString) {
                if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
                    break; // strip from here to end of line
                if (c == '\'')
                    break; // strip from here to end of line
            }
            newline.push_back(c);
        }
        // Process line continuations (underscore at end)
        std::string trimmed = rtrim(newline);
        if (!trimmed.empty() && trimmed.back() == '_') {
            size_t endPos = trimmed.find_last_not_of(" \t_");
            result += trimmed.substr(0, endPos + 1);
        }
        else {
            result += newline + "\n";
        }
    }
    return result;
}

// ============================================================================  
// Bytecode Instructions
// ============================================================================
enum OpCode {
    OP_CONSTANT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_NEGATE,
    OP_POW,
    OP_MOD,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_NE,
    OP_EQ,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_PRINT,
    OP_POP,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_GET_REF,
    OP_SET_GLOBAL,
    OP_NEW,
    OP_CALL,
    OP_OPTIONAL_CALL,
    OP_RETURN,
    OP_NIL,
    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_CLASS,
    OP_METHOD,
    OP_ARRAY,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_PROPERTIES,
    OP_DUP,
    OP_CONSTRUCTOR_END,
    // Unary NOT
    OP_NOT
};

std::string opcodeToString(int opcode) {
    switch (opcode) {
    case OP_CONSTANT:      return "OP_CONSTANT";
    case OP_ADD:           return "OP_ADD";
    case OP_SUB:           return "OP_SUB";
    case OP_MUL:           return "OP_MUL";
    case OP_DIV:           return "OP_DIV";
    case OP_NEGATE:        return "OP_NEGATE";
    case OP_POW:           return "OP_POW";
    case OP_MOD:           return "OP_MOD";
    case OP_LT:            return "OP_LT";
    case OP_LE:            return "OP_LE";
    case OP_GT:            return "OP_GT";
    case OP_GE:            return "OP_GE";
    case OP_NE:            return "OP_NE";
    case OP_EQ:            return "OP_EQ";
    case OP_AND:           return "OP_AND";
    case OP_OR:            return "OP_OR";
    case OP_XOR:           return "OP_XOR";
    case OP_PRINT:         return "OP_PRINT";
    case OP_POP:           return "OP_POP";
    case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
    case OP_GET_GLOBAL:    return "OP_GET_GLOBAL";
    case OP_GET_REF:       return "OP_GET_REF";
    case OP_SET_GLOBAL:    return "OP_SET_GLOBAL";
    case OP_NEW:           return "OP_NEW";
    case OP_CALL:          return "OP_CALL";
    case OP_OPTIONAL_CALL: return "OP_OPTIONAL_CALL";
    case OP_RETURN:        return "OP_RETURN";
    case OP_NIL:           return "OP_NIL";
    case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
    case OP_JUMP:          return "OP_JUMP";
    case OP_CLASS:         return "OP_CLASS";
    case OP_METHOD:        return "OP_METHOD";
    case OP_ARRAY:         return "OP_ARRAY";
    case OP_GET_PROPERTY:  return "OP_GET_PROPERTY";
    case OP_SET_PROPERTY:  return "OP_SET_PROPERTY";
    case OP_PROPERTIES:    return "OP_PROPERTIES";
    case OP_DUP:           return "OP_DUP";
    case OP_CONSTRUCTOR_END: return "OP_CONSTRUCTOR_END";
    case OP_NOT:           return "OP_NOT";
    default:               return "UNKNOWN";
    }
}

// ============================================================================  
// Virtual Machine
// ============================================================================
struct VM {
    std::vector<Value> stack;
    std::shared_ptr<Environment> globals;
    std::shared_ptr<Environment> environment;
    ObjFunction::CodeChunk mainChunk;
    // Module Extends - map[typeName][methodName] → BuiltinFn
    std::unordered_map<std::string,
        std::unordered_map<std::string, Value>> extensionMethods;
};

// ----------------------------------------------------------------------------  
// Helper: pop from VM stack (with logging)
// ----------------------------------------------------------------------------
Value pop(VM& vm) {
    if (vm.stack.empty()) {
        debugLog("OP_POP: Attempted to pop from an empty stack.");
        runtimeError("VM: Stack underflow on POP.");
    }
    Value v = vm.stack.back();
    vm.stack.pop_back();
    return v;
}


Value runVM(VM& vm, const ObjFunction::CodeChunk& chunk);



// ============================================================================
// Extension-method helpers
// ============================================================================

static inline std::string canonicalExtTypeName(const std::string& tRaw)
{
    std::string t = toLower(tRaw);

    // Common aliases -> canonical keys used by extensionMethods
    if (t == "int")      return "integer";
    if (t == "integer")  return "integer";

    if (t == "number")   return "double";
    if (t == "double")   return "double";

    if (t == "bool")     return "boolean";
    if (t == "boolean")  return "boolean";

    if (t == "str")      return "string";
    if (t == "string")   return "string";

    if (t == "ptr")      return "pointer";
    if (t == "pointer")  return "pointer";

    if (t == "color")    return "color";
    if (t == "array")    return "array";

    // If someone uses Extends on a class name, keep it as-is (lowercased)
    return t;
}

static inline std::string extKeyFromValue(const Value& v)
{
    // Primitive/value types
    if (holds<int>(v))                        return "integer";
    if (holds<double>(v))                     return "double";
    if (holds<bool>(v))                       return "boolean";
    if (holds<std::string>(v))                return "string";
    if (holds<Color>(v))                      return "color";
    if (holds<void*>(v))                      return "pointer";
    if (holds<std::shared_ptr<ObjArray>>(v))  return "array";

    // Allow Extends on classes too (by class name)
    if (holds<std::shared_ptr<ObjInstance>>(v)) {
        auto inst = getVal<std::shared_ptr<ObjInstance>>(v);
        return canonicalExtTypeName(inst->klass ? inst->klass->name : "");
    }

    return "";
}

// Returns a callable that is already "bound" to receiver (like a bound method).
// If no extension method exists, returns nil (monostate).
// static inline Value bindExtensionMethod(VM& vm,
//                                         const Value& receiver,
//                                         const std::string& methodLower)
// {
//     const std::string key = canonicalExtTypeName(extKeyFromValue(receiver));
//     if (key.empty()) return Value(std::monostate{});

//     auto itType = vm.extensionMethods.find(key);
//     if (itType == vm.extensionMethods.end()) return Value(std::monostate{});

//     auto itMeth = itType->second.find(methodLower);
//     if (itMeth == itType->second.end()) return Value(std::monostate{});

//     Value target = itMeth->second;

//     // In your compiler you store a BuiltinFn wrapper for extensions, so bind it here.
//     if (holds<BuiltinFn>(target)) {
//         BuiltinFn fn = getVal<BuiltinFn>(target);

//         BuiltinFn bound = [fn, receiver](const std::vector<Value>& args) -> Value {
//             std::vector<Value> full;
//             full.reserve(args.size() + 1);
//             full.push_back(receiver);                 // receiver becomes args[0]
//             full.insert(full.end(), args.begin(), args.end());
//             return fn(full);
//         };

//         return Value(bound);
//     }

//     // If you ever store raw ObjFunction for extensions, you can handle it here.
//     if (holds<std::shared_ptr<ObjFunction>>(target)) {
//         auto fnObj = getVal<std::shared_ptr<ObjFunction>>(target);
//         BuiltinFn bound = [fnObj, receiver](const std::vector<Value>& args) -> Value {
//             if (!globalVM) runtimeError("No active VM for extension call.");

//             auto previousEnv = globalVM->environment;
//             auto localEnv    = std::make_shared<Environment>(globalVM->globals);
//             globalVM->environment = localEnv;

//             // Make receiver available as `self` (or bind however you like)
//             localEnv->define("self", receiver);

//             // Bind parameters by position: receiver + args
//             // (if you want tighter binding to fnObj->params, you can extend this)
//             Value result = runVM(*globalVM, fnObj->chunk);
//             globalVM->environment = previousEnv;
//             return result;
//         };
//         return Value(bound);
//     }

//     return Value(std::monostate{});
// }

// Helper: bind extension methods for any receiver type.
// Looks up vm.extensionMethods[bucket][methodLower] where bucket is chosen
// from runtime type name and per-type synonyms (integer, double, boolean,
// string, array, plugin/instance, and a generic "object" bucket).
//
// Returns:
//   - Value(BuiltinFn)  : a callable with the receiver pre-pended to args
//   - Value(ObjBoundMethod) : for scripted extension functions
//   - Value(std::monostate{}): if not found
//
Value bindExtensionMethod(VM& vm, const Value& receiver, const std::string& methodLower)
{
    std::vector<std::string> buckets;

    // Primary type name from your runtime helper (e.g. "Integer", "String", "Color")
    std::string typeName = toLower(getTypeName(receiver));
    if (!typeName.empty()) {
        buckets.push_back(typeName); // e.g. "integer", "string", "color"
    }

    // ---- Per-variant synonyms ------------------------------------------------
    if (holds<int>(receiver)) {
        // numeric aliases
        buckets.push_back("integer");
        buckets.push_back("int");
        buckets.push_back("i32");
        buckets.push_back("i64");
        buckets.push_back("number");

        // If this int is actually used as a Color (typeName == "color")
        if (typeName == "color" || typeName == "colour") {
            buckets.push_back("color");
        }
    }
    else if (holds<double>(receiver)) {
        buckets.push_back("double");
        buckets.push_back("float");
        buckets.push_back("f32");
        buckets.push_back("f64");
        buckets.push_back("number");
        buckets.push_back("numeric");
    }
    else if (holds<bool>(receiver)) {
        buckets.push_back("boolean");
        buckets.push_back("bool");
    }
    else if (holds<std::string>(receiver)) {
        buckets.push_back("string");
        buckets.push_back("str");
        buckets.push_back("text");
    }
    else if (holds<std::shared_ptr<ObjArray>>(receiver)) {
        buckets.push_back("array");
        buckets.push_back("list");
    }
    else if (holds<std::shared_ptr<ObjInstance>>(receiver)) {
        auto inst = getVal<std::shared_ptr<ObjInstance>>(receiver);
        if (inst && inst->klass) {
            // Class name like "Demo", "Color", "Socket", etc.
            std::string clsNameLower = toLower(inst->klass->name);
            if (!clsNameLower.empty())
                buckets.push_back(clsNameLower);  // e.g. "demo", "color"

            if (inst->klass->isPlugin) {
                buckets.push_back("plugin");
            }
        }
    }

    // Always allow a generic "object" bucket last.
    buckets.push_back("object");

    // ---- Deduplicate buckets and search vm.extensionMethods ------------------
    std::unordered_set<std::string> seen;
    for (const std::string& bucket : buckets) {
        if (bucket.empty()) continue;
        if (!seen.insert(bucket).second) continue; // already tried this bucket

        auto bIt = vm.extensionMethods.find(bucket);
        if (bIt == vm.extensionMethods.end())
            continue;

        auto& methods = bIt->second;
        auto mIt = methods.find(methodLower);
        if (mIt == methods.end())
            continue;

        Value fnVal = mIt->second;

        // Case 1: extension is a builtin function
        if (holds<BuiltinFn>(fnVal)) {
            BuiltinFn fn = getVal<BuiltinFn>(fnVal);
            // Pre-bind 'receiver' as first argument
            BuiltinFn bound = [fn, receiver](const std::vector<Value>& args) -> Value {
                std::vector<Value> full;
                full.reserve(args.size() + 1);
                full.push_back(receiver);          // Extends receiver
                full.insert(full.end(), args.begin(), args.end());
                return fn(full);
            };
            return Value(bound);
        }

        // Case 2: extension is a scripted function (ObjFunction)
        if (holds<std::shared_ptr<ObjFunction>>(fnVal)) {
            auto bm = std::make_shared<ObjBoundMethod>();
            bm->receiver = receiver;
            bm->name     = methodLower;
            return Value(bm);
        }

        // Unexpected type stored in extensionMethods; ignore and continue
    }

    // Not found
    return Value(std::monostate{});
}


static bool tryGetEnumMember(const Value& receiver, const std::string& propLower, Value& out)
{
    if (!holds<std::shared_ptr<ObjEnum>>(receiver)) return false;

    auto e = getVal<std::shared_ptr<ObjEnum>>(receiver);
    if (!e) return false;

    // Optional: expose enum name
    if (propLower == "name") {
        out = Value(e->name);
        return true;
    }

    auto it = e->members.find(propLower);
    if (it == e->members.end())
        return false;

    out = Value((int)it->second);
    return true;
}




// ----------------------------------------------------------------------------  
// Helper: Return a string naming the underlying type of a Value.
// ----------------------------------------------------------------------------
std::string getTypeName(const Value& v) {
    struct TypeVisitor {
        std::string operator()(std::monostate) const { return "nil"; }
        std::string operator()(int) const { return "int"; }
        std::string operator()(double) const { return "double"; }
        std::string operator()(bool) const { return "bool"; }
        std::string operator()(const std::string&) const { return "string"; }
        std::string operator()(const Color&) const { return "Color"; }
        std::string operator()(const std::shared_ptr<ObjFunction>&) const { return "ObjFunction"; }
        std::string operator()(const std::shared_ptr<ObjClass>&) const { return "ObjClass"; }
        std::string operator()(const std::shared_ptr<ObjInstance>&) const { return "ObjInstance"; }
        std::string operator()(const std::shared_ptr<ObjArray>&) const { return "ObjArray"; }
        std::string operator()(const std::shared_ptr<ObjBoundMethod>&) const { return "ObjBoundMethod"; }
        std::string operator()(const BuiltinFn&) const { return "BuiltinFn"; }
        std::string operator()(const PropertiesType&) const { return "PropertiesType"; }
        std::string operator()(const std::vector<std::shared_ptr<ObjFunction>>&) const { return "OverloadedFunctions"; }
        std::string operator()(const std::shared_ptr<ObjModule>&) const { return "ObjModule"; }
        std::string operator()(const std::shared_ptr<ObjEnum>&) const { return "ObjEnum"; }
        std::string operator()(const std::shared_ptr<ObjRef>&) const { return "ObjRef"; }
        std::string operator()(void* ptr) const { return "pointer"; }
    } visitor;
    return std::visit(visitor, v);
}




// ---------------------------------------------------------------------------
//  wrapHandleIfPluginClass -- turn a bare handle (string/int) into a plugin
//  instance if  paramDecl.type  names a loaded plugin class.
// ---------------------------------------------------------------------------
static Value wrapHandleIfPluginClass(const std::string &raw,
                                     const std::string &typeName,
                                     VM &vm)
{
    if (typeName.empty())
        return Value(raw); // no declared type
    if (!std::all_of(raw.begin(), raw.end(), ::isdigit))
        return Value(raw); // not a number

    // Does the declared type correspond to a loaded plugin class?
    std::string key = toLower(typeName);
    Value clsVal;
    try
    {
        clsVal = vm.environment->get(key);
    }
    catch (...)
    {
        return Value(raw);
    }

    if (!holds<std::shared_ptr<ObjClass>>(clsVal))
        return Value(raw);
    auto cls = getVal<std::shared_ptr<ObjClass>>(clsVal);
    if (!cls->isPlugin)
        return Value(raw);

    // Build the ObjInstance that represents this handle
    auto inst = std::make_shared<ObjInstance>();
    inst->klass = cls;
    inst->pluginInstance = reinterpret_cast<void *>((intptr_t)std::stol(raw));
    for (auto &p : cls->properties)
        inst->fields[p.first] = p.second;
    return Value(inst);
}

// ----------------------------------------------------------------------------
// Handler for Plugin Script Callbacks (aka Event Handlers)
// ----------------------------------------------------------------------------
static inline std::string _cbTrimWS(const std::string& s)
{
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

static std::vector<std::string> _cbSplitArgs(const std::string& p, size_t expectedCount)
{
    std::vector<std::string> out;

    // If the callback target expects 0-1 parameter(s), preserve legacy behavior:
    // pass the raw string through unmodified (even if it contains commas).
    if (expectedCount <= 1) {
        out.push_back(p);
        return out;
    }

    // Only split when commas are present (our plugin event convention).
    if (p.find(',') == std::string::npos) {
        out.push_back(p);
        return out;
    }

    size_t start = 0;
    while (start <= p.size()) {
        size_t comma = p.find(',', start);
        if (comma == std::string::npos) comma = p.size();
        out.push_back(_cbTrimWS(p.substr(start, comma - start)));
        start = comma + 1;
        if (comma == p.size()) break;
    }
    return out;
}

static bool _cbTryParseInt(const std::string& s, int& out)
{
    std::string t = _cbTrimWS(s);
    if (t.empty()) return false;

    char* end = nullptr;
    errno = 0;
    long v = std::strtol(t.c_str(), &end, 10);
    if (errno != 0 || end == t.c_str() || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

static bool _cbTryParseDouble(const std::string& s, double& out)
{
    std::string t = _cbTrimWS(s);
    if (t.empty()) return false;

    char* end = nullptr;
    errno = 0;
    double v = std::strtod(t.c_str(), &end);
    if (errno != 0 || end == t.c_str() || *end != '\0') return false;
    out = v;
    return true;
}

static bool _cbTryParseBool(const std::string& s, bool& out)
{
    std::string t = toLower(_cbTrimWS(s));
    if (t == "true" || t == "1")  { out = true;  return true; }
    if (t == "false" || t == "0") { out = false; return true; }
    return false;
}

static Value _cbCoerceToken(const std::string& raw, const Param& pd, VM& vm)
{
    std::string token = _cbTrimWS(raw);
    std::string t = toLower(pd.type);

    // No type specified → keep as string (legacy behavior)
    if (pd.type.empty() || t == "variant")
        return Value(token);

    // Primitive coercions for common event patterns
    if (t == "string" || t == "text")
        return Value(token);

    if (t == "integer" || t == "int") {
        int iv;
        if (_cbTryParseInt(token, iv)) return Value(iv);
        return Value(token);
    }

    if (t == "double" || t == "number" || t == "float") {
        double dv;
        if (_cbTryParseDouble(token, dv)) return Value(dv);
        int iv;
        if (_cbTryParseInt(token, iv)) return Value(static_cast<double>(iv));
        return Value(token);
    }

    if (t == "boolean" || t == "bool") {
        bool bv;
        if (_cbTryParseBool(token, bv)) return Value(bv);
        return Value(token);
    }

    // Otherwise, attempt to wrap numeric handles into plugin class instances.
    // If it doesn't match a plugin class, wrapHandleIfPluginClass will fall back.
    return wrapHandleIfPluginClass(token, pd.type, vm);
}

void invokeScriptCallback(const Value& funcVal, const char* param)
{
    // 1) Prevent concurrent VM mutations
    std::lock_guard<std::recursive_mutex> lock(vmMutex);

    // 2) Remember where our stack was so we can pop back to it
    size_t oldDepth = globalVM->stack.size();

    // 3) Log entry
    std::string p = param ? param : "";
    debugLog("invokeScriptCallback: Called with param: " + (p.empty() ? "null" : p));

    // 4) Dispatch either a host function or a script function
    if (holds<BuiltinFn>(funcVal)) {
        debugLog("invokeScriptCallback: Detected BuiltinFn.");
        BuiltinFn hostFn = getVal<BuiltinFn>(funcVal);

        // Preserve legacy behavior for BuiltinFn callbacks: a single string param.
        hostFn({ Value(p) });
        debugLog("invokeScriptCallback: BuiltinFn executed.");
    }
    else if (holds<std::shared_ptr<ObjFunction>>(funcVal)) {
        debugLog("invokeScriptCallback: Detected ObjFunction.");
        auto fnObj = getVal<std::shared_ptr<ObjFunction>>(funcVal);

        // 5) Swap in a fresh environment (child of globals)
        auto previousEnv = globalVM->environment;
        globalVM->environment = std::make_shared<Environment>(globalVM->globals);

        // 6) If the handler expects multiple args, split "x,y" into tokens.
        //    If it expects one arg, pass the raw string through unmodified.
        std::vector<std::string> rawArgs = _cbSplitArgs(p, fnObj->params.size());

        // 7) Define parameters (or default values)
        for (size_t i = 0; i < fnObj->params.size(); ++i) {
            const auto& pd = fnObj->params[i];

            Value actual;
            if (i < rawArgs.size()) {
                // Coerce token according to declared parameter type.
                actual = _cbCoerceToken(rawArgs[i], pd, *globalVM);
            } else {
                actual = pd.defaultValue;
            }

            globalVM->environment->define(pd.name, actual);
        }

        // 8) Execute the function body
        Value result = runVM(*globalVM, fnObj->chunk);
        debugLog("invokeScriptCallback: Function executed with result: " + valueToString(result));

        // 9) Restore the old environment
        globalVM->environment = previousEnv;
    }
    else {
        runtimeError("invokeScriptCallback: Not a callable function.");
    }

    // 10) Pop any values the callback may have left on the stack
    globalVM->stack.resize(oldDepth);
}


// // ============================================================================  
// // Script callback trampoline for AddressOf built-in.
// // This function is called by the ffi closure.
void scriptCallbackTrampoline(ffi_cif* cif, void* ret, void** args, void* user_data) {
    // Log entry and key pointer values.
    debugLog("scriptCallbackTrampoline: Entered.");
    debugLog("  cif pointer: " + std::to_string(reinterpret_cast<uintptr_t>(cif)));
    debugLog("  ret pointer: " + std::to_string(reinterpret_cast<uintptr_t>(ret)));
    debugLog("  user_data pointer: " + std::to_string(reinterpret_cast<uintptr_t>(user_data)));
    
    // If available, log the number of arguments from the CIF.
    int nargs = 1;
    #ifdef FFI_CIF_NARGS
      nargs = cif->nargs;
    #endif
    debugLog("  Number of arguments (nargs): " + std::to_string(nargs));

    if (args == nullptr) {
         debugLog("scriptCallbackTrampoline: args is null!");
         return;
    }
    debugLog("  args pointer: " + std::to_string(reinterpret_cast<uintptr_t>(args)));
    
    // Log each argument's pointer value.
    for (int i = 0; i < nargs; i++) {
         debugLog("  args[" + std::to_string(i) + "] pointer: " + std::to_string(reinterpret_cast<uintptr_t>(args[i])));
    }
    
    // Since we now expect one parameter (a const char*), try to extract it.
    const char* param = *(const char**)args[0];
    debugLog("scriptCallbackTrampoline: Parameter: " + std::string(param ? param : "null"));
    
    Value* funcVal = (Value*)user_data;
    debugLog("scriptCallbackTrampoline: user_data as funcVal pointer: " +
             std::to_string(reinterpret_cast<uintptr_t>(funcVal)));
    
    // Now, if on the main thread, invoke directly; else, queue the callback.
    // Queued callbacks immediately handled with processpendingCallbacks() for proper concurrent threading and access.
    if (std::this_thread::get_id() == mainThreadId) {
         debugLog("scriptCallbackTrampoline: On main thread, invoking callback directly.");
         invokeScriptCallback(*funcVal, param);
    } else {
         debugLog("scriptCallbackTrampoline: Not on main thread, queueing callback.");
         std::lock_guard<std::mutex> lock(callbackQueueMutex);
         callbackQueue.push(CallbackRequest{ *funcVal, param ? std::string(param) : std::string("") });
    }
}

// ------------------------------------------------------------------------------------
//  AddressOfBuiltin   – returns a C‑callable pointer that invokes a scripted function.
// ------------------------------------------------------------------------------------
static std::unordered_set<ffi_closure*> liveClosures;   // <‑‑ keeps them alive

BuiltinFn addressOfBuiltin = [](const std::vector<Value>& args) -> Value
{
    debugLog("AddressOf: received " + std::to_string(args.size()) + " arg(s)");
    if (args.size() != 1)
        runtimeError("AddressOf expects exactly one argument");

    // The argument must be either a scripted ObjFunction or another BuiltinFn
    if (!holds<std::shared_ptr<ObjFunction>>(args[0]) && !holds<BuiltinFn>(args[0]))
        runtimeError("AddressOf expects a function reference (omit the parentheses)");

    //---------------------------------------------------------------------------
    // 1.  Build the libffi call interface  ‑‑  void callback(const char *param)
    //---------------------------------------------------------------------------
    static ffi_type* argTypes[1] = { &ffi_type_pointer };
    ffi_cif* cif = new ffi_cif;
    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI,
                     /*nargs=*/1, &ffi_type_void, argTypes) != FFI_OK)
        runtimeError("AddressOf: ffi_prep_cif failed");

    //---------------------------------------------------------------------------
    // 2.  Allocate the closure and wire it to our trampoline
    //---------------------------------------------------------------------------
    void* entryPoint = nullptr;
    ffi_closure* closure = (ffi_closure*)ffi_closure_alloc(sizeof(ffi_closure),
                                                           &entryPoint);
    if (!closure)
        runtimeError("AddressOf: ffi_closure_alloc failed");

    // Store the script function so the trampoline can call back into the VM
    Value* funcHolder = new Value(args[0]);

    if (ffi_prep_closure_loc(closure,        // the closure object
                             cif,            // call interface
                             scriptCallbackTrampoline,
                             /*user_data=*/funcHolder,
                             entryPoint) != FFI_OK)
    {
        ffi_closure_free(closure);
        runtimeError("AddressOf: ffi_prep_closure_loc failed");
    }

    // Keep the closure alive until its Deref'd or closed.
    liveClosures.insert(closure);

    debugLog("AddressOf: returning callback pointer " +
             std::to_string(reinterpret_cast<uintptr_t>(entryPoint)));
    return Value(entryPoint);   // expose the raw code pointer to the script
};



// -----------------------------------------------------------------------------
//  AddHandlerBuiltin   – (instance, eventKey, callbackPtr)  → Boolean
// -----------------------------------------------------------------------------
BuiltinFn addHandlerBuiltin = [](const std::vector<Value>& args) -> Value
{
    debugLog("AddHandler: received " + std::to_string(args.size()) + " arg(s)");
    if (args.size() != 2)
        runtimeError("AddHandler expects exactly two arguments");

    // ------------------------------------------------------------------------
    // Argument 0  – “plugin:HANDLE:EventName”  (string produced by inst.Event)
    // ------------------------------------------------------------------------
    if (!holds<std::string>(args[0]))
        runtimeError("AddHandler: first argument must be the event identifier string");

    const std::string target = getVal<std::string>(args[0]);
    debugLog("AddHandler: target = " + target);

    const size_t p1 = target.find(':');
    const size_t p2 = target.find(':', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos)
        runtimeError("AddHandler: bad event identifier format");

    const std::string pluginName = toLower(target.substr(0, p1));            // Ex. "myinstance"
    const int         handle     = std::stoi(target.substr(p1 + 1, p2 - p1 - 1));
    const std::string eventName  = target.substr(p2 + 1);                    // Ex. "OnTrigger"

    // ------------------------------------------------------------------------
    // Argument 1  – callback code pointer (void*)
    // ------------------------------------------------------------------------
    if (!holds<void*>(args[1]))
        runtimeError("AddHandler: second argument must be a pointer returned by AddressOf");
    void* callbackPtr = getVal<void*>(args[1]);

    debugLog("AddHandler: plugin=" + pluginName +
             "  handle=" + std::to_string(handle) +
             "  event="  + eventName +
             "  cbPtr="  + std::to_string(reinterpret_cast<uintptr_t>(callbackPtr)));

    // ------------------------------------------------------------------------
    // Locate the plugin’s SetEventCallback routine that the loader registered
    // as   "<pluginName>_seteventcallback"
    // ** Defined in external plugin.
    // ------------------------------------------------------------------------
    const std::string setterKey = pluginName + "_seteventcallback";
    Value setterVal = globalVM->globals->get(setterKey);
    if (!holds<BuiltinFn>(setterVal))
        runtimeError("AddHandler: could not find " + setterKey);

    BuiltinFn setEventCallback = getVal<BuiltinFn>(setterVal);

    // Call into the plugin:  Boolean SetEventCallback(Integer handle, String  eventName, Ptr callback)
    Value ok = setEventCallback({ Value(handle),
                                  Value(eventName),
                                  Value(callbackPtr) });

    debugLog("AddHandler: plugin returned " + valueToString(ok));
    return ok;
};

// ============================================================================  
// AST Definitions: Expressions
// ============================================================================
enum class BinaryOp { ADD, SUB, MUL, DIV, LT, LE, GT, GE, NE, EQ, AND, OR, XOR, POW, MOD };

struct Expr { virtual ~Expr() = default; };

struct LiteralExpr : Expr { 
    Value value; 
    LiteralExpr(const Value& value) : value(value) { }
};

struct VariableExpr : Expr {
    std::string name;
    VariableExpr(const std::string& name) : name(name) { }
};

struct UnaryExpr : Expr {
    std::string op;
    std::shared_ptr<Expr> right;
    UnaryExpr(const std::string& op, std::shared_ptr<Expr> right)
        : op(op), right(right) { }
};

struct AssignmentExpr : Expr {
    std::string name;
    std::shared_ptr<Expr> value;
    AssignmentExpr(const std::string& name, std::shared_ptr<Expr> value)
        : name(name), value(value) { }
};

struct BinaryExpr : Expr {
    std::shared_ptr<Expr> left;
    BinaryOp op;
    std::shared_ptr<Expr> right;
    BinaryExpr(std::shared_ptr<Expr> left, BinaryOp op, std::shared_ptr<Expr> right)
        : left(left), op(op), right(right) { }
};

struct GroupingExpr : Expr {
    std::shared_ptr<Expr> expression;
    GroupingExpr(std::shared_ptr<Expr> expression)
        : expression(expression) { }
};

struct CallExpr : Expr {
    std::shared_ptr<Expr> callee;
    std::vector<std::shared_ptr<Expr>> arguments;
    CallExpr(std::shared_ptr<Expr> callee, const std::vector<std::shared_ptr<Expr>>& arguments)
        : callee(callee), arguments(arguments) { }
};

struct ArrayLiteralExpr : Expr {
    std::vector<std::shared_ptr<Expr>> elements;
    ArrayLiteralExpr(const std::vector<std::shared_ptr<Expr>>& elements)
        : elements(elements) { }
};

struct GetPropExpr : Expr {
    std::shared_ptr<Expr> object;
    std::string name;
    GetPropExpr(std::shared_ptr<Expr> object, const std::string& name)
        : object(object), name(toLower(name)) { }
};

struct SetPropExpr : Expr {
    std::shared_ptr<Expr> object;
    std::string name;
    std::shared_ptr<Expr> value;
    SetPropExpr(std::shared_ptr<Expr> object, const std::string& name, std::shared_ptr<Expr> value)
        : object(object), name(toLower(name)), value(value) { }
};

struct NewExpr : Expr {
    std::string className;
    std::vector<std::shared_ptr<Expr>> arguments;
    NewExpr(const std::string& className, const std::vector<std::shared_ptr<Expr>>& arguments)
        : className(toLower(className)), arguments(arguments) { }
};

// ============================================================================  
// AST Definitions: Statements
// ============================================================================
enum class StmtType { EXPRESSION, FUNCTION, RETURN, CLASS, VAR, IF, WHILE, BLOCK, FOR, MODULE, DECLARE };

struct Stmt { virtual ~Stmt() = default; };

struct ExpressionStmt : Stmt {
    std::shared_ptr<Expr> expression;
    ExpressionStmt(std::shared_ptr<Expr> expression) : expression(expression) { }
};

struct ReturnStmt : Stmt {
    std::shared_ptr<Expr> value;
    ReturnStmt(std::shared_ptr<Expr> value) : value(value) { }
};

struct FunctionStmt : Stmt {
    std::string name;
    std::vector<Param> params;
    std::vector<std::shared_ptr<Stmt>> body;
    AccessModifier access;

    // ← new fields:
    bool        isExtension      = false;
    std::string extendedParam;    // e.g. "Container"
    std::string extendedType;     // e.g. "string"

    FunctionStmt(const std::string& name,
                const std::vector<Param>& params,
                const std::vector<std::shared_ptr<Stmt>>& body,
                AccessModifier access = AccessModifier::PUBLIC,
                bool isExt = false,
                std::string extParam = "",
                std::string extType  = "")
    : name(name), 
        params(params), 
        body(body), 
        access(access),
        isExtension(isExt),
        extendedParam(extParam),
        extendedType(extType)
    { }
};

struct VarStmt : Stmt {
    std::string name; 
    std::shared_ptr<Expr> initializer;
    std::string varType;
    bool isConstant; // for Const declarations
    AccessModifier access;
    VarStmt(const std::string& name, std::shared_ptr<Expr> initializer, const std::string& varType = "",
        bool isConstant = false, AccessModifier access = AccessModifier::PUBLIC) 
        : name(name), initializer(initializer), varType(toLower(varType)), isConstant(isConstant), access(access) { }
};

struct PropertyAssignmentStmt : Stmt {
    std::shared_ptr<Expr> object;
    std::string property;
    std::shared_ptr<Expr> value;
    PropertyAssignmentStmt(std::shared_ptr<Expr> object, const std::string& property, std::shared_ptr<Expr> value)
        : object(object), property(property), value(value) { }
};

struct ClassStmt : Stmt {
    std::string name;
    std::vector<std::shared_ptr<FunctionStmt>> methods;
    PropertiesType properties;
    ClassStmt(const std::string& name,
        const std::vector<std::shared_ptr<FunctionStmt>>& methods,
        const PropertiesType& properties)
        : name(name), methods(methods), properties(properties) { }
};

struct IfStmt : Stmt {
    std::shared_ptr<Expr> condition;
    std::vector<std::shared_ptr<Stmt>> thenBranch;
    std::vector<std::shared_ptr<Stmt>> elseBranch;
    IfStmt(std::shared_ptr<Expr> condition,
        const std::vector<std::shared_ptr<Stmt>>& thenBranch,
        const std::vector<std::shared_ptr<Stmt>>& elseBranch)
        : condition(condition), thenBranch(thenBranch), elseBranch(elseBranch) { }
};

struct WhileStmt : Stmt {
    std::shared_ptr<Expr> condition;
    std::vector<std::shared_ptr<Stmt>> body;
    WhileStmt(std::shared_ptr<Expr> condition, const std::vector<std::shared_ptr<Stmt>>& body)
        : condition(condition), body(body) { } 
};

struct AssignmentStmt : Stmt {
    std::string name;
    std::shared_ptr<Expr> value;
    AssignmentStmt(const std::string& name, std::shared_ptr<Expr> value)
        : name(name), value(value) { }
};

struct BlockStmt : Stmt {
    std::vector<std::shared_ptr<Stmt>> statements;
    BlockStmt(const std::vector<std::shared_ptr<Stmt>>& statements)
        : statements(statements) { }
};

struct ForStmt : Stmt {
    std::string varName;
    std::shared_ptr<Expr> start;
    std::shared_ptr<Expr> end;
    std::shared_ptr<Expr> step;
    std::vector<std::shared_ptr<Stmt>> body;
    ForStmt(const std::string& varName,
        std::shared_ptr<Expr> start,
        std::shared_ptr<Expr> end,
        std::shared_ptr<Expr> step,
        const std::vector<std::shared_ptr<Stmt>>& body)
        : varName(varName), start(start), end(end), step(step), body(body) { }
};

// Module AST node
struct ModuleStmt : Stmt {
     std::string name;
     std::vector<std::shared_ptr<Stmt>> body;
     ModuleStmt(const std::string& name, const std::vector<std::shared_ptr<Stmt>>& body)
        : name(name), body(body) { }
};

// Declare API statement AST node
struct DeclareStmt : Stmt {
    bool isFunction; // true if Function, false if Sub
    std::string apiName;
    std::string libraryName;
    std::string aliasName;    // optional; empty if not provided
    std::string selector;     // optional; empty if not provided
    std::vector<Param> params;
    std::string returnType;   // if function; empty for Sub
    DeclareStmt(bool isFunc, const std::string& name, const std::string& lib,
        const std::string& alias, const std::string& sel,
        const std::vector<Param>& params, const std::string& retType)
        : isFunction(isFunc), apiName(name), libraryName(lib), aliasName(alias), selector(sel), params(params), returnType(retType) { }
};

// Label and Goto AST node
// AFTER (add just below existing Stmt structs)
struct LabelStmt : Stmt {
    std::string name;
    explicit LabelStmt(const std::string& n) : name(toLower(n)) {}
};

struct GotoStmt : Stmt {
    std::string label;
    explicit GotoStmt(const std::string& l) : label(toLower(l)) {}
};


// Enum AST node
struct EnumStmt : Stmt {
    std::string name;
    std::unordered_map<std::string, int> members;
    EnumStmt(const std::string& name, const std::unordered_map<std::string, int>& members)
    : name(name), members(members) { }
};

// ---------------------------------------------------------------------------  
// Forward declaration for plugin function wrapper for Declare statements.
BuiltinFn wrapPluginFunctionForDeclare(const std::vector<Param>& params, const std::string& retType, const std::string& apiName, const std::string& libName);

// ============================================================================  
// Parser
// ============================================================================
class Parser {
public:
    Parser(const std::vector<Token>& tokens) : tokens(tokens), inModule(false) {}
    std::vector<std::shared_ptr<Stmt>> parse() {
       debugLog("Parser: Starting parse. Total tokens: " + std::to_string(tokens.size()));
        std::vector<std::shared_ptr<Stmt>> statements;
        while (!isAtEnd()) {
            statements.push_back(declaration());
        }
        debugLog("Parser: Finished parse.");
        return statements;
    }
    
private:
    std::vector<Token> tokens;
    int current = 0;
    bool inModule; // Flag to indicate module context

    bool isAtEnd() { return peek().type == XTokenType::EOF_TOKEN; }
    Token peek() { return tokens[current]; }
    Token previous() { return tokens[current - 1]; }
    Token advance() { if (!isAtEnd()) current++; return previous(); }
    bool check(XTokenType type) { return !isAtEnd() && peek().type == type; }
    bool match(const std::vector<XTokenType>& types) {
        for (auto type : types)
            if (check(type)) { advance(); return true; }
        return false;
    }

    Token consume(XTokenType type, const std::string& msg) {
        if (check(type)) return advance();
        std::cerr << "Parse error at line " << peek().line << ": " << msg << std::endl;
        exit(1);
    }

    std::vector<std::shared_ptr<Stmt>> block(const std::vector<XTokenType>& terminators) {
        std::vector<std::shared_ptr<Stmt>> statements;
        while (!isAtEnd() && std::find(terminators.begin(), terminators.end(), peek().type) == terminators.end()) {
            statements.push_back(declaration());
        }
        return statements;
    }

    bool isPotentialCallArgument(const Token& token) {
        if (token.type == XTokenType::RIGHT_PAREN || token.type == XTokenType::EOF_TOKEN)
            return false;
        switch (token.type) {
        case XTokenType::NUMBER:
        case XTokenType::STRING:
        case XTokenType::COLOR:
        case XTokenType::BOOLEAN_TRUE:
        case XTokenType::BOOLEAN_FALSE:
        case XTokenType::IDENTIFIER:
        case XTokenType::LEFT_PAREN:
            return true;
        default:
            return false;
        }
    }

    // Goto Label statement - TODO Fix to find even if before.
    std::shared_ptr<Stmt> gotoStatement() {
        Token lbl = consume(XTokenType::IDENTIFIER, "Expect label name after Goto.");
        return std::make_shared<GotoStmt>(lbl.lexeme);
    }


    // Parse enum declaration
    std::shared_ptr<Stmt> enumDeclaration() {
        advance(); // consume ENUM
        Token name = consume(XTokenType::IDENTIFIER, "Expect enum name.");
        std::unordered_map<std::string, int> members;
        while (!check(XTokenType::END)) {
            Token memberName = consume(XTokenType::IDENTIFIER, "Expect enum member name.");
            consume(XTokenType::EQUAL, "Expect '=' after enum member name.");
            Token numberToken = consume(XTokenType::NUMBER, "Expect number for enum member value.");
            int value = std::stoi(numberToken.lexeme);
            members[toLower(memberName.lexeme)] = value;
        }
        consume(XTokenType::END, "Expect 'End' after enum definition.");
        if (check(XTokenType::ENUM)) { advance(); }
        return std::make_shared<EnumStmt>(name.lexeme, members);
    }

    // Parse module declaration
    std::shared_ptr<Stmt> moduleDeclaration() {
        Token name = consume(XTokenType::IDENTIFIER, "Expect module name.");
        bool oldInModule = inModule;
        inModule = true;
        // std::vector<std::shared_ptr<Stmt>> body = block({ XTokenType::END });

        std::vector<std::shared_ptr<Stmt>> body;
        while (!isAtEnd() && !(check(XTokenType::END) &&
                            tokens[current + 1].type == XTokenType::MODULE))
        {
            body.push_back(declaration());
        }

        consume(XTokenType::END, "Expect 'End' after module body.");
        consume(XTokenType::MODULE, "Expect 'Module' after End in module declaration.");
        inModule = oldInModule;
        return std::make_shared<ModuleStmt>(name.lexeme, body);
    }
    // Parse Declare statement
    std::shared_ptr<Stmt> declareStatement() {
        bool isFunc;
        if (match({ XTokenType::SUB }))
            isFunc = false;
        else if (match({ XTokenType::FUNCTION }))
            isFunc = true;
        else {
            std::cerr << "Parse error at line " << peek().line << ": Expected Sub or Function after Declare." << std::endl;
            exit(1);
        }
        Token nameTok = consume(XTokenType::IDENTIFIER, "Expect API name in Declare statement.");
        std::string apiName = nameTok.lexeme;
        Token libTok = consume(XTokenType::IDENTIFIER, "Expect 'Lib' keyword in Declare statement.");
        if (toLower(libTok.lexeme) != "lib") {
            std::cerr << "Parse error at line " << libTok.line << ": Expected 'Lib' keyword in Declare statement." << std::endl;
            exit(1);
        }
        Token libNameTok = consume(XTokenType::STRING, "Expect library name (a string literal) in Declare statement.");
        std::string libraryName = libNameTok.lexeme.substr(1, libNameTok.lexeme.size() - 2);
        std::string aliasName = "";
        if (check(XTokenType::IDENTIFIER) && toLower(peek().lexeme) == "alias") {
            advance();
            Token aliasTok = consume(XTokenType::STRING, "Expect alias name (a string literal) in Declare statement.");
            aliasName = aliasTok.lexeme.substr(1, aliasTok.lexeme.size() - 2);
        }
        std::string selector = "";
        if (check(XTokenType::IDENTIFIER) && toLower(peek().lexeme) == "selector") {
            advance();
            Token selTok = consume(XTokenType::STRING, "Expect selector (a string literal) in Declare statement.");
            selector = selTok.lexeme.substr(1, selTok.lexeme.size() - 2);
        }
        std::vector<Param> params;
        consume(XTokenType::LEFT_PAREN, "Expect '(' for parameter list in Declare statement.");
        if (!check(XTokenType::RIGHT_PAREN)) {
            do {
                bool isOptional = false;
                if (match({ XTokenType::XOPTIONAL })) { isOptional = true; }
                Token paramNameTok = consume(XTokenType::IDENTIFIER, "Expect parameter name in Declare statement.");
                std::string paramName = paramNameTok.lexeme;
                std::string paramType = "";
                if (match({ XTokenType::AS })) {
                    Token typeTok = consume(XTokenType::IDENTIFIER, "Expect type after 'As' in parameter list.");
                    paramType = toLower(typeTok.lexeme);
                }
                Value defaultValue = Value(std::monostate{});
                if (isOptional && match({ XTokenType::EQUAL })) {
                    std::shared_ptr<Expr> defaultExpr = expression();
                    if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(defaultExpr))
                        defaultValue = lit->value;
                    else
                        runtimeError("Optional parameter default value must be a literal.");
                }
                params.push_back({ paramName, paramType, isOptional, false, defaultValue });
            } while (match({ XTokenType::COMMA }));
        }
        consume(XTokenType::RIGHT_PAREN, "Expect ')' after parameter list in Declare statement.");
        std::string retType = "";
        if (isFunc && check(XTokenType::AS)) {
            advance();
            Token retTok = consume(XTokenType::IDENTIFIER, "Expect return type after 'As' in Declare statement.");
            retType = toLower(retTok.lexeme);
        }
        return std::make_shared<DeclareStmt>(isFunc, apiName, libraryName, aliasName, selector, params, retType);
    }

    // Modified declaration to capture access modifiers in module context.
    std::shared_ptr<Stmt> declaration() {
        AccessModifier access = AccessModifier::PUBLIC;
        if (inModule && (check(XTokenType::PUBLIC) || check(XTokenType::PRIVATE))) {
            if (match({ XTokenType::PUBLIC })) access = AccessModifier::PUBLIC;
            else if (match({ XTokenType::PRIVATE })) access = AccessModifier::PRIVATE;
        }
        if (check(XTokenType::IDENTIFIER) && current + 1 < tokens.size()
            && tokens[current + 1].type == XTokenType::COLON) {
            Token labelTok = advance();             // IDENTIFIER
            consume(XTokenType::COLON, "Expect ':' after label.");
            return std::make_shared<LabelStmt>(labelTok.lexeme);
        }
        if (check(XTokenType::MODULE))
            return (advance(), moduleDeclaration());
        if (check(XTokenType::ENUM))
            return enumDeclaration();
        if (check(XTokenType::DECLARE))
            return (advance(), declareStatement());
        if (check(XTokenType::SELECT))
            return (advance(), selectCaseStatement());
        if (check(XTokenType::IDENTIFIER) && (current + 3 < tokens.size() &&
            tokens[current + 1].type == XTokenType::DOT &&
            tokens[current + 2].type == XTokenType::IDENTIFIER &&
            tokens[current + 3].type == XTokenType::EQUAL)) {
            Token obj = advance();
            consume(XTokenType::DOT, "Expect '.' in property assignment.");
            Token prop = consume(XTokenType::IDENTIFIER, "Expect property name in property assignment.");
            consume(XTokenType::EQUAL, "Expect '=' in property assignment.");
            std::shared_ptr<Expr> valueExpr = expression();
            return std::make_shared<PropertyAssignmentStmt>(std::make_shared<VariableExpr>(obj.lexeme), prop.lexeme, valueExpr);
        }

        // -----------------------------------------------------------------
        // Assigns-style / array-element set statement
        //
        // BASIC uses '=' for equality in expressions, so we must NOT rewrite
        // comparisons like:   If data(i) = key Then
        //
        // But at the statement level, we DO want to support the Xojo-style
        // "Assigns" / array-set syntax:
        //   data(i) = value        → data(i, value)
        //   obj.Method(i) = value  → obj.Method(i, value)
        //
        // This block recognizes that pattern only when it appears as a
        // standalone statement.
        // -----------------------------------------------------------------
        if (check(XTokenType::IDENTIFIER)) {
            int saved = current;
            std::shared_ptr<Expr> lhs = call();
            if (auto callExpr = std::dynamic_pointer_cast<CallExpr>(lhs)) {
                if (match({ XTokenType::EQUAL })) {
                    std::shared_ptr<Expr> rhs = expression();
                    auto args = callExpr->arguments;
                    args.push_back(rhs);
                    return std::make_shared<ExpressionStmt>(
                        std::make_shared<CallExpr>(callExpr->callee, args)
                    );
                }
            }
            // Not a call-assignment statement; rewind and continue normally.
            current = saved;
        }
        if (match({ XTokenType::FUNCTION, XTokenType::SUB }))
            return functionDeclaration(access);
        if (match({ XTokenType::CLASS }))
            return classDeclaration();
        if (match({ XTokenType::XCONST }))
            return varDeclaration(access, true);
        if (match({ XTokenType::DIM }))
            return varDeclaration(access, false);
        if (match({ XTokenType::IF }))
            return ifStatement();
        if (match({ XTokenType::FOR }))
            return forStatement();
        if (match({ XTokenType::WHILE }))
            return whileStatement();

        //TODO Fix +=
        if (check(XTokenType::IDENTIFIER) && (current + 1 < tokens.size() && tokens[current + 1].type == XTokenType::EQUAL)) {
                Token id = advance();
                advance();
                std::shared_ptr<Expr> value = expression();
                return std::make_shared<AssignmentStmt>(id.lexeme, value);
            }
        if (match({ XTokenType::GOTO })) {
            return gotoStatement();
        }
        return statement();
    }

    std::shared_ptr<Stmt> functionDeclaration(AccessModifier access) {
        Token name = consume(XTokenType::IDENTIFIER, "Expect function name.");
        consume(XTokenType::LEFT_PAREN, "Expect '(' after function name.");

        // ─── Module Extends ───────────────────────────────
        bool        isExtension = false;
        std::string extParam;     // name of the first (extended) parameter
        std::string extType;      // its declared type

        if (match({ XTokenType::EXTENDS })) {
            isExtension = true;
            // parse: Extends <param> As <Type>
            Token p = consume(XTokenType::IDENTIFIER,
                            "Expect parameter name after Extends.");
            extParam = p.lexeme;
            consume(XTokenType::AS,
                    "Expect 'As' after Extends parameter name.");
            Token t = consume(XTokenType::IDENTIFIER,
                            "Expect type name after As in Extends.");
            extType  = toLower(t.lexeme);

            // Optional comma before the next parameter:
            match({ XTokenType::COMMA });
        }
        // ────

        std::vector<Param> parameters;
        if (!check(XTokenType::RIGHT_PAREN)) {
            do {
                bool isByRef   = false;
                bool isOptional = false;
                bool isAssigns  = false;

                // Accept keywords in any order before the parameter name.
                bool scanning = true;
                while (scanning) {
                    if (match({ XTokenType::BYREF }))      { isByRef = true; continue; }
                    if (match({ XTokenType::ASSIGNS }))    { isAssigns = true; continue; }
                    if (match({ XTokenType::XOPTIONAL }))  { isOptional = true; continue; }
                    scanning = false;
                }

                Token paramName = consume(XTokenType::IDENTIFIER, "Expect parameter name.");

                std::string paramType = "";

                if (match({ XTokenType::AS })) {
                    Token typeToken = consume(XTokenType::IDENTIFIER, "Expect type after 'As'.");
                    paramType = toLower(typeToken.lexeme);
                }
                Value defaultValue = Value(std::monostate{});
                if (isOptional && match({ XTokenType::EQUAL })) {
                    std::shared_ptr<Expr> defaultExpr = expression();
                    if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(defaultExpr))
                        defaultValue = lit->value;
                    else
                        runtimeError("Optional parameter default value must be a literal.");
                }

                parameters.push_back({ paramName.lexeme, paramType, isOptional, isAssigns, defaultValue, isByRef });


            } while (match({ XTokenType::COMMA }));
        }
        consume(XTokenType::RIGHT_PAREN, "Expect ')' after parameters.");
        if (match({ XTokenType::AS }))
            consume(XTokenType::IDENTIFIER, "Expect return type after 'As'.");
        std::vector<std::shared_ptr<Stmt>> body = block({ XTokenType::END });
        consume(XTokenType::END, "Expect 'End' after function body.");
        match({ XTokenType::FUNCTION, XTokenType::SUB });
        int req = 0;
        for (auto& p : parameters)
            if (!p.optional) req++;
        //return std::make_shared<FunctionStmt>(name.lexeme, parameters, body, access);
         auto stmt = std::make_shared<FunctionStmt>(
        name.lexeme,
        parameters,
        body,
        access,
        isExtension,    // ← NEW
        extParam,       // ← NEW
        extType         // ← NEW
        );
        return stmt;
    }

    std::shared_ptr<Stmt> classDeclaration() {
        Token name = consume(XTokenType::IDENTIFIER, "Expect class name.");
        std::vector<std::shared_ptr<FunctionStmt>> methods;
        PropertiesType properties;
        while (!check(XTokenType::END) && !isAtEnd()) {
            if (match({ XTokenType::DIM })) {
                Token propName = consume(XTokenType::IDENTIFIER, "Expect property name.");
                std::string typeStr = "";
                if (match({ XTokenType::AS })) {
                    Token typeToken = consume(XTokenType::IDENTIFIER, "Expect type after 'As'.");
                    typeStr = toLower(typeToken.lexeme);
                }
                Value defaultVal;
                if (typeStr == "integer" || typeStr == "double")
                    defaultVal = 0;
                else if (typeStr == "boolean")
                    defaultVal = false;
                else if (typeStr == "string")
                    defaultVal = std::string("");
                else if (typeStr == "color")
                    defaultVal = Color{ 0 };
                else if (typeStr == "array")
                    defaultVal = Value(std::make_shared<ObjArray>());
                else
                    defaultVal = std::monostate{};
                properties.push_back({ toLower(propName.lexeme), defaultVal });
            }
            else if (match({ XTokenType::FUNCTION, XTokenType::SUB })) {
                Token methodName = consume(XTokenType::IDENTIFIER, "Expect method name.");
                consume(XTokenType::LEFT_PAREN, "Expect '(' after method name.");
                std::vector<Param> parameters;
                if (!check(XTokenType::RIGHT_PAREN)) {
                    do {
                        bool isByRef   = false;
                        bool isOptional = false;
                        bool isAssigns  = false;

                        bool scanning = true;
                        while (scanning) {
                            if (match({ XTokenType::BYREF }))      { isByRef = true; continue; }
                            if (match({ XTokenType::ASSIGNS }))    { isAssigns = true; continue; }
                            if (match({ XTokenType::XOPTIONAL }))  { isOptional = true; continue; }
                            scanning = false;
                        }

                        Token param = consume(XTokenType::IDENTIFIER, "Expect parameter name.");
                        std::string paramType = "";
                        if (match({ XTokenType::AS })) {
                            Token typeToken = consume(XTokenType::IDENTIFIER, "Expect type after 'As'.");
                            paramType = toLower(typeToken.lexeme);
                        }
                        Value defaultValue = Value(std::monostate{});
                        if (isOptional && match({ XTokenType::EQUAL })) {
                            std::shared_ptr<Expr> defaultExpr = expression();
                            if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(defaultExpr))
                                defaultValue = lit->value;
                            else
                                runtimeError("Optional parameter default value must be a literal.");
                        }
                        parameters.push_back({ param.lexeme, paramType, isOptional, isAssigns, defaultValue, isByRef });
                    } while (match({ XTokenType::COMMA }));
                }
                consume(XTokenType::RIGHT_PAREN, "Expect ')' after parameters.");
                if (match({ XTokenType::AS }))
                    consume(XTokenType::IDENTIFIER, "Expect return type after 'As'.");
                std::vector<std::shared_ptr<Stmt>> body = block({ XTokenType::END });
                consume(XTokenType::END, "Expect 'End' after method body.");
                match({ XTokenType::FUNCTION, XTokenType::SUB });
                methods.push_back(std::make_shared<FunctionStmt>(methodName.lexeme, parameters, body));
            }
            else {
                advance();
            }
        }
        consume(XTokenType::END, "Expect 'End' after class.");
        consume(XTokenType::CLASS, "Expect 'Class' after End.");
        return std::make_shared<ClassStmt>(name.lexeme, methods, properties);
    }

    std::shared_ptr<Stmt> varDeclaration(AccessModifier access, bool isConstant) {
        Token name = consume(XTokenType::IDENTIFIER, "Expect variable name.");
        bool isArray = false;
        if (match({ XTokenType::LEFT_PAREN })) {
            consume(XTokenType::RIGHT_PAREN, "Expect ')' in array declaration.");
            isArray = true;
        }
        std::string typeStr = "";
        std::shared_ptr<Expr> initializer = nullptr;
        if (match({ XTokenType::AS })) {
            if (check(XTokenType::NEW)) {
                advance(); // consume NEW
                Token typeToken = consume(XTokenType::IDENTIFIER, "Expect class name after 'New' in variable declaration.");
                typeStr = typeToken.lexeme;
                initializer = std::make_shared<NewExpr>(typeToken.lexeme, std::vector<std::shared_ptr<Expr>>{});
                if (match({ XTokenType::LEFT_PAREN })) {
                    std::vector<std::shared_ptr<Expr>> args;
                    if (!check(XTokenType::RIGHT_PAREN)) {
                        do {
                            args.push_back(expression());
                        } while (match({ XTokenType::COMMA }));
                    }
                    consume(XTokenType::RIGHT_PAREN, "Expect ')' after constructor arguments.");
                    initializer = std::make_shared<NewExpr>(typeToken.lexeme, args);
                }
            }
            else {
                Token typeToken = consume(XTokenType::IDENTIFIER, "Expect type after 'As' in variable declaration.");
                typeStr = toLower(typeToken.lexeme);
                if (match({ XTokenType::NEW })) {
                    Token classToken = consume(XTokenType::IDENTIFIER, "Expect class name after 'New'.");
                    initializer = std::make_shared<NewExpr>(classToken.lexeme, std::vector<std::shared_ptr<Expr>>{});
                    if (match({ XTokenType::LEFT_PAREN })) {
                        std::vector<std::shared_ptr<Expr>> args;
                        if (!check(XTokenType::RIGHT_PAREN)) {
                            do {
                                args.push_back(expression());
                            } while (match({ XTokenType::COMMA }));
                        }
                        consume(XTokenType::RIGHT_PAREN, "Expect ')' after constructor arguments.");
                        initializer = std::make_shared<NewExpr>(classToken.lexeme, args);
                    }
                }
            }
        }
        if (!initializer && match({ XTokenType::EQUAL }))
            initializer = expression();
        else if (isArray)
            initializer = std::make_shared<ArrayLiteralExpr>(std::vector<std::shared_ptr<Expr>>{});
        else if (typeStr == "pointer" || typeStr == "ptr")
            initializer = std::make_shared<LiteralExpr>(static_cast<void*>(nullptr)); // Initialize pointer to nullptr
        return std::make_shared<VarStmt>(name.lexeme, initializer, typeStr, isConstant, access);
    }

    // ========================================================================
    //  single-line / multi-line IF
    // ========================================================================
    std::shared_ptr<Stmt> ifStatement() {
        auto cond = expression();
        consume(XTokenType::THEN, "expect 'Then' after condition");
        int  thenLine  = previous().line;
        bool singleLine = (peek().line == thenLine);

        // ---------- single-line ----------
        if (singleLine) {
            auto thenBranch = std::vector<std::shared_ptr<Stmt>>{ statement() };
            std::vector<std::shared_ptr<Stmt>> elseBranch;
            if (match({XTokenType::ELSE}) && peek().line == thenLine)
                elseBranch.push_back(statement());
            return std::make_shared<IfStmt>(cond, thenBranch, elseBranch);
        }

        // ---------- multi-line ----------
        auto thenBranch = block({XTokenType::ELSEIF, XTokenType::ELSE, XTokenType::END});
        std::vector<std::shared_ptr<Stmt>> elseBranch;

        while (match({XTokenType::ELSEIF})) {
            auto elseifCond  = expression();
            consume(XTokenType::THEN, "expect Then");
            auto elseifBody  = block({XTokenType::ELSEIF, XTokenType::ELSE, XTokenType::END});
            auto elseifStmt  = std::make_shared<IfStmt>(elseifCond, elseifBody, std::vector<std::shared_ptr<Stmt>>{});
            elseBranch = { elseifStmt };
        }

        if (match({XTokenType::ELSE}))
            elseBranch = block({XTokenType::END});

        consume(XTokenType::END, "expect End If");
        consume(XTokenType::IF,  "expect 'If' after End");
        return std::make_shared<IfStmt>(cond, thenBranch, elseBranch);
    }


    std::shared_ptr<Stmt> forStatement() {
        Token varName = consume(XTokenType::IDENTIFIER, "Expect loop variable name.");
        if (match({ XTokenType::AS })) { 
            consume(XTokenType::IDENTIFIER, "Expect type after 'As'.");
        }
        consume(XTokenType::EQUAL, "Expect '=' after loop variable.");
        std::shared_ptr<Expr> startExpr = expression();
    
        bool isDown = false;
        if (match({ XTokenType::TO })) {
            isDown = false;
        } else if (match({ XTokenType::DOWNTO })) {
            isDown = true;
        } else {
            runtimeError("Expect 'To' or 'DownTo' after initializer in For loop.");
        }
    
        std::shared_ptr<Expr> endExpr = expression();
        std::shared_ptr<Expr> stepExpr;
        if (match({ XTokenType::STEP })) {
            stepExpr = expression();
        } else {
            // Default step: 1 for upward, -1 for downward loops
            stepExpr = std::make_shared<LiteralExpr>(isDown ? -1 : 1);
        }
        std::vector<std::shared_ptr<Stmt>> body = block({ XTokenType::NEXT });
        consume(XTokenType::NEXT, "Expect 'Next' after For loop body.");
        if (check(XTokenType::IDENTIFIER)) advance();
    
        // Create the initializer for the loop variable
        std::shared_ptr<Stmt> initializer = std::make_shared<VarStmt>(varName.lexeme, startExpr);
        std::shared_ptr<Expr> loopVar = std::make_shared<VariableExpr>(varName.lexeme);
    
        // Set loop condition: <= for upward, >= for downward
        std::shared_ptr<Expr> condition;
        if (isDown) {
            condition = std::make_shared<BinaryExpr>(loopVar, BinaryOp::GE, endExpr);
        } else {
            condition = std::make_shared<BinaryExpr>(loopVar, BinaryOp::LE, endExpr);
        }
    
        // Update the loop variable: always using addition (the step will be negative if downward)
        std::shared_ptr<Expr> increment = std::make_shared<AssignmentExpr>(
            varName.lexeme,
            std::make_shared<BinaryExpr>(loopVar, BinaryOp::ADD, stepExpr)
        );
        body.push_back(std::make_shared<ExpressionStmt>(increment));
    
        std::vector<std::shared_ptr<Stmt>> forBlock = { initializer, std::make_shared<WhileStmt>(condition, body) };
        return std::make_shared<BlockStmt>(forBlock);
    }
    
    std::shared_ptr<Stmt> whileStatement() {
        std::shared_ptr<Expr> condition = expression();
        std::vector<std::shared_ptr<Stmt>> body = block({ XTokenType::WEND });
        consume(XTokenType::WEND, "Expect 'Wend' after while loop.");
        return std::make_shared<WhileStmt>(condition, body);
    }

    std::shared_ptr<Stmt> statement() {
        if (match({ XTokenType::RETURN }))
            return returnStatement();
        if (match({ XTokenType::PRINT }))
            return printStatement();
        return expressionStatement();
    }

    std::shared_ptr<Stmt> printStatement() {
        std::shared_ptr<Expr> value = expression();
        return std::make_shared<ExpressionStmt>(
            std::make_shared<CallExpr>(
                std::make_shared<LiteralExpr>(std::string("print")),
                std::vector<std::shared_ptr<Expr>>{value}
            )
        );
    }

    std::shared_ptr<Stmt> returnStatement() {
        std::shared_ptr<Expr> value = expression();
        return std::make_shared<ReturnStmt>(value);
    }

    std::shared_ptr<Stmt> expressionStatement() {
        std::shared_ptr<Expr> expr = expression();
        return std::make_shared<ExpressionStmt>(expr);
    }

    std::shared_ptr<Expr> assignment() 
    {
        std::shared_ptr<Expr> expr = orExpr();

        /*
          IMPORTANT:
          ---------
          In earlier versions, we attempted to support Xojo-style "Assigns"
          (e.g.  Foo(i) = v  →  Foo(i, v)) by rewriting *any* equality
          expression whose left-hand side was a CallExpr.

          That was too broad because BASIC also uses '=' for equality in
          conditional expressions.

          Example that must remain a comparison:
              If data(i) = key Then

          The old rewrite would turn this into an array set:
              data(i, key)
          which mutates the array and makes the condition always truthy.

          The Assigns / array-element-set rewrite is now handled at the
          statement level (see Parser::declaration) so comparisons inside
          expressions stay comparisons.
        */

        // --- Compound assignment (+=, -=, *=, /=) ---
        if ( match({
                XTokenType::PLUS_EQUAL,
                XTokenType::MINUS_EQUAL,
                XTokenType::STAR_EQUAL,
                XTokenType::SLASH_EQUAL
            }) )
        {
            Token op = previous();                     // the ‘+=’ or ‘-=’ etc.
            // parse the right‐hand side (itself can be another assignment)
            std::shared_ptr<Expr> right = assignment();
    
            // must be a simple variable on the left
            if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
                // pick the correct binary operator
                BinaryOp binop;
                switch (op.type) {
                    case XTokenType::PLUS_EQUAL:  binop = BinaryOp::ADD; break;
                    case XTokenType::MINUS_EQUAL: binop = BinaryOp::SUB; break;
                    case XTokenType::STAR_EQUAL:  binop = BinaryOp::MUL; break;
                    case XTokenType::SLASH_EQUAL: binop = BinaryOp::DIV; break;
                    default:                      binop = BinaryOp::ADD; break; // never happens
                }
                // build “var = var <op> right”
                auto leftVar = std::make_shared<VariableExpr>(var->name);
                auto binary  = std::make_shared<BinaryExpr>(leftVar, binop, right);
                return std::make_shared<AssignmentExpr>(var->name, binary);
            }
    
            runtimeError("Invalid target for compound assignment.");
        }
    
        // --- Plain assignment (=) ---
        if ( match({ XTokenType::EQUAL }) ) {
            Token equals = previous();
            std::shared_ptr<Expr> value = assignment();

            /* existing rules for “x = …” */
            if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr))
                return std::make_shared<AssignmentExpr>(var->name, value);
 
            runtimeError("Invalid assignment target.");

        }
    
        // no assignment here → just return the subexpression
        return expr;
    }
    

    std::shared_ptr<Expr> expression() { return assignment(); }
    std::shared_ptr<Expr> equality() {
        std::shared_ptr<Expr> expr = comparison();
        while (match({ XTokenType::EQUAL, XTokenType::NOT_EQUAL })) {
            Token op = previous();
            BinaryOp binOp = (op.type == XTokenType::EQUAL) ? BinaryOp::EQ : BinaryOp::NE;
            std::shared_ptr<Expr> right = comparison();
            expr = std::make_shared<BinaryExpr>(expr, binOp, right);
        }
        return expr;
    }


    // ------------ logical OR / XOR / AND precedence ------------
    std::shared_ptr<Expr> orExpr() {
        auto expr = xorExpr();
        while (match({XTokenType::OR})) {
            auto rhs = xorExpr();
            expr = std::make_shared<BinaryExpr>(expr, BinaryOp::OR, rhs);
        }
        return expr;
    }

    std::shared_ptr<Expr> xorExpr() {
        auto expr = andExpr();
        while (match({XTokenType::XOR})) {
            auto rhs = andExpr();
            expr = std::make_shared<BinaryExpr>(expr, BinaryOp::XOR, rhs);
        }
        return expr;
    }

    std::shared_ptr<Expr> andExpr() {
        auto expr = equality();
        while (match({XTokenType::AND})) {
            auto rhs = equality();
            expr = std::make_shared<BinaryExpr>(expr, BinaryOp::AND, rhs);
        }
        return expr;
    }

    std::shared_ptr<Expr> comparison() {
        std::shared_ptr<Expr> expr = addition();
        while (match({ XTokenType::LESS, XTokenType::LESS_EQUAL, XTokenType::GREATER, XTokenType::GREATER_EQUAL })) {
            Token op = previous();
            BinaryOp binOp;
            switch (op.type) {
            case XTokenType::LESS: binOp = BinaryOp::LT; break;
            case XTokenType::LESS_EQUAL: binOp = BinaryOp::LE; break;
            case XTokenType::GREATER: binOp = BinaryOp::GT; break;
            case XTokenType::GREATER_EQUAL: binOp = BinaryOp::GE; break;
            default: binOp = BinaryOp::EQ; break;
            }
            std::shared_ptr<Expr> right = addition();
            expr = std::make_shared<BinaryExpr>(expr, binOp, right);
        }
        return expr;
    }

    std::shared_ptr<Expr> addition() {
        std::shared_ptr<Expr> expr = multiplication();
        while (match({ XTokenType::PLUS, XTokenType::MINUS })) {
            Token op = previous();
            BinaryOp binOp = (op.type == XTokenType::PLUS) ? BinaryOp::ADD : BinaryOp::SUB;
            std::shared_ptr<Expr> right = multiplication();
            expr = std::make_shared<BinaryExpr>(expr, binOp, right);
        }
        return expr;
    }

    std::shared_ptr<Expr> multiplication() {
        std::shared_ptr<Expr> expr = exponentiation();
        while (match({ XTokenType::STAR, XTokenType::SLASH, XTokenType::MOD })) {
            Token op = previous();
            BinaryOp binOp;
            if (op.type == XTokenType::STAR) binOp = BinaryOp::MUL;
            else if (op.type == XTokenType::SLASH) binOp = BinaryOp::DIV;
            else if (op.type == XTokenType::MOD) binOp = BinaryOp::MOD;
            std::shared_ptr<Expr> right = exponentiation();
            expr = std::make_shared<BinaryExpr>(expr, binOp, right);
        }
        return expr;
    }

    std::shared_ptr<Expr> exponentiation() {
        std::shared_ptr<Expr> expr = unary();
        if (match({ XTokenType::CARET })) {
            std::shared_ptr<Expr> right = exponentiation();
            expr = std::make_shared<BinaryExpr>(expr, BinaryOp::POW, right);
        }
        return expr;
    }

    std::shared_ptr<Expr> unary() {
        if ( match({ XTokenType::PLUS }) ) {
          // unary “+” is a no-op, just parse the next unary
          return unary();
        }
        if ( match({ XTokenType::MINUS, XTokenType::NOT }) ) {
          Token op = previous();
          auto right = unary();
          if (op.type == XTokenType::MINUS)
            return std::make_shared<UnaryExpr>("-", right);
          else
            return std::make_shared<UnaryExpr>("not", right);
        }
        return call();
      }

    std::shared_ptr<Expr> call() {
        std::shared_ptr<Expr> expr = primary();
        bool explicitCallUsed = false;
        while (true) {
            if (match({ XTokenType::LEFT_PAREN })) {
                explicitCallUsed = true;
                expr = finishCall(expr);
            }
            else if (match({ XTokenType::DOT })) {
                Token prop = consume(XTokenType::IDENTIFIER, "Expect property name after '.'");
                expr = std::make_shared<GetPropExpr>(expr, prop.lexeme);
            }
            else {
                break;
            }
        }
        return expr;
    }

    std::shared_ptr<Expr> finishCall(std::shared_ptr<Expr> callee) {
        std::vector<std::shared_ptr<Expr>> arguments;
        if (!check(XTokenType::RIGHT_PAREN)) {
            do {
                arguments.push_back(expression());
            } while (match({ XTokenType::COMMA }));
        }
        consume(XTokenType::RIGHT_PAREN, "Expect ')' after arguments.");
        return std::make_shared<CallExpr>(callee, arguments);
    }
    
    std::shared_ptr<Expr> primary() {
        if (match({ XTokenType::NUMBER })) {
            std::string lex = previous().lexeme;
            if (lex.find('.') != std::string::npos)
                return std::make_shared<LiteralExpr>(std::stod(lex));
            else
                return std::make_shared<LiteralExpr>(std::stoi(lex));
        }
        if (match({ XTokenType::STRING })) {
            std::string s = previous().lexeme;
            s = s.substr(1, s.size() - 2);
            return std::make_shared<LiteralExpr>(s);
        }
        if (match({ XTokenType::COLOR })) {
            std::string s = previous().lexeme;
            std::string hex = s.substr(2);
            unsigned int col = std::stoul(hex, nullptr, 16);
            return std::make_shared<LiteralExpr>(Color{ col });
        }
        if (match({ XTokenType::BOOLEAN_TRUE }))
            return std::make_shared<LiteralExpr>(true);
        if (match({ XTokenType::BOOLEAN_FALSE }))
            return std::make_shared<LiteralExpr>(false);
        if (match({ XTokenType::IDENTIFIER })) {
            Token id = previous();
            if (toLower(id.lexeme) == "array" && match({ XTokenType::LEFT_BRACKET })) {
                std::vector<std::shared_ptr<Expr>> elements;
                if (!check(XTokenType::RIGHT_BRACKET)) {
                    do {
                        elements.push_back(expression());
                    } while (match({ XTokenType::COMMA }));
                }
                consume(XTokenType::RIGHT_BRACKET, "Expect ']' after array literal.");
                return std::make_shared<ArrayLiteralExpr>(elements);
            }
            return std::make_shared<VariableExpr>(id.lexeme);
        }
        if (match({ XTokenType::LEFT_PAREN })) {
            std::shared_ptr<Expr> expr = expression();
            consume(XTokenType::RIGHT_PAREN, "Expect ')' after expression.");
            return std::make_shared<GroupingExpr>(expr);
        }
        std::cerr << "Parse error at line " << peek().line << ": Expected expression." << std::endl;
        exit(1);
        return nullptr;
    }

    // ***** selectCaseStatement() to support "Select Case" constructs *****
    std::shared_ptr<Stmt> selectCaseStatement() {
        consume(XTokenType::CASE, "Expect 'Case' after 'Select' in Select Case statement.");
        std::shared_ptr<Expr> switchExpr = expression();
        struct CaseClause {
            bool isDefault = false;
            std::shared_ptr<Expr> expr;
            std::vector<std::shared_ptr<Stmt>> statements;
        };
        std::vector<CaseClause> clauses;
        while (!check(XTokenType::END)) {
            consume(XTokenType::CASE, "Expect 'Case' at start of case clause.");
            CaseClause clause;
            if (match({ XTokenType::ELSE })) {
                clause.isDefault = true;
            }
            else {
                clause.isDefault = false;
                clause.expr = expression();
            }
            clause.statements = block({ XTokenType::CASE, XTokenType::END });
            clauses.push_back(clause);
        }
        consume(XTokenType::END, "Expect 'End' after Select Case statement.");
        consume(XTokenType::SELECT, "Expect 'Select' after 'End' in Select Case statement.");
        std::vector<std::shared_ptr<Stmt>> currentElse;
        for (int i = clauses.size() - 1; i >= 0; i--) {
            if (clauses[i].isDefault) {
                currentElse = clauses[i].statements;
            }
            else {
                auto condition = std::make_shared<BinaryExpr>(switchExpr, BinaryOp::EQ, clauses[i].expr);
                auto ifStmt = std::make_shared<IfStmt>(condition, clauses[i].statements, currentElse);
                currentElse.clear();
                currentElse.push_back(ifStmt);
            }
        }
        if (currentElse.empty()) {
            return std::make_shared<BlockStmt>(std::vector<std::shared_ptr<Stmt>>{});
        }
        return currentElse[0];
    }
    // ***** End of Select Case support *****
};

// ============================================================================  
// Helpers for constant pool management
// ============================================================================
int addConstant(ObjFunction::CodeChunk& chunk, const Value& v) {
    chunk.constants.push_back(v);
    return chunk.constants.size() - 1;
}

int addConstantString(ObjFunction::CodeChunk& chunk, const std::string& s) {
    for (int i = 0; i < chunk.constants.size(); i++) {
        if (holds<std::string>(chunk.constants[i])) {
            if (getVal<std::string>(chunk.constants[i]) == s)
                return i;
        }
    }
    chunk.constants.push_back(s);
    return chunk.constants.size() - 1;
}

// ============================================================================  
// Built-in Array Methods
// ============================================================================
Value callArrayMethod(std::shared_ptr<ObjArray> array, const std::string& method, const std::vector<Value>& args) {
    std::string m = toLower(method);
    if (m == "add") {
        if (args.size() != 1) runtimeError("Array.add expects 1 argument.");
        array->elements.push_back(args[0]);
        return Value(std::monostate{});
    }
    else if (m == "indexof") {
        if (args.size() != 1) runtimeError("Array.indexof expects 1 argument.");
        for (size_t i = 0; i < array->elements.size(); i++) {
            if (valueToString(array->elements[i]) == valueToString(args[0]))
                return (int)i;
        }
        return -1;
    }
    else if (m == "lastindex") {
        return array->elements.empty() ? -1 : (int)(array->elements.size() - 1);
    }
    else if (m == "count") {
        return (int)array->elements.size();
    }
    else if (m == "join") {
    // Array.join(separator As String) As String
    if (args.size() != 1) 
        runtimeError("Array.join expects exactly one argument: the separator string.");
    if (!holds<std::string>(args[0]))
        runtimeError("Array.join expects the separator to be a string.");

    const std::string sep = getVal<std::string>(args[0]);
    std::string result;
    for (size_t i = 0; i < array->elements.size(); ++i) {
        // ensure each element is a string
        if (!holds<std::string>(array->elements[i]))
            runtimeError("Array.join: all elements must be strings.");
        result += getVal<std::string>(array->elements[i]);
        if (i + 1 < array->elements.size())
            result += sep;
    }
    return Value(result);
    }

    else if (m == "pop") {
        if (array->elements.empty()) runtimeError("Array.pop called on empty array.");
        Value last = array->elements.back();
        array->elements.pop_back();
        return last;
    }
    else if (m == "removeat") {
        if (args.size() != 1) runtimeError("Array.removeat expects 1 argument.");
        int index = 0;
        if (holds<int>(args[0]))
            index = getVal<int>(args[0]);
        else runtimeError("Array.removeat expects an integer index.");
        if (index < 0 || index >= (int)array->elements.size())
            runtimeError("Array.removeat index out of bounds.");
        array->elements.erase(array->elements.begin() + index);
        return Value(std::monostate{});
    }
    else if (m == "removeall") {
        array->elements.clear();
        return Value(std::monostate{});
    }
    else {
        runtimeError("Unknown array method: " + method);
    }
    return Value(std::monostate{});
}

// ============================================================================  
// Plugin Loader and libffi wrappers
// ============================================================================
struct PluginEntry {
    const char* name;
    void* funcPtr;
    int arity = 0;
    const char* paramTypes[10]; // Supports up to 10 parameters
    const char* returnType;     // Return type string
};

typedef PluginEntry* (*GetPluginEntriesFunc)(int*);

struct ClassProperty {
    const char* name;
    const char* type;
    void* getter;
    void* setter;
};

struct ClassEntry {
    const char* name;
    void* funcPtr;
    int arity;
    const char* paramTypes[10];
    const char* retType;
};

struct ClassConstant {
    const char* declaration;
};

struct ClassDefinition {
    const char* className;
    size_t classSize;
    void* constructor;
    ClassProperty* properties;
    size_t propertiesCount;
    ClassEntry* methods;
    size_t methodsCount;
    ClassConstant* constants;
    size_t constantsCount;
};
typedef ClassDefinition* (*GetClassDefinitionFunc)();


ffi_type* mapType(const std::string& type)
{
    std::string t = toLower(type);
    if (t=="string")   return &ffi_type_pointer;
    if (t=="double" || t=="number") return &ffi_type_double;
    if (t=="integer"|| t=="int")    return &ffi_type_sint;
    if (t=="boolean"|| t=="bool")   return &ffi_type_uint8;
    if (t=="color")    return &ffi_type_uint32;
    if (t=="variant")  return &ffi_type_pointer;
    if (t=="pointer"|| t=="ptr"|| t=="array") return &ffi_type_pointer;
    if (t=="void")     return &ffi_type_uint8;

    // --- NEW -------------------------------------------------------
    // Anything else is assumed to be a *plugin-class name*.
    // The underlying C function will return an `int` handle.
    return &ffi_type_sint;
}


// ---------------------------------------------------------------------------
//  wrapPluginFunction
//     • funcPtr       – raw address of the exported C/C++ symbol
//     • arity         – number of arguments the function expects
//     • paramTypes    – C-strings with the declared parameter types
//     • returnTypeStr – declared return type  (built-ins or plugin class)
// ---------------------------------------------------------------------------
BuiltinFn wrapPluginFunction(void *funcPtr,
                             int arity,
                             const char **paramTypes,
                             const char *returnTypeStr)
{
    debugLog("wrapPluginFunction: building wrapper  funcPtr=" + std::to_string((uintptr_t)funcPtr) + "  arity=" + std::to_string(arity));

    // ----------------------------------------------------------------------
    // 1)  Create and prepare the libffi call interface (CIF)
    // ----------------------------------------------------------------------
    ffi_cif *cif = new ffi_cif;
    ffi_type **argTypes = new ffi_type *[arity];

    for (int i = 0; i < arity; ++i)
    {
        std::string pRaw = paramTypes[i] ? paramTypes[i] : "";
        std::string pType = toLower(pRaw);
        argTypes[i] = mapType(pType);

        debugLog("  param[" + std::to_string(i) + "] = '" + pRaw + "' -> " + (argTypes[i] ? "OK" : "UNKNOWN"));

        if (!argTypes[i])
            runtimeError("Unknown plugin parameter type: " + pType);
    }

    std::string retTypeString = toLower(returnTypeStr ? returnTypeStr : "variant");
    ffi_type *retType = mapType(retTypeString);

    debugLog("  return type = '" + std::string(returnTypeStr ? returnTypeStr : "") + "'  -> " + (retType ? "built-in" : "custom/plugin"));

    if (!retType)
        retType = &ffi_type_sint; // treat unknown returns as int

    if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, arity, retType, argTypes) != FFI_OK)
        runtimeError("ffi_prep_cif failed for plugin function");

    // ----------------------------------------------------------------------
    // 2)  Make a *local* copy of the parameter-type strings (captured by lambda)
    // ----------------------------------------------------------------------
    std::vector<std::string> localParamTypes;
    for (int i = 0; i < arity; ++i)
        localParamTypes.emplace_back(paramTypes[i] ? paramTypes[i] : "");

    // ----------------------------------------------------------------------
    // 3)  Detect whether the return type is a plugin-class name
    // ----------------------------------------------------------------------
    static const std::unordered_set<std::string> builtinTypes = {
        "string", "double", "number", "integer", "int", "boolean", "bool",
        "color", "variant", "pointer", "ptr", "array", "void"};
    bool isCustomClass = (builtinTypes.find(retTypeString) == builtinTypes.end());
    debugLog("  isCustomClass = " + std::string(isCustomClass ? "true" : "false"));

    // ----------------------------------------------------------------------
    // 4)  Return the VM-visible lambda wrapper
    // ----------------------------------------------------------------------
    return [=](const std::vector<Value> &args) -> Value
    {
        debugLog("PluginFunction: invoked with " + std::to_string(args.size()) + " args");

        // --------------------------------------------------------------
        // 4-a)  Argument marshalling
        // --------------------------------------------------------------
        std::vector<void*> heapAlloc;             // ➊ keep track of temp buffers
        
        if ((int)args.size() != arity)
            runtimeError("Plugin function expects " + std::to_string(arity) +
                         " arguments, got " + std::to_string(args.size()));

        void **argValues = new void *[arity];

        int intStorage[10] = {};
        double dblStorage[10] = {};
        bool boolStorage[10] = {};
        unsigned int uintStorage[10] = {};
        const char *strStorage[10] = {};
        Value *varStorage[10] = {};
        void *ptrStorage[10] = {};

        for (int i = 0; i < arity; ++i)
        {
            std::string pType = toLower(localParamTypes[i]);

            /* ----------------------------------------------------------------
            New block: turn an ObjArray into a flat C array (double[])
            ---------------------------------------------------------------- */
            if (pType == "array") {
                // Array parameters are passed to plugins as a raw pointer.
                // If the argument is a CrossBasic ObjArray, we flatten it to a temporary double[].
                // IMPORTANT: libffi expects the *address of the pointer* as the argument value.
                if (holds<std::shared_ptr<ObjArray>>(args[i])) {
                    auto src = getVal<std::shared_ptr<ObjArray>>(args[i]);

                    size_t n = src->elements.size();
                    double *buf = (n > 0) ? new double[n] : nullptr;
                    for (size_t k = 0; k < n; ++k) {
                        const Value &v = src->elements[k];
                        buf[k] =  holds<double>(v) ? getVal<double>(v)
                                : holds<int>(v)    ? (double)getVal<int>(v)
                                : /* otherwise */    0.0;
                    }

                    heapAlloc.push_back(buf);   // remember to free at end of call
                    ptrStorage[i] = buf;        // store pointer so we can pass &ptrStorage[i]
                    argValues[i] = &ptrStorage[i];
                    continue;
                }

                // Fallback: caller supplied an explicit pointer/int.
                if (holds<void *>(args[i])) {
                    ptrStorage[i] = getVal<void *>(args[i]);
                    argValues[i] = &ptrStorage[i];
                    continue;
                }
                if (holds<int>(args[i])) {
                    ptrStorage[i] = reinterpret_cast<void *>((intptr_t)getVal<int>(args[i]));
                    argValues[i] = &ptrStorage[i];
                    continue;
                }

                runtimeError("Plugin expects array/ObjArray or array pointer/int @" + std::to_string(i));
            } else if (pType == "string")
            {
                if (!holds<std::string>(args[i]))
                    runtimeError("Plugin expects string @" + std::to_string(i));
                strStorage[i] = strdup(getVal<std::string>(args[i]).c_str());
                argValues[i] = &strStorage[i];
            }
            else if (pType == "double" || pType == "number")
            {
                dblStorage[i] = holds<double>(args[i]) ? getVal<double>(args[i])
                                                       : (double)getVal<int>(args[i]);
                argValues[i] = &dblStorage[i];
            }
            else if (pType == "integer" || pType == "int")
            {
                intStorage[i] = holds<int>(args[i]) ? getVal<int>(args[i])
                                                    : (int)getVal<double>(args[i]);
                argValues[i] = &intStorage[i];
            }
            else if (pType == "boolean" || pType == "bool")
            {
                boolStorage[i] = holds<bool>(args[i]) ? getVal<bool>(args[i]) : false;
                argValues[i] = &boolStorage[i];
            }
            else if (pType == "color")
            {
                if (!holds<Color>(args[i]))
                    runtimeError("Plugin expects Color @" + std::to_string(i));
                uintStorage[i] = getVal<Color>(args[i]).value;
                argValues[i] = &uintStorage[i];
            }
            else if (pType == "variant")
            {
                varStorage[i] = new Value(args[i]);
                argValues[i] = &varStorage[i];
            }
            else if (pType == "pointer" || pType == "ptr") // || pType == "array")
            {
                if (holds<void *>(args[i]))
                    ptrStorage[i] = getVal<void *>(args[i]);
                else if (holds<int>(args[i]))
                    ptrStorage[i] =
                        reinterpret_cast<void *>((intptr_t)getVal<int>(args[i]));
                else
                    runtimeError("Plugin expects pointer/int @" + std::to_string(i));
                argValues[i] = &ptrStorage[i];
            }
            else
            {
                // Plugin-class parameters are passed as their integer handle.
                // The VM argument may be:
                //   - an ObjInstance (plugin object), or
                //   - a raw integer handle.
                if (holds<std::shared_ptr<ObjInstance>>(args[i])) {
                    auto inst = getVal<std::shared_ptr<ObjInstance>>(args[i]);
                    intStorage[i] = (int)(intptr_t)inst->pluginInstance;
                } else if (holds<int>(args[i])) {
                    intStorage[i] = getVal<int>(args[i]);
                } else {
                    intStorage[i] = 0;
                }
                argValues[i] = &intStorage[i];
            }

            debugLog("  marshalled arg[" + std::to_string(i) + "] type=" + pType);
        }

        // --------------------------------------------------------------
        // 4-b)  Call through libffi
        // --------------------------------------------------------------
        union
        {
            int i;
            double d;
            bool b;
            const char *s;
            unsigned int ui;
            Value *var;
            void *p;
        } result{};

        ffi_call(cif, FFI_FN(funcPtr), &result, argValues);
        debugLog("  ffi_call complete");

        /* -----------------------------------------
        free any buffers we created for arrays
        ----------------------------------------- */
        for (void* p : heapAlloc)
            delete[] static_cast<double*>(p);   // every entry is a double[]

        // --------------------------------------------------------------
        // 4-c)  Clean up temporaries
        // --------------------------------------------------------------
        for (int i = 0; i < arity; ++i)
        {
            std::string pType = toLower(localParamTypes[i]);
            if (pType == "string")
                free((void *)strStorage[i]);
            if (pType == "variant")
                delete varStorage[i];
        }
        delete[] argValues;

        // --------------------------------------------------------------
        // 4-d)  Convert the return value
        // --------------------------------------------------------------
        if (isCustomClass)
        {
            debugLog("  converting return-value as plugin class '" + retTypeString + "'");
            int handle = result.i;

            Value clsVal = globalVM->environment->get(toLower(retTypeString));
            if (!holds<std::shared_ptr<ObjClass>>(clsVal))
                runtimeError("Plugin class '" + retTypeString + "' not found");

            auto cls = getVal<std::shared_ptr<ObjClass>>(clsVal);
            auto inst = std::make_shared<ObjInstance>();
            inst->klass = cls;
            inst->pluginInstance = reinterpret_cast<void *>((intptr_t)handle);

            for (auto &p : cls->properties)
                inst->fields[p.first] = p.second;

            debugLog("  returning new instance handle=" + std::to_string(handle));
            return Value(inst);
        }

        // ------------ built-in return conversions ----------------------
        if (retTypeString == "void")
            return Value(std::monostate{});

        if (retTypeString == "string")
            return Value(std::string(result.s ? result.s : ""));

        if (retTypeString == "double" || retTypeString == "number")
            return Value(result.d);

        if (retTypeString == "integer" || retTypeString == "int")
            return Value(result.i);

        if (retTypeString == "boolean" || retTypeString == "bool")
            return Value(result.b);

        if (retTypeString == "color")
            return Value(Color{result.ui});

        if (retTypeString == "variant")
            return result.var ? *result.var : Value(std::monostate{});

        if (retTypeString == "pointer" || retTypeString == "ptr")
            return Value(result.p);

        if (retTypeString == "array")
        {
            ObjArray *raw = static_cast<ObjArray *>(result.p);
            auto arr = std::shared_ptr<ObjArray>(raw, [](ObjArray *) {});
            return Value(arr);
        }

        runtimeError("Unsupported plugin return type: " + retTypeString);
        return Value(std::monostate{});
    };
}


// In our system, we create a helper for Declare statements that loads the plugin or library
// and wraps the exported function using libffi.
BuiltinFn wrapPluginFunctionForDeclare(const std::vector<Param>& params, const std::string& retType,
    const std::string& apiName, const std::string& libName) {
    void* libHandle = nullptr;
#ifdef _WIN32
    libHandle = LoadLibraryA(libName.c_str());
    if (!libHandle) {
        debugLog("Error loading library: " + libName);
        exit(1);
    }
    void* funcPtr = reinterpret_cast<void*>(GetProcAddress((HMODULE)libHandle, apiName.c_str()));
#else
    libHandle = dlopen(libName.c_str(), RTLD_LAZY);
    if (!libHandle) {
        std::cerr << "Error loading library: " << libName << std::endl;
        exit(1);
    }
    void* funcPtr = dlsym(libHandle, apiName.c_str());
#endif
    if (!funcPtr) {
        std::cerr << "Error finding symbol: " << apiName << " in library: " << libName << std::endl;
        exit(1);
    }

    int arity = params.size();
    std::vector<std::string> typeStrings;
    std::vector<const char*> pTypes;
    for (int i = 0; i < arity; i++) {
        typeStrings.push_back(params[i].type); // make a copy
        pTypes.push_back(typeStrings.back().c_str());
    }
    return wrapPluginFunction(funcPtr, arity, pTypes.data(), retType.c_str());
}


#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
    const std::string PATH_SEPARATOR = "\\\\";
    #define LOAD_LIBRARY(path) LoadLibraryA((path).c_str())
    #define GET_PROC_ADDRESS(module, proc) GetProcAddress(module, proc)
    typedef HMODULE LIB_HANDLE;
#else
    #include <dlfcn.h>
    #include <dirent.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
    const std::string PATH_SEPARATOR = "/";
    #define LOAD_LIBRARY(path) dlopen((path).c_str(), RTLD_LAZY)
    #define GET_PROC_ADDRESS(module, proc) dlsym(module, proc)
    typedef void* LIB_HANDLE;
#endif

// Returns the directory of the current executable.
std::string getExecutableDir() {
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeDir = exePath;
    size_t pos = exeDir.find_last_of("\\/");
    if (pos != std::string::npos) {
        exeDir = exeDir.substr(0, pos);
    }
    return exeDir;
#elif defined(__linux__)
    char exePath[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
        exePath[len] = '\0';
        std::string pathStr(exePath);
        size_t pos = pathStr.find_last_of("/");
        if (pos != std::string::npos) {
            return pathStr.substr(0, pos);
        }
    }
    return "";
#elif defined(__APPLE__)
    char exePath[PATH_MAX];
    uint32_t size = sizeof(exePath);
    if (_NSGetExecutablePath(exePath, &size) == 0) {
        std::string pathStr(exePath);
        size_t pos = pathStr.find_last_of("/");
        if (pos != std::string::npos) {
            return pathStr.substr(0, pos);
        }
    }
    return "";
#else
    return "";
#endif
}

// Processes a single plugin library (DLL on Windows, .so/.dylib on Linux/macOS)
// and loads any plugin functions or classes it exports.
void processPluginLibrary(const std::string& libPath, VM& vm) {
    LIB_HANDLE libHandle = LOAD_LIBRARY(libPath);
    if (!libHandle) {
        debugLog("Failed to load library: " + libPath);
        return;
    }

    // Try to load function-based plugins.
    GetPluginEntriesFunc getEntries = (GetPluginEntriesFunc)GET_PROC_ADDRESS(libHandle, "GetPluginEntries");
    if (getEntries) {
        int count = 0;
        PluginEntry* entries = getEntries(&count);
        for (int i = 0; i < count; i++) {
            PluginEntry& entry = entries[i];
            BuiltinFn fn = wrapPluginFunction(entry.funcPtr, entry.arity, entry.paramTypes, entry.returnType);
            std::string funcName = toLower(entry.name);
            vm.environment->define(funcName, fn);
            debugLog("Loaded plugin function: " + std::string(entry.name) +
                     " with arity " + std::to_string(entry.arity) + " from " + libPath);
        }
    } else {
        // If GetPluginEntries not found, try loading a plugin class.
        GetClassDefinitionFunc getClassDef = (GetClassDefinitionFunc)GET_PROC_ADDRESS(libHandle, "GetClassDefinition");
        if (getClassDef) {
            ClassDefinition* classDef = getClassDef();
            auto pluginClass = std::make_shared<ObjClass>();
            pluginClass->name = toLower(classDef->className);
            pluginClass->isPlugin = true;
            pluginClass->pluginConstructor = wrapPluginFunction(classDef->constructor, 0, nullptr, "pointer");

            // Load class properties.
            for (size_t i = 0; i < classDef->propertiesCount; i++) {
                ClassProperty& prop = classDef->properties[i];
                const char* getterParams[1] = { "int" };    // Handle is represented as "int"
                BuiltinFn getterFn = wrapPluginFunction(prop.getter, 1, getterParams, prop.type);
                const char* setterParams[2] = { "int", prop.type }; // Setter parameters
                BuiltinFn setterFn = wrapPluginFunction(prop.setter, 2, setterParams, "void");
                pluginClass->pluginProperties[toLower(prop.name)] = std::make_pair(getterFn, setterFn);
            }

            // Load class methods.
            for (size_t i = 0; i < classDef->methodsCount; i++) {
                ClassEntry& entry = classDef->methods[i];
                BuiltinFn methodFn = wrapPluginFunction(entry.funcPtr, entry.arity, entry.paramTypes, entry.retType);
                std::string methodName = toLower(entry.name);
                pluginClass->methods[methodName] = methodFn;
            }

            // Load plugin constants.
            for (size_t i = 0; i < classDef->constantsCount; i++) {
                ClassConstant& constant = classDef->constants[i];
                std::string decl(constant.declaration); // e.g. "kMaxValue as Integer = 100"
                size_t eqPos = decl.find('=');
                if (eqPos != std::string::npos) {
                    std::string valueStr = decl.substr(eqPos + 1);
                    // Trim trailing whitespace
                    valueStr.erase(valueStr.find_last_not_of(" \t\r\n") + 1);
                    int constValue = std::stoi(valueStr);
                    // Extract constant name (assumed to be the first token)
                    std::istringstream iss(decl);
                    std::string constName;
                    iss >> constName; // e.g. "kMaxValue"
                    std::string propName = toLower(constName);
                    // Optionally remove a leading 'k'
                    if (!propName.empty() && propName[0] == 'k')
                        propName = propName.substr(1);
                    pluginClass->properties.push_back({ propName, Value(constValue) });
                }
            }
            // Define the plugin class in the environment.
            vm.environment->define(toLower(pluginClass->name), Value(pluginClass));
            debugLog("Loaded plugin class: " + pluginClass->name + " from " + libPath);

            // Also register the event callback registration function.
            std::string setEventCallbackKey = toLower(pluginClass->name) + "_seteventcallback";
            auto methodIt = pluginClass->methods.find(setEventCallbackKey);
            if (methodIt != pluginClass->methods.end()) {
                vm.environment->define(setEventCallbackKey, methodIt->second);
                debugLog("Registered event callback setter as global: " + setEventCallbackKey);
            } else {
                debugLog("Warning: Event callback setter " + setEventCallbackKey + " not found in class methods.");
            }
        } else {
            debugLog("Library " + libPath + " does not export GetPluginEntries or GetClassDefinition.");
        }
    }
}

// Loads plugin libraries from the "libs" folder (located beside the executable)
// using cross-platform directory listing and the helper functions above.
void loadPlugins(VM& vm) {
    std::string exeDir = getExecutableDir();
    std::string libsDir = exeDir + PATH_SEPARATOR + "libs" + PATH_SEPARATOR;

#ifdef _WIN32
    std::string pattern = libsDir + "*.dll";
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string libPath = libsDir + findData.cFileName;
            processPluginLibrary(libPath, vm);
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    } else {
        debugLog("No plugins found in " + libsDir);
    }
#else
    DIR* dir = opendir(libsDir.c_str());
    if (!dir) {
        debugLog("Failed to open libs directory: " + libsDir);
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
#ifdef __APPLE__
        if (filename.size() >= 6 && filename.substr(filename.size() - 6) == ".dylib")
#else
        if (filename.size() >= 3 && filename.substr(filename.size() - 3) == ".so")
#endif
        {
            std::string libPath = libsDir + filename;
            processPluginLibrary(libPath, vm);
        }
    }
    closedir(dir);
#endif
}


// ============================================================================  
// Compiler
// ============================================================================
class Compiler {
public:
    Compiler(VM& virtualMachine) : vm(virtualMachine), compilingModule(false) {}
    void compile(const std::vector<std::shared_ptr<Stmt>>& stmts) {
        for (auto stmt : stmts) {
            compileStmt(stmt, vm.mainChunk);
            debugLog("Compiler: Compiled a statement. Main chunk now has " +
                std::to_string(vm.mainChunk.code.size()) + " instructions.");
        }
        // patch unresolved gotos in main chunk
        for (auto& f : gotoFixups) {
            if (labelTable.find(f.label) == labelTable.end())
                runtimeError("Undefined label: " + f.label);
            vm.mainChunk.code[f.patchIndex] = labelTable[f.label];
        }
        labelTable.clear();
        gotoFixups.clear();
    }
private:
    VM& vm;
    bool compilingModule; // Flag indicating if compiling a module
    std::string currentModuleName; // Current module name
    std::unordered_map<std::string, Value> currentModulePublicMembers;  // Public members of current module

    //
    struct Fixup { std::string label; int patchIndex; };
    std::unordered_map<std::string,int> labelTable;
    std::vector<Fixup> gotoFixups;
    //

    void emit(ObjFunction::CodeChunk& chunk, int byte) {
        chunk.code.push_back(byte);
    }

    void emitWithOperand(ObjFunction::CodeChunk& chunk, int opcode, int operand) {
        emit(chunk, opcode);
        emit(chunk, operand);
    }

    void compileStmt(std::shared_ptr<Stmt> stmt, ObjFunction::CodeChunk& chunk) {
        if (auto modStmt = std::dynamic_pointer_cast<ModuleStmt>(stmt)) {
            auto previousEnv = vm.environment;
            auto moduleEnv = std::make_shared<Environment>(previousEnv);
            vm.environment = moduleEnv;
            bool oldCompilingModule = compilingModule;
            compilingModule = true;
            currentModuleName = toLower(modStmt->name);
            currentModulePublicMembers.clear();
            for (auto s : modStmt->body)
                compileStmt(s, chunk);
            auto moduleObj = std::make_shared<ObjModule>();
            moduleObj->name = currentModuleName;
            moduleObj->publicMembers = currentModulePublicMembers;
            vm.environment = previousEnv;
            compilingModule = oldCompilingModule;
            vm.environment->define(toLower(currentModuleName), Value(moduleObj));
            for (auto& entry : currentModulePublicMembers) {
                vm.environment->define(entry.first, entry.second);
            }
            return;
        }
        // AFTER – add inside compileStmt switch chain
        else if (auto label = std::dynamic_pointer_cast<LabelStmt>(stmt)) {
            labelTable[label->name] = chunk.code.size();
        }
        else if (auto gs = std::dynamic_pointer_cast<GotoStmt>(stmt)) {
            int pos = chunk.code.size();
            emitWithOperand(chunk, OP_JUMP, 0);           // placeholder
            gotoFixups.push_back({ gs->label, pos + 1 }); // operand cell to patch
        }
        else if (auto declStmt = std::dynamic_pointer_cast<DeclareStmt>(stmt)) {
            compileDeclare(declStmt, chunk);
        }
        else if (auto enumStmt = std::dynamic_pointer_cast<EnumStmt>(stmt)) {
            auto enumObj = std::make_shared<ObjEnum>();
            enumObj->name = toLower(enumStmt->name);
            enumObj->members = enumStmt->members;
            if (!compilingModule) {
                int nameConst = addConstantString(chunk, toLower(enumStmt->name));
                int enumConstant = addConstant(chunk, Value(enumObj));
                emitWithOperand(chunk, OP_CONSTANT, enumConstant);
                emitWithOperand(chunk, OP_DEFINE_GLOBAL, nameConst);
            }
            else {
                currentModulePublicMembers[toLower(enumStmt->name)] = Value(enumObj);
                vm.environment->define(toLower(enumStmt->name), Value(enumObj));
            }
        }
        else if (auto exprStmt = std::dynamic_pointer_cast<ExpressionStmt>(stmt)) {
            compileExpr(exprStmt->expression, chunk);
            emit(chunk, OP_POP);
        }
        else if (auto retStmt = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
            if (retStmt->value)
                compileExpr(retStmt->value, chunk);
            else
                emit(chunk, OP_NIL);
            emit(chunk, OP_RETURN);
        }

        else if (auto funcStmt = std::dynamic_pointer_cast<FunctionStmt>(stmt)) {
            //
            // ─── 1) Extension-method registration ───────────────────────────────
            //
            if (funcStmt->isExtension) {

                // ── NEW -----------------------------------------------
                // ① create a dummy ObjFunction so the name exists
                auto placeholder = std::make_shared<ObjFunction>();
                placeholder->name = funcStmt->name;

                int required = 0;
                for (auto &p : funcStmt->params)
                    if (!p.optional) required++;
                placeholder->arity  = required;
                placeholder->params = funcStmt->params;

                // ② register the placeholder (define, *not* assign)
                vm.environment->define(
                    toLower(funcStmt->name),
                    Value(placeholder)
                );
                // ─────────────────────────────────────────────────────

                // ③ compile the real body and overwrite the placeholder
                compileFunction(funcStmt);
                vm.environment->assign(
                    toLower(funcStmt->name),
                    Value(lastFunction)
                );


                // (b) Grab the compiled function Value
                Value fnVal = vm.environment->get(toLower(funcStmt->name));
                // (c) Wrap it so that calls insert the receiver as the first argument
                std::string extName = funcStmt->name;              // <-- capture the name

                BuiltinFn extWrapper =
                        [fnVal,
                        receiverName = funcStmt->extendedParam]   // capture “a” in Extends a As …
                        (const std::vector<Value>& args) -> Value
                    {
                        /* args[0] is the receiver, args[1…] are the regular parameters */

                        // dispatching host built-ins is unchanged
                        if (holds<BuiltinFn>(fnVal))
                            return getVal<BuiltinFn>(fnVal)(args);

                        /* scripted function */
                        auto fn = getVal<std::shared_ptr<ObjFunction>>(fnVal);

                        size_t total    = fn->params.size();
                        size_t required = fn->arity;

                        if (args.size() - 1 < required || args.size() - 1 > total)
                            runtimeError("Extension " + fn->name + " expects between " +
                                        std::to_string(required) + " and " + std::to_string(total) +
                                        " argument(s) after the receiver.");

                        VM temp;
                        temp.globals     = std::make_shared<Environment>();
                        temp.environment = temp.globals;

                        /* 1. bind the receiver (“a” in the user’s code) */
                        temp.environment->define(receiverName, args[0]);

                        /* 2. bind the declared parameters */
                        for (size_t i = 0; i < fn->params.size(); ++i) {
                            Value actual = (i + 1 < args.size())
                                        ? args[i + 1]
                                        : fn->params[i].defaultValue;
                            temp.environment->define(fn->params[i].name, actual);
                        }

                        return runVM(temp, fn->chunk);
                    };



                // (d) Store in VM.registry[type][method]
                // vm.extensionMethods
                // [ funcStmt->extendedType ]              // e.g. "string"
                // [ toLower(funcStmt->name) ]             // e.g. "contains"
                // = Value(extWrapper);
                vm.extensionMethods
                [ canonicalExtTypeName(funcStmt->extendedType) ]
                [ toLower(funcStmt->name) ]
                = Value(extWrapper);


                // ── NEW: export public extension methods out of the module ──────────
                if (compilingModule && funcStmt->access == AccessModifier::PUBLIC) {
                    currentModulePublicMembers[toLower(funcStmt->name)] =
                        vm.environment->get(toLower(funcStmt->name));
                }
                // (e) Don’t emit a DEFINE_GLOBAL for extension methods
                return;
            }

            //
            // ─── 2) Original non‑extension function compilation ────────────────
            //
            // Create a placeholder so recursive calls resolve
            std::shared_ptr<ObjFunction> placeholder = std::make_shared<ObjFunction>();
            placeholder->name   = funcStmt->name;
            int req = 0;
            for (auto& p : funcStmt->params)
                if (!p.optional) req++;
            placeholder->arity  = req;
            placeholder->params = funcStmt->params;
            vm.environment->define(
                toLower(funcStmt->name),
                Value(placeholder)
            );

            // Actually compile the body
            compileFunction(funcStmt);
            // Replace placeholder with real function
            vm.environment->assign(
                toLower(funcStmt->name),
                Value(lastFunction)
            );

            if (!compilingModule) {
                // Emit as a global
                int fnConst   = addConstant(chunk, vm.environment->get(toLower(funcStmt->name)));
                emitWithOperand(chunk, OP_CONSTANT, fnConst);
                int nameConst = addConstantString(chunk, toLower(funcStmt->name));
                emitWithOperand(chunk, OP_DEFINE_GLOBAL, nameConst);
            }
            else {
                // Module‑scoped
                if (funcStmt->access == AccessModifier::PUBLIC) {
                    currentModulePublicMembers
                    [ toLower(funcStmt->name) ]
                    = vm.environment->get(toLower(funcStmt->name));
                }
            }
        }

        
        else if (auto varStmt = std::dynamic_pointer_cast<VarStmt>(stmt)) {
            if (varStmt->initializer)
                compileExpr(varStmt->initializer, chunk);
            else {

                //TODO: Implememnt array() returns...

                if (varStmt->varType == "integer" || varStmt->varType == "double")
                    compileExpr(std::make_shared<LiteralExpr>(0), chunk);
                else if (varStmt->varType == "boolean")
                    compileExpr(std::make_shared<LiteralExpr>(false), chunk);
                else if (varStmt->varType == "string")
                    compileExpr(std::make_shared<LiteralExpr>(std::string("")), chunk);
                else if (varStmt->varType == "color")
                    compileExpr(std::make_shared<LiteralExpr>(Color{ 0 }), chunk);
                else if (varStmt->varType == "array")
                    compileExpr(std::make_shared<LiteralExpr>(Value(std::make_shared<ObjArray>())), chunk);
                else if (varStmt->varType == "pointer" || varStmt->varType == "ptr")
                    compileExpr(std::make_shared<LiteralExpr>(static_cast<void*>(nullptr)), chunk);
                else
                    compileExpr(std::make_shared<LiteralExpr>(std::monostate{}), chunk);
            }
            if (!compilingModule) {
                int nameConst = addConstantString(chunk, toLower(varStmt->name));
                emitWithOperand(chunk, OP_DEFINE_GLOBAL, nameConst);
            }
            else {
                if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(varStmt->initializer)) {
                    if (varStmt->access == AccessModifier::PUBLIC) {
                        currentModulePublicMembers[toLower(varStmt->name)] = lit->value;
                    }
                    vm.environment->define(toLower(varStmt->name), lit->value);
                }
            }
        }
        else if (auto classStmt = std::dynamic_pointer_cast<ClassStmt>(stmt)) {
            int nameConst = addConstantString(chunk, toLower(classStmt->name));
            emitWithOperand(chunk, OP_CLASS, nameConst);
            for (auto method : classStmt->methods) {
                compileFunction(method);
                int fnConst = addConstant(chunk, Value(lastFunction));
                emitWithOperand(chunk, OP_CONSTANT, fnConst);
                int methodNameConst = addConstantString(chunk, toLower(method->name));
                emitWithOperand(chunk, OP_METHOD, methodNameConst);
            }
            if (!classStmt->properties.empty()) {
                int propConst = addConstant(chunk, Value(classStmt->properties));
                emitWithOperand(chunk, OP_PROPERTIES, propConst);
            }
            int classNameConst = addConstantString(chunk, toLower(classStmt->name));
            emitWithOperand(chunk, OP_DEFINE_GLOBAL, classNameConst);
        }
        else if (auto propAssign = std::dynamic_pointer_cast<PropertyAssignmentStmt>(stmt)) {
            compileExpr(propAssign->object, chunk);
            compileExpr(propAssign->value, chunk);
            int propConst = addConstantString(chunk, toLower(propAssign->property));
            emitWithOperand(chunk, OP_SET_PROPERTY, propConst);
            emit(chunk, OP_POP);
        }
        else if (auto assignStmt = std::dynamic_pointer_cast<AssignmentStmt>(stmt)) {
            compileExpr(std::make_shared<VariableExpr>(assignStmt->name), chunk);
            compileExpr(assignStmt->value, chunk);
            int nameConst = addConstantString(chunk, toLower(assignStmt->name));
            emitWithOperand(chunk, OP_SET_GLOBAL, nameConst);
            emit(chunk, OP_POP);   // <— pop the old LHS value off the stack
        }
        else if (auto setProp = std::dynamic_pointer_cast<SetPropExpr>(stmt)) {
            compileExpr(setProp->object, chunk);
            compileExpr(setProp->value, chunk);
            int propConst = addConstantString(chunk, toLower(setProp->name));
            emitWithOperand(chunk, OP_SET_PROPERTY, propConst);
            emit(chunk, OP_POP);
        }
        else if (auto ifStmt = std::dynamic_pointer_cast<IfStmt>(stmt)) {
            compileExpr(ifStmt->condition, chunk);
            int jumpIfFalsePos = chunk.code.size();
            emitWithOperand(chunk, OP_JUMP_IF_FALSE, 0);
            for (auto thenStmt : ifStmt->thenBranch)
                compileStmt(thenStmt, chunk);
            int jumpPos = chunk.code.size();
            emitWithOperand(chunk, OP_JUMP, 0);
            int elseStart = chunk.code.size();
            chunk.code[jumpIfFalsePos + 1] = elseStart;
            for (auto elseStmt : ifStmt->elseBranch)
                compileStmt(elseStmt, chunk);
            int endIf = chunk.code.size();
            chunk.code[jumpPos + 1] = endIf;
        }
        else if (auto whileStmt = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
            int loopStart = chunk.code.size();
            compileExpr(whileStmt->condition, chunk);
            int exitJumpPos = chunk.code.size();
            emitWithOperand(chunk, OP_JUMP_IF_FALSE, 0);
            for (auto bodyStmt : whileStmt->body)
                compileStmt(bodyStmt, chunk);
            emitWithOperand(chunk, OP_JUMP, loopStart);
            int loopEnd = chunk.code.size();
            chunk.code[exitJumpPos + 1] = loopEnd;
        }
        else if (auto blockStmt = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
            for (auto s : blockStmt->statements)
                compileStmt(s, chunk);
        }
    }

    // compileDeclare for API declarations using libffi
    void compileDeclare(std::shared_ptr<DeclareStmt> declStmt, ObjFunction::CodeChunk& chunk) {
        BuiltinFn apiFunc = wrapPluginFunctionForDeclare(
            declStmt->params,
            declStmt->returnType,
            declStmt->apiName,
            declStmt->libraryName
        );
        vm.environment->define(toLower(declStmt->apiName), Value(apiFunc));
        if (!compilingModule) {
            int fnConst = addConstant(chunk, vm.environment->get(toLower(declStmt->apiName)));
            emitWithOperand(chunk, OP_CONSTANT, fnConst);
            int nameConst = addConstantString(chunk, toLower(declStmt->apiName));
            emitWithOperand(chunk, OP_DEFINE_GLOBAL, nameConst);
        }
        else {
            currentModulePublicMembers[toLower(declStmt->apiName)] = vm.environment->get(toLower(declStmt->apiName));
        }
    }

    void compileExpr(std::shared_ptr<Expr> expr, ObjFunction::CodeChunk& chunk) {
        if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(expr)) {
            int constIndex = addConstant(chunk, lit->value);
            emitWithOperand(chunk, OP_CONSTANT, constIndex);
        }
        else if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
            int nameConst = addConstantString(chunk, toLower(var->name));
            emitWithOperand(chunk, OP_GET_GLOBAL, nameConst);
        }
        else if (auto un = std::dynamic_pointer_cast<UnaryExpr>(expr)) {
            compileExpr(un->right, chunk);

            if (un->op == "-") {
                emit(chunk, OP_NEGATE);
            } else if (un->op == "not") {
                emit(chunk, OP_NOT);
            } else {
                runtimeError("Compiler: Unknown unary operator: " + un->op);
            }
        }
        else if (auto assignExpr = std::dynamic_pointer_cast<AssignmentExpr>(expr)) {
            compileExpr(std::make_shared<VariableExpr>(assignExpr->name), chunk);
            compileExpr(assignExpr->value, chunk);
            int nameConst = addConstantString(chunk, toLower(assignExpr->name));
            emitWithOperand(chunk, OP_SET_GLOBAL, nameConst);
        }
        else if (auto setProp = std::dynamic_pointer_cast<SetPropExpr>(expr)) {
            compileExpr(setProp->object, chunk);
            compileExpr(setProp->value, chunk);
            int propConst = addConstantString(chunk, toLower(setProp->name));
            emitWithOperand(chunk, OP_SET_PROPERTY, propConst);
            emit(chunk, OP_POP);   // <— drop the instance that SET_PROPERTY pushed back
        }
        else if (auto bin = std::dynamic_pointer_cast<BinaryExpr>(expr)) {
            compileExpr(bin->left, chunk);
            compileExpr(bin->right, chunk);
            switch (bin->op) {
            case BinaryOp::ADD: emit(chunk, OP_ADD); break;
            case BinaryOp::SUB: emit(chunk, OP_SUB); break;
            case BinaryOp::MUL: emit(chunk, OP_MUL); break;
            case BinaryOp::DIV: emit(chunk, OP_DIV); break;
            case BinaryOp::LT:  emit(chunk, OP_LT); break;
            case BinaryOp::LE:  emit(chunk, OP_LE); break;
            case BinaryOp::GT:  emit(chunk, OP_GT); break;
            case BinaryOp::GE:  emit(chunk, OP_GE); break;
            case BinaryOp::NE:  emit(chunk, OP_NE); break;
            case BinaryOp::EQ:  emit(chunk, OP_EQ); break;
            case BinaryOp::AND: emit(chunk, OP_AND); break;
            case BinaryOp::OR:  emit(chunk, OP_OR); break;
            case BinaryOp::XOR: emit(chunk, OP_XOR); break;
            case BinaryOp::POW: emit(chunk, OP_POW); break;
            case BinaryOp::MOD: emit(chunk, OP_MOD); break;
            default: break;
            }
        }
        else if (auto group = std::dynamic_pointer_cast<GroupingExpr>(expr)) {
            compileExpr(group->expression, chunk);
        }
        else if (auto call = std::dynamic_pointer_cast<CallExpr>(expr)) {
            // If the callee is a known script function with ByRef parameters, compile
            // matching arguments as references (OP_GET_REF) instead of values.
            std::vector<Param> calleeParams;
            bool haveSig = false;

            if (auto calleeVar = std::dynamic_pointer_cast<VariableExpr>(call->callee)) {
                std::string calleeName = toLower(calleeVar->name);
                Value raw;
                if (vm.environment->tryGetRaw(calleeName, raw)) {
                    // Direct function
                    if (holds<std::shared_ptr<ObjFunction>>(raw)) {
                        auto fn = getVal<std::shared_ptr<ObjFunction>>(raw);
                        if (fn) {
                            calleeParams = fn->params;
                            haveSig = true;
                        }
                    }
                    // Overloads
                    else if (holds<std::vector<std::shared_ptr<ObjFunction>>>(raw)) {
                        auto fns = getVal<std::vector<std::shared_ptr<ObjFunction>>>(raw);
                        int argc = (int)call->arguments.size();
                        for (auto& f : fns) {
                            if (!f) continue;
                            int minArgs = 0;
                            for (auto& p : f->params) {
                                if (!p.optional) minArgs++;
                            }
                            if (argc >= minArgs && argc <= (int)f->params.size()) {
                                calleeParams = f->params;
                                haveSig = true;
                                break;
                            }
                        }
                    }
                }
            }

            compileExpr(call->callee, chunk);

            for (size_t i = 0; i < call->arguments.size(); i++) {
                bool wantByRef = haveSig && (i < calleeParams.size()) && calleeParams[i].byRef;

                if (wantByRef) {
                    // ByRef arguments must be addressable variables for now.
                    if (auto v = std::dynamic_pointer_cast<VariableExpr>(call->arguments[i])) {
                        int nameConst = addConstantString(chunk, toLower(v->name));
                        emitWithOperand(chunk, OP_GET_REF, nameConst);
                    } else {
                        runtimeError("ByRef argument must be a variable name.");
                    }
                } else {
                    compileExpr(call->arguments[i], chunk);
                }
            }

            emitWithOperand(chunk, OP_CALL, call->arguments.size());
        }
        else if (auto arrLit = std::dynamic_pointer_cast<ArrayLiteralExpr>(expr)) {
            for (auto& elem : arrLit->elements)
                compileExpr(elem, chunk);
            emitWithOperand(chunk, OP_ARRAY, arrLit->elements.size());
        }
        else if (auto getProp = std::dynamic_pointer_cast<GetPropExpr>(expr)) {
            compileExpr(getProp->object, chunk);
            int propConst = addConstantString(chunk, toLower(getProp->name));
            emitWithOperand(chunk, OP_GET_PROPERTY, propConst);
        }
        else if (auto newExpr = std::dynamic_pointer_cast<NewExpr>(expr)) {
            int classConst = addConstantString(chunk, toLower(newExpr->className));
            emitWithOperand(chunk, OP_GET_GLOBAL, classConst);
            emit(chunk, OP_NEW);
        
            /* -------- constructor dispatch -------- */
            emit(chunk, OP_DUP);                       // instance
            int consName = addConstantString(chunk, "constructor");
            emitWithOperand(chunk, OP_GET_PROPERTY, consName);   // push constructor (or nil)
        
            /* NEW: push each argument */
            for (auto &arg : newExpr->arguments)
                compileExpr(arg, chunk);
        
            emitWithOperand(chunk, OP_OPTIONAL_CALL, (int)newExpr->arguments.size());
            emit(chunk, OP_CONSTRUCTOR_END);
        }
        
    }

    std::shared_ptr<ObjFunction> lastFunction;

    void compileFunction(std::shared_ptr<FunctionStmt> funcStmt) {
        auto function = std::make_shared<ObjFunction>();
        function->name = funcStmt->name;
        int req = 0;
        for (auto& p : funcStmt->params)
            if (!p.optional) req++;
        function->arity = req;
        function->params = funcStmt->params;
        ObjFunction::CodeChunk fnChunk;
        labelTable.clear();
        gotoFixups.clear();
        for (auto stmt : funcStmt->body){
            compileStmt(stmt, fnChunk);
        }
        for (auto& f : gotoFixups) {
            if (labelTable.find(f.label) == labelTable.end())
                runtimeError("Undefined label: " + f.label + " in function " + function->name);
            fnChunk.code[f.patchIndex] = labelTable[f.label];
        }
        
        emit(fnChunk, OP_NIL);
        emit(fnChunk, OP_RETURN);
        function->chunk = fnChunk;
        lastFunction = function;
        debugLog("Compiler: Compiled function: " + function->name + " with required arity " + std::to_string(function->arity));
    }
};

// choose the first overload whose required/total arity matches |args|
static Value resolveOverload(const Value &candidate,
                             const std::vector<Value> &args)
{
    if (!holds<std::vector<std::shared_ptr<ObjFunction>>>(candidate))
        return candidate; // already a single fn

    const auto &overloads =
        getVal<std::vector<std::shared_ptr<ObjFunction>>>(candidate);

    for (auto &f : overloads)
    {
        int total = f->params.size();
        int required = f->arity;
        if ((int)args.size() >= required && (int)args.size() <= total)
            return Value(f);
    }
    runtimeError("VM: no matching overload for call with " + std::to_string(args.size()) + " argument(s).");
    return Value(std::monostate{});
}

// ============================================================================  
// Virtual Machine Execution
// ============================================================================
Value runVM(VM& vm, const ObjFunction::CodeChunk& chunk) {
    int ip = 0;
    while (ip < chunk.code.size()) {

        // Process any pending callbacks from plugin events for any yielded threads.
        processPendingCallbacks();

        int currentIp = ip;
        int instruction = chunk.code[ip++];

        debugLog("VM: IP " + std::to_string(currentIp) + ": Executing " + opcodeToString(instruction));

        switch (instruction) {
        case OP_CONSTANT: {
            int index = chunk.code[ip++];
            Value constant = chunk.constants[index];
            vm.stack.push_back(constant);
            debugLog("VM: Loaded constant: " + valueToString(constant));
            break;
        }
        case OP_ADD: {
            Value b = pop(vm), a = pop(vm);
            if (holds<int>(a) && holds<int>(b))
                vm.stack.push_back(getVal<int>(a) + getVal<int>(b));
            else if (holds<double>(a) || holds<double>(b)) {
                double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
                double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
                vm.stack.push_back(ad + bd);
            }
            else if (holds<std::string>(a) && holds<std::string>(b))
                vm.stack.push_back(getVal<std::string>(a) + getVal<std::string>(b));
            else runtimeError("VM: Operands must be numbers or strings for addition.");
            break;
        }
        case OP_SUB: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for subtraction.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) - getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad - bd);
    }
    break;
}
        case OP_MUL: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for multiplication.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) * getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad * bd);
    }
    break;
}
        case OP_DIV: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for division.");

    double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
    double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
    vm.stack.push_back(ad / bd);
    break;
}
        case OP_NEGATE: {
            Value v = pop(vm);
            if (holds<int>(v))
                vm.stack.push_back(-getVal<int>(v));
            else if (holds<double>(v))
                vm.stack.push_back(-getVal<double>(v));
            else runtimeError("VM: Operand must be a number for negation.");
            break;
        }
        case OP_POW: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for exponentiation.");

    double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
    double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
    vm.stack.push_back(std::pow(ad, bd));
    break;
}
        case OP_MOD: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for modulo.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) % getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(std::fmod(ad, bd));
    }
    break;
}
        case OP_LT: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for comparison.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) < getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad < bd);
    }
    break;
}
        case OP_LE: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for comparison.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) <= getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad <= bd);
    }
    break;
}
        case OP_GT: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for comparison.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) > getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad > bd);
    }
    break;
}
        case OP_GE: {
    Value b = pop(vm), a = pop(vm);

    const bool aNum = holds<int>(a) || holds<double>(a);
    const bool bNum = holds<int>(b) || holds<double>(b);
    if (!aNum || !bNum)
        runtimeError("VM: Operands must be numbers for comparison.");

    if (holds<int>(a) && holds<int>(b))
        vm.stack.push_back(getVal<int>(a) >= getVal<int>(b));
    else {
        double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
        double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
        vm.stack.push_back(ad >= bd);
    }
    break;
}
        case OP_EQ: {
            Value b = pop(vm), a = pop(vm);
        
            /* ──────────  numbers  ────────── */
            if (holds<int>(a) && holds<int>(b))
                vm.stack.push_back(getVal<int>(a) == getVal<int>(b));
            else if (holds<double>(a) || holds<double>(b)) {
                double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
                double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
                vm.stack.push_back(ad == bd);
            }
            /* ──────────  simple scalars  ────────── */
            else if (holds<bool>(a)  && holds<bool>(b))
                vm.stack.push_back(getVal<bool>(a)  == getVal<bool>(b));
            else if (holds<std::string>(a) && holds<std::string>(b))
                vm.stack.push_back(getVal<std::string>(a) == getVal<std::string>(b));
        
            /* ──────────  NEW:  Color literals  ────────── */
            else if (holds<Color>(a) && holds<Color>(b))
                vm.stack.push_back(getVal<Color>(a).value == getVal<Color>(b).value);
        
            /* ──────────  NEW:  reference / pointer types  ────────── */
            else if (holds<std::shared_ptr<ObjInstance>>(a) && holds<std::shared_ptr<ObjInstance>>(b))
                vm.stack.push_back(getVal<std::shared_ptr<ObjInstance>>(a) == getVal<std::shared_ptr<ObjInstance>>(b));
            else if (holds<std::shared_ptr<ObjClass>>(a)     && holds<std::shared_ptr<ObjClass>>(b))
                vm.stack.push_back(getVal<std::shared_ptr<ObjClass>>(a)     == getVal<std::shared_ptr<ObjClass>>(b));
            else if (holds<void*>(a) && holds<void*>(b))
                vm.stack.push_back(getVal<void*>(a) == getVal<void*>(b));
        
            /* ──────────  fallback  ────────── */
            else
                vm.stack.push_back(false);
            break;
        }
        
        case OP_NE: {
            Value b = pop(vm), a = pop(vm);
        
            if (holds<int>(a) && holds<int>(b))
                vm.stack.push_back(getVal<int>(a) != getVal<int>(b));
            else if (holds<double>(a) || holds<double>(b)) {
                double ad = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
                double bd = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
                vm.stack.push_back(ad != bd);
            }
            else if (holds<bool>(a)  && holds<bool>(b))
                vm.stack.push_back(getVal<bool>(a)  != getVal<bool>(b));
            else if (holds<std::string>(a) && holds<std::string>(b))
                vm.stack.push_back(getVal<std::string>(a) != getVal<std::string>(b));
        
            /* NEW comparisons mirror OP_EQ */
            else if (holds<Color>(a) && holds<Color>(b))
                vm.stack.push_back(getVal<Color>(a).value != getVal<Color>(b).value);
            else if (holds<std::shared_ptr<ObjInstance>>(a) && holds<std::shared_ptr<ObjInstance>>(b))
                vm.stack.push_back(getVal<std::shared_ptr<ObjInstance>>(a) != getVal<std::shared_ptr<ObjInstance>>(b));
            else if (holds<std::shared_ptr<ObjClass>>(a)     && holds<std::shared_ptr<ObjClass>>(b))
                vm.stack.push_back(getVal<std::shared_ptr<ObjClass>>(a)     != getVal<std::shared_ptr<ObjClass>>(b));
            else if (holds<void*>(a) && holds<void*>(b))
                vm.stack.push_back(getVal<void*>(a) != getVal<void*>(b));
            else
                runtimeError("VM: Operands are not comparable for '<>'.");
            break;
        }
        
        case OP_AND: {
            Value b = pop(vm), a = pop(vm);
            bool ab = (holds<bool>(a)) ? getVal<bool>(a) : (holds<int>(a) ? (getVal<int>(a) != 0) : false);
            bool bb = (holds<bool>(b)) ? getVal<bool>(b) : (holds<int>(b) ? (getVal<int>(b) != 0) : false);
            vm.stack.push_back(ab && bb);
            break;
        }
        case OP_OR: {
            Value b = pop(vm), a = pop(vm);
            bool ab = (holds<bool>(a)) ? getVal<bool>(a) : (holds<int>(a) ? (getVal<int>(a) != 0) : false);
            bool bb = (holds<bool>(b)) ? getVal<bool>(b) : (holds<int>(b) ? (getVal<int>(b) != 0) : false);
            vm.stack.push_back(ab || bb);
            break;
        }

        case OP_XOR: {
            Value b = pop(vm), a = pop(vm);

            // Boolean XOR (logical)
            if (holds<bool>(a) && holds<bool>(b)) {
                bool av = getVal<bool>(a);
                bool bv = getVal<bool>(b);
                vm.stack.push_back(Value(av != bv));
                break;
            }

            // Integer XOR (bitwise)
            if (holds<int>(a) && holds<int>(b)) {
                vm.stack.push_back(Value(getVal<int>(a) ^ getVal<int>(b)));
                break;
            }

            runtimeError("VM: Xor expects (Boolean, Boolean) or (Integer, Integer).");
            break;
        }


        case OP_PRINT: {
            Value v = pop(vm);
            std::cout << valueToString(v) << std::endl;
            break;
        }
        case OP_POP: {
            debugLog("OP_POP: Attempting to pop a value.");
            if (vm.stack.empty())
                runtimeError("VM: Stack underflow on POP.");
            vm.stack.pop_back();
            break;
        }
        case OP_DEFINE_GLOBAL: {
            int nameIndex = chunk.code[ip++];
            if (nameIndex < 0 || nameIndex >= (int)chunk.constants.size())
                runtimeError("VM: Invalid constant index for global name.");
            Value nameVal = chunk.constants[nameIndex];
            if (!holds<std::string>(nameVal))
                runtimeError("VM: Global name must be a string.");
            std::string name = getVal<std::string>(nameVal);
            if (vm.stack.empty())
                runtimeError("VM: Stack underflow on global definition for " + name);
            Value val = pop(vm);
            vm.environment->define(name, val);
            debugLog("VM: Defined global variable: " + name + " = " + valueToString(val));
            break;
        }
        case OP_GET_GLOBAL: {
            int nameIndex = chunk.code[ip++];
            if (nameIndex < 0 || nameIndex >= (int)chunk.constants.size())
                runtimeError("VM: Invalid constant index for global name.");
            Value nameVal = chunk.constants[nameIndex];
            if (!holds<std::string>(nameVal))
                runtimeError("VM: Global name must be a string.");
            std::string name = getVal<std::string>(nameVal);
            if (toLower(name) == "microseconds") {
                auto now = std::chrono::steady_clock::now();
                double us = std::chrono::duration<double, std::micro>(now - startTime).count();
                vm.stack.push_back(us);
                debugLog("VM: Loaded built-in microseconds: " + std::to_string(us));
            }
            else if (toLower(name) == "ticks") {
                auto now = std::chrono::steady_clock::now();
                double seconds = std::chrono::duration<double>(now - startTime).count();
                int ticks = static_cast<int>(seconds * 60);
                vm.stack.push_back(ticks);
                debugLog("VM: Loaded built-in ticks: " + std::to_string(ticks));
            }
            else {
                Value val = vm.environment->get(name);
                vm.stack.push_back(val);
                debugLog("VM: Loaded global variable: " + name + " = " + valueToString(val));
            }
            break;
        }

case OP_GET_REF: {
    int nameIndex = chunk.code[ip++];
    if (nameIndex < 0 || nameIndex >= (int)chunk.constants.size())
        runtimeError("VM: Invalid constant index for ref name.");
    Value nameVal = chunk.constants[nameIndex];
    if (!holds<std::string>(nameVal))
        runtimeError("VM: Ref name must be a string.");
    std::string name = getVal<std::string>(nameVal);

    // ByRef requires an addressable variable cell.
    Value* cell = vm.environment->getCell(name);
    auto r = std::make_shared<ObjRef>();
    r->target = cell;
    vm.stack.push_back(Value(r));
    debugLog("VM: Loaded ref for variable: " + name);
    break;
}
        case OP_SET_GLOBAL: {
            int nameIndex = chunk.code[ip++];
            if (nameIndex < 0 || nameIndex >= (int)chunk.constants.size())
                runtimeError("VM: Invalid constant index for global name.");
            Value nameVal = chunk.constants[nameIndex];
            if (!holds<std::string>(nameVal))
                runtimeError("VM: Global name must be a string.");
            std::string name = getVal<std::string>(nameVal);
            Value newVal = pop(vm);
            vm.environment->assign(name, newVal);
            debugLog("VM: Set global variable: " + name + " = " + valueToString(newVal));
            break;
        }
        case OP_NEW: {
            Value classVal = pop(vm);
            if (!holds<std::shared_ptr<ObjClass>>(classVal))
                runtimeError("VM: 'new' applied to non-class.");
            auto cls = getVal<std::shared_ptr<ObjClass>>(classVal);
            if (cls->isPlugin) {
                // For plugin classes, call the pluginConstructor to create a new instance.
                Value result = cls->pluginConstructor({});
                auto instance = std::make_shared<ObjInstance>();
                instance->klass = cls;
                // If the constructor returned an integer handle, store it by converting to void*
                if (holds<int>(result)) {
                    int handle = getVal<int>(result);
                    instance->pluginInstance = reinterpret_cast<void*>(static_cast<intptr_t>(handle));
                } else {
                    // Otherwise, use the returned pointer as is.
                    instance->pluginInstance = getVal<void*>(result);
                }
                // Copy the plugin class properties into the instance's environment fields
                for (auto& prop : cls->properties) {
                    instance->fields[prop.first] = prop.second;
                }
                vm.stack.push_back(Value(instance));
            } else {
                // For built-in classes, use the standard instance creation.
                auto instance = std::make_shared<ObjInstance>();
                instance->klass = cls;
                for (auto& p : cls->properties) {
                    instance->fields[p.first] = p.second;
                }
                vm.stack.push_back(Value(instance));
            }
            break;
        }
        

        case OP_DUP: {
            if (vm.stack.empty())
                runtimeError("VM: Stack underflow on DUP.");
            vm.stack.push_back(vm.stack.back());
            break;
        }


        case OP_CALL: {
            // Number of arguments to pop
            int argCount = chunk.code[ip++];
            std::vector<Value> args;
            // Pop arguments off the stack
            for (int i = 0; i < argCount; i++) {
                args.push_back(pop(vm));
            }
            // Reverse so args[0] is the first argument
            std::reverse(args.begin(), args.end());
        
            // Pop the callable
            Value callee = pop(vm);
            debugLog("VM: Calling function with " + std::to_string(argCount) + " arguments.");
        
            // ---------------------------  BUILTIN  -----------------------------------
            if (holds<BuiltinFn>(callee)) {
                BuiltinFn fn = getVal<BuiltinFn>(callee);
                Value result = fn(args);
                vm.stack.push_back(result);
            }
        
            // -------------------------  SCRIPTED FN  ---------------------------------
            else if (holds<std::shared_ptr<ObjFunction>>(callee)) {
                auto function = getVal<std::shared_ptr<ObjFunction>>(callee);
                int total    = function->params.size();
                int required = function->arity;
                if ((int)args.size() < required || (int)args.size() > total) {
                    runtimeError("VM: Expected between " +
                                 std::to_string(required) +
                                 " and " +
                                 std::to_string(total) +
                                 " arguments for function " +
                                 function->name);
                }
        
                // Fill in optional parameters
                for (int i = args.size(); i < total; i++) {
                    args.push_back(function->params[i].defaultValue);
                }
        
                // Remember stack depth
                size_t savedDepth = vm.stack.size();
        
                // Swap in new environment
                auto previousEnv = vm.environment;
                vm.environment = std::make_shared<Environment>(previousEnv);
        
                // Bind parameters
                for (size_t i = 0; i < function->params.size(); i++) {
                    vm.environment->define(function->params[i].name, args[i]);
                }
        
                // Execute
                Value result = runVM(vm, function->chunk);
        
                // Restore environment
                vm.environment = previousEnv;
        
                // Clear any extra stack values left by the call, then push exactly one result
                vm.stack.resize(savedDepth);
                vm.stack.push_back(result);
        
                debugLog("VM: Function " + function->name + " returned " + valueToString(result));
            }
        
            // ------------------  SCRIPTED OVERLOAD RESOLUTION  -----------------------
            else if (holds<std::vector<std::shared_ptr<ObjFunction>>>(callee)) {
                auto overloads = getVal<std::vector<std::shared_ptr<ObjFunction>>>(callee);
                std::shared_ptr<ObjFunction> chosen = nullptr;
                for (auto& f : overloads) {
                    int total    = f->params.size();
                    int required = f->arity;
                    if ((int)args.size() >= required && (int)args.size() <= total) {
                        chosen = f;
                        break;
                    }
                }
                if (!chosen) {
                    runtimeError("VM: No matching overload found for function call with " +
                                 std::to_string(args.size()) + " arguments.");
                }
        
                // Fill in optional parameters
                for (int i = args.size(); i < (int)chosen->params.size(); i++) {
                    args.push_back(chosen->params[i].defaultValue);
                }
        
                size_t savedDepth = vm.stack.size();
        
                auto previousEnv = vm.environment;
                vm.environment = std::make_shared<Environment>(previousEnv);
                for (size_t i = 0; i < chosen->params.size(); i++) {
                    vm.environment->define(chosen->params[i].name, args[i]);
                }
        
                Value result = runVM(vm, chosen->chunk);
        
                vm.environment = previousEnv;
                vm.stack.resize(savedDepth);
                vm.stack.push_back(result);
        
                debugLog("VM: Function " + chosen->name + " returned " + valueToString(result));
            }
        
            // ----------------------  BOUND METHOD CALL  -------------------------------
            else if (holds<std::shared_ptr<ObjBoundMethod>>(callee)) {
                auto bound = getVal<std::shared_ptr<ObjBoundMethod>>(callee);
        
                // NEW: string extension
                if (holds<std::string>(bound->receiver)) {
                    // look up the extension
                    auto &exts = vm.extensionMethods["string"];
                    auto it   = exts.find(bound->name);
                    if (it == exts.end())
                        runtimeError("No string extension: " + bound->name);
                    // invoke it
                    return getVal<BuiltinFn>(it->second)(
                        /* prepend the receiver */ 
                        [&]{
                            std::vector<Value> x = args;
                            x.insert(x.begin(), bound->receiver);
                            return x;
                        }()
                    );
                }
                /* ── NEW: Integer / Double / Boolean extensions ────────────────── */
                else if (holds<int>(bound->receiver) ||
                        holds<double>(bound->receiver) ||
                        holds<bool>(bound->receiver))
                {
                    std::string typeKey =
                        holds<int>(bound->receiver)    ? "integer" :
                        holds<double>(bound->receiver) ? "double"  : "boolean";

                    auto &exts = vm.extensionMethods[typeKey];
                    auto it    = exts.find(bound->name);
                    if (it == exts.end())
                        runtimeError("No " + typeKey + " extension: " + bound->name);

                    std::vector<Value> newArgs = args;            // the call’s arguments
                    newArgs.insert(newArgs.begin(), bound->receiver);   // prepend receiver

                    Value result = getVal<BuiltinFn>(it->second)(newArgs);
                    vm.stack.push_back(result);
                    break;
                }

                // Instance methods
                if (holds<std::shared_ptr<ObjInstance>>(bound->receiver)) {
                    auto instance = getVal<std::shared_ptr<ObjInstance>>(bound->receiver);
                    std::string key = toLower(bound->name);
                    Value methodVal = instance->klass->methods[key];
        
                    // If it's a BuiltinFn on a plugin class, prepend handle
                    if (holds<BuiltinFn>(methodVal) && instance->klass->isPlugin) {
                        BuiltinFn fn = getVal<BuiltinFn>(methodVal);
                        int handle = static_cast<int>(reinterpret_cast<intptr_t>(instance->pluginInstance));
                        std::vector<Value> newArgs;
                        newArgs.push_back(Value(handle));
                        newArgs.insert(newArgs.end(), args.begin(), args.end());
                        Value result = fn(newArgs);
                        vm.stack.push_back(result);
                    }
                    // If it's a simple BuiltinFn
                    else if (holds<BuiltinFn>(methodVal)) {
                        BuiltinFn fn = getVal<BuiltinFn>(methodVal);
                        Value result = fn(args);
                        vm.stack.push_back(result);
                    }
                    // Otherwise, it's a scripted ObjFunction or overload set
                    else {
                        // Resolve to a single ObjFunction
                        std::shared_ptr<ObjFunction> methodFn = nullptr;
                        if (holds<std::shared_ptr<ObjFunction>>(methodVal)) {
                            methodFn = getVal<std::shared_ptr<ObjFunction>>(methodVal);
                        }
                        else {
                            auto overloads = getVal<std::vector<std::shared_ptr<ObjFunction>>>(methodVal);
                            for (auto& f : overloads) {
                                int total    = f->params.size();
                                int required = f->arity;
                                if ((int)args.size() >= required && (int)args.size() <= total) {
                                    methodFn = f;
                                    break;
                                }
                            }
                        }
                        if (!methodFn) {
                            runtimeError("VM: No matching method found for " + bound->name);
                        }
        
                        size_t savedDepth = vm.stack.size();
        
                        auto previousEnv = vm.environment;
                        vm.environment = std::make_shared<Environment>(previousEnv);
        
                        // Define 'self'
                        if (instance->klass->isPlugin) {
                            int handle = static_cast<int>(reinterpret_cast<intptr_t>(instance->pluginInstance));
                            vm.environment->define("self", Value(handle));
                        } else {
                            vm.environment->define("self", bound->receiver);
                        }
        
                        // Bind parameters
                        for (size_t i = 0; i < methodFn->params.size(); i++) {
                            if (i < args.size()) {
                                vm.environment->define(methodFn->params[i].name, args[i]);
                            } else {
                                vm.environment->define(
                                    methodFn->params[i].name,
                                    methodFn->params[i].defaultValue
                                );
                            }
                        }
        
                        Value result = runVM(vm, methodFn->chunk);
        
                        vm.environment = previousEnv;
                        vm.stack.resize(savedDepth);
                        vm.stack.push_back(result);
        
                        debugLog("VM: Method " + methodFn->name + " returned " + valueToString(result));
                    }
                }
                // Array methods
                else if (holds<std::shared_ptr<ObjArray>>(bound->receiver)) {
                    auto array = getVal<std::shared_ptr<ObjArray>>(bound->receiver);
                    Value result = callArrayMethod(array, bound->name, args);
                    vm.stack.push_back(result);
                }
                else {
                    runtimeError("VM: Bound method receiver is of unsupported type.");
                }
            }

            // -----------------------  ARRAY CALL  -----------------------------------
            else if (holds<std::shared_ptr<ObjArray>>(callee)) {
                auto array = getVal<std::shared_ptr<ObjArray>>(callee);

                /* ---------------- get item ---------------- */
                if (argCount == 1) {
                    Value idx = args[0];
                    if (!holds<int>(idx))
                        runtimeError("VM: Array index must be an Integer.");
                    int i = getVal<int>(idx);
                    if (i < 0 || i >= (int)array->elements.size())
                        runtimeError("VM: Array index out of bounds.");
                    vm.stack.push_back(array->elements[i]);
                }

                /* ---------------- set item ---------------- */
                else if (argCount == 2) {
                    Value idx = args[0];
                    if (!holds<int>(idx))
                        runtimeError("VM: Array index must be an Integer.");
                    int i = getVal<int>(idx);
                    if (i < 0)
                        runtimeError("VM: Array index must be ≥ 0.");

                    /* auto-grow, like Xojo */
                    if (i >= (int)array->elements.size())
                        array->elements.resize(i + 1, Value(std::monostate{}));

                    array->elements[i] = args[1];      // assign
                    vm.stack.push_back(args[1]);       // return the new value
                }

                /* anything else is an error */
                else {
                    runtimeError("VM: Array call expects 1 (get) or 2 (set) arguments.");
                }
            }
        
            // -----------------  STRING-BASED BUILT-INS  ------------------------------
            else if (holds<std::string>(callee)) {
                std::string funcName = toLower(getVal<std::string>(callee));
                if (funcName == "print") {
                    if (args.empty()) runtimeError("VM: print expects an argument.");
                    std::cout << valueToString(args[0]) << std::endl;
                    vm.stack.push_back(args[0]);
                }
                else if (funcName == "str") {
                    if (args.empty()) runtimeError("VM: str expects an argument.");
                    vm.stack.push_back(Value(valueToString(args[0])));
                }
                else if (funcName == "ticks") {
                    auto now = std::chrono::steady_clock::now();
                    double seconds = std::chrono::duration<double>(now - startTime).count();
                    vm.stack.push_back((int)(seconds * 60));
                }
                else if (funcName == "microseconds") {
                    auto now = std::chrono::steady_clock::now();
                    double us = std::chrono::duration<double, std::micro>(now - startTime).count();
                    vm.stack.push_back(us);
                }
                else if (funcName == "val") {
                    if (args.size() != 1) runtimeError("VM: val expects exactly one argument.");
                    if (!holds<std::string>(args[0])) runtimeError("VM: val expects a string argument.");
                    double d = std::stod(getVal<std::string>(args[0]));
                    vm.stack.push_back(d);
                }
                else {
                    runtimeError("VM: Unknown built-in function: " + funcName);
                }
            }
        
            // -----------------------  INVALID CALL  ----------------------------------
            else {
                runtimeError("VM: Can only call functions, methods, arrays, or built-in functions.");
            }
        
            break;
        }
        
        case OP_OPTIONAL_CALL: {
            int argCount = chunk.code[ip++];
            std::vector<Value> args;
            for (int i = 0; i < argCount; i++) {
                args.push_back(pop(vm));
            }
            std::reverse(args.begin(), args.end());
            Value callee = pop(vm);
            debugLog("OP_OPTIONAL_CALL: callee type: " + getTypeName(callee));
            if (holds<std::monostate>(callee)) {
                debugLog("OP_OPTIONAL_CALL: No constructor found; skipping call.");
                vm.stack.push_back(Value(std::monostate{}));   // so constructor_end sees [instance, nil]
            }
            else if (holds<std::shared_ptr<ObjFunction>>(callee)) {
                auto function = getVal<std::shared_ptr<ObjFunction>>(callee);
                int total = function->params.size();
                int required = function->arity;
                if ((int)args.size() < required || (int)args.size() > total)
                    runtimeError("VM: Expected between " + std::to_string(required) + " and " + std::to_string(total) + " arguments for constructor " + function->name);
                for (int i = args.size(); i < total; i++) {
                    args.push_back(function->params[i].defaultValue);
                }
                auto previousEnv = vm.environment;
                vm.environment = std::make_shared<Environment>(previousEnv);
                for (size_t i = 0; i < function->params.size(); i++) {
                    vm.environment->define(function->params[i].name, args[i]);
                }

                Value result = runVM(vm, function->chunk);
                vm.environment = previousEnv;
                debugLog("OP_OPTIONAL_CALL: Constructor function "
                            + function->name + " returned " + valueToString(result));
                
                vm.stack.push_back(result);                  //  ← always push, even if “nil”
                    
            }

           /* ─────────────  NEW: handle bound-methods (scripted or plugin) ───────────── */
            else if (holds<std::shared_ptr<ObjBoundMethod>>(callee)) {

                auto bound = getVal<std::shared_ptr<ObjBoundMethod>>(callee);
                std::string key = toLower(bound->name);

                /* 1.  Receiver is a *scripted* instance -- fetch the target method */
                if (holds<std::shared_ptr<ObjInstance>>(bound->receiver)) {
                    auto instance = getVal<std::shared_ptr<ObjInstance>>(bound->receiver);
                    Value methodVal = instance->klass->methods[key];

                    /* fall back to existing OP_CALL logic ------------------------- */
                    // --- scripted function overload set?
                    if (holds<std::vector<std::shared_ptr<ObjFunction>>>(methodVal))
                        methodVal = resolveOverload(methodVal, args);   // helper you already have

                    /* scripted ObjFunction */
                    if (holds<std::shared_ptr<ObjFunction>>(methodVal)) {

                        auto fn = getVal<std::shared_ptr<ObjFunction>>(methodVal);

                        /* arity / default-arg handling (same as OP_CALL) */
                        int total    = fn->params.size();
                        int required = fn->arity;
                        if ((int)args.size() < required || (int)args.size() > total)
                            runtimeError("VM: Expected between " + std::to_string(required) +
                                        " and " + std::to_string(total) + " arguments for " + fn->name);
                        for (int i = args.size(); i < total; ++i)
                            args.push_back(fn->params[i].defaultValue);

                        auto prevEnv = vm.environment;
                        vm.environment = std::make_shared<Environment>(prevEnv);
                        vm.environment->define("self", bound->receiver);          // bind Self

                        for (size_t i = 0; i < fn->params.size(); ++i)
                            vm.environment->define(fn->params[i].name, args[i]);

                        Value result = runVM(vm, fn->chunk);
                        vm.environment = prevEnv;
                        vm.stack.push_back(result);
                    }
                    /* builtin for plugin instance (shouldn’t happen for “constructor”, but safe) */
                    else if (holds<BuiltinFn>(methodVal)) {
                        BuiltinFn fn = getVal<BuiltinFn>(methodVal);
                        int handle = static_cast<int>(
                                        reinterpret_cast<intptr_t>(
                                            getVal<std::shared_ptr<ObjInstance>>(bound->receiver)->pluginInstance));
                        std::vector<Value> newArgs{ Value(handle) };
                        newArgs.insert(newArgs.end(), args.begin(), args.end());
                        vm.stack.push_back(fn(newArgs));
                    }
                    else {
                        runtimeError("OP_OPTIONAL_CALL: Unsupported bound-method target.");
                    }
                }
                else {
                    runtimeError("OP_OPTIONAL_CALL: Bound-method receiver unsupported.");
                }
            }
            /* ─────────────────────────────────────────────────────────────────────────── */
            else {
                runtimeError("OP_OPTIONAL_CALL: Can only call functions, bound methods, or nil.");
            }

            break;
        }
        case OP_RETURN: {
            Value ret = vm.stack.empty() ? Value(std::monostate{}) : pop(vm);
            return ret;
        }
        case OP_NIL: {
            vm.stack.push_back(Value(std::monostate{}));
            break;
        }
        case OP_JUMP_IF_FALSE: {
            int offset = chunk.code[ip++];
            Value condition = pop(vm);
            bool condTruth = false;
            if (holds<bool>(condition))
                condTruth = getVal<bool>(condition);
            else if (holds<int>(condition))
                condTruth = (getVal<int>(condition) != 0);
            else if (holds<std::string>(condition))
                condTruth = !getVal<std::string>(condition).empty();
            else if (std::holds_alternative<std::monostate>(condition))
                condTruth = false;
            if (!condTruth) {
                ip = offset;
            }
            break;
        }
        case OP_JUMP: {
            int offset = chunk.code[ip++];
            ip = offset;
            break;
        }
        case OP_CLASS: {
            int nameIndex = chunk.code[ip++];
            Value nameVal = chunk.constants[nameIndex];
            if (!holds<std::string>(nameVal))
                runtimeError("VM: Class name must be a string.");
            auto klass = std::make_shared<ObjClass>();
            klass->name = getVal<std::string>(nameVal);
            vm.stack.push_back(Value(klass));
            break;
        }
        case OP_METHOD: {
            int methodNameIndex = chunk.code[ip++];
            Value methodNameVal = chunk.constants[methodNameIndex];
            if (!holds<std::string>(methodNameVal))
                runtimeError("VM: Method name must be a string.");

            // Method being attached (scripted methods are ObjFunction)
            Value newMethodVal = pop(vm);
            if (!holds<std::shared_ptr<ObjFunction>>(newMethodVal))
                runtimeError("VM: Method must be a function.");

            Value classVal = pop(vm);
            if (!holds<std::shared_ptr<ObjClass>>(classVal))
                runtimeError("VM: No class found for method.");

            auto klass = getVal<std::shared_ptr<ObjClass>>(classVal);
            std::string methodName = toLower(getVal<std::string>(methodNameVal));

            auto it = klass->methods.find(methodName);
            if (it == klass->methods.end()) {
                // First definition of this method name.
                klass->methods[methodName] = newMethodVal;
            }
            else {
                // Existing method: create/extend an overload set resolved by arg-count.
                Value &existing = it->second;
                auto newFn = getVal<std::shared_ptr<ObjFunction>>(newMethodVal);

                if (holds<std::shared_ptr<ObjFunction>>(existing)) {
                    auto oldFn = getVal<std::shared_ptr<ObjFunction>>(existing);
                    std::vector<std::shared_ptr<ObjFunction>> overloads;
                    overloads.push_back(oldFn);
                    overloads.push_back(newFn);
                    existing = Value(overloads);
                }
                else if (holds<std::vector<std::shared_ptr<ObjFunction>>>(existing)) {
                    auto overloads = getVal<std::vector<std::shared_ptr<ObjFunction>>>(existing);
                    overloads.push_back(newFn);
                    existing = Value(overloads);
                }
                else if (holds<BuiltinFn>(existing)) {
                    // Keeping behavior strict here avoids surprising changes with plugin/builtin methods.
                    runtimeError("VM: Cannot overload builtin method '" + methodName + "' with a scripted method.");
                }
                else {
                    runtimeError("VM: Unsupported method type for overload set: " + methodName);
                }
            }

            vm.stack.push_back(Value(klass));
            break;
        }
        case OP_PROPERTIES: {
            int propIndex = chunk.code[ip++];
            Value propVal = chunk.constants[propIndex];
            if (!holds<PropertiesType>(propVal))
                runtimeError("VM: Properties must be a property map.");
            auto props = getVal<PropertiesType>(propVal);
            Value classVal = pop(vm);
            if (!holds<std::shared_ptr<ObjClass>>(classVal))
                runtimeError("VM: Properties can only be set on a class object.");
            auto klass = getVal<std::shared_ptr<ObjClass>>(classVal);
            klass->properties = props;
            vm.stack.push_back(Value(klass));
            break;
        }
        case OP_ARRAY: {
            int count = chunk.code[ip++];
            std::vector<Value> elems;
            for (int i = 0; i < count; i++) {
                elems.push_back(pop(vm));
            }
            std::reverse(elems.begin(), elems.end());
            auto array = std::make_shared<ObjArray>();
            array->elements = elems;
            vm.stack.push_back(Value(array));
            debugLog("VM: Created array with " + std::to_string(count) + " elements.");
            break;
        }


        case OP_GET_PROPERTY:
        {
            // constant index of the property name
            int nameIndex = chunk.code[ip++];
            if (nameIndex < 0 || nameIndex >= (int)chunk.constants.size() ||
                !holds<std::string>(chunk.constants[nameIndex]))
            {
                runtimeError("OP_GET_PROPERTY: name constant is not a string");
            }

            std::string name      = getVal<std::string>(chunk.constants[nameIndex]);
            std::string lowerName = toLower(name);

            if (vm.stack.empty())
                runtimeError("OP_GET_PROPERTY: stack underflow");

            // Object whose property/method we’re accessing
            Value receiver = pop(vm);


            // --------------------------------------------------------
            // 0) Enums – EnumName.Member
            // --------------------------------------------------------
            {
                Value enumOut;
                if (tryGetEnumMember(receiver, lowerName, enumOut)) {
                    vm.stack.push_back(enumOut);
                    break;
                }
            }

            // --------------------------------------------------------
            // 1) Instance fields / methods (including plugin classes)
            // --------------------------------------------------------
            if (holds<std::shared_ptr<ObjInstance>>(receiver)) {
                auto inst  = getVal<std::shared_ptr<ObjInstance>>(receiver);
                auto klass = inst ? inst->klass : nullptr;

                // Instance fields first
                if (inst) {
                    auto fIt = inst->fields.find(lowerName);
                    if (fIt != inst->fields.end()) {
                        vm.stack.push_back(fIt->second);
                        break;
                    }
                }

                // Plugin-backed properties (getter)
                if (inst && klass && klass->isPlugin) {
                    auto pit = klass->pluginProperties.find(lowerName);
                    if (pit != klass->pluginProperties.end()) {
                        BuiltinFn getter = pit->second.first;
                        if (!getter)
                            runtimeError("Property '" + name + "' does not have a getter.");

                        int handle = static_cast<int>(reinterpret_cast<intptr_t>(inst->pluginInstance));
                        Value result = getter({ Value(handle) });
                        vm.stack.push_back(result);
                        break;
                    }
                }

                // Class methods
                if (inst && klass) {
                    auto mit = klass->methods.find(lowerName);
                    if (mit != klass->methods.end()) {
                        Value methVal = mit->second;

                        // Plugin methods – bind the handle as the first arg.
                        if (klass->isPlugin && holds<BuiltinFn>(methVal)) {
                            BuiltinFn raw = getVal<BuiltinFn>(methVal);
                            int handle = static_cast<int>(reinterpret_cast<intptr_t>(inst->pluginInstance));
                            BuiltinFn bound = [raw, handle](const std::vector<Value>& args) -> Value {
                                std::vector<Value> full;
                                full.reserve(args.size() + 1);
                                full.emplace_back(handle);
                                full.insert(full.end(), args.begin(), args.end());
                                return raw(full);
                            };
                            vm.stack.push_back(Value(bound));
                            break;
                        }

                        // Scripted method – return bound-method object
                        auto bm = std::make_shared<ObjBoundMethod>();
                        bm->receiver = receiver;
                        bm->name     = lowerName;
                        vm.stack.push_back(Value(bm));
                        break;
                    }
                }

                // If we fall through, we’ll still give extension methods a chance
                // and then the legacy "constructor" / "tostring" / plugin fallback below.
            }

            // --------------------------------------------------------
            // 2) Arrays – expose methods (Add, Count, Join, etc.)
            // --------------------------------------------------------
            if (holds<std::shared_ptr<ObjArray>>(receiver)) {
                auto arr = getVal<std::shared_ptr<ObjArray>>(receiver);

                // Return a callable that dispatches to callArrayMethod(...)
                BuiltinFn bound = [arr, lowerName](const std::vector<Value>& args) -> Value {
                    return callArrayMethod(arr, lowerName, args);
                };
                vm.stack.push_back(Value(bound));
                break;
            }

            // --------------------------------------------------------
            // 3) Modules – module.member
            // --------------------------------------------------------
            if (holds<std::shared_ptr<ObjModule>>(receiver)) {
                auto mod = getVal<std::shared_ptr<ObjModule>>(receiver);
                auto it  = mod->publicMembers.find(lowerName);
                if (it != mod->publicMembers.end()) {
                    vm.stack.push_back(it->second);
                    break;
                }
                // fall through for extension methods / constructor / tostring / plugin fallback
            }

            // --------------------------------------------------------
            // 4) Classes – static methods
            // --------------------------------------------------------
            if (holds<std::shared_ptr<ObjClass>>(receiver)) {
                auto cls = getVal<std::shared_ptr<ObjClass>>(receiver);
                auto mit = cls->methods.find(lowerName);
                if (mit != cls->methods.end()) {
                    vm.stack.push_back(mit->second);
                    break;
                }
                // fall through for extension methods / constructor / tostring / plugin fallback
            }

            // --------------------------------------------------------
            // 5) EXTENSION METHODS on primitives (Integer, Double, String, …)
            // --------------------------------------------------------
            {
                Value ext = bindExtensionMethod(vm, receiver, lowerName);
                if (!holds<std::monostate>(ext)) {
                    // ext is already a callable (BuiltinFn) bound to this receiver.
                    vm.stack.push_back(ext);
                    break;
                }
            }

            // --------------------------------------------------------
            // 5.5) Legacy ".constructor" sentinel
            //
            // Older bytecode and the plugin class machinery sometimes probe
            // `.constructor` just to see if anything is there. Historically this
            // did NOT throw – it pushed a "no ctor" sentinel instead.
            // --------------------------------------------------------
            if (lowerName == "constructor") {
                vm.stack.push_back(Value(std::monostate{})); // "no ctor" sentinel
                break;
            }

            // --------------------------------------------------------
            // 6) Built-in .tostring on core types (after extensions / ctor)
            //
            // Mirrors the old behavior:
            //   - String: returns the raw string (no extra quoting)
            //   - Int/Double/Arrays/Instances/etc: valueToString(receiver)
            // --------------------------------------------------------
            if (lowerName == "tostring") {
                if (holds<std::string>(receiver)) {
                    // Old semantics: for string, just return the string itself.
                    const std::string& s = getVal<std::string>(receiver);
                    vm.stack.push_back(Value(s));
                } else {
                    vm.stack.push_back(Value(valueToString(receiver)));
                }
                break;
            }

            // --------------------------------------------------------
            // 7) Optional: built-in string helpers AFTER extension / tostring
            // --------------------------------------------------------
            if (holds<std::string>(receiver)) {
                const std::string& s = getVal<std::string>(receiver);

                if (lowerName == "length") {
                    vm.stack.push_back((int)s.size());
                    break;
                }
                // Add other built-in string properties/methods here if needed.
            }

            // --------------------------------------------------------
            // 8) FINAL PLUGIN INSTANCE FALLBACK (for things like ".handle")
            //
            // This restores the old behavior:
            //   unknown plugin property ⇒ "ClassName:handle:propName"
            // so that ClassObject.handle and similar patterns work.
            // --------------------------------------------------------
            if (holds<std::shared_ptr<ObjInstance>>(receiver)) {
                auto inst  = getVal<std::shared_ptr<ObjInstance>>(receiver);
                auto klass = inst ? inst->klass : nullptr;
                if (inst && klass && klass->isPlugin) {
                    int handle = static_cast<int>(reinterpret_cast<intptr_t>(inst->pluginInstance));
                    std::string target = klass->name + ":" +
                                        std::to_string(handle) + ":" +
                                        lowerName;
                    vm.stack.push_back(Value(target));
                    break;
                }
            }

            // If nothing matched, this really is an error.
            runtimeError("Undefined property or method '" + name + "'");
            break;
        }


        case OP_SET_PROPERTY: {
            int propNameIndex = chunk.code[ip++];
            Value propNameVal = chunk.constants[propNameIndex];
            if (!holds<std::string>(propNameVal))
                runtimeError("VM: Property name must be a string.");
            std::string propName = toLower(getVal<std::string>(propNameVal));
            Value value = pop(vm);
            Value object = pop(vm);
            debugLog("OP_SET_PROPERTY: About to set property '" + propName + "'.");
            debugLog("OP_SET_PROPERTY: Value = " + valueToString(value));
            debugLog("OP_SET_PROPERTY: Object type = " + getTypeName(object) + " (" + valueToString(object) + ")");
            if (holds<std::shared_ptr<ObjInstance>>(object)) {
                auto instance = getVal<std::shared_ptr<ObjInstance>>(object);
                if (instance->klass->isPlugin) {
                    // For plugin instances, look for an explicit setter.
                    auto it = instance->klass->pluginProperties.find(propName);
                    if (it != instance->klass->pluginProperties.end()) {
                        int handle = static_cast<int>(reinterpret_cast<intptr_t>(instance->pluginInstance));
                        BuiltinFn setter = it->second.second;
                        setter({ Value(handle), value });
                        vm.stack.push_back(object);
                    } else {
                        // Fallback: store the value in the instance's field map.
                        instance->fields[propName] = value;
                        vm.stack.push_back(object);
                    }
                } else {
                    instance->fields[propName] = value;
                    vm.stack.push_back(object);
                }
            } else {
                runtimeError("VM: Can only set properties on instances. Instead got type: " + getTypeName(object));
            }
            break;
        }


        case OP_CONSTRUCTOR_END: {
            if (vm.stack.size() < 2)
                runtimeError("VM: Not enough values for constructor end.");
            Value constructorResult = pop(vm);
            Value instance = pop(vm);
            if (holds<std::monostate>(constructorResult))
                vm.stack.push_back(instance);
            else
                vm.stack.push_back(constructorResult);
            break;
        }


        case OP_NOT: {
            Value v = pop(vm);

            // Boolean NOT
            if (holds<bool>(v)) {
                vm.stack.push_back(!getVal<bool>(v));
                break;
            }

            // Integer NOT (bitwise complement)
            if (holds<int>(v)) {
                vm.stack.push_back(~getVal<int>(v));
                break;
            }

            // Optional: allow whole-number doubles by coercing to int
            if (holds<double>(v)) {
                double d = getVal<double>(v);
                double ipart;
                if (std::modf(d, &ipart) == 0.0 &&
                    ipart >= (double)std::numeric_limits<int>::min() &&
                    ipart <= (double)std::numeric_limits<int>::max())
                {
                    vm.stack.push_back(~(int)ipart);
                    break;
                }
            }

            runtimeError("VM: Operand must be Boolean or Integer for Not.");
            break;
        }


        default:
            break;
        }
        {
            std::string s = "[";
            for (auto& v : vm.stack)
                s += valueToString(v) + ", ";
            s += "]";
            debugLog("VM: Stack after execution: " + s);
        }
    }
    return Value(std::monostate{});
}

const char MARKER[9] = "BYTECODE"; // 8 characters + null terminator = 9

std::string retrieveData(const std::string& exePath) {
    std::ifstream exeFile(exePath, std::ios::binary);
    if (!exeFile) {
        //debugLog("Error: Cannot load bytecode.\n");
        return "";
    }
    // Determine file size
    exeFile.seekg(0, std::ios::end);
    std::streampos fileSize = exeFile.tellg();
    if (fileSize < 12) { // at least marker (8 bytes) + length (4 bytes)
        debugLog("No bytecode data found.\n");
        return "";
    }
    // Read the last 12 bytes: marker (8) and text length (4)
    exeFile.seekg(-12, std::ios::end);
    char markerBuffer[9] = {0};
    exeFile.read(markerBuffer, 8);
    uint32_t textLength;
    exeFile.read(reinterpret_cast<char*>(&textLength), sizeof(textLength));
    // Verify marker
    if (std::strncmp(markerBuffer, MARKER, 8) != 0) {
        debugLog("Bytecode not found.\n");
        return "";
    }
    // Ensure file contains enough data for the embedded text
    if (fileSize < static_cast<std::streamoff>(12 + textLength)) {
        debugLog("Invalid bytecode data length.\n");
        return "";
    }
    // Calculate position of text data
    std::streampos textPos = fileSize - static_cast<std::streamoff>(12) - static_cast<std::streamoff>(textLength);
    exeFile.seekg(textPos, std::ios::beg);
    std::vector<char> textData(textLength);
    exeFile.read(textData.data(), textLength);
    return std::string(textData.begin(), textData.end());
}


// ============================================================================
// Environment Initialization
// ============================================================================

void InitializeEnvironment(VM& vm) {
        vm.globals = std::make_shared<Environment>(nullptr);
        vm.environment = vm.globals;
        globalVM = &vm;

        // Define built-in constants.
        vm.environment->define("pi", Value(3.141592653589793));

        #ifdef _WIN32
            std::string nativeEndOfLine = "\r\n";
        #else
            std::string nativeEndOfLine = "\n";
        #endif
        
        vm.environment->define("endofline", nativeEndOfLine);
        vm.environment->define("eol", nativeEndOfLine);

        vm.environment->define("sortwith", BuiltinFn([](const std::vector<Value>& args) -> Value {
            // Check that exactly 2 arguments were passed.
            if (args.size() != 2)
                runtimeError("sortwith expects exactly 2 arguments.");
        
            // Ensure both arguments are arrays.
            if (!holds<std::shared_ptr<ObjArray>>(args[0]) || !holds<std::shared_ptr<ObjArray>>(args[1]))
                runtimeError("sortwith expects both arguments to be arrays.");
        
            auto arr1 = getVal<std::shared_ptr<ObjArray>>(args[0]);
            auto arr2 = getVal<std::shared_ptr<ObjArray>>(args[1]);
        
            // They must be of equal length.
            if (arr1->elements.size() != arr2->elements.size())
                runtimeError("sortwith: both arrays must have the same number of elements.");
        
            size_t n = arr1->elements.size();
            // Create an index vector [0, 1, 2, ... n-1]
            std::vector<size_t> indices(n);
            for (size_t i = 0; i < n; i++) {
                indices[i] = i;
            }
        
            // Sort the indices based on the values in arr1.
            std::sort(indices.begin(), indices.end(), [arr1](size_t i, size_t j) {
                const Value &a = arr1->elements[i];
                const Value &b = arr1->elements[j];
                // First, if both are int, compare as integers.
                if (holds<int>(a) && holds<int>(b))
                    return getVal<int>(a) < getVal<int>(b);
                // Otherwise, if both are numbers (int or double), compare numerically.
                else if ((holds<double>(a) || holds<int>(a)) && (holds<double>(b) || holds<int>(b))) {
                    double da = holds<double>(a) ? getVal<double>(a) : static_cast<double>(getVal<int>(a));
                    double db = holds<double>(b) ? getVal<double>(b) : static_cast<double>(getVal<int>(b));
                    return da < db;
                }
                // Fallback: use string comparison.
                return valueToString(a) < valueToString(b);
            });
        
            // Create new sorted vectors for both arrays.
            std::vector<Value> newArr1(n), newArr2(n);
            for (size_t i = 0; i < n; i++) {
                newArr1[i] = arr1->elements[indices[i]];
                newArr2[i] = arr2->elements[indices[i]];
            }
        
            // Replace the contents of the original arrays with the sorted ones.
            arr1->elements = newArr1;
            arr2->elements = newArr2;
        
            // sortwith is a procedure so we return nil.
            return Value(std::monostate{});
        }));

        // ------------------------------------------------------------------
        // Built‑in IIF: IIF(condition, trueValue, falseValue)
        // ------------------------------------------------------------------
        vm.environment->define("iif", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 3)
                runtimeError("IIF expects exactly three arguments: IIF(condition, trueVal, falseVal).");

            // Evaluate truthiness of the first argument
            const Value& c = args[0];
            bool cond = false;
            if      (holds<bool>(c))   cond = getVal<bool>(c);
            else if (holds<int>(c))    cond = getVal<int>(c) != 0;
            else if (holds<double>(c)) cond = getVal<double>(c) != 0.0;
            else if (holds<std::string>(c))
                cond = !getVal<std::string>(c).empty();
            // other types are “false”

            // Return either the second or third argument
            return cond ? args[1] : args[2];
        }));

        // -----------------------------------------------------------------------------
        // Built‑ins from the old plugin, now in‑process
        // -----------------------------------------------------------------------------

        // Beep(Frequency As Integer, Duration As Integer) As Boolean
        vm.environment->define("beep", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) runtimeError("Beep expects 2 arguments: frequency, duration.");

            int freq;
            if (holds<int>(args[0])) {
                freq = getVal<int>(args[0]);
            } else if (holds<double>(args[0])) {
                freq = (int)getVal<double>(args[0]);
            } else {
                runtimeError("Beep: frequency must be a number.");
            }

            int dur;
            if (holds<int>(args[1])) {
                dur = getVal<int>(args[1]);
            } else if (holds<double>(args[1])) {
                dur = (int)getVal<double>(args[1]);
            } else {
                runtimeError("Beep: duration must be a number.");
            }

        #ifdef _WIN32
            bool ok = ::Beep(freq, dur) != 0;
        #else
            std::printf("\a");
            std::fflush(stdout);
            usleep(dur * 1000);
            bool ok = true;
        #endif
            return Value(ok);
        }));

        // Sleep(Milliseconds As Integer) As Boolean
        vm.environment->define("sleep", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Sleep expects 1 argument: milliseconds.");

            int ms;
            if (holds<int>(args[0])) {
                ms = getVal<int>(args[0]);
            } else if (holds<double>(args[0])) {
                ms = (int)getVal<double>(args[0]);
            } else {
                runtimeError("Sleep: argument must be a number.");
            }

        #ifdef _WIN32
            ::Sleep(ms);
        #else
            usleep(ms * 1000);
        #endif
            return Value(true);
        }));

        // DoEvents(Milliseconds As Integer) As Boolean
        // — processes UI/events, then sleeps for the given ms
        vm.environment->define("doevents", BuiltinFn([&](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("DoEvents expects 1 argument: milliseconds.");

            int ms;
            if (holds<int>(args[0])) {
                ms = getVal<int>(args[0]);
            } else if (holds<double>(args[0])) {
                ms = (int)getVal<double>(args[0]);
            } else {
                runtimeError("DoEvents: argument must be a number.");
            }

        #ifdef _WIN32
            // pump Windows messages
            MSG msg;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            MsgWaitForMultipleObjectsEx(0, nullptr, 0, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        #else
            // give the OS a chance
            sched_yield();
        #endif

            // then sleep
            auto sleepFn = getVal<BuiltinFn>(vm.environment->get("sleep"));
            sleepFn({ Value(ms) });
            return Value(true);
        }));

        // IsNumeric(Text As String) As Boolean
        vm.environment->define("isnumeric", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("IsNumeric expects 1 argument: text.");

            std::string s;
            if (holds<std::string>(args[0])) {
                s = getVal<std::string>(args[0]);
            } else {
                s = valueToString(args[0]);
            }

            if (s.empty()) return Value(false);
            char* end = nullptr;
            std::strtod(s.c_str(), &end);
            while (*end && std::isspace((unsigned char)*end)) ++end;
            return Value(*end == '\0');
        }));



        
        // Define built-in functions.
        vm.environment->define("print", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() < 1) runtimeError("print expects an argument.");
            std::cout << valueToString(args[0]) << std::endl;
            return args[0];
        }));

        vm.environment->define("input", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (!args.empty())
                runtimeError("Input() expects no arguments.");
            std::string userInput;
            std::getline(std::cin, userInput);
            return Value(userInput);
        }));

        // Built-in function: replace(input, findText, replaceWith)
        // Replaces the first occurrence of findText in input with replaceWith.
        vm.environment->define("replace", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 3)
                runtimeError("replace expects exactly 3 arguments: input, findText, replaceWith.");
            if (!holds<std::string>(args[0]) || !holds<std::string>(args[1]) || !holds<std::string>(args[2]))
                runtimeError("replace expects all arguments to be strings.");
            std::string input = getVal<std::string>(args[0]);
            std::string findText = getVal<std::string>(args[1]);
            std::string replaceWith = getVal<std::string>(args[2]);
            size_t pos = input.find(findText);
            if (pos != std::string::npos) {
                input.replace(pos, findText.length(), replaceWith);
            }
            return Value(input);
        }));

        // Built-in function: replaceall(input, findText, replacement)
        // Replaces all occurrences of findText in input with replacement.
        vm.environment->define("replaceall", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 3)
                runtimeError("replaceall expects exactly 3 arguments: input, findText, replacement.");
            if (!holds<std::string>(args[0]) || !holds<std::string>(args[1]) || !holds<std::string>(args[2]))
                runtimeError("replaceall expects all arguments to be strings.");
            std::string input = getVal<std::string>(args[0]);
            std::string findText = getVal<std::string>(args[1]);
            std::string replacement = getVal<std::string>(args[2]);
            if (findText.empty())
                runtimeError("replaceall: find text cannot be an empty string.");
            size_t pos = 0;
            while ((pos = input.find(findText, pos)) != std::string::npos) {
                input.replace(pos, findText.length(), replacement);
                pos += replacement.length(); // Move past the replacement.
            }
            return Value(input);
        }));

        vm.environment->define("length", BuiltinFn([](const std::vector<Value>& args) -> Value {
            // len expects exactly one argument
            if (args.size() != 1) runtimeError("length expects exactly one argument.");
        
            // string case
            if (holds<std::string>(args[0])) {
                const std::string& s = getVal<std::string>(args[0]);
                return Value((int)s.size());
            }
            // array case
            else if (holds<std::shared_ptr<ObjArray>>(args[0])) {
                auto arr = getVal<std::shared_ptr<ObjArray>>(args[0]);
                return Value((int)arr->elements.size());
            }
            else {
                runtimeError("length expects a string or an array.");
                // unreachable, but suppress compiler warning:
                return Value(0);
            }
        }));

        vm.environment->define("len", BuiltinFn([](const std::vector<Value>& args) -> Value {
            // len expects exactly one argument
            if (args.size() != 1) runtimeError("len expects exactly one argument.");
        
            // string case
            if (holds<std::string>(args[0])) {
                const std::string& s = getVal<std::string>(args[0]);
                return Value((int)s.size());
            }
            // array case
            else if (holds<std::shared_ptr<ObjArray>>(args[0])) {
                auto arr = getVal<std::shared_ptr<ObjArray>>(args[0]);
                return Value((int)arr->elements.size());
            }
            else {
                runtimeError("len expects a string or an array.");
                // unreachable, but suppress compiler warning:
                return Value(0);
            }
        }));
        
        vm.environment->define("space", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("space expects exactly one argument.");
            
            int count;
            if (holds<int>(args[0])) {
                count = getVal<int>(args[0]);
            }
            else if (holds<double>(args[0])) {
                count = (int)getVal<double>(args[0]);
            }
            else {
                runtimeError("space expects a number.");
            }
        
            if (count < 0) runtimeError("space expects a non-negative number.");
        
            return Value(std::string(count, ' '));
        }));

        vm.environment->define("quit", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (!args.empty()) runtimeError("quit expects no arguments.");
            // flush any pending output
            std::cout << std::flush;
            std::exit(0);
            // unreachable, but satisfy the compiler
            return Value();
        }));
        

        vm.environment->define("str", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() < 1) runtimeError("str expects an argument.");
            return Value(valueToString(args[0]));
        }));
        vm.environment->define("microseconds", std::string("microseconds"));
        vm.environment->define("ticks", std::string("ticks"));
        vm.environment->define("val", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("val expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("val expects a string argument.");
            double d = std::stod(getVal<std::string>(args[0]));
            return d;
        }));


        // Built-in function: join(inputStringArray, separator) as String
        //   - inputStringArray must be an Array of String
        //   - separator must be a String
        // Returns the concatenation of all elements, separated by separator.
        vm.environment->define("join", BuiltinFn([](const std::vector<Value>& args) -> Value {
            // Expect exactly two arguments
            if (args.size() != 2)
                runtimeError("join expects exactly two arguments: inputStringArray and separator.");

            // args[0] must be an array
            if (!holds<std::shared_ptr<ObjArray>>(args[0]))
                runtimeError("join expects the first argument to be an array.");

            // args[1] must be a string
            if (!holds<std::string>(args[1]))
                runtimeError("join expects the second argument to be a string.");

            auto arr = getVal<std::shared_ptr<ObjArray>>(args[0]);
            const std::string sep = getVal<std::string>(args[1]);

            // Build the result
            std::string result;
            for (size_t i = 0; i < arr->elements.size(); ++i) {
                // Each element must be a string (or convertible)
                if (!holds<std::string>(arr->elements[i]))
                    runtimeError("join: all array elements must be strings.");
                result += getVal<std::string>(arr->elements[i]);
                if (i + 1 < arr->elements.size())
                    result += sep;
            }
            return Value(result);
        }));


        vm.environment->define("split", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2)
                runtimeError("split expects exactly two arguments: text and delimiter.");
            if (!holds<std::string>(args[0]) || !holds<std::string>(args[1]))
                runtimeError("split expects both arguments to be strings.");
            std::string text = getVal<std::string>(args[0]);
            std::string delimiter = getVal<std::string>(args[1]);
            auto arr = std::make_shared<ObjArray>();
            if (delimiter.empty()) {
                for (char c : text) {
                    arr->elements.push_back(std::string(1, c));
                }
            } else {
                size_t start = 0;
                size_t pos = text.find(delimiter, start);
                while (pos != std::string::npos) {
                    arr->elements.push_back(text.substr(start, pos - start));
                    start = pos + delimiter.length();
                    pos = text.find(delimiter, start);
                }
                arr->elements.push_back(text.substr(start));
            }
            return Value(arr);
        }));
        vm.environment->define("array", BuiltinFn([](const std::vector<Value>& args) -> Value {
            auto arr = std::make_shared<ObjArray>();
            arr->elements = args;
            return Value(arr);
        }));
        vm.environment->define("abs", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Abs expects exactly one argument.");
            if (holds<int>(args[0]))
                return std::abs(getVal<int>(args[0]));
            else if (holds<double>(args[0]))
                return std::fabs(getVal<double>(args[0]));
            else
                runtimeError("Abs expects a number.");
            return Value(std::monostate{});
        }));
        vm.environment->define("acos", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Acos expects exactly one argument.");
            double x = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Acos expects a number."), 0.0));
            return std::acos(x);
        }));
        vm.environment->define("asc", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Asc expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("Asc expects a string.");
            std::string s = getVal<std::string>(args[0]);
            if (s.empty()) runtimeError("Asc expects a non-empty string.");
            return (int)s[0];
        }));
        vm.environment->define("asin", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Asin expects exactly one argument.");
            double x = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Asin expects a number."), 0.0));
            return std::asin(x);
        }));
        vm.environment->define("atan", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Atan expects exactly one argument.");
            double x = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Atan expects a number."), 0.0));
            return std::atan(x);
        }));
        vm.environment->define("atan2", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) runtimeError("Atan2 expects exactly two arguments.");
            double y = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Atan2 expects numbers."), 0.0));
            double x = holds<int>(args[1]) ? getVal<int>(args[1]) : (holds<double>(args[1]) ? getVal<double>(args[1]) : (runtimeError("Atan2 expects numbers."), 0.0));
            return std::atan2(y, x);
        }));
        vm.environment->define("ceiling", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Ceiling expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Ceiling expects a number."), 0.0));
            return std::ceil(v);
        }));
        vm.environment->define("cos", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Cos expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Cos expects a number."), 0.0));
            return std::cos(v);
        }));
        vm.environment->define("exp", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Exp expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Exp expects a number."), 0.0));
            return std::exp(v);
        }));
        vm.environment->define("floor", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Floor expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Floor expects a number."), 0.0));
            return std::floor(v);
        }));
        vm.environment->define("log", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Log expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : (holds<double>(args[0]) ? getVal<double>(args[0]) : (runtimeError("Log expects a number."), 0.0));
            return std::log(v);
        }));
        vm.environment->define("max", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) runtimeError("Max expects exactly two arguments.");
            if (holds<int>(args[0]) && holds<int>(args[1])) {
                int a = getVal<int>(args[0]), b = getVal<int>(args[1]);
                return a > b ? a : b;
            }
            else {
                double a = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
                double b = holds<int>(args[1]) ? getVal<int>(args[1]) : getVal<double>(args[1]);
                return a > b ? a : b;
            }
        }));
        vm.environment->define("min", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) runtimeError("Min expects exactly two arguments.");
            if (holds<int>(args[0]) && holds<int>(args[1])) {
                int a = getVal<int>(args[0]), b = getVal<int>(args[1]);
                return a < b ? a : b;
            }
            else {
                double a = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
                double b = holds<int>(args[1]) ? getVal<int>(args[1]) : getVal<double>(args[1]);
                return a < b ? a : b;
            }
        }));
        vm.environment->define("oct", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Oct expects exactly one argument.");
            int n = 0;
            if (holds<int>(args[0])) n = getVal<int>(args[0]);
            else if (holds<double>(args[0])) n = static_cast<int>(getVal<double>(args[0]));
            else runtimeError("Oct expects a number.");
            std::stringstream ss;
            ss << std::oct << n;
            return ss.str();
        }));
        vm.environment->define("pow", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2) runtimeError("Pow expects exactly two arguments.");
            double a = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            double b = holds<int>(args[1]) ? getVal<int>(args[1]) : getVal<double>(args[1]);
            return std::pow(a, b);
        }));
        vm.environment->define("round", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Round expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            return std::round(v);
        }));
        vm.environment->define("sign", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Sign expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            if (v < 0) return -1;
            else if (v == 0) return 0;
            else return 1;
        }));
        vm.environment->define("sin", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Sin expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            return std::sin(v);
        }));
        vm.environment->define("sqrt", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Sqrt expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            return std::sqrt(v);
        }));
        vm.environment->define("tan", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1) runtimeError("Tan expects exactly one argument.");
            double v = holds<int>(args[0]) ? getVal<int>(args[0]) : getVal<double>(args[0]);
            return std::tan(v);
        }));
        vm.environment->define("rnd", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 0) runtimeError("Rnd expects no arguments.");
            std::uniform_real_distribution<double> dist(0.0, 1.0);
            std::lock_guard<std::mutex> lock(rngMutex);
            return dist(global_rng);
        }));


        // Built-in function: trim(input) as String
        // Returns the input string with both leading and trailing whitespace removed.
        vm.environment->define("trim", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1)
                runtimeError("trim expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("trim expects a string argument.");
            std::string s = getVal<std::string>(args[0]);
            size_t first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return Value(std::string(""));
            size_t last = s.find_last_not_of(" \t\r\n");
            return Value(s.substr(first, last - first + 1));
        }));

        // Built-in function: right(input, count) as String
        // Returns the rightmost 'count' characters of the input string.
        vm.environment->define("right", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2)
                runtimeError("right expects exactly two arguments: input and count.");
            if (!holds<std::string>(args[0]))
                runtimeError("right expects the first argument to be a string.");
            if (!(holds<int>(args[1]) || holds<double>(args[1])))
                runtimeError("right expects the second argument to be a number.");
            std::string s = getVal<std::string>(args[0]);
            int count = holds<int>(args[1]) ? getVal<int>(args[1]) : static_cast<int>(getVal<double>(args[1]));
            if (count < 0)
                runtimeError("right expects a non-negative count.");
            if (count > s.size())
                count = s.size();
            return Value(s.substr(s.size() - count, count));
        }));

        // Built-in function: left(input, count) as String
        // Returns the leftmost 'count' characters of the input string.
        vm.environment->define("left", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 2)
                runtimeError("left expects exactly two arguments: input and count.");
            if (!holds<std::string>(args[0]))
                runtimeError("left expects the first argument to be a string.");
            if (!(holds<int>(args[1]) || holds<double>(args[1])))
                runtimeError("left expects the second argument to be a number.");
            std::string s = getVal<std::string>(args[0]);
            int count = holds<int>(args[1]) ? getVal<int>(args[1]) : static_cast<int>(getVal<double>(args[1]));
            if (count < 0)
                runtimeError("left expects a non-negative count.");
            if (count > s.size())
                count = s.size();
            return Value(s.substr(0, count));
        }));

        // Built-in function: titlecase(input) as String
        // Returns the string with each new word capitalized.
        // New words are assumed to start after any of these characters: space, newline, period, colon, or semicolon.
        vm.environment->define("titlecase", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1)
                runtimeError("titlecase expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("titlecase expects a string argument.");
            std::string s = getVal<std::string>(args[0]);
            std::string result = s;
            bool capitalizeNext = true;
            for (size_t i = 0; i < result.length(); i++) {
                char c = result[i];
                if (capitalizeNext && std::isalpha(static_cast<unsigned char>(c))) {
                    result[i] = std::toupper(c);
                    capitalizeNext = false;
                } else {
                    result[i] = std::tolower(c);
                }
                // Set the flag if we encounter a word-separator.
                if (c == ' ' || c == '\n' || c == '.' || c == ':' || c == ';')
                    capitalizeNext = true;
            }
            return Value(result);
        }));

        // Built-in function: lowercase(input) as String
        // Returns the entire input string in lowercase.
        vm.environment->define("lowercase", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1)
                runtimeError("lowercase expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("lowercase expects a string argument.");
            std::string s = getVal<std::string>(args[0]);
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return Value(s);
        }));

        // Built-in function: uppercase(input) as String
        // Returns the entire input string in uppercase.
        vm.environment->define("uppercase", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 1)
                runtimeError("uppercase expects exactly one argument.");
            if (!holds<std::string>(args[0]))
                runtimeError("uppercase expects a string argument.");
            std::string s = getVal<std::string>(args[0]);
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            return Value(s);
        }));

        // Built-in function: middle(input as String, start as Number, length as Number) as String
        // Returns the substring starting at the 1-based position for the specified length.
        // If the start position is greater than the input length, returns an empty string.
        vm.environment->define("middle", BuiltinFn([](const std::vector<Value>& args) -> Value {
            if (args.size() != 3)
                runtimeError("middle expects exactly three arguments: input, start position, and length.");
            if (!holds<std::string>(args[0]))
                runtimeError("middle expects the first argument to be a string.");
            if (!(holds<int>(args[1]) || holds<double>(args[1])))
                runtimeError("middle expects the second argument (start position) to be a number.");
            if (!(holds<int>(args[2]) || holds<double>(args[2])))
                runtimeError("middle expects the third argument (length) to be a number.");

            std::string s = getVal<std::string>(args[0]);
            // Assume 1-based indexing; convert to zero-based.
            int startPos = holds<int>(args[1]) ? getVal<int>(args[1]) : static_cast<int>(getVal<double>(args[1]));
            int len = holds<int>(args[2]) ? getVal<int>(args[2]) : static_cast<int>(getVal<double>(args[2]));

            if (startPos < 1)
                runtimeError("middle expects a start position of 1 or greater.");
            if (len < 0)
                runtimeError("middle expects a non-negative length.");

            int zeroBased = startPos - 1;
            if (zeroBased >= s.size())
                return Value(std::string("")); // Return empty string if start is past the end.
            if (zeroBased + len > s.size())
                len = s.size() - zeroBased; // Adjust length if it goes beyond the end.
            return Value(s.substr(zeroBased, len));
        }));


        // Register built-in AddressOf and AddHandler functions.
        // AddressOf converts a script function to a C callback pointer.
        // AddHandler attaches the callback pointer to a plugin event target.
        // Register AddressOf built-in.
        vm.environment->define("AddressOf", BuiltinFn(addressOfBuiltin));
        // Register AddHandler built-in.
        vm.environment->define("AddHandler", BuiltinFn(addHandlerBuiltin));

        {
            auto randomClass = std::make_shared<ObjClass>();
            randomClass->name = "random";
            randomClass->methods["inrange"] = BuiltinFn([](const std::vector<Value>& args) -> Value {
                if (args.size() != 2) runtimeError("Random.InRange expects exactly two arguments.");
                int minVal = 0, maxVal = 0;
                if (holds<int>(args[0]))
                    minVal = getVal<int>(args[0]);
                else if (holds<double>(args[0]))
                    minVal = static_cast<int>(getVal<double>(args[0]));
                else
                    runtimeError("Random.InRange expects a number as first argument.");
                if (holds<int>(args[1]))
                    maxVal = getVal<int>(args[1]);
                else if (holds<double>(args[1]))
                    maxVal = static_cast<int>(getVal<double>(args[1]));
                else
                    runtimeError("Random.InRange expects a number as second argument.");
                if (minVal > maxVal) runtimeError("Random.InRange: min is greater than max.");
                std::uniform_int_distribution<int> dist(minVal, maxVal);
                std::lock_guard<std::mutex> lock(rngMutex);
                return dist(global_rng);
            });
            vm.environment->define("random", randomClass);
        }

        // Load Plugin functions, classes, and modules into the VM environment.
        loadPlugins(vm);

}

// ============================================================================  
// Main
// ============================================================================


// XTEA bytecode encryption: operates on 64-bit blocks (two 32-bit values)
void xtea_encrypt(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0], v1 = v[1], sum = 0;
    const uint32_t delta = 0x9E3779B9;
    for (unsigned int i = 0; i < 32; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

void xtea_decrypt(uint32_t v[2], const uint32_t key[4]) {
    uint32_t v0 = v[0], v1 = v[1];
    const uint32_t delta = 0x9E3779B9;
    uint32_t sum = delta * 32;
    for (unsigned int i = 0; i < 32; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
    }
    v[0] = v0;
    v[1] = v1;
}

// Encrypts bytecode with a key and returns raw binary code.
std::string encrypt(const std::string &plaintext, const std::string &keyStr) {
    // Prepend the original length (4 bytes, little-endian) to allow correct unpadding.
    uint32_t origLen = static_cast<uint32_t>(plaintext.size());
    std::string data;
    for (int i = 0; i < 4; i++) {
        data.push_back(static_cast<char>((origLen >> (i * 8)) & 0xFF));
    }
    data.append(plaintext);
    // Pad data to a multiple of 8 bytes.
    size_t pad = 8 - (data.size() % 8);
    if (pad != 8) {
        data.append(pad, '\0');
    }

    // Derive a 128-bit key from keyStr (first 16 bytes, padded with zeros if needed).
    uint32_t key[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (i < keyStr.size())
            key[i / 4] |= ((uint32_t)(unsigned char)keyStr[i]) << ((i % 4) * 8);
    }

    std::string cipher = data;
    // Process each 8-byte block.
    for (size_t i = 0; i < data.size(); i += 8) {
        uint32_t block[2] = {0, 0};
        for (int j = 0; j < 4; j++) {
            block[0] |= ((uint32_t)(unsigned char)data[i + j]) << (j * 8);
            block[1] |= ((uint32_t)(unsigned char)data[i + 4 + j]) << (j * 8);
        }
        xtea_encrypt(block, key);
        for (int j = 0; j < 4; j++) {
            cipher[i + j] = static_cast<char>((block[0] >> (j * 8)) & 0xFF);
            cipher[i + 4 + j] = static_cast<char>((block[1] >> (j * 8)) & 0xFF);
        }
    }
    // Return raw encrypted binary bytecode.
    return cipher;
}

// Decrypts the binary bytecode with the given key and returns the original bytecode.
std::string decrypt(const std::string &cipher, const std::string &keyStr) {
    if (cipher.size() % 8 != 0) return "";
    uint32_t key[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++) {
        if (i < keyStr.size())
            key[i / 4] |= ((uint32_t)(unsigned char)keyStr[i]) << ((i % 4) * 8);
    }
    std::string plain = cipher;
    for (size_t i = 0; i < cipher.size(); i += 8) {
        uint32_t block[2] = {0, 0};
        for (int j = 0; j < 4; j++) {
            block[0] |= ((uint32_t)(unsigned char)cipher[i + j]) << (j * 8);
            block[1] |= ((uint32_t)(unsigned char)cipher[i + 4 + j]) << (j * 8);
        }
        xtea_decrypt(block, key);
        for (int j = 0; j < 4; j++) {
            plain[i + j] = static_cast<char>((block[0] >> (j * 8)) & 0xFF);
            plain[i + 4 + j] = static_cast<char>((block[1] >> (j * 8)) & 0xFF);
        }
    }
    if (plain.size() < 4) return "";
    uint32_t origLen = 0;
    for (int i = 0; i < 4; i++) {
        origLen |= ((uint32_t)(unsigned char)plain[i]) << (i * 8);
    }
    return plain.substr(4, origLen);
}


#ifndef BUILD_SHARED

    //binary bytecode encryption
    std::string cipherkey = "MySecretKey12345";

    // stub.cpp
    #ifdef _WIN32
    #include <windows.h>
    #include <vector>
    #include <string>

    // Forward WinMain to your normal main(argc,argv) for GUI-based applications.
    int main(int argc, char** argv);

    int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
    {
    int argc;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<char*> argv;
    argv.reserve(argc);

    for(int i=0;i<argc;++i){
        int len = WideCharToMultiByte(CP_UTF8,0,wargv[i],-1,nullptr,0,nullptr,nullptr);
        char* arg = (char*)malloc(len);
        WideCharToMultiByte(CP_UTF8,0,wargv[i],-1,arg,len,nullptr,nullptr);
        argv.push_back(arg);
    }
    LocalFree(wargv);
    int ret = main(argc, argv.data());
    for(char* s : argv) free(s);
    return ret;
    }
    #endif


    int main(int argc, char* argv[]) {
        #ifdef _WIN32
            SetDllDirectory("libs");
        #endif
        mainThreadId = std::this_thread::get_id();
        startTime = std::chrono::steady_clock::now();
        std::string filename = "default.xs";
        // Iterate through arguments, skipping argv[0] (program name)
        for (int i = 1; i < argc - 1; i++) {
            std::string arg = argv[i];
            if (arg == "--s" && (i + 1 < argc)) {
                filename = argv[i + 1];
            }
            else if (arg == "--d" && (i + 1 < argc)) {
                std::string debugArg = argv[i + 1];
                
                // Convert to lowercase for case-insensitive comparison
                std::transform(debugArg.begin(), debugArg.end(), debugArg.begin(), ::tolower);
                if (debugArg == "true") {
                    DEBUG_MODE = true; 
                } else if (debugArg == "false") {
                    DEBUG_MODE = false; 
                } else {
                    std::cerr << "Error: Argument for --d must be 'true' or 'false'." << std::endl;
                    return 1;
                }
            }
        }
        debugLog(std::string("DEBUG_MODE: ") + (DEBUG_MODE ? "ON" : "OFF"));
    ///////////////Initialize Envrironment////////////////
            // Create and initialize the VM environment.
            VM vm;
            InitializeEnvironment(vm);
    //////////////////////////////////////////////////////

        std::string exePath = argv[0]; // path to the current executable
        
        #ifdef _WIN32
            // Convert path’s suffix to lowercase for comparison
            auto toLower = [](unsigned char c){ return std::tolower(c); };
            std::string suffix;
            if (exePath.size() >= 4) {
                suffix = exePath.substr(exePath.size() - 4);
                std::transform(suffix.begin(), suffix.end(), suffix.begin(), toLower);
            }

            if (suffix != ".exe") {
                exePath += ".exe";
            }
        #endif

        std::string retrieved = decrypt(retrieveData(exePath), cipherkey); // retrieve bytecode if exists
        std::string source;

        if (!retrieved.empty()) {
            source = preprocessSource(retrieved);
        } else {
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Notice: Unable to find " << filename << std::endl;
                return EXIT_FAILURE;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            source = preprocessSource(buffer.str());
        }


        debugLog("Starting lexing...");
        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        debugLog("Lexing complete. Tokens count: " + std::to_string(tokens.size()));

        debugLog("Starting parsing...");
        Parser parser(tokens);
        std::vector<std::shared_ptr<Stmt>> statements = parser.parse();
        debugLog("Parsing complete. Statements count: " + std::to_string(statements.size()));
    ///////////////////////////////////////

        // Compile the CrossBasic program.
        debugLog("Starting compilation...");
        Compiler compiler(vm);
        compiler.compile(statements);
        debugLog("Compilation complete. Main chunk instructions count: " + std::to_string(vm.mainChunk.code.size()));

        if (vm.environment->values.find("main") != vm.environment->values.end() &&
            (holds<std::shared_ptr<ObjFunction>>(vm.environment->get("main")) ||
            holds<std::vector<std::shared_ptr<ObjFunction>>>(vm.environment->get("main")))) {
            Value mainVal = vm.environment->get("main");
            if (holds<std::shared_ptr<ObjFunction>>(mainVal)) {
                auto mainFunction = getVal<std::shared_ptr<ObjFunction>>(mainVal);
                debugLog("Calling main function...");
                // Run the compiled bytecode
                runVM(vm, mainFunction->chunk);
            }
            else if (holds<std::vector<std::shared_ptr<ObjFunction>>>(mainVal)) {
                auto overloads = getVal<std::vector<std::shared_ptr<ObjFunction>>>(mainVal);
                std::shared_ptr<ObjFunction> mainFunction = nullptr;
                for (auto f : overloads) {
                    if (f->arity == 0) { mainFunction = f; break; }
                }
                if (!mainFunction)
                    runtimeError("No main function with 0 parameters found.");
                debugLog("Calling main function...");
                runVM(vm, mainFunction->chunk);
            }
        }
        else {
            debugLog("No main function found. Executing top-level code...");
            runVM(vm, vm.mainChunk);
        }
        debugLog("Program execution finished.");
        return 0;
    }


    #ifdef _WIN32
    // Optional: DllMain for Windows initialization
    BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
        switch (ul_reason_for_call) {
            case DLL_PROCESS_ATTACH:
            case DLL_THREAD_ATTACH:
            case DLL_THREAD_DETACH:
            case DLL_PROCESS_DETACH:
                break;
        }
        return TRUE;
    }
    #endif


#endif


// ============================================================================
// For building the CrossBasic VM as a library for use with other software.
// ============================================================================

// Ensure this function is exported with C linkage.
extern "C" {
#ifdef _WIN32
__declspec(dllexport)
#endif
// This function takes a C-string (code) and returns a dynamically allocated C-string containing the output.
const char* CompileAndRun(const char* code, bool enableDebug) {
    // Set debug mode based on the parameter.
    DEBUG_MODE = enableDebug;
    // Redirect std::cout to capture output.
    std::streambuf* origBuf = std::cout.rdbuf();
    std::ostringstream captureStream;
    std::cout.rdbuf(captureStream.rdbuf());

    // --- Setup the VM and environment ---
    // Create and initialize the VM environment.
    VM vm;
    InitializeEnvironment(vm);

////////////////////////////////////////////////

    // --- Compile the provided code ---
    std::string source = preprocessSource(code);
    Lexer lexer(source);
    auto tokens = lexer.scanTokens();
    Parser parser(tokens);
    std::vector<std::shared_ptr<Stmt>> statements = parser.parse();
    Compiler compiler(vm);
    compiler.compile(statements);

    // --- Run the compiled code ---
    // If a 'main' function exists, run it; otherwise run top-level code.
    if (vm.environment->values.find("main") != vm.environment->values.end() &&
       (holds<std::shared_ptr<ObjFunction>>(vm.environment->get("main")) ||
        holds<std::vector<std::shared_ptr<ObjFunction>>>(vm.environment->get("main")))) {
        Value mainVal = vm.environment->get("main");
        if (holds<std::shared_ptr<ObjFunction>>(mainVal)) {
            auto mainFunction = getVal<std::shared_ptr<ObjFunction>>(mainVal);
            runVM(vm, mainFunction->chunk);
        } else if (holds<std::vector<std::shared_ptr<ObjFunction>>>(mainVal)) {
            auto overloads = getVal<std::vector<std::shared_ptr<ObjFunction>>>(mainVal);
            std::shared_ptr<ObjFunction> mainFunction = nullptr;
            for (auto f : overloads) {
                if (f->arity == 0) { mainFunction = f; break; }
            }
            if (!mainFunction)
                runtimeError("No main function with 0 parameters found.");
            runVM(vm, mainFunction->chunk);
        }
    } else {
        runVM(vm, vm.mainChunk);
    }

    // --- Restore std::cout ---
    std::cout.rdbuf(origBuf);

    // Get the captured output.
    std::string result = captureStream.str();
    // Allocate a new buffer to return; caller software/program must free this buffer (destroy or dereference the object).
    char* retBuffer = new char[result.size() + 1];
    std::strcpy(retBuffer, result.c_str());
    return retBuffer;
}
}

//////////////////////////////////////////
/////////////  Happy Coding!  ////////////
//////////////////////////////////////////