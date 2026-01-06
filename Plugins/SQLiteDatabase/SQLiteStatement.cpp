/*

  SQLiteStatement.cpp
  CrossBasic Plugin: SQLiteStatement                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      4r
 
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
// 
// Build (Linux/macOS):
//    g++ -shared -fPIC -o SQLiteStatementPlugin.so SQLiteStatementClass.cpp -lsqlite3 -Wl,--allow-shlib-undefined
// Build (Windows):
//    g++ -shared -o SQLiteStatementPlugin.dll SQLiteStatementClass.cpp -lsqlite3
// (Ensure that an import library for SQLiteDatabasePlugin is available.)
 
#include <sqlite3.h>
#include <map>
#include <string>
#include <mutex>
#include <atomic>
#include <cstdlib>

#ifdef _WIN32
  #include <windows.h>
  #define XPLUGIN_API __declspec(dllexport)
#else
  #define XPLUGIN_API __attribute__((visibility("default")))
#endif

// Declaration of the external function from the SQLiteDatabase plugin.
extern "C" {
    XPLUGIN_API sqlite3* SQLiteDatabase_GetPointer(int dbHandle);
}

//------------------------------------------------------------------------------
// SQLiteStatement Class Declaration
//------------------------------------------------------------------------------
class SQLiteStatement {
public:
    sqlite3_stmt* stmt;
    int DatabaseHandle;      // to be set via property
    std::string SQL;         // to be set via property

    // Parameterless constructor.
    SQLiteStatement() : stmt(nullptr), DatabaseHandle(0) { }

    // Prepares the statement using the current properties.
    bool Prepare() {
        sqlite3* dbPtr = SQLiteDatabase_GetPointer(DatabaseHandle);
        if (!dbPtr) return false;
        if (stmt) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (sqlite3_prepare_v2(dbPtr, SQL.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            stmt = nullptr;
            return false;
        }
        return true;
    }
    
    // Binds an integer value to the parameter at the given index.
    bool BindInteger(int index, int value) {
        if (!stmt) return false;
        return sqlite3_bind_int(stmt, index, value) == SQLITE_OK;
    }
    
    // Binds a double value.
    bool BindDouble(int index, double value) {
        if (!stmt) return false;
        return sqlite3_bind_double(stmt, index, value) == SQLITE_OK;
    }
    
    // Binds a string value.
    bool BindString(int index, const std::string &value) {
        if (!stmt) return false;
        return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
    }
    
    // Executes the statement (for non-select queries).
    bool Execute() {
        if (!stmt) return false;
        return sqlite3_step(stmt) == SQLITE_DONE;
    }
    
    // Resets and steps to the first row (for SELECT queries).
    bool MoveToFirstRow() {
        if (!stmt) return false;
        sqlite3_reset(stmt);
        return sqlite3_step(stmt) == SQLITE_ROW;
    }
    
    // Moves to the next row.
    bool MoveToNextRow() {
        if (!stmt) return false;
        return sqlite3_step(stmt) == SQLITE_ROW;
    }
    
    // Retrieves an integer column value.
    int ColumnInteger(int index) {
        if (!stmt) return 0;
        return sqlite3_column_int(stmt, index);
    }
    
    // Retrieves a double column value.
    double ColumnDouble(int index) {
        if (!stmt) return 0.0;
        return sqlite3_column_double(stmt, index);
    }
    
    // Retrieves a string column value.
    const char* ColumnString(int index) {
        if (!stmt) return "";
        return reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    }
    
    // Finalizes the statement.
    bool Finalize() {
        if (stmt) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        return true;
    }
};

//------------------------------------------------------------------------------
// Global Instance Management for SQLiteStatement Instances
//------------------------------------------------------------------------------
static std::mutex stmtMutex;
static std::map<int, SQLiteStatement*> stmtMap;
static std::atomic<int> nextStmtHandle(1);

//------------------------------------------------------------------------------
// Exported Functions (Statement)
//------------------------------------------------------------------------------

// Constructor: Creates a new SQLiteStatement instance and returns its unique handle.
extern "C" XPLUGIN_API int NewSQLiteStatement() {
    SQLiteStatement* s = new SQLiteStatement();
    int handle = nextStmtHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(stmtMutex);
        stmtMap[handle] = s;
    }
    return handle;
}

// Sets the DatabaseHandle property.
extern "C" XPLUGIN_API void SetStatementDatabase(int stmtHandle, int dbHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it != stmtMap.end())
        it->second->DatabaseHandle = dbHandle;
}

// Sets the SQL property.
extern "C" XPLUGIN_API void SetStatementSQL(int stmtHandle, const char* sql) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it != stmtMap.end())
        it->second->SQL = sql;
}

// Prepares the statement (using the current DatabaseHandle and SQL).
extern "C" XPLUGIN_API bool PrepareStatementInstance(int stmtHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->Prepare();
}

extern "C" XPLUGIN_API bool BindInteger(int stmtHandle, int index, int value) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->BindInteger(index, value);
}

extern "C" XPLUGIN_API bool BindDouble(int stmtHandle, int index, double value) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->BindDouble(index, value);
}

extern "C" XPLUGIN_API bool BindString(int stmtHandle, int index, const char* value) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->BindString(index, std::string(value));
}

extern "C" XPLUGIN_API bool ExecutePrepared(int stmtHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->Execute();
}

extern "C" XPLUGIN_API bool MoveToFirstRow(int stmtHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->MoveToFirstRow();
}

extern "C" XPLUGIN_API bool MoveToNextRow(int stmtHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    return it->second->MoveToNextRow();
}

extern "C" XPLUGIN_API int ColumnInteger(int stmtHandle, int index) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return 0;
    return it->second->ColumnInteger(index);
}

extern "C" XPLUGIN_API double ColumnDouble(int stmtHandle, int index) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return 0.0;
    return it->second->ColumnDouble(index);
}

extern "C" XPLUGIN_API const char* ColumnString(int stmtHandle, int index) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return "";
    return it->second->ColumnString(index);
}

extern "C" XPLUGIN_API bool FinalizeStatement(int stmtHandle) {
    std::lock_guard<std::mutex> lock(stmtMutex);
    auto it = stmtMap.find(stmtHandle);
    if (it == stmtMap.end()) return false;
    bool ok = it->second->Finalize();
    delete it->second;
    stmtMap.erase(it);
    return ok;
}

//------------------------------------------------------------------------------
// Class Definition Structures for SQLiteStatement
//------------------------------------------------------------------------------
typedef struct {
    const char* name;
    const char* type;
    void* getter;
    void* setter;
} ClassProperty;

typedef struct {
    const char* name;
    void* funcPtr;
    int arity;
    const char* paramTypes[10];
    const char* retType;
} ClassEntry;

typedef struct {
    const char* declaration;
} ClassConstant;

typedef struct {
    const char* className;
    size_t classSize;
    void* constructor;
    ClassProperty* properties;
    size_t propertiesCount;
    ClassEntry* methods;
    size_t methodsCount;
    ClassConstant* constants;
    size_t constantsCount;
} ClassDefinition;

// Expose properties for setting DatabaseHandle and SQL.
static ClassProperty SQLiteStatementProperties[] = {
    { "DatabaseHandle", "integer", nullptr, (void*)SetStatementDatabase },
    { "SQL", "string", nullptr, (void*)SetStatementSQL }
};

static ClassEntry SQLiteStatementMethods[] = {
    { "Prepare", (void*)PrepareStatementInstance, 1, {"integer"}, "boolean" },
    { "BindInteger", (void*)BindInteger, 3, {"integer", "integer", "integer"}, "boolean" },
    { "BindDouble", (void*)BindDouble, 3, {"integer", "integer", "double"}, "boolean" },
    { "BindString", (void*)BindString, 3, {"integer", "integer", "string"}, "boolean" },
    { "Execute", (void*)ExecutePrepared, 1, {"integer"}, "boolean" },
    { "MoveToFirstRow", (void*)MoveToFirstRow, 1, {"integer"}, "boolean" },
    { "MoveToNextRow", (void*)MoveToNextRow, 1, {"integer"}, "boolean" },
    { "ColumnInteger", (void*)ColumnInteger, 2, {"integer", "integer"}, "integer" },
    { "ColumnDouble", (void*)ColumnDouble, 2, {"integer", "integer"}, "double" },
    { "ColumnString", (void*)ColumnString, 2, {"integer", "integer"}, "string" },
    { "Finalize", (void*)FinalizeStatement, 1, {"integer"}, "boolean" }
};

static ClassConstant SQLiteStatementConstants[] = { };

static ClassDefinition SQLiteStatementClass = {
    "SQLiteStatement",
    sizeof(SQLiteStatement),
    (void*)NewSQLiteStatement, // Parameterless constructor.
    SQLiteStatementProperties,
    sizeof(SQLiteStatementProperties)/sizeof(ClassProperty),
    SQLiteStatementMethods,
    sizeof(SQLiteStatementMethods)/sizeof(ClassEntry),
    SQLiteStatementConstants,
    sizeof(SQLiteStatementConstants)/sizeof(ClassConstant)
};

extern "C" XPLUGIN_API ClassDefinition* GetClassDefinition() {
    return &SQLiteStatementClass;
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    return TRUE;
}
#endif
