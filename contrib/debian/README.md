
Debian
====================
This directory contains files used to package stockd/stock-qt
for Debian-based Linux systems. If you compile stockd/stock-qt yourself, there are some useful files here.

## stock: URI support ##


stock-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install stock-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your stock-qt binary to `/usr/bin`
and the `../../share/pixmaps/stock128.png` to `/usr/share/pixmaps`

stock-qt.protocol (KDE)

