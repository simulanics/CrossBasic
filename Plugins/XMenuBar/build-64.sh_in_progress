#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building XMenuBar.dylib..."
  g++ -shared -fPIC -m64 -static -static-libgcc -static-libstdc++ -o XMenuBar.dylib XMenuBar.cpp -pthread
  echo "Build complete: XMenuBar.dylib"
else
  echo "Detected Linux. Building XMenuBar.so..."
  g++ -shared -fPIC -m64 -o XMenuBar.so XMenuBar.cpp -pthread
  echo "Build complete: XMenuBar.so"
fi
