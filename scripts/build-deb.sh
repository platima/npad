#!/bin/sh
# Build npad_<version>_<arch>.deb from an installed staging tree: the
# fallback `make deb` uses when nfpm is not installed. packages.yml checks
# that its Depends and Version agree with nfpm's.
# Usage: scripts/build-deb.sh <version> <binary> <outdir> [arch]
# arch defaults to the binary's own ELF machine, as in build-package.sh; given
# explicitly, the binary must match it.
set -eu
version=$1
binary=$2
outdir=$3
arch=$(sh "$(dirname "$0")/elf-arch.sh" "$binary" "${4:-}")
# The file keeps the tag's spelling (npad_0.36.0-rc2_amd64.deb), but inside
# the package a pre-release is spelled the way dpkg sorts it before the final
# release, as nfpm spells it: 0.36.0~rc2. "0.36.0-rc2" would read as
# version 0.36.0, Debian revision rc2, which dpkg sorts AFTER 0.36.0, so the
# final release would never replace the release candidate. Any further
# hyphen in the pre-release becomes a dot, as for the Arch package.
deb_version=$(printf '%s' "$version" | sed 's/-/~/; s/-/./g')
pkg=npad_${version}_${arch}
root=$outdir/$pkg

rm -rf "$root"
make install DESTDIR="$root" PREFIX=/usr LINUX_GTK_TARGET="$binary"

# Highest glibc symbol version the binary needs becomes the libc6 floor, so
# the .deb stays installable on the oldest distro that can run it. readelf, not
# objdump: it reads a foreign architecture's ELF too.
glibc=$(readelf -W --dyn-syms "$binary" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n1)
[ -n "$glibc" ] || glibc=2.31

size=$(du -sk "$root" | cut -f1)
mkdir -p "$root/DEBIAN"
# libgtk-3-0t64 is the name since Debian 13 / Ubuntu 24.04 (64-bit time_t
# transition); older releases ship libgtk-3-0. Either satisfies npad.
cat > "$root/DEBIAN/control" <<CONTROL
Package: npad
Version: $deb_version
Architecture: $arch
Maintainer: Platima <13729856+platima@users.noreply.github.com>
Section: editors
Priority: optional
Installed-Size: $size
Depends: libc6 (>= $glibc), libgtk-3-0t64 (>= 3.24) | libgtk-3-0 (>= 3.24)
Homepage: https://npad.dev
Description: Lightweight Notepad-style text editor
 npad is a small, fast text editor inspired by classic Windows Notepad.
 It preserves each file's encoding and line endings and saves atomically.
CONTROL

mkdir -p "$outdir"
dpkg-deb --root-owner-group --build "$root" "$outdir/$pkg.deb"
echo "Built $outdir/$pkg.deb"
