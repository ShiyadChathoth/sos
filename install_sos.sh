#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE_NAME="SOS-Game-x86_64-glibc2.27.AppImage"
APPIMAGE_SRC="${SCRIPT_DIR}/${APPIMAGE_NAME}"
REPO_RAW_BASE="https://raw.githubusercontent.com/ShiyadChathoth/sos/master"
APPIMAGE_URL="${REPO_RAW_BASE}/${APPIMAGE_NAME}"
ICON_URL="${REPO_RAW_BASE}/assets/sos.svg"

INSTALL_DIR="/opt/sos"
DESKTOP_DIR="/usr/share/applications"
ICON_DEST="${INSTALL_DIR}/sos.svg"
ICON_LINE=""
MODE="local"

print_usage() {
  echo "Usage:"
  echo "  sudo ./install_sos.sh /path/to/${APPIMAGE_NAME}"
  echo "  sudo ./install_sos.sh --download"
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  print_usage
  exit 0
fi

if [[ "${1:-}" == "--download" ]]; then
  MODE="download"
elif [[ -n "${1:-}" ]]; then
  APPIMAGE_SRC="$1"
fi

if [[ $EUID -ne 0 ]]; then
  echo "Please run as root." >&2
  print_usage >&2
  exit 1
fi

mkdir -p "$INSTALL_DIR"

if [[ "$MODE" == "download" ]]; then
  if ! command -v curl >/dev/null 2>&1; then
    echo "curl is required for --download mode" >&2
    exit 1
  fi

  TMP_APPIMAGE="$(mktemp)"
  cleanup() {
    rm -f "$TMP_APPIMAGE"
  }
  trap cleanup EXIT

  echo "Downloading AppImage..."
  curl -fL "$APPIMAGE_URL" -o "$TMP_APPIMAGE"
  install -m 0755 "$TMP_APPIMAGE" "${INSTALL_DIR}/${APPIMAGE_NAME}"

  echo "Downloading icon..."
  if curl -fL "$ICON_URL" -o "$ICON_DEST"; then
    ICON_LINE="Icon=${ICON_DEST}"
  elif [[ -f "$ICON_DEST" ]]; then
    ICON_LINE="Icon=${ICON_DEST}"
  else
    echo "Warning: icon download failed, launcher will use default icon." >&2
  fi
else
  if [[ ! -f "$APPIMAGE_SRC" ]]; then
    echo "AppImage not found: $APPIMAGE_SRC" >&2
    print_usage >&2
    exit 1
  fi

  cp "$APPIMAGE_SRC" "${INSTALL_DIR}/${APPIMAGE_NAME}"
  chmod +x "${INSTALL_DIR}/${APPIMAGE_NAME}"

  ICON_SRC_SVG="${SCRIPT_DIR}/assets/sos.svg"
  if [[ -f "$ICON_SRC_SVG" ]]; then
    cp "$ICON_SRC_SVG" "$ICON_DEST"
  fi

  if [[ -f "$ICON_DEST" ]]; then
    ICON_LINE="Icon=${ICON_DEST}"
  fi
fi

cat > "$DESKTOP_DIR/sos.desktop" <<EOF2
[Desktop Entry]
Name=SOS Game
Comment=Multiplayer SOS game
Exec=${INSTALL_DIR}/${APPIMAGE_NAME}
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
echo "Run: ${INSTALL_DIR}/${APPIMAGE_NAME}"
