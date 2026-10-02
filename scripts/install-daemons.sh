#!/usr/bin/env bash
# One-time setup for vitals' optional root daemons, LACT-style: installs both
# as systemd services and enables them, so they start now and at every boot
# until explicitly turned off (`sudo systemctl disable --now <service>`).
#   vitals-ramocd — ram_oc_daemon: DRAM timings, SMU telemetry, memory benchmark
#   vitals-gpud   — gpu_ctl_daemon: GPU power limit / clocks / fan control
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

for bin in ram_oc_daemon gpu_ctl_daemon; do
    if [ ! -x "$BUILD_DIR/$bin" ]; then
        echo "$bin not found in $BUILD_DIR — build first (cmake --build build), or pass the build dir." >&2
        exit 1
    fi
done

# Stop running instances first so the binaries can be replaced (re-running
# this script is how you update the daemons after a rebuild).
systemctl stop vitals-ramocd.service vitals-gpud.service 2>/dev/null || true
pkill -x ram_oc_daemon 2>/dev/null || true   # a manually started instance would hold the socket

install -Dm755 "$BUILD_DIR/ram_oc_daemon" /usr/libexec/vitals/ram_oc_daemon
install -Dm755 "$BUILD_DIR/gpu_ctl_daemon" /usr/sbin/gpu_ctl_daemon
install -Dm644 "$REPO_DIR/packaging/systemd/vitals-ramocd.service" /etc/systemd/system/vitals-ramocd.service
install -Dm644 "$REPO_DIR/packaging/systemd/vitals-gpud.service" /etc/systemd/system/vitals-gpud.service

getent group vitals-gpu >/dev/null || groupadd --system vitals-gpu
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] && ! id -nG "$SUDO_USER" | tr ' ' '\n' | grep -qx vitals-gpu; then
    usermod -aG vitals-gpu "$SUDO_USER"
    echo "Added $SUDO_USER to vitals-gpu — log out and back in once to use GPU controls."
fi

systemctl daemon-reload
systemctl enable --now vitals-ramocd.service vitals-gpud.service

echo "Done. Both daemons run now and start at every boot."
echo "Turn one off with: sudo systemctl disable --now vitals-ramocd   (or vitals-gpud)"
