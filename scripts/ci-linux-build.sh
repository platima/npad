#!/bin/sh
# CI: build npad-linux inside a distro container. packages.yml runs it as
#   docker run --platform linux/<arch> -v "$PWD:/src" -w /src <image> sh scripts/ci-linux-build.sh
# natively for amd64 and arm64 (Debian 12, for a low glibc floor) and under
# QEMU for riscv64 (Debian 13: Debian 12 has no official riscv64). Packaging
# happens afterwards on the host, where nfpm runs; it only needs the binary.
# NPAD_VALIDATE_METADATA=1 also validates the .desktop file and the AppStream
# metainfo with this distro's tools (once per run is enough).
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
	build-essential pkg-config libgtk-3-dev git ca-certificates binutils
# The checkout belongs to the runner's user, not to root in here; without this
# git refuses it and the Makefile cannot derive the version string
git config --global --add safe.directory "$PWD"

make clean
make linux-gtk WERROR=1
readelf -h npad-linux | grep -E 'Class|Machine'

if [ "${NPAD_VALIDATE_METADATA:-}" = 1 ]; then
	apt-get install -y -q --no-install-recommends desktop-file-utils appstream
	desktop-file-validate packaging/linux/io.github.platima.npad.desktop
	appstreamcli validate --no-net packaging/linux/io.github.platima.npad.metainfo.xml
fi
