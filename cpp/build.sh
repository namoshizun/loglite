#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CONAN_TOOLCHAIN="$BUILD_DIR/conan_toolchain.cmake"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

# ── Parse flags ───────────────────────────────────────────────────────────────

RELEASE=0
for arg in "$@"; do
    case "$arg" in
        --release) RELEASE=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

if [[ $RELEASE -eq 1 ]]; then
    BUILD_TYPE=Release
    EXTRA_CMAKE_FLAGS=(-DLOGLITE_LTO=ON -DLOGLITE_STRIP=ON)
    echo "Mode: RELEASE  (O3 · LTO · stripped · hidden symbols · dead-code elimination)"
else
    BUILD_TYPE=Debug
    EXTRA_CMAKE_FLAGS=()
    echo "Mode: DEBUG  (unoptimised · debug symbols · fast recompile)"
fi

# ── Compiler ──────────────────────────────────────────────────────────────────
# MacOS.
if [[ -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    export CC=/opt/homebrew/opt/llvm/bin/clang
    export CXX=/opt/homebrew/opt/llvm/bin/clang++
fi

# ── Conan (CMakeDeps matches CMAKE_BUILD_TYPE) ────────────────────────────────
if ! command -v conan >/dev/null 2>&1; then
    echo "error: conan not found in PATH (install Conan 2)" >&2
    exit 1
fi

echo ""
echo "── Conan ($BUILD_TYPE) ───────────────────────────────────────────────────────"
conan install "$SCRIPT_DIR" --output-folder="$BUILD_DIR" -s "build_type=$BUILD_TYPE" --build=missing

# ── Configure ─────────────────────────────────────────────────────────────────

echo ""
echo "── Configure ────────────────────────────────────────────────────────────────"
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="$CONAN_TOOLCHAIN" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DLOGLITE_COVERAGE=OFF \
    -DBUILD_TESTING=OFF \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} \
    ${CC:+-DCMAKE_C_COMPILER="$CC"} \
    ${EXTRA_CMAKE_FLAGS[@]+"${EXTRA_CMAKE_FLAGS[@]}"}

# ── Build ─────────────────────────────────────────────────────────────────────
echo ""
echo "── Build (jobs=$JOBS) ───────────────────────────────────────────────────────"
cmake --build "$BUILD_DIR" --target loglite -j"$JOBS"

# ── Report ────────────────────────────────────────────────────────────────────
BINARY="$BUILD_DIR/loglite"
SIZE_BYTES=$(stat -f%z "$BINARY" 2>/dev/null || stat -c%s "$BINARY")
SIZE_KB=$(( SIZE_BYTES / 1024 ))

echo ""
echo "── Artifacts ────────────────────────────────────────────────────────────────"
printf "  %-8s %s  (%s KB)\n" "binary"  "$BINARY"                        "$SIZE_KB"
printf "  %-8s %s\n"          "compdb"  "$BUILD_DIR/compile_commands.json"
