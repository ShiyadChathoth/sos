SOS Game (Linux) Installer Package

Contents:
- SOS-Game-x86_64-glibc2.27.AppImage
- install_sos.sh
- uninstall_sos.sh
- assets/sos.svg

GitHub Quick Start (minimum commands):
Install from GitHub (one line):
  git clone https://github.com/ShiyadChathoth/sos.git && cd sos && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage

Update from GitHub (one line):
  cd sos && git pull && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage

Modify the code (minimum):
  git clone https://github.com/ShiyadChathoth/sos.git && cd sos

Install:
1) Open a terminal in this folder.
2) Run:
   sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage

Uninstall:
1) Run:
   sudo ./uninstall_sos.sh

Notes:
- The app is installed to /opt/sos
- A desktop launcher is created in /usr/share/applications

Rebuild AppImage on Ubuntu 18.04 (GLIBC 2.27):
If apt-get update fails because 18.04 is end-of-life, switch to old-releases:
  sudo sed -i 's|http://archive.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' /etc/apt/sources.list

Install build dependencies:
  sudo apt-get update
  sudo apt-get install -y build-essential pkg-config libsdl2-dev libsdl2-ttf-dev curl

Download appimagetool and rebuild:
  curl -L -o /tmp/appimagetool https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x /tmp/appimagetool
  APPIMAGE_EXTRACT_AND_RUN=1 PATH="/tmp:$PATH" ./build_appimage.sh

Verify the bundled GLIBC symbols:
  ./SOS-Game-x86_64-glibc2.27.AppImage --appimage-extract >/dev/null 2>&1
  strings squashfs-root/usr/bin/sos | grep GLIBC_ | sort -u | tail -n 5
You should see GLIBC_2.27 and no GLIBC_2.34.
