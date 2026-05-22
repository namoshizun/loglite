#!/usr/bin/env bash
# Build the frontend and print bundle size breakdown + treemap report.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

if ! command -v node >/dev/null 2>&1; then
  echo "error: node is required" >&2
  exit 1
fi

if [[ ! -d node_modules ]]; then
  echo "Installing dependencies..."
  npm ci 2>/dev/null || npm install
fi

if ! node -e "require.resolve('rollup-plugin-visualizer')" >/dev/null 2>&1; then
  echo "Installing rollup-plugin-visualizer (dev)..."
  npm install -D rollup-plugin-visualizer
fi

exec node scripts/bundle-analyze.mjs
