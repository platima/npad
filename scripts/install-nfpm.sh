#!/bin/sh
# Install a pinned, checksum-verified nfpm into a directory on PATH.
# Usage: scripts/install-nfpm.sh [bindir]   (default /usr/local/bin; needs curl)
# Bump NFPM_VERSION and both sums together; each is the matching
# nfpm_<v>_Linux_<arch>.tar.gz line of the release's checksums.txt.
# nfpm runs on the build host (x86_64 or arm64) and writes packages for any
# architecture, so riscv64 packages are built by an x86_64 nfpm: nfpm
# publishes no riscv64 binary.
set -eu
NFPM_VERSION=2.48.0
NFPM_SHA256_x86_64=6d7f9a28b2f397934403950fcbba83f598b08d02edbe79a0815b3b4897341750
NFPM_SHA256_arm64=a9a75213763783b7669719e505474c748c5ad7dc66591a195619deb50f85c2f4
bindir=${1:-/usr/local/bin}

case $(uname -m) in
x86_64 | amd64)
	asset=x86_64
	sum=$NFPM_SHA256_x86_64
	;;
aarch64 | arm64)
	asset=arm64
	sum=$NFPM_SHA256_arm64
	;;
*)
	echo "install-nfpm.sh: no pinned nfpm for $(uname -m); package on an x86_64 or arm64 host" >&2
	exit 1
	;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/nfpm.tgz" \
	"https://github.com/goreleaser/nfpm/releases/download/v${NFPM_VERSION}/nfpm_${NFPM_VERSION}_Linux_${asset}.tar.gz"
echo "$sum  $tmp/nfpm.tgz" | sha256sum -c -
tar -xzf "$tmp/nfpm.tgz" -C "$tmp" nfpm
install -D -m 755 "$tmp/nfpm" "$bindir/nfpm"
"$bindir/nfpm" --version | grep GitVersion
