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

## Rebuild AppImage on Ubuntu 18.04 (GLIBC 2.27)
If `apt-get update` fails because 18.04 is end-of-life, switch to old-releases:
```bash
sudo sed -i 's|http://archive.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g; s|http://security.ubuntu.com/ubuntu|http://old-releases.ubuntu.com/ubuntu|g' /etc/apt/sources.list
```

Minimal, copy/paste rebuild on the Ubuntu 18.04 host:
```bash
cd /home/shiyad/sos

sudo apt-get update
sudo apt-get install -y build-essential pkg-config libsdl2-dev libsdl2-ttf-dev curl

curl -L -o /tmp/appimagetool https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x /tmp/appimagetool
APPIMAGE_EXTRACT_AND_RUN=1 PATH="/tmp:$PATH" ./build_appimage.sh

./SOS-Game-x86_64-glibc2.27.AppImage --appimage-extract >/dev/null 2>&1
strings squashfs-root/usr/bin/sos | grep GLIBC_ | sort -u | tail -n 5
```

You should see `GLIBC_2.27` and no `GLIBC_2.34`. Please send the `GLIBC_` output so I can confirm.

After rebuild, update the repo from the same machine:
```bash
git add SOS-Game-x86_64-glibc2.27.AppImage
git commit -m "Rebuild AppImage on Ubuntu 18.04"
git push
```
