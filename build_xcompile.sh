#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
SRC="XCompile/xcompile.cpp"
DEFAULT_OUT="xcompile"
RELEASE_DIR="release-64"
ERROR_LOG="error.log"

# allow overriding output binary name: ./build_xcompile.sh mybin
OUT="${1:-$DEFAULT_OUT}"

# --- 0. Sanity / safety checks ---
echo "🔧 Build invoked with output binary name: ${OUT}"
if [[ -d "${OUT}" ]]; then
  echo "❌ ERROR: '${OUT}' exists and is a directory; cannot emit binary with that name."
  echo "    Rename or remove the directory, or invoke with a different output name:"
  echo "    ./$(basename "$0") your_binary_name"
  exit 1
fi

if [[ ! -f "${SRC}" ]]; then
  echo "❌ ERROR: Source file '${SRC}' does not exist."
  exit 1
fi

# --- 1. Prepare release directory ---
mkdir -p "${RELEASE_DIR}"

# --- 2. Compile ---
echo "↪ Compiling ${SRC} to ${OUT}..."
# Uses $CXX if set; otherwise falls back to g++
"${CXX:-g++}" -O3 -march=native -mtune=native -o "${OUT}" "${SRC}" 2> "${ERROR_LOG}" \
  || {
    echo "❌ Compilation failed! See ${ERROR_LOG}:" 
    sed -n '1,200p' "${ERROR_LOG}" || true
    echo "…(full log at ${ERROR_LOG})"
    exit 1
  }

# --- 3. Move binary ---
mv -f "${OUT}" "${RELEASE_DIR}/"

# --- 4. Dump shared-library dependencies ---
echo
echo "🛠 Shared-library dependencies of ${RELEASE_DIR}/${OUT}:"
if [[ "$(uname)" == "Darwin" ]]; then
  otool -L "${RELEASE_DIR}/${OUT}"
else
  ldd "${RELEASE_DIR}/${OUT}"
fi

echo
echo "✅ Build complete. Binary is in ${RELEASE_DIR}/"
exit 0
