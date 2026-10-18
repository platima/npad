#!/bin/sh
# CI: check the AUR package's text in a real Arch. packages.yml runs it, as root,
# in an archlinux container with the repository mounted read-only at /src:
#   docker run --rm -v "$PWD:/src:ro" archlinux:latest sh /src/scripts/ci-aur-check.sh
# It proves that .SRCINFO is what makepkg makes of the PKGBUILD, and that
# scripts/aur-update.sh rewrites both consistently (with made-up sums from a
# local asset directory, since no release is involved). makepkg refuses to run
# as root, so the work is done as an ordinary user on a copy.
set -eu
if [ "$(id -u)" = 0 ]; then
	pacman -Syu --noconfirm --needed base-devel
	id builder >/dev/null 2>&1 || useradd -m builder
	rm -rf /home/builder/npad
	cp -r /src /home/builder/npad
	chown -R builder /home/builder/npad
	exec su builder -c 'sh /home/builder/npad/scripts/ci-aur-check.sh'
fi

cd "$HOME/npad"
(cd packaging/arch/aur/npad-bin && makepkg --printsrcinfo | diff -u .SRCINFO -)
echo "OK: .SRCINFO matches the PKGBUILD"

mkdir aur-assets
for a in amd64 arm64 riscv64; do
	echo "$(printf %064d 7)  npad-v9.9.9-linux-$a.tar.gz" >"aur-assets/npad-v9.9.9-linux-$a.tar.gz.sha256"
done
sh scripts/aur-update.sh v9.9.9 aur-assets
grep -q '^pkgver=9.9.9$' packaging/arch/aur/npad-bin/PKGBUILD
grep -q 'pkgver = 9.9.9$' packaging/arch/aur/npad-bin/.SRCINFO
grep -q "sha256sums_riscv64 = $(printf %064d 7)" packaging/arch/aur/npad-bin/.SRCINFO
echo "OK: aur-update.sh rewrote the PKGBUILD and .SRCINFO"

# The Arch package's pre-release versions (build-package.sh arch: v0.36.0-rc2
# becomes pkgver 0.36.0rc2) must sort the way a release candidate should:
# after the previous release, rc1 before rc2, and before the final release
vc() { [ "$(vercmp "$1" "$2")" = "$3" ] || { echo "ERROR: vercmp $1 $2 is not $3"; exit 1; }; }
vc 0.35.0 0.36.0rc2 -1
vc 0.36.0rc1 0.36.0rc2 -1
vc 0.36.0rc2 0.36.0rc10 -1
vc 0.36.0rc2 0.36.0 -1
vc 0.36.0 0.36.1rc1 -1
echo "OK: pre-release pkgver values sort before their final release"
