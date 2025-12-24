#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

mapfile -t SCENARIOS < <(find scenarios -maxdepth 2 -name scenario.yaml -print \
  | sed 's#^scenarios/##; s#/scenario.yaml$##' \
  | sort)

if (( ${#SCENARIOS[@]} == 0 )); then
  echo "No scenarios found under scenarios/*/scenario.yaml"
  exit 1
fi

printf "Running %d scenarios...\n" "${#SCENARIOS[@]}"
start_ts=$(date +%s)

for s in "${SCENARIOS[@]}"; do
  echo
  echo "=== RUN ${s} ==="
  ./e2e.sh --scenario "${s}" --skip-build
  echo "=== DONE ${s} ==="
done

duration=$(( $(date +%s) - start_ts ))
echo
printf "ALL SCENARIOS COMPLETED in %ds\n" "${duration}"
