#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR="/opt/sos"
DESKTOP_DIR="/usr/share/applications"

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root: sudo ./uninstall_sos.sh" >&2
  exit 1
fi

rm -f "$DESKTOP_DIR/sos.desktop"
rm -rf "$INSTALL_DIR"

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
fi

echo "Uninstalled from ${INSTALL_DIR}"
