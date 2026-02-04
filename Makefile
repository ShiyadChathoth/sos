.PHONY: install update

APPIMAGE := SOS-Game-x86_64-glibc2.27.AppImage

install:
	sudo ./install_sos.sh ./$(APPIMAGE)

update:
	git pull
	sudo ./install_sos.sh ./$(APPIMAGE)
