#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_NAME="SOS-Game"
APPIMAGE_OUT="${ROOT_DIR}/SOS-Game-x86_64-glibc2.27.AppImage"
BUILD_DIR="${ROOT_DIR}/build"
APPDIR="${BUILD_DIR}/AppDir"

BIN_NAME="sos"

mkdir -p "${BUILD_DIR}"
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

echo "Building binary..."
if command -v pkg-config >/dev/null 2>&1; then
  CFLAGS="$(pkg-config --cflags sdl2 SDL2_ttf)"
  LIBS="$(pkg-config --libs sdl2 SDL2_ttf)"
else
  CFLAGS="$(sdl2-config --cflags)"
  LIBS="$(sdl2-config --libs) -lSDL2_ttf"
fi

gcc -O2 -pipe -s -o "${BUILD_DIR}/${BIN_NAME}" "${ROOT_DIR}/sos.c" ${CFLAGS} ${LIBS} -lm -lpthread

cp "${BUILD_DIR}/${BIN_NAME}" "${APPDIR}/usr/bin/${BIN_NAME}"
chmod +x "${APPDIR}/usr/bin/${BIN_NAME}"

cat > "${APPDIR}/sos.desktop" <<EOF
[Desktop Entry]
Name=SOS Game
Comment=Multiplayer SOS game
Exec=${BIN_NAME}
Icon=sos
Type=Application
Categories=Game;
Terminal=false
EOF

cp "${APPDIR}/sos.desktop" "${APPDIR}/usr/share/applications/sos.desktop"

if [[ -f "${ROOT_DIR}/assets/sos.svg" ]]; then
  cp "${ROOT_DIR}/assets/sos.svg" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/sos.svg"
  cp "${ROOT_DIR}/assets/sos.svg" "${APPDIR}/sos.svg"
fi

cat > "${APPDIR}/AppRun" <<'EOF'
#!/usr/bin/env bash
HERE="$(cd "$(dirname "$0")" && pwd)"
exec "${HERE}/usr/bin/sos" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

if ! command -v appimagetool >/dev/null 2>&1; then
  echo "appimagetool not found in PATH." >&2
  echo "Install it or place it in PATH, then re-run this script." >&2
  exit 1
fi

echo "Packaging AppImage..."
appimagetool "${APPDIR}" "${APPIMAGE_OUT}"

echo "Done: ${APPIMAGE_OUT}"
