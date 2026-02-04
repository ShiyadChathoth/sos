#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE_SRC="${1:-${SCRIPT_DIR}/SOS-Game-x86_64-glibc2.27.AppImage}"

INSTALL_DIR="/opt/sos"
DESKTOP_DIR="/usr/share/applications"
ICON_DEST="${INSTALL_DIR}/sos.svg"

if [[ ! -f "$APPIMAGE_SRC" ]]; then
  echo "AppImage not found: $APPIMAGE_SRC" >&2
  echo "Usage: sudo ./install_sos.sh /path/to/sos-x86_64.AppImage" >&2
  exit 1
fi

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root: sudo ./install_sos.sh /path/to/sos-x86_64.AppImage" >&2
  exit 1
fi

mkdir -p "$INSTALL_DIR"
cp "$APPIMAGE_SRC" "$INSTALL_DIR/sos-x86_64.AppImage"
chmod +x "$INSTALL_DIR/sos-x86_64.AppImage"

ICON_LINE=""
ICON_SRC_SVG="${SCRIPT_DIR}/assets/sos.svg"
if [[ -f "$ICON_SRC_SVG" ]]; then
  cp "$ICON_SRC_SVG" "$ICON_DEST"
  ICON_LINE="Icon=${ICON_DEST}"
fi

cat > "$DESKTOP_DIR/sos.desktop" <<EOF2
[Desktop Entry]
Name=SOS Game
Comment=Multiplayer SOS game
Exec=${INSTALL_DIR}/sos-x86_64.AppImage
${ICON_LINE}
Type=Application
Categories=Game;
Terminal=false
StartupNotify=true
EOF2

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$DESKTOP_DIR" >/dev/null 2>&1 || true
fi

echo "Installed to ${INSTALL_DIR}"
echo "Desktop launcher: ${DESKTOP_DIR}/sos.desktop"
