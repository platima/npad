#!/bin/sh
# Build the .deb, .rpm or Arch .pkg.tar.zst with nfpm from
# packaging/linux/nfpm.yaml, or the portable tarball.
# Usage: scripts/build-package.sh <deb|rpm|arch|tar> <version> <binary> <outdir> [arch]
# arch is the Debian name (amd64, arm64, riscv64) and defaults to the binary's
# own ELF machine; given explicitly, the binary must match it.
# Output: <outdir>/npad_<version>_<arch>.deb (amd64, arm64, riscv64),
#         <outdir>/npad-<version>-1.<arch>.rpm (x86_64, aarch64, riscv64),
#         <outdir>/npad-<version>-1-<arch>.pkg.tar.zst (x86_64, aarch64,
#         riscv64; Arch Linux, Arch Linux ARM and Arch RISC-V) or
#         <outdir>/npad-v<version>-linux-<arch>.tar.gz (amd64, arm64, riscv64)
# The deb, rpm and Arch package need nfpm on PATH (CI installs a pinned copy with
# scripts/install-nfpm.sh); all three need readelf. The binary may be for
# another architecture than the host: readelf reads any ELF, where an amd64
# host's objdump may refuse a riscv64 one.
#
# The tarball is the Linux counterpart of the Windows portable exe: the same
# binary the packages ship, unpacked into npad-v<version>-linux-<arch>/ with
# the files `make install` would put under share/ and a README.txt
# (packaging/linux/README-portable.txt) on running it in place or copying it
# under ~/.local. It is the normal dynamically linked binary: GTK cannot be
# linked statically in any useful way, and bundling it would not be npad.
set -eu
format=$1
packager=$format
version=$2
binary=$3
outdir=$4
arch=${5:-}

# The architecture the binary was actually built for. Packaging an x86_64
# binary as riscv64 would install and then fail to execute, so never trust a
# file name or a variable over the ELF header.
arch=$(sh "$(dirname "$0")/elf-arch.sh" "$binary" "$arch")
# rpm and pacman spell the same architectures differently; nfpm maps them,
# the file name is ours
case $arch in
amd64) rpm_arch=x86_64 ;;
arm64) rpm_arch=aarch64 ;;
riscv64) rpm_arch=riscv64 ;;
esac

# Highest glibc symbol version the binary needs becomes the libc floor, so the
# package stays installable on the oldest distro that can run it.
glibc=$(readelf -W --dyn-syms "$binary" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -n1)
[ -n "$glibc" ] || glibc=2.31

case $format in
deb)
	out=$outdir/npad_${version}_${arch}.deb
	release=
	;;
rpm)
	out=$outdir/npad-${version}-1.${rpm_arch}.rpm
	release=1
	;;
arch)
	# Arch's own naming: <pkgname>-<pkgver>-<pkgrel>-<arch>.pkg.tar.zst
	out=$outdir/npad-${version}-1-${rpm_arch}.pkg.tar.zst
	release=1
	packager=archlinux
	# pkgver may not contain a hyphen, and nfpm reads "0.36.0-rc2" as version
	# 0.36.0 plus a pre-release that its archlinux packager drops, leaving a
	# pkgver of 0.36.0 that pacman would treat as the final release. Arch
	# spells a pre-release as a suffix on the version (0.36.0rc2 sorts before
	# 0.36.0 and after 0.35.0 under vercmp), so give nfpm that; the file name
	# keeps the tag's spelling, as release.yml and validation expect
	base=${version%%-*}
	pre=${version#"$base"}
	pre=$(printf '%s' "${pre#-}" | tr '-' '.')
	nfpm_version=$base$pre
	;;
tar)
	name=npad-v${version}-linux-${arch}
	out=$outdir/$name.tar.gz
	# The oldest releases each architecture is built and tested for
	# (packages.yml's glibc_max): riscv64 builds on Debian 13, the oldest
	# Debian with an official riscv64 port
	case $arch in
	riscv64) distros="Debian 13 / Ubuntu 24.04" ;;
	*) distros="Debian 12 / Ubuntu 22.04" ;;
	esac
	stage=$outdir/stage-tar-$arch
	rm -rf "$stage"
	mkdir -p "$stage/$name"
	# The binary at the top, everything else where `make install` puts it
	# under the prefix, so the README's `cp -r share/. ~/.local/share/` is
	# the same layout the packages install under /usr
	make install DESTDIR="$stage/$name" PREFIX= BINDIR= DATADIR=/share LINUX_GTK_TARGET="$binary"
	sed -e "s|@VERSION@|v$version|g" -e "s|@ARCH@|$arch|g" \
		-e "s|@GLIBC@|$glibc|g" -e "s|@DISTROS@|$distros|g" \
		packaging/linux/README-portable.txt >"$stage/$name/README.txt"
	if grep -n '@[A-Z]*@' "$stage/$name/README.txt"; then
		echo "README-portable.txt has a placeholder this script does not fill" >&2
		exit 1
	fi
	# Reproducible: fixed order, owner and timestamps (the commit's), and
	# no name or time in the gzip header
	epoch=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || date +%s)}
	tar -C "$stage" --sort=name --owner=0 --group=0 --numeric-owner \
		--mtime="@$epoch" -cf - "$name" | gzip -9n >"$out"
	rm -rf "$stage"
	echo "Built $out (glibc floor $glibc)"
	exit 0
	;;
*)
	echo "format must be deb, rpm, arch or tar" >&2
	exit 2
	;;
esac
command -v nfpm >/dev/null 2>&1 || {
	echo "nfpm not found; scripts/install-nfpm.sh fetches the pinned release" >&2
	exit 1
}

stage=$outdir/stage-$format-$arch
rm -rf "$stage"
mkdir -p "$outdir"
make install DESTDIR="$stage" PREFIX=/usr LINUX_GTK_TARGET="$binary"

config=packaging/linux/nfpm.yaml
if [ "$packager" = archlinux ]; then
	# pacman shows pkgdesc on one line (pacman -Si, software centres), but
	# nfpm.yaml carries the deb's synopsis plus long description, which nfpm
	# would run together into one long sentence. Use the synopsis alone.
	config=$stage.nfpm.yaml
	awk '/^description:/ { getline synopsis; sub(/^ +/, "", synopsis); print "description: " synopsis; skip = 1; next }
		skip && /^ / { next }
		{ skip = 0; print }' packaging/linux/nfpm.yaml >"$config"
fi

NPAD_VERSION=${nfpm_version:-$version} NPAD_RELEASE=$release NPAD_GLIBC=$glibc NPAD_STAGE=$stage NPAD_ARCH=$arch \
	nfpm package --packager "$packager" --config "$config" --target "$out"
rm -rf "$stage" "$stage.nfpm.yaml"
echo "Built $out (libc floor $glibc)"
