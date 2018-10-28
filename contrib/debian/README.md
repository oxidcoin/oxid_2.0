
Debian
====================
This directory contains files used to package oxidd/oxid-qt
for Debian-based Linux systems. If you compile oxidd/oxid-qt yourself, there are some useful files here.

## oxid: URI support ##


oxid-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install oxid-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your oxidqt binary to `/usr/bin`
and the `../../share/pixmaps/oxid128.png` to `/usr/share/pixmaps`

oxid-qt.protocol (KDE)

