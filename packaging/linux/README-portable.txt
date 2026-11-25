npad for Linux - portable download
==================================

Version @VERSION@, @ARCH@.

npad is a lightweight Notepad-style text editor. This archive holds the same
npad binary the .deb, .rpm and Arch (pacman) packages install, with its
launcher entry, icons and AppStream metadata. Nothing here needs installing
as root.

Requirements
------------
- GTK 3, which almost every Linux desktop already has. If yours does not:
    Debian 13 / Ubuntu 24.04 and newer:  sudo apt install libgtk-3-0t64
    Debian 12 / Ubuntu 22.04:            sudo apt install libgtk-3-0
    Fedora:                              sudo dnf install gtk3
    openSUSE:                            sudo zypper install libgtk-3-0
    Arch:                                sudo pacman -S gtk3
- glibc @GLIBC@ or newer: @DISTROS@ or newer.
- X11 or Wayland; the one binary runs on both.

Run it
------
From the directory this README is in:

    ./npad
    ./npad notes.txt

Add it to your applications menu
--------------------------------
Copy the files under ~/.local (your own account only; run these from the
directory this README is in):

    mkdir -p ~/.local/bin ~/.local/share
    cp npad ~/.local/bin/
    cp -r share/. ~/.local/share/
    sed -i "s|^Exec=npad |Exec=$HOME/.local/bin/npad |" ~/.local/share/applications/io.github.platima.npad.desktop

npad then appears in the menu (log out and back in if it does not show at
once). Optionally, so "Open With" lists npad for text files:

    update-desktop-database ~/.local/share/applications

To remove it again:

    rm -f ~/.local/bin/npad
    rm -f ~/.local/share/applications/io.github.platima.npad.desktop
    rm -f ~/.local/share/metainfo/io.github.platima.npad.metainfo.xml
    rm -f ~/.local/share/icons/hicolor/*/apps/io.github.platima.npad*.png

Your settings, and the crash-recovery snapshots of unsaved work (in its
recovery folder), live in ~/.config/npad - or in $XDG_CONFIG_HOME/npad when
XDG_CONFIG_HOME is set to an absolute path - and are left alone either way.

Automatic updates
-----------------
This copy does not update itself; Help > Check for Updates tells you when
there is a new release. For updates with the rest of your system, use the
signed npad repository for your distribution instead - apt (Debian, Ubuntu,
Mint), dnf (Fedora) or pacman (Arch Linux, Arch Linux ARM, Arch RISC-V) -
from https://pkg.npad.dev, or let one script set up the right one:

    curl -fsSL https://pkg.npad.dev/install.sh | sudo sh

More: https://npad.dev  -  https://github.com/platima/npad
