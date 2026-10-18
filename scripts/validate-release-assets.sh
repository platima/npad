#!/bin/sh
# Check that <dir> holds a complete, consistent set of release assets for
# <tag> (e.g. v0.35.0): every Windows deliverable for x64 and arm64, every
# Linux package and portable tarball for amd64, arm64 and riscv64, each with
# a .sha256 that verifies, each plausibly sized, and the portable exes and
# tarballs built for the architecture their names claim. Also proves the in-app updater can find its
# download on this release, for every architecture it knows.
# Usage: scripts/validate-release-assets.sh <dir> <tag>   (run from the repo root)
# release.yml runs it on the published release's assets, and on the build
# artifacts in a dry run; it works the same on a directory you downloaded.
set -eu
dir=$1
tag=$2
version=${tag#v}
failed=0

fail() {
	echo "ERROR: $*"
	failed=1
}

check() {
	name=$1
	min_size=$2
	f=$dir/$name
	if [ ! -f "$f" ]; then
		fail "missing $name"
		return 0
	fi
	size=$(wc -c <"$f" | tr -d ' ')
	if [ "$size" -lt "$min_size" ]; then
		fail "$name is implausibly small ($size bytes, expected >= $min_size)"
	else
		echo "OK: $name ($size bytes)"
	fi
	# Hash the asset itself and compare with the companion's digest. Not
	# `sha256sum -c`: that checks whichever file the companion NAMES, so a
	# companion carrying another asset's line would vouch for a corrupt file.
	if [ ! -f "$f.sha256" ]; then
		fail "missing checksum $name.sha256"
		return 0
	fi
	read -r want listed <"$f.sha256" || true
	listed=${listed#\*} # sha256sum's binary-mode marker
	got=$(sha256sum "$f" | cut -d' ' -f1)
	if [ "$listed" != "$name" ]; then
		fail "$name.sha256 names '$listed', not $name"
	fi
	if [ "$(printf '%s' "$want" | tr 'A-F' 'a-f')" != "$got" ]; then
		fail "$name does not match $name.sha256 (expected $want, got $got)"
	fi
}

# The PE machine field: 8664 for x64, aa64 for arm64. Only the portable exe is
# npad.exe itself; the setup exe is Inno's own x86 bootstrapper.
pe_machine() {
	off=$(od -An -t u4 -j 60 -N 4 "$1" | tr -d ' ')
	od -An -t x2 -j $((off + 4)) -N 2 "$1" | tr -d ' '
}

# Windows: npad-v<version>-<type>-win-<arch>.<ext>
for arch in x64 arm64; do
	check "npad-$tag-setup-win-$arch.exe" 1000000
	check "npad-$tag-msi-win-$arch.msi" 500000
	check "npad-$tag-portable-win-$arch.exe" 100000
	portable=$dir/npad-$tag-portable-win-$arch.exe
	if [ -f "$portable" ]; then
		want=8664
		[ "$arch" = arm64 ] && want=aa64
		got=$(pe_machine "$portable")
		[ "$got" = "$want" ] || fail "npad-$tag-portable-win-$arch.exe has PE machine $got, expected $want"
	fi
done

# Linux: the packages keep their native names - Debian's architecture names
# for the .deb, rpm's for the .rpm
for pair in amd64:x86_64 arm64:aarch64 riscv64:riscv64; do
	deb_arch=${pair%%:*}
	rpm_arch=${pair##*:}
	check "npad_${version}_${deb_arch}.deb" 30000
	check "npad-${version}-1.${rpm_arch}.rpm" 30000
	deb=$dir/npad_${version}_${deb_arch}.deb
	if [ -f "$deb" ] && command -v dpkg-deb >/dev/null 2>&1; then
		got=$(dpkg-deb -f "$deb" Architecture)
		[ "$got" = "$deb_arch" ] || fail "$(basename "$deb") says Architecture: $got"
	fi
	# The portable tarball is named like the Windows downloads (tag
	# spelling) with the .deb's architecture names. Its name claims an
	# architecture; the binary's ELF header is the proof.
	top=npad-$tag-linux-$deb_arch
	check "$top.tar.gz" 50000
	if [ -f "$dir/$top.tar.gz" ]; then
		x=$(mktemp -d)
		if ! tar -C "$x" -xzf "$dir/$top.tar.gz" "$top/npad" "$top/README.txt"; then
			fail "$top.tar.gz has no $top/npad and $top/README.txt"
		elif ! got=$(sh scripts/elf-arch.sh "$x/$top/npad" 2>&1); then
			fail "$top.tar.gz: $got"
		elif [ "$got" != "$deb_arch" ]; then
			fail "$top.tar.gz: its npad is $got, not $deb_arch"
		else
			echo "OK: $top.tar.gz: its npad is $got"
		fi
		rm -rf "$x"
	fi
done

# npad resolves its download by matching the END of each release asset name
# (update_find_asset_url) against the suffixes in src/core/update_check.c.
# Read them straight from the source so this cannot drift: renaming assets
# without updating the updater - or the reverse - fails here, loudly, instead
# of silently stranding every npad already installed. That happened once, in
# v0.17.x.
suffixes=$(grep -o '"-setup-win-[^"]*"' src/core/update_check.c | tr -d '"' | sort -u)
if [ -z "$suffixes" ]; then
	fail "could not read the updater's asset suffixes from src/core/update_check.c"
fi
names=$(ls "$dir")
for sfx in $suffixes; do
	# Case-insensitive, as update_find_asset_url matches
	if echo "$names" | awk -v s="$sfx" 'BEGIN { s = tolower(s) }
		{ n = tolower($0); if (length(n) >= length(s) && substr(n, length(n) - length(s) + 1) == s) found = 1 }
		END { exit !found }'; then
		echo "OK: an asset ends with '$sfx' (in-app updater)"
	else
		fail "no asset ends with '$sfx' - the in-app updater would not find its download"
	fi
done

if [ "$failed" -ne 0 ]; then
	echo "Release asset validation FAILED"
	exit 1
fi
echo "Release asset validation passed"
