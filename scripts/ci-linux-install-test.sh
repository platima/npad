#!/bin/sh
# CI: install the package in pkgs/ on this distro and check every shared
# library resolves. packages.yml runs it as
#   docker run --platform linux/<arch> -v "$PWD:/w" -w /w <image> sh scripts/ci-linux-install-test.sh <deb|dnf|zypper|tarball>
# pkgs/ holds one architecture's packages, the one the container runs.
# tarball: install nothing of npad's, only GTK 3 (what the portable
# download's README.txt asks for), then unpack the tarball, run ./npad in
# place and follow the README's ~/.local steps. --version is handled before
# GTK is initialised, so it needs no display.
set -eu
kind=$1

if [ "$kind" = tarball ]; then
	if command -v apt-get >/dev/null 2>&1; then
		export DEBIAN_FRONTEND=noninteractive
		apt-get update -q
		# The README's names: libgtk-3-0t64 from Debian 13 / Ubuntu 24.04 on
		apt-get install -y -q --no-install-recommends libgtk-3-0t64 ||
			apt-get install -y -q --no-install-recommends libgtk-3-0
	elif command -v pacman >/dev/null 2>&1; then
		pacman -Sy --noconfirm --needed gtk3 tar gzip
	elif command -v dnf >/dev/null 2>&1; then
		dnf install -y gtk3 tar gzip
	elif command -v zypper >/dev/null 2>&1; then
		zypper --non-interactive install libgtk-3-0 tar gzip
	else
		echo "no package manager to install GTK 3 with" >&2
		exit 2
	fi
	tarball=$(ls pkgs/npad-v*-linux-*.tar.gz)
	top=$(basename "$tarball" .tar.gz)
	work=$(mktemp -d)
	tar -C "$work" -xzf "$tarball"
	cd "$work/$top"
	test -x ./npad
	test -f README.txt
	if ldd ./npad | grep 'not found'; then
		echo "ERROR: unresolved libraries"
		exit 1
	fi
	out=$(env -u DISPLAY -u WAYLAND_DISPLAY ./npad --version)
	printf '%s\n' "$out"
	printf '%s\n' "$out" | head -n1 | grep -q '^npad version ' || {
		echo "ERROR: ./npad --version did not print its version"
		exit 1
	}
	# README.txt's "Add it to your applications menu", under a throwaway HOME
	# (keep these in step with packaging/linux/README-portable.txt)
	HOME=$work/home
	export HOME
	mkdir -p "$HOME"
	mkdir -p ~/.local/bin ~/.local/share
	cp npad ~/.local/bin/
	cp -r share/. ~/.local/share/
	sed -i "s|^Exec=npad |Exec=$HOME/.local/bin/npad |" ~/.local/share/applications/io.github.platima.npad.desktop
	exe=$(sed -n 's/^Exec=\([^ ]*\).*/\1/p' ~/.local/share/applications/io.github.platima.npad.desktop)
	[ "$exe" = "$HOME/.local/bin/npad" ] || {
		echo "ERROR: the launcher runs '$exe', not ~/.local/bin/npad"
		exit 1
	}
	"$exe" --version >/dev/null
	test -f ~/.local/share/icons/hicolor/48x48/apps/io.github.platima.npad.png
	echo "OK: $top runs on $(uname -m), $(. /etc/os-release && echo "$PRETTY_NAME")"
	exit 0
fi

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
