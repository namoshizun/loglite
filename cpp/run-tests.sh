#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/debug"
TEST_BIN="$BUILD_DIR/tests/loglite_tests"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

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
conan install "$SCRIPT_DIR" --output-folder="$BUILD_DIR" --settings build_type=Debug --build=missing

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
