#!/bin/sh
# Build the .deb or .rpm with nfpm from packaging/linux/nfpm.yaml.
# Usage: scripts/build-package.sh <deb|rpm> <version> <binary> <outdir> [arch]
# arch is the Debian name (amd64, arm64, riscv64) and defaults to the binary's
# own ELF machine; given explicitly, the binary must match it.
# Output: <outdir>/npad_<version>_<arch>.deb (amd64, arm64, riscv64) or
#         <outdir>/npad-<version>-1.<arch>.rpm (x86_64, aarch64, riscv64)
# Needs nfpm on PATH (CI installs a pinned copy with scripts/install-nfpm.sh)
# and readelf. The binary may be for another architecture than the host:
# readelf reads any ELF, where an amd64 host's objdump may refuse a riscv64 one.
set -eu
format=$1
version=$2
binary=$3
outdir=$4
arch=${5:-}

# The architecture the binary was actually built for. Packaging an x86_64
# binary as riscv64 would install and then fail to execute, so never trust a
# file name or a variable over the ELF header.
machine=$(readelf -h "$binary" | sed -n 's/^ *Machine: *//p')
case $machine in
*X86-64*) elf_arch=amd64 ;;
AArch64) elf_arch=arm64 ;;
RISC-V) elf_arch=riscv64 ;;
*)
	echo "unsupported ELF machine '$machine' in $binary" >&2
	exit 2
	;;
esac
if [ -z "$arch" ]; then
	arch=$elf_arch
elif [ "$arch" != "$elf_arch" ]; then
	echo "$binary is $elf_arch ($machine), not $arch" >&2
	exit 2
fi
# rpm spells the same architectures differently; nfpm maps them, the file
# name is ours
case $arch in
amd64) rpm_arch=x86_64 ;;
arm64) rpm_arch=aarch64 ;;
riscv64) rpm_arch=riscv64 ;;
esac

case $format in
deb)
	out=$outdir/npad_${version}_${arch}.deb
	release=
	;;
rpm)
	out=$outdir/npad-${version}-1.${rpm_arch}.rpm
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
glibc=$(readelf -W --dyn-syms "$binary" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n1)
[ -n "$glibc" ] || glibc=2.31

stage=$outdir/stage-$format-$arch
rm -rf "$stage"
mkdir -p "$outdir"
make install DESTDIR="$stage" PREFIX=/usr LINUX_GTK_TARGET="$binary"

NPAD_VERSION=$version NPAD_RELEASE=$release NPAD_GLIBC=$glibc NPAD_STAGE=$stage NPAD_ARCH=$arch \
	nfpm package --packager "$format" --config packaging/linux/nfpm.yaml --target "$out"
rm -rf "$stage"
echo "Built $out (libc floor $glibc)"
