#!/usr/bin/env bash
# One-time setup for vitals' optional root daemon: installs it as a systemd
# service and enables it, so it starts now and at every boot
# until explicitly turned off (`sudo systemctl disable --now <service>`).
#   vitals-ramocd — ram_oc_daemon: DRAM timings, SMU telemetry, memory benchmark
# `vitals` itself never runs this or elevates itself; Settings → Daemons shows
# this command and the per-service enable/disable commands.
#
# Usage: sudo scripts/install-daemons.sh [build-dir]   (default: ./build)
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-$REPO_DIR/build}"

if [ ! -x "$BUILD_DIR/ram_oc_daemon" ]; then
    echo "ram_oc_daemon not found in $BUILD_DIR — build first (cmake --build build), or pass the build dir." >&2
    exit 1
fi

# Stop running instances first so the binaries can be replaced (re-running
# this script is how you update the daemon after a rebuild).
systemctl stop vitals-ramocd.service 2>/dev/null || true
pkill -x ram_oc_daemon 2>/dev/null || true   # a manually started instance would hold the socket

install -Dm755 "$BUILD_DIR/ram_oc_daemon" /usr/libexec/vitals/ram_oc_daemon
install -Dm644 "$REPO_DIR/packaging/systemd/vitals-ramocd.service" /etc/systemd/system/vitals-ramocd.service

systemctl daemon-reload
systemctl enable --now vitals-ramocd.service

echo "Done. vitals-ramocd runs now and starts at every boot."
echo "Turn it off with: sudo systemctl disable --now vitals-ramocd"
