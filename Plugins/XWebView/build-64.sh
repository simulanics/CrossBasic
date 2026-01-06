#!/usr/bin/env bash
set -euo pipefail

SRC="XWebView.cpp"
ERROR_LOG="build_error.log"
RELEASE_DIR="release-64"
mkdir -p "${RELEASE_DIR}"

UNAME="$(uname)"

if [[ "${UNAME}" == "Darwin" ]]; then
  OUT="XWebView.dylib"
  echo "↪ Building ${OUT} (macOS/Cocoa/WebKit)..."
  g++ -O3 -std=c++17 -shared -fPIC -x objective-c++ "${SRC}" -o "${OUT}" -framework Cocoa -framework WebKit -ldl -pthread -s 2> "${ERROR_LOG}" || {
    echo "❌ Build failed; see ${ERROR_LOG}:"
    sed -n '1,200p' "${ERROR_LOG}"
    exit 1
  }
  mv -f "${OUT}" "${RELEASE_DIR}/"
  echo "✅ Built ${OUT} -> ${RELEASE_DIR}/"
  otool -L "${RELEASE_DIR}/${OUT}" || true
  exit 0
fi

# Linux build (GTK3 + WebKit2GTK)
if [[ "${UNAME}" == "Linux" ]]; then
  # Determine available webkit package (prefer 4.1 then 4.0)
  if pkg-config --exists webkit2gtk-4.1; then
    WEBKIT_PKG="webkit2gtk-4.1"
  elif pkg-config --exists webkit2gtk-4.0; then
    WEBKIT_PKG="webkit2gtk-4.0"
  else
    echo "❌ Neither webkit2gtk-4.1 nor webkit2gtk-4.0 found. Install libwebkit2gtk dev package." >&2
    exit 1
  fi

  if ! pkg-config --exists gtk+-3.0; then
    echo "❌ gtk+-3.0 not found; install libgtk-3-dev." >&2
    exit 1
  fi

  CFLAGS="$(pkg-config --cflags gtk+-3.0 ${WEBKIT_PKG})"
  LIBS="$(pkg-config --libs gtk+-3.0 ${WEBKIT_PKG})"

  OUT="XWebView.so"
  echo "↪ Building ${OUT} using ${WEBKIT_PKG}..."
  g++ -O3 -std=c++17 -fPIC "${SRC}" -shared ${CFLAGS} -o "${OUT}" ${LIBS} -ldl -pthread -s 2> "${ERROR_LOG}" || {
    echo "❌ Build failed; see ${ERROR_LOG}:"
    sed -n '1,200p' "${ERROR_LOG}"
    exit 1
  }

  mv -f "${OUT}" "${RELEASE_DIR}/"
  echo "✅ Built ${OUT} -> ${RELEASE_DIR}/"
  ldd "${RELEASE_DIR}/${OUT}" || true
  exit 0
fi

# Windows: keep a simple MinGW build (requires system WebView2 loader availability at runtime)
OUT="XWebView.dll"
echo "↪ Building ${OUT} (Windows/MinGW)..."
g++ -O3 -std=c++17 -shared -o "${OUT}" "${SRC}" -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows -mwindows -pthread 2> "${ERROR_LOG}" || {
  echo "❌ Build failed; see ${ERROR_LOG}:"
  sed -n '1,200p' "${ERROR_LOG}"
  exit 1
}
mv -f "${OUT}" "${RELEASE_DIR}/"
echo "✅ Built ${OUT} -> ${RELEASE_DIR}/"
