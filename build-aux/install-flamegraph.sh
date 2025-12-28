#!/usr/bin/env bash
set -euo pipefail

# Local helper to fetch Brendan Gregg's FlameGraph scripts.
# Used by tests/e2e when --perf-flamegraph is enabled.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="${ROOT_DIR}/tools/FlameGraph"
REPO_URL="https://github.com/brendangregg/FlameGraph"

if [[ -d "${DEST_DIR}/.git" ]]; then
  echo "[install-flamegraph] Updating existing clone: ${DEST_DIR}"
  git -C "${DEST_DIR}" pull --ff-only
  exit 0
fi

if [[ -e "${DEST_DIR}" ]]; then
  echo "[install-flamegraph] ERROR: ${DEST_DIR} exists but is not a git clone"
  exit 1
fi

echo "[install-flamegraph] Cloning FlameGraph into: ${DEST_DIR}"
git clone --depth 1 "${REPO_URL}" "${DEST_DIR}"

echo "[install-flamegraph] Done"
