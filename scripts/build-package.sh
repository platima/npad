#!/bin/sh
# Build the .deb or .rpm with nfpm from packaging/linux/nfpm.yaml.
# Usage: scripts/build-package.sh <deb|rpm> <version> <binary> <outdir>
# Output: <outdir>/npad_<version>_amd64.deb or <outdir>/npad-<version>-1.x86_64.rpm
# Needs nfpm on PATH (CI installs a pinned copy with scripts/install-nfpm.sh)
# and objdump.
set -eu
format=$1
version=$2
binary=$3
outdir=$4

case $format in
deb)
	out=$outdir/npad_${version}_amd64.deb
	release=
	;;
rpm)
	out=$outdir/npad-${version}-1.x86_64.rpm
	release=1
	;;
*)
	echo "format must be deb or rpm" >&2
	exit 2
	;;
esac
command -v nfpm >/dev/null 2>&1 || {
	echo "nfpm not found; scripts/install-nfpm.sh fetches the pinned release" >&2
	exit 1
}

# Highest glibc symbol version the binary needs becomes the libc floor, so the
# package stays installable on the oldest distro that can run it.
glibc=$(objdump -T "$binary" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n1)
[ -n "$glibc" ] || glibc=2.31

stage=$outdir/stage-$format
rm -rf "$stage"
mkdir -p "$outdir"
make install DESTDIR="$stage" PREFIX=/usr

NPAD_VERSION=$version NPAD_RELEASE=$release NPAD_GLIBC=$glibc NPAD_STAGE=$stage \
	nfpm package --packager "$format" --config packaging/linux/nfpm.yaml --target "$out"
rm -rf "$stage"
echo "Built $out (libc floor $glibc)"
