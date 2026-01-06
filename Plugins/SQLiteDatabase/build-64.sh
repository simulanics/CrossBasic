#!/usr/bin/env bash
# build.sh
# Usage: ./build.sh
# Builds SQLiteDatabase + SQLiteStatement on macOS or Linux.

set -euo pipefail

OS="$(uname -s)"
DB_SRC="SQLiteDatabase.cpp"
STMT_SRC="SQLiteStatement.cpp"

if [ "$OS" = "Darwin" ]; then
  echo "Detected macOS. Building .dylib files..."

  # --- Build SQLiteDatabase.dylib ---
  g++ -s -shared -fPIC -m64 -O3 -o SQLiteDatabase.dylib "$DB_SRC" -lsqlite3
  #cp SQLiteDatabase.dylib ../SQLiteStatement/SQLiteDatabase.dylib
  echo "Built: SQLiteDatabase.dylib"

  # --- Build SQLiteStatement.dylib (links against SQLiteDatabase.dylib) ---
  g++ -s -shared -fPIC -m64 -O3 -o SQLiteStatement.dylib "$STMT_SRC" ./SQLiteDatabase.dylib -lsqlite3 \
    -Wl,-rpath,@loader_path
  echo "Built: SQLiteStatement.dylib"

else
  echo "Detected Linux. Building .so files..."

  # --- Build SQLiteDatabase.so ---
  g++ -s -shared -fPIC -m64 -O3 -o SQLiteDatabase.so "$DB_SRC" -lsqlite3
  #cp SQLiteDatabase.so ../SQLiteStatement/SQLiteDatabase.so
  echo "Built: SQLiteDatabase.so"

  # --- Build SQLiteStatement.so (links against SQLiteDatabase.so) ---
  g++ -s -shared -fPIC -m64 -O3 -o SQLiteStatement.so "$STMT_SRC" ./SQLiteDatabase.so -lsqlite3 \
    -Wl,-rpath='$ORIGIN'
  echo "Built: SQLiteStatement.so"
fi

echo "All builds complete."
