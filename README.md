# SOS Game (Ubuntu 18.04+)

This package contains a GLIBC 2.27 compatible AppImage and installer scripts.

## GitHub Quick Start (Minimum Commands)
Install from GitHub (one line):
```bash
git clone https://github.com/ShiyadChathoth/sos.git && cd sos && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage
```

Update from GitHub (one line, re-installs to pick up the new AppImage):
```bash
cd sos && git pull && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage
```

Modify the code (minimum):
```bash
git clone https://github.com/ShiyadChathoth/sos.git && cd sos
```

## Contents
- `SOS-Game-x86_64-glibc2.27.AppImage`
- `install_sos.sh`
- `uninstall_sos.sh`
- `assets/sos.svg`

## Install
1. Open a terminal in this folder.
2. Run:
   ```bash
   sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage
   ```

## Uninstall
```bash
sudo ./uninstall_sos.sh
```

## Run without install
```bash
./SOS-Game-x86_64-glibc2.27.AppImage
```
