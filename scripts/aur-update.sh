#!/bin/sh
# Point packaging/arch/aur/npad-bin at a release: set pkgver, reset pkgrel, fill
# in the SHA-256 of the LICENSE and of each architecture's portable tarball,
# and regenerate .SRCINFO with makepkg.
# Usage: scripts/aur-update.sh <tag> [asset-dir]   (e.g. v0.36.0)
#
# The sums come from the release's own .sha256 files (the ones CHECKSUMS.txt is
# made of): downloaded from the GitHub release, or read from asset-dir, which
# must then hold npad-<tag>-portable-linux-<arch>.tar.gz.sha256 for x86_64,
# aarch64 and riscv64. The tarballs themselves are never downloaded here. The LICENSE's sum
# is taken from the tagged source on raw.githubusercontent.com (what the
# PKGBUILD downloads), or from LICENSE in the working directory with asset-dir.
#
# makepkg --printsrcinfo needs Arch's makepkg and refuses to run as root:
# release.yml's optional AUR job runs this in an archlinux container as an
# ordinary user; on Arch itself, just run it.
set -eu
tag=$1
assets=${2:-}
version=${tag#v}
dir=$(dirname "$0")/../packaging/arch/aur/npad-bin
repo=${NPAD_REPO:-platima/npad}

case $tag in
v[0-9]*.[0-9]*.[0-9]*) ;;
*)
	echo "tag must look like v0.36.0, not '$tag'" >&2
	exit 2
	;;
esac
case $tag in
*-*)
	echo "refusing a pre-release ($tag): the AUR package follows final releases" >&2
	exit 2
	;;
esac

# Print the SHA-256 recorded for <name> by <name>.sha256
sum_of() {
	name=$1
	if [ -n "$assets" ]; then
		line=$(cat "$assets/$name.sha256")
	else
		line=$(curl -fsSL "https://github.com/$repo/releases/download/$tag/$name.sha256")
	fi
	set -- $line
	hash=$1
	listed=${2#\*}
	if [ "$listed" != "$name" ] || [ "${#hash}" -ne 64 ]; then
		echo "$name.sha256 does not look like '<sha256>  $name': $line" >&2
		exit 1
	fi
	printf '%s' "$hash"
}

amd64=$(sum_of "npad-$tag-portable-linux-x86_64.tar.gz")
arm64=$(sum_of "npad-$tag-portable-linux-aarch64.tar.gz")
riscv64=$(sum_of "npad-$tag-portable-linux-riscv64.tar.gz")
if [ -n "$assets" ]; then
	license=$(sha256sum LICENSE | cut -d' ' -f1)
else
	curl -fsSL -o "${TMPDIR:-/tmp}/npad-LICENSE.$$" "https://raw.githubusercontent.com/$repo/$tag/LICENSE"
	license=$(sha256sum "${TMPDIR:-/tmp}/npad-LICENSE.$$" | cut -d' ' -f1)
	rm -f "${TMPDIR:-/tmp}/npad-LICENSE.$$"
fi
[ "${#license}" -eq 64 ] || {
	echo "could not hash the LICENSE" >&2
	exit 1
}

sed -i \
	-e "s/^pkgver=.*/pkgver=$version/" \
	-e "s/^pkgrel=.*/pkgrel=1/" \
	-e "s/^sha256sums=.*/sha256sums=('$license')/" \
	-e "s/^sha256sums_x86_64=.*/sha256sums_x86_64=('$amd64')/" \
	-e "s/^sha256sums_aarch64=.*/sha256sums_aarch64=('$arm64')/" \
	-e "s/^sha256sums_riscv64=.*/sha256sums_riscv64=('$riscv64')/" \
	"$dir/PKGBUILD"

command -v makepkg >/dev/null 2>&1 || {
	echo "PKGBUILD updated, but makepkg is not available to write .SRCINFO (run this on Arch, or in an archlinux container as a non-root user)" >&2
	exit 1
}
(cd "$dir" && makepkg --printsrcinfo >.SRCINFO)
echo "npad-bin is now $version-1:"
grep -E '^(pkgver|pkgrel|sha256sums)' "$dir/PKGBUILD"
