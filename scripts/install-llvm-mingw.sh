#!/bin/sh
# Install a pinned, checksum-verified llvm-mingw (clang + lld cross toolchain
# for Windows) for the Windows on Arm build: `make windows-arm64`.
# Usage: scripts/install-llvm-mingw.sh [dir]   (default /opt/llvm-mingw; needs curl, xz)
# Then put <dir>/bin on PATH. Linux x86_64 hosts only (the CI runners); on
# Windows, use the release's ucrt-x86_64.zip and the same `make windows-arm64`.
# Bump LLVM_MINGW_VERSION and LLVM_MINGW_SHA256 together; the sum is the
# release asset's sha256 digest as GitHub shows it.
# The ucrt flavour: Windows 10 and 11 ship the UCRT, and every Windows on Arm
# device runs one of them.
set -eu
LLVM_MINGW_VERSION=20260922
LLVM_MINGW_SHA256=bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21
dir=${1:-/opt/llvm-mingw}

name=llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/llvm-mingw.tar.xz" \
	"https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${name}.tar.xz"
echo "$LLVM_MINGW_SHA256  $tmp/llvm-mingw.tar.xz" | sha256sum -c -
mkdir -p "$dir"
tar -xJf "$tmp/llvm-mingw.tar.xz" -C "$dir" --strip-components=1
"$dir/bin/aarch64-w64-mingw32-clang" --version | head -n1
