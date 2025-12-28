#!/usr/bin/env bash
set -euo pipefail

# Install sudoers + optional persistent sysctl config to enable perf profiling without repeated prompts.
# This is intended for local developer machines only.

usage() {
  cat <<'EOF'
Usage:
  build-aux/install-perf-sudoers.sh [--no-sudoers] [--no-sysctl-conf]

What it does (defaults):
  - Installs /etc/sudoers.d/c64stream-perf allowing passwordless sysctl for:
      kernel.perf_event_paranoid=1
      kernel.kptr_restrict=0
  - Installs /etc/sysctl.d/60-c64stream-perf.conf with the same values (persistent)

Notes:
  - Run via sudo: sudo build-aux/install-perf-sudoers.sh
  - This changes system-wide kernel settings; remove the files to undo.
EOF
}

NO_SUDOERS=false
NO_SYSCTL_CONF=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-sudoers) NO_SUDOERS=true; shift ;;
    --no-sysctl-conf) NO_SYSCTL_CONF=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ $(id -u) -ne 0 ]]; then
  echo "ERROR: must run as root (use sudo)" >&2
  exit 1
fi

SYSCTL_BIN=$(command -v sysctl || true)
if [[ -z "$SYSCTL_BIN" ]]; then
  echo "ERROR: sysctl not found" >&2
  exit 1
fi

# Apply immediately
"$SYSCTL_BIN" -w kernel.perf_event_paranoid=1 >/dev/null
"$SYSCTL_BIN" -w kernel.kptr_restrict=0 >/dev/null

echo "Applied sysctls: kernel.perf_event_paranoid=1, kernel.kptr_restrict=0"

if [[ "$NO_SYSCTL_CONF" != true ]]; then
  cat > /etc/sysctl.d/60-c64stream-perf.conf <<'EOF'
# Enable perf profiling for c64stream E2E perf capture (local dev).
# Remove this file to revert to distro defaults.
kernel.perf_event_paranoid=1
kernel.kptr_restrict=0
EOF
  echo "Installed /etc/sysctl.d/60-c64stream-perf.conf"
fi

if [[ "$NO_SUDOERS" != true ]]; then
  # Restrict NOPASSWD to exactly these two sysctl invocations.
  # Use the current invoking user if available.
  INVOKING_USER=${SUDO_USER:-}
  if [[ -z "$INVOKING_USER" ]]; then
    echo "WARN: SUDO_USER is empty; skipping sudoers install. Run via sudo to enable it." >&2
  else
    cat > /etc/sudoers.d/c64stream-perf <<EOF
# Allow ${INVOKING_USER} to adjust perf profiling sysctls without a password.
# Installed by c64stream build-aux/install-perf-sudoers.sh
${INVOKING_USER} ALL=(root) NOPASSWD: ${SYSCTL_BIN} -w kernel.perf_event_paranoid=1
${INVOKING_USER} ALL=(root) NOPASSWD: ${SYSCTL_BIN} -w kernel.kptr_restrict=0
EOF
    chmod 0440 /etc/sudoers.d/c64stream-perf
    echo "Installed /etc/sudoers.d/c64stream-perf (NOPASSWD sysctl for ${INVOKING_USER})"
  fi
fi
