#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building XListbox.dylib..."
  g++ -shared -fPIC -m64 -static -static-libgcc -static-libstdc++ -o XListbox.dylib XListbox.cpp -pthread
  echo "Build complete: XListbox.dylib"
else
  echo "Detected Linux. Building XListbox.so..."
  g++ -shared -fPIC -m64 -o XListbox.so XListbox.cpp -pthread
  echo "Build complete: XListbox.so"
fi
