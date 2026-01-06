#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building FileWatcher.dylib..."
  g++ -s -shared -fPIC -m64 -O3 -o FileWatcher.dylib FileWatcher.cpp
  echo "Build complete: FileWatcher.dylib"
else
  echo "Detected Linux. Building FileWatcher.so..."
  g++ -s -shared -fPIC -m64 -O3 -o FileWatcher.so FileWatcher.cpp
  echo "Build complete: FileWatcher.so"
fi
