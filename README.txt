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
