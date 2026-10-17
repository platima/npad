#!/bin/sh
# Print the Debian architecture name (amd64, arm64, riscv64) of an ELF binary,
# from its header, or fail. With a second argument, also fail unless the
# binary is that architecture.
# Usage: scripts/elf-arch.sh <binary> [expected-arch]
# Both package builders (build-package.sh and the dpkg-deb fallback
# build-deb.sh) take the architecture from here, never from a variable alone:
# a mislabelled package installs and then cannot execute. readelf, not
# objdump: it reads a foreign architecture's ELF too.
set -eu
binary=$1
want=${2:-}

machine=$(readelf -h "$binary" | sed -n 's/^ *Machine: *//p')
case $machine in
*X86-64*) arch=amd64 ;;
AArch64) arch=arm64 ;;
RISC-V) arch=riscv64 ;;
*)
	echo "unsupported ELF machine '$machine' in $binary" >&2
	exit 2
	;;
esac
if [ -n "$want" ] && [ "$want" != "$arch" ]; then
	echo "$binary is $arch ($machine), not $want" >&2
	exit 2
fi
echo "$arch"
