#!/bin/sh
# CI: turn an Arch-family root filesystem into a local Docker image, for the
# architectures Arch has no official container image for.
# Usage: scripts/ci-arch-rootfs.sh <alarm|archriscv> <image-tag>   (needs docker)
#
#   alarm      Arch Linux ARM's generic aarch64 rootfs, the project's own
#              download (https://archlinuxarm.org/about/downloads). The file
#              behind "latest" moves, so it cannot be pinned by hash; instead its
#              detached signature must verify against the Arch Linux ARM Build
#              System key, whose fingerprint is pinned below and whose public
#              key is kept in packaging/arch/ (the same key that signs the
#              repository's packages). The .md5 next to the file is served over
#              plain http, so it proves nothing and is not used.
#   archriscv  The Arch RISC-V port's rootfs from its own mirror
#              (https://riscv.mirror.pkgbuild.com/images/). The mirror
#              publishes no checksums or signatures, so this pins the SHA-256 of
#              one dated image (the dated files stay on the mirror; "latest"
#              does not). Bump both lines together, taking the sum from a
#              download you have looked over.
#
# Nothing from Docker Hub is used: the community images for these two
# architectures are third-party rebuilds of the same tarballs, and a pinned
# official tarball is a shorter chain of trust. The image is what
# scripts/ci-linux-install-test.sh pacman runs in.
set -eu
which=$1
tag=$2

ALARM_URL=http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz
ALARM_FPR=68B3537F39A313B3E574D06777193F152BDBE6A6
RISCV_URL=https://riscv.mirror.pkgbuild.com/images/archriscv-2026-08-27.tar.zst
RISCV_SHA256=a2045c8b62232db2f60d8e4db610dbb5d9e12856dab0ba08634ad3d7cb7ad498

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

case $which in
alarm)
	curl -fsSL -o "$tmp/rootfs.tar.gz" "$ALARM_URL"
	curl -fsSL -o "$tmp/rootfs.tar.gz.sig" "$ALARM_URL.sig"
	export GNUPGHOME=$tmp/gnupg
	mkdir -m 700 "$GNUPGHOME"
	gpg --batch --quiet --import "$(dirname "$0")/../packaging/arch/archlinuxarm-build-system.asc"
	# VALIDSIG's last field is the fingerprint of the primary key that
	# signed, whichever of its subkeys did
	signer=$(gpg --batch --status-fd 1 --verify "$tmp/rootfs.tar.gz.sig" "$tmp/rootfs.tar.gz" |
		awk '$2 == "VALIDSIG" { print $NF }')
	if [ "$signer" != "$ALARM_FPR" ]; then
		echo "ERROR: the Arch Linux ARM rootfs is not signed by $ALARM_FPR (got '$signer')" >&2
		exit 1
	fi
	echo "OK: rootfs signature verifies against $ALARM_FPR"
	docker import --platform linux/arm64 "$tmp/rootfs.tar.gz" "$tag"
	;;
archriscv)
	curl -fsSL -o "$tmp/rootfs.tar.zst" "$RISCV_URL"
	echo "$RISCV_SHA256  $tmp/rootfs.tar.zst" | sha256sum -c -
	zstd -dc "$tmp/rootfs.tar.zst" | docker import --platform linux/riscv64 - "$tag"
	;;
*)
	echo "rootfs must be alarm or archriscv" >&2
	exit 2
	;;
esac
docker image inspect "$tag" --format '{{.Os}}/{{.Architecture}}'
