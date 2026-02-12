## One-Line Install (Recommended)
No clone needed. This single command downloads and runs the installer script, then installs AppImage + desktop icon/launcher:

```bash
curl -fsSL https://raw.githubusercontent.com/ShiyadChathoth/sos/master/install_sos.sh | sudo bash -s -- --download
```

## One-Line Update
Run the same command anytime to update to latest AppImage and refresh launcher/icon:

```bash
curl -fsSL https://raw.githubusercontent.com/ShiyadChathoth/sos/master/install_sos.sh | sudo bash -s -- --download
```

## Quick Install (From Cloned Repo)
To install from local files in this repo:

```bash
chmod +x install_sos.sh && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage
```

## Update App (From Cloned Repo)
From inside cloned `sos` folder:

```bash
git pull && chmod +x install_sos.sh && sudo ./install_sos.sh ./SOS-Game-x86_64-glibc2.27.AppImage
```

# SOS Game (Ubuntu 18.04+)

This package contains a GLIBC 2.27 compatible AppImage and installer scripts.

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
