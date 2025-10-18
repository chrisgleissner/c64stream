#!/usr/bin/env bash

set -euo pipefail

# Determine repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Inputs
GITHUB_EVENT_NAME_INPUT="${GITHUB_EVENT_NAME:-}"
GITHUB_RUN_NUMBER_INPUT="${GITHUB_RUN_NUMBER:-}"

# Utilities
jq_safe() {
    local jq_path="$1"
    if command -v jq >/dev/null 2>&1 && [[ -f "${jq_path}" ]]; then
        jq -r '.' "${jq_path}" >/dev/null 2>&1 || true
        jq -r "$2" "${jq_path}" 2>/dev/null || echo ""
    else
        echo ""
    fi
}

# Resolve base fields from buildspec.json
base_version=$(jq -r '.version' "${PROJECT_ROOT}/buildspec.json" 2>/dev/null || echo "")

# Compute plugin_version using the same semantics as build-project.yaml
plugin_version=""

if [[ "${GITHUB_EVENT_NAME_INPUT}" == "pull_request" ]]; then
    # PR builds: derive dev patch in 900-1899 range from run number, else 999
    if [[ -n "${base_version}" ]]; then
        IFS='.' read -r major minor patch <<<"${base_version}"
        if [[ -n "${GITHUB_RUN_NUMBER_INPUT}" ]]; then
            run_mod=$(( GITHUB_RUN_NUMBER_INPUT % 1000 ))
            dev_patch=$(( 900 + run_mod ))
            plugin_version="${major}.${minor}.${dev_patch}"
        else
            plugin_version="${major}.${minor}.999"
        fi
    fi
else
    # Non-PR builds: use git tag when possible, else fallback to buildspec.json
    git_version_tag=$(git -C "${PROJECT_ROOT}" describe --tags --always --dirty 2>/dev/null || echo "")
    if [[ "${git_version_tag}" =~ ^v?([0-9]+\.[0-9]+\.[0-9]+) ]]; then
        plugin_version=$(echo "${git_version_tag}" | sed -E 's/^v?([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
    else
        plugin_version="${base_version}"
    fi
fi

# Final fallback
if [[ -z "${plugin_version}" ]]; then
    plugin_version="${base_version:-unknown}"
fi

echo "${plugin_version}"
