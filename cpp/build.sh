#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="$SCRIPT_DIR/build"
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
    BUILD_DIR="$BUILD_ROOT/release"
    RELEASE_FLAGS=(-DLOGLITE_LTO=ON -DLOGLITE_STRIP=ON)
    echo "Mode: RELEASE  (O3 · LTO · stripped · hidden symbols · dead-code elimination)"
else
    BUILD_TYPE=Debug
    BUILD_DIR="$BUILD_ROOT/debug"
    RELEASE_FLAGS=(-DLOGLITE_LTO=OFF -DLOGLITE_STRIP=OFF)
    echo "Mode: DEBUG  (unoptimised · debug symbols · fast recompile)"
fi

# ── Compiler ──────────────────────────────────────────────────────────────────
# MacOS.
if [[ -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    export CC=/opt/homebrew/opt/llvm/bin/clang
    export CXX=/opt/homebrew/opt/llvm/bin/clang++
fi

# ── Conan (CMakeDeps matches CMAKE_BUILD_TYPE) ────────────────────────────────
echo ""
echo "── Conan ($BUILD_TYPE) ───────────────────────────────────────────────────────"
conan install "$SCRIPT_DIR" --output-folder="$BUILD_DIR" --settings "build_type=$BUILD_TYPE" --build=missing

# ── Configure ─────────────────────────────────────────────────────────────────

echo ""
echo "── Configure ────────────────────────────────────────────────────────────────"
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake"
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DBoost_DIR="$BUILD_DIR"
    -DLOGLITE_COVERAGE=OFF
    -DBUILD_TESTING=OFF
)

CMAKE_ARGS+=("${RELEASE_FLAGS[@]}")
if [[ -n "${CXX:-}" ]]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="$CXX")
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

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
