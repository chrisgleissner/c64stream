#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

# Enforce deterministic A/V sync ordering to avoid cross-test pollution:
#   1) Mocked sender
#   2) Real device
#   3) Mocked sender (verifies device reset / no lingering traffic)
#
# All args are forwarded to e2e.sh (e.g. --duration, --csv-max-rows, --skip-build, --verbose).

RESULTS_BASE="${SCRIPT_DIR}/results/avsync_suite"
rm -rf "${RESULTS_BASE}"
mkdir -p "${RESULTS_BASE}"

common_args=("$@")

bash ./e2e.sh --scenario ntsc_default_avsync --output-dir "${RESULTS_BASE}/01_mock_pre" "${common_args[@]}"
bash ./e2e.sh --scenario ntsc_default_avsync_device --output-dir "${RESULTS_BASE}/02_device" "${common_args[@]}"
# Give the device teardown a moment to settle before the post-mock pollution check.
sleep 2
bash ./e2e.sh --scenario ntsc_default_avsync --output-dir "${RESULTS_BASE}/03_mock_post" "${common_args[@]}"

echo "✅ A/V sync suite completed: ${RESULTS_BASE}"
