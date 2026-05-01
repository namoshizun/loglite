#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

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
TARGET_TOOLCHAIN="$SCRIPT_DIR/cmake/toolchains/$TARGET.cmake"
if [[ ! -f "$TARGET_TOOLCHAIN" ]]; then
    TARGET_TOOLCHAIN=""
fi

BUILD_DIR="$SCRIPT_DIR/build/$TARGET/debug"
TEST_BIN="$BUILD_DIR/tests/loglite_tests"

# ── Parse flags ───────────────────────────────────────────────────────────────
#
# --cov   Compile with coverage instrumentation, run all tests, print lcov
#         summary + per-file table to the terminal.
# CMake is always configured with BUILD_TESTING=ON (see cmake invocation below).
# All other arguments are forwarded verbatim to the GTest runner (ignored when
# --cov is given, since coverage always runs the full suite).

COVERAGE=0
GTEST_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --cov) COVERAGE=1 ;;
        *) GTEST_ARGS+=("$arg") ;;
    esac
done

# ── Compiler ──────────────────────────────────────────────────────────────────
# MacOS.
if [[ -x /opt/homebrew/opt/llvm/bin/clang++ ]]; then
    export CC=/opt/homebrew/opt/llvm/bin/clang
    export CXX=/opt/homebrew/opt/llvm/bin/clang++
fi

# ── Conan (this script always configures Debug) ───────────────────────────────
echo "── Conan (Debug) ─────────────────────────────────────────────────────────────"
CONAN_ARGS=(
    "$SCRIPT_DIR"
    --output-folder="$BUILD_DIR"
    --settings:h "os=$CONAN_OS"
    --settings:h "arch=$CONAN_ARCH"
    --settings:h build_type=Debug
    --settings:h compiler.cppstd=23
    --settings:b build_type=Release
    --conf "tools.cmake.cmaketoolchain:user_presets="
    --build=missing
)

if [[ -n "$TARGET_TOOLCHAIN" ]]; then
    CONAN_ARGS+=(--conf "tools.cmake.cmaketoolchain:user_toolchain+=$TARGET_TOOLCHAIN")
fi

conan install "${CONAN_ARGS[@]}"

# ── Configure ─────────────────────────────────────────────────────────────────
echo "── Configure ────────────────────────────────────────────────────────────────"

# Explicitly set LOGLITE_COVERAGE either way: switching between --cov and plain
# mode triggers a cmake reconfigure so the instrumentation is always in sync.
if [[ $COVERAGE -eq 1 ]]; then
    COVERAGE_FLAG=-DLOGLITE_COVERAGE=ON
else
    COVERAGE_FLAG=-DLOGLITE_COVERAGE=OFF
fi

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_TOOLCHAIN_FILE="$BUILD_DIR/conan_toolchain.cmake"
    -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DBoost_DIR="$BUILD_DIR"
    -DBUILD_TESTING=ON
    "$COVERAGE_FLAG"
)

if [[ -n "${CXX:-}" ]]; then
    CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="$CXX")
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

# ── Build & Run ───────────────────────────────────────────────────────────────

echo ""
echo "── Build (jobs=$JOBS) ───────────────────────────────────────────────────────"

if [[ $COVERAGE -eq 1 ]]; then
    # loglite_coverage builds loglite_tests (DEPENDS), resets counters, runs the
    # suite, collects gcov data and prints the lcov report — all in one target.
    cmake --build "$BUILD_DIR" --target loglite_coverage -j"$JOBS"
else
    cmake --build "$BUILD_DIR" --target loglite_tests -j"$JOBS"
    echo ""
    echo "── Tests ────────────────────────────────────────────────────────────────────"
    exec "$TEST_BIN" ${GTEST_ARGS[@]+"${GTEST_ARGS[@]}"}
fi
