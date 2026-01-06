#!/usr/bin/env bash

# build.sh
# Usage: ./build.sh
# Automatically detects macOS or Linux and builds accordingly.

if [ "$(uname -s)" = "Darwin" ]; then
  echo "Detected macOS. Building XButton.dylib..."
  g++ -shared -fPIC -m64 -static -static-libgcc -static-libstdc++ -o XButton.dylib XButton.cpp -pthread
  echo "Build complete: XButton.dylib"
else
  echo "Detected Linux. Building XButton.so..."
  g++ -shared -fPIC -m64 -o XButton.so XButton.cpp -pthread
  echo "Build complete: XButton.so"
fi
