#!/bin/sh
# Build npad_<version>_<arch>.deb from an installed staging tree.
# Usage: scripts/build-deb.sh <version> <binary> <outdir>
set -eu
version=$1
binary=$2
outdir=$3
arch=$(dpkg --print-architecture 2>/dev/null || echo amd64)
pkg=npad_${version}_${arch}
root=$outdir/$pkg

rm -rf "$root"
make install DESTDIR="$root" PREFIX=/usr

# Highest glibc symbol version the binary needs becomes the libc6 floor, so
# the .deb stays installable on the oldest distro that can run it.
glibc=$(objdump -T "$binary" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n1)
[ -n "$glibc" ] || glibc=2.31

size=$(du -sk "$root" | cut -f1)
mkdir -p "$root/DEBIAN"
# libgtk-3-0t64 is the name since Debian 13 / Ubuntu 24.04 (64-bit time_t
# transition); older releases ship libgtk-3-0. Either satisfies npad.
cat > "$root/DEBIAN/control" <<CONTROL
Package: npad
Version: $version
Architecture: $arch
Maintainer: Platima <noreply@users.noreply.github.com>
Section: editors
Priority: optional
Installed-Size: $size
Depends: libc6 (>= $glibc), libgtk-3-0t64 (>= 3.24) | libgtk-3-0 (>= 3.24)
Homepage: https://github.com/platima/npad
Description: Lightweight Notepad-style text editor
 npad is a small, fast text editor inspired by classic Windows Notepad.
 It preserves each file's encoding and line endings and saves atomically.
CONTROL

mkdir -p "$outdir"
dpkg-deb --root-owner-group --build "$root" "$outdir/$pkg.deb"
echo "Built $outdir/$pkg.deb"
