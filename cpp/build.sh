#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

# Pick the Homebrew clang if present (required for C++23 on macOS).
if [[ -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    export CC=/opt/homebrew/opt/llvm/bin/clang
    export CXX=/opt/homebrew/opt/llvm/bin/clang++
fi

echo "── Configure ────────────────────────────────────────────────────────────────"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} \
    ${CC:+-DCMAKE_C_COMPILER="$CC"}

echo ""
echo "── Build (jobs=$JOBS) ───────────────────────────────────────────────────────"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "── Artifacts ────────────────────────────────────────────────────────────────"
echo "  binary : $BUILD_DIR/loglite"
echo "  tests  : $BUILD_DIR/tests/loglite_tests"
echo "  compdb : $BUILD_DIR/compile_commands.json (used by clangd / IDE)"
