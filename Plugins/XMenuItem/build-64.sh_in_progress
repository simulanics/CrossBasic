#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building XMenuItem.dylib..."
  g++ -shared -fPIC -m64 -static -static-libgcc -static-libstdc++ -o XMenuItem.dylib XMenuItem.cpp -pthread
  echo "Build complete: XMenuItem.dylib"
else
  echo "Detected Linux. Building XMenuItem.so..."
  g++ -shared -fPIC -m64 -o XMenuItem.so XMenuItem.cpp -pthread
  echo "Build complete: XMenuItem.so"
fi
