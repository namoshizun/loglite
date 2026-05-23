#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CPP_DIR="$SCRIPT_DIR/../cpp"
BUILD_ROOT="$SCRIPT_DIR/build"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

RELEASE=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)
            RELEASE=1
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [--release]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"

case "$HOST_OS" in
    Darwin)
        TARGET_OS=apple
        CONAN_OS=Macos
        ;;
    Linux)
        TARGET_OS=linux
        CONAN_OS=Linux
        ;;
    *)
        echo "error: unsupported OS: $HOST_OS" >&2
        exit 1
        ;;
esac

case "$HOST_ARCH" in
    arm64|aarch64)
        TARGET_ARCH=arm64
        CONAN_ARCH=armv8
        ;;
    x86_64|amd64)
        TARGET_ARCH=x86_64
        CONAN_ARCH=x86_64
        ;;
    *)
        echo "error: unsupported architecture: $HOST_ARCH" >&2
        exit 1
        ;;
esac

TARGET="$TARGET_OS-$TARGET_ARCH"

TARGET_TOOLCHAIN="$CPP_DIR/cmake/toolchains/$TARGET.cmake"
if [[ ! -f "$TARGET_TOOLCHAIN" ]]; then
    TARGET_TOOLCHAIN=""
fi

if [[ $RELEASE -eq 1 ]]; then
    BUILD_TYPE=Release
    BUILD_FLAVOR=release
    RELEASE_FLAGS=(-DBURN_LTO=ON -DBURN_STRIP=ON)
    MODE_LABEL="Mode: RELEASE  (O3 · LTO · stripped · -march=native · hidden symbols · dead-code elimination)"
else
    BUILD_TYPE=Debug
    BUILD_FLAVOR=debug
    RELEASE_FLAGS=(-DBURN_LTO=OFF -DBURN_STRIP=OFF)
    MODE_LABEL="Mode: DEBUG  (unoptimised · debug symbols · fast recompile · IDE compile_commands)"
fi

BUILD_DIR="$BUILD_ROOT/$TARGET/$BUILD_FLAVOR"
echo "$MODE_LABEL"
echo "Target: $TARGET"

if [[ "$TARGET" == "apple-arm64" && -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    export CC=/opt/homebrew/opt/llvm/bin/clang
    export CXX=/opt/homebrew/opt/llvm/bin/clang++
fi

echo ""
echo "── Conan ($TARGET, $BUILD_TYPE) ─────────────────────────────────────────────"
mkdir -p "$BUILD_DIR"
if ! conan profile path default >/dev/null 2>&1; then
    conan profile detect --force
fi

CONAN_ARGS=(
    "$SCRIPT_DIR"
    --output-folder="$BUILD_DIR"
    --settings:h "os=$CONAN_OS"
    --settings:h "arch=$CONAN_ARCH"
    --settings:h "build_type=$BUILD_TYPE"
    --settings:h "compiler.cppstd=20"
    --settings:b "build_type=Release"
    --options:h "*:shared=False"
    --conf "tools.cmake.cmaketoolchain:user_presets="
    --build=missing
)

if [[ -n "$TARGET_TOOLCHAIN" ]]; then
    CONAN_ARGS+=(--conf "tools.cmake.cmaketoolchain:user_toolchain+=$TARGET_TOOLCHAIN")
fi

conan install "${CONAN_ARGS[@]}"

echo ""
echo "── Configure ────────────────────────────────────────────────────────────────"
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake"
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DBoost_DIR="$BUILD_DIR"
)

CMAKE_ARGS+=("${RELEASE_FLAGS[@]}")
if [[ -n "${CXX:-}" ]]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="$CXX")
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

echo ""
echo "── Build (jobs=$JOBS) ───────────────────────────────────────────────────────"
cmake --build "$BUILD_DIR" --target burn -j"$JOBS"

BINARY="$BUILD_DIR/burn"
SIZE_BYTES=$(stat -f%z "$BINARY" 2>/dev/null || stat -c%s "$BINARY")
SIZE_KB=$(( SIZE_BYTES / 1024 ))

echo ""
echo "── Artifacts ────────────────────────────────────────────────────────────────"
printf "  %-8s %s  (%s KB)\n" "binary"  "$BINARY"                        "$SIZE_KB"
printf "  %-8s %s\n"          "compdb"  "$BUILD_DIR/compile_commands.json"
