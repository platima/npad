#!/bin/sh
# Install a pinned, checksum-verified nfpm into a directory on PATH.
# Usage: scripts/install-nfpm.sh [bindir]   (default /usr/local/bin; needs curl)
# Bump NFPM_VERSION and NFPM_SHA256 together; the sum is the
# nfpm_<v>_Linux_x86_64.tar.gz line of the release's checksums.txt.
set -eu
NFPM_VERSION=2.48.0
NFPM_SHA256=6d7f9a28b2f397934403950fcbba83f598b08d02edbe79a0815b3b4897341750
bindir=${1:-/usr/local/bin}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/nfpm.tgz" \
	"https://github.com/goreleaser/nfpm/releases/download/v${NFPM_VERSION}/nfpm_${NFPM_VERSION}_Linux_x86_64.tar.gz"
echo "$NFPM_SHA256  $tmp/nfpm.tgz" | sha256sum -c -
tar -xzf "$tmp/nfpm.tgz" -C "$tmp" nfpm
install -D -m 755 "$tmp/nfpm" "$bindir/nfpm"
"$bindir/nfpm" --version | grep GitVersion
