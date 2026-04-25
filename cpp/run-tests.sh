#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
TEST_BIN="$BUILD_DIR/tests/loglite_tests"

if [[ ! -x "$TEST_BIN" ]]; then
    echo "Test binary not found. Run build.sh first." >&2
    exit 1
fi

# Forward any extra arguments directly to the GTest runner.
# Examples:
#   ./run-tests.sh                                  # all tests
#   ./run-tests.sh --gtest_filter="DatabaseTest.*"  # one suite
#   ./run-tests.sh --gtest_filter="ConfigTest.*" --gtest_repeat=5
exec "$TEST_BIN" "$@"
