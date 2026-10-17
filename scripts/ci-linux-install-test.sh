#!/bin/sh
# CI: install the package in pkgs/ on this distro and check every shared
# library resolves. packages.yml runs it as
#   docker run --platform linux/<arch> -v "$PWD:/w" -w /w <image> sh scripts/ci-linux-install-test.sh <deb|dnf|zypper>
# pkgs/ holds one architecture's packages, the one the container runs.
set -eu
kind=$1

case $kind in
deb)
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q
	apt-get install -y -q ./pkgs/npad_*.deb
	;;
dnf)
	dnf install -y ./pkgs/npad-*.rpm
	;;
zypper)
	# The rpm is unsigned (the signed copy lives in the dnf repository at
	# pkg.npad.dev); zypper refuses unsigned local files unless told
	zypper --non-interactive install --allow-unsigned-rpm ./pkgs/npad-*.rpm
	;;
*)
	echo "kind must be deb, dnf or zypper" >&2
	exit 2
	;;
esac

test -x /usr/bin/npad
test -f /usr/share/applications/io.github.platima.npad.desktop
if ldd /usr/bin/npad | grep 'not found'; then
	echo "ERROR: unresolved libraries"
	exit 1
fi
echo "OK: $(uname -m), $(. /etc/os-release && echo "$PRETTY_NAME")"
