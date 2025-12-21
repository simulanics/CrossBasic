#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building JSONItem.dylib..."
  g++ -s -shared -fPIC -m64 -O3 -o JSONItem.dylib JSONItem.cpp
  echo "Build complete: JSONItem.dylib"
else
  echo "Detected Linux. Building JSONItem.so..."
  g++ -s -shared -fPIC -m64 -O3 -o JSONItem.so JSONItem.cpp
  echo "Build complete: JSONItem.so"
fi
