#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"          # shared with build.sh
TEST_BIN="$BUILD_DIR/tests/loglite_tests"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"

# ── Parse flags ───────────────────────────────────────────────────────────────
#
# --cov   Compile with coverage instrumentation, run all tests, print lcov
#         summary + per-file table to the terminal.
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

# ── Configure ─────────────────────────────────────────────────────────────────

echo "── Configure ────────────────────────────────────────────────────────────────"

# Explicitly set LOGLITE_COVERAGE either way: switching between --cov and plain
# mode triggers a cmake reconfigure so the instrumentation is always in sync.
if [[ $COVERAGE -eq 1 ]]; then
    COVERAGE_CMAKE=(-DLOGLITE_COVERAGE=ON)
else
    COVERAGE_CMAKE=(-DLOGLITE_COVERAGE=OFF)
fi

cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    ${CXX:+-DCMAKE_CXX_COMPILER="$CXX"} \
    ${CC:+-DCMAKE_C_COMPILER="$CC"} \
    "${COVERAGE_CMAKE[@]}"

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
    exec "$TEST_BIN" "${GTEST_ARGS[@]}"
fi
