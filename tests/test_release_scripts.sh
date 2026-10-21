#!/bin/sh
# Self-test for the release and CI scripts that guard a release:
#   scripts/check-release-version.sh  (the version-consistency gate)
#   scripts/ci-checksums.sh           (CHECKSUMS.txt, written and verified)
#   scripts/ci-installer-refs.sh      (installer file references)
# Each runs against throwaway fixtures under a temporary directory; nothing in
# the tree is modified. Run from the repo root: sh tests/test_release_scripts.sh
# (make test runs it as test-release-scripts).
set -u
repo=$(pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
passed=0
failed=0

ok() {
	passed=$((passed + 1))
	echo "  PASS: $1"
}
bad() {
	failed=$((failed + 1))
	echo "  FAIL: $1"
}
# expect <0|1> <description> <command...>: the command succeeds (0) or fails (1)
expect() {
	want=$1
	what=$2
	shift 2
	if "$@" >"$tmp/out" 2>&1; then got=0; else got=1; fi
	if [ "$got" = "$want" ]; then
		ok "$what"
	else
		bad "$what (exit status said $got, expected $want)"
		sed 's/^/      /' "$tmp/out"
	fi
}

# --- check-release-version.sh -------------------------------------------------

# A fixture tree: main.h, npad.rc, CHANGELOG.md and the metainfo for
# <main.h version> <rc version> <changelog heading> <metainfo version>
fixture() {
	d=$tmp/tree
	rm -rf "$d"
	mkdir -p "$d/src/platform" "$d/scripts" "$d/packaging/linux"
	cp "$repo/scripts/release-notes.sh" "$d/scripts/"
	h=$1
	r=$2
	maj=${h%%.*}
	rest=${h#*.}
	mi=${rest%%.*}
	pa=${rest#*.}
	printf '#define NPAD_VERSION_MAJOR %s\n#define NPAD_VERSION_MINOR %s\n#define NPAD_VERSION_PATCH %s\n#define NPAD_VERSION_RELEASE "dev"\n' \
		"$maj" "$mi" "$pa" >"$d/src/main.h"
	rc_commas=$(printf '%s' "$r" | tr '.' ',')
	{
		printf '1 VERSIONINFO\nFILEVERSION %s,0\nPRODUCTVERSION %s,0\nBEGIN\n' "$rc_commas" "$rc_commas"
		printf '            VALUE "FileVersion", "%s.0"\n' "$r"
		printf '            VALUE "ProductVersion", "%s.0"\nEND\n' "$r"
	} >"$d/src/platform/npad.rc"
	{
		printf '# Changelog\n\n%s\n\n### ✨ Highlights\n\n- Something a user would notice\n\n### Fixed\n\n- A fix\n\n' "$3"
		printf '## [0.1.0] - 2020-01-01\n\n### ✨ Highlights\n\n- The first release\n'
	} >"$d/CHANGELOG.md"
	printf '<component>\n  <releases>\n    <release version="%s" date="2026-10-20"/>\n    <release version="0.1.0" date="2020-01-01"/>\n  </releases>\n</component>\n' \
		"$4" >"$d/packaging/linux/io.github.platima.npad.metainfo.xml"
}
gate() {
	(cd "$tmp/tree" && sh "$repo/scripts/check-release-version.sh" "$@")
}

echo "check-release-version.sh"
fixture 0.36.0 0.36.0 "## [0.36.0] - Unreleased" 0.35.0
expect 0 "a consistent tree passes without a tag" gate
expect 0 "a pre-release tag passes with an undated entry" gate v0.36.0-rc4
expect 0 "a dotted pre-release tag passes" gate v0.36.0-beta.2
expect 1 "a final tag fails with an 'Unreleased' entry" gate v0.36.0
expect 1 "a tag for another version fails" gate v0.37.0
expect 1 "a pre-release of another version fails" gate v0.35.0-rc1
for t in v0.36 0.36.0 v0.36.0- 'v0.36.0-rc2;id' 'v0.36.0 x' 'v0.36.0-rc_2' 'v0.36.0-$(id)' v0.36.0/x; do
	expect 1 "the malformed tag '$t' is refused" gate "$t"
done

fixture 0.36.0 0.36.0 "## [0.36.0] - 2026-10-20" 0.36.0
expect 0 "a final tag passes with a dated entry and a current metainfo" gate v0.36.0
fixture 0.36.0 0.36.0 "## [0.36.0] - 2026-10-20" 0.35.0
expect 1 "a final tag fails while the metainfo's newest release is older" gate v0.36.0
expect 0 "... which a pre-release tag does not need" gate v0.36.0-rc4

fixture 0.36.0 0.35.0 "## [0.36.0] - Unreleased" 0.35.0
expect 1 "npad.rc left on the previous version fails, without a tag" gate
expect 1 "npad.rc left on the previous version fails a pre-release tag" gate v0.36.0-rc1
fixture 0.36.0 0.36.0 "## [0.36.0] - Unreleased" 0.35.0
sed 's/"ProductVersion", "0.36.0.0"/"ProductVersion", "0.35.0.0"/' "$tmp/tree/src/platform/npad.rc" >"$tmp/rc" &&
	mv "$tmp/rc" "$tmp/tree/src/platform/npad.rc"
expect 1 "a stale ProductVersion string alone fails" gate
fixture 0.36.0 0.36.0 "## [0.36.0] - Unreleased" 0.35.0
sed 's/$/\r/' "$tmp/tree/src/platform/npad.rc" >"$tmp/rc" && mv "$tmp/rc" "$tmp/tree/src/platform/npad.rc"
sed 's/$/\r/' "$tmp/tree/src/main.h" >"$tmp/h" && mv "$tmp/h" "$tmp/tree/src/main.h"
expect 0 "CRLF line endings in main.h and npad.rc are read correctly" gate v0.36.0-rc1
fixture 0.36.0 0.36.0 "## [0.35.0] - 2026-10-17" 0.35.0
expect 1 "a missing CHANGELOG entry for the version fails" gate
fixture 0.36.0 0.36.0 "## [0.36.0] - Unreleased" 0.35.0
sed '/### ✨ Highlights/,/^### Fixed/{/^### Fixed/!d}' "$tmp/tree/CHANGELOG.md" >"$tmp/cl" && mv "$tmp/cl" "$tmp/tree/CHANGELOG.md"
expect 1 "an entry without highlights fails" gate v0.36.0-rc1

# --- ci-checksums.sh ----------------------------------------------------------

echo "ci-checksums.sh"
assets() {
	d=$tmp/assets
	rm -rf "$d"
	mkdir -p "$d"
	i=0
	while [ "$i" -lt "$1" ]; do
		n=npad-v9.9.9-file-$i.bin
		echo "content $i" >"$d/$n"
		(cd "$d" && sha256sum "$n" >"$n.sha256")
		i=$((i + 1))
	done
	# The kinds of names a release has, including the .deb sorting last
	echo deb >"$d/npad_9.9.9_amd64.deb"
	(cd "$d" && sha256sum npad_9.9.9_amd64.deb >npad_9.9.9_amd64.deb.sha256)
}
sums() {
	sh "$repo/scripts/ci-checksums.sh" write "$tmp/assets" v9.9.9 platima/npad abc123 >"$tmp/CHECKSUMS.txt"
}
assets 40
expect 0 "41 assets write a CHECKSUMS.txt" sums
lines=$(grep -vc '^#' "$tmp/CHECKSUMS.txt")
[ "$lines" = 41 ] && ok "it has one line for each of the 41 assets" || bad "it has $lines lines, not 41"
grep -q '  npad_9.9.9_amd64.deb$' "$tmp/CHECKSUMS.txt" && ok "the .deb (sorted last) is listed" || bad "the .deb is missing"
cp "$tmp/CHECKSUMS.txt" "$tmp/assets/"
expect 0 "verify accepts the complete set" sh "$repo/scripts/ci-checksums.sh" verify "$tmp/assets"
mkdir -p "$tmp/one"
cp "$tmp/assets/CHECKSUMS.txt" "$tmp/assets/npad_9.9.9_amd64.deb" "$tmp/one/"
expect 0 "the documented 'sha256sum -c --ignore-missing' passes with one file downloaded" \
	sh -c "cd '$tmp/one' && sha256sum -c --ignore-missing CHECKSUMS.txt"

assets 3
rm "$tmp/assets/npad-v9.9.9-file-1.bin.sha256"
expect 1 "an asset without its .sha256 stops the write" sums
assets 3
rm "$tmp/assets/npad-v9.9.9-file-1.bin"
expect 1 "a .sha256 without its asset stops the write" sums
assets 3
(cd "$tmp/assets" && sha256sum npad-v9.9.9-file-2.bin >npad-v9.9.9-file-1.bin.sha256)
expect 1 "a .sha256 naming another asset stops the write" sums
assets 3
: >"$tmp/assets/npad-v9.9.9-file-0.bin.sha256"
expect 1 "an empty .sha256 stops the write" sums

assets 3
sums
grep -v 'file-2.bin$' "$tmp/CHECKSUMS.txt" >"$tmp/assets/CHECKSUMS.txt"
expect 1 "verify catches an asset with no line" sh "$repo/scripts/ci-checksums.sh" verify "$tmp/assets"
cp "$tmp/CHECKSUMS.txt" "$tmp/assets/"
echo "$(printf %064d 0)  npad-v9.9.9-gone.bin" >>"$tmp/assets/CHECKSUMS.txt"
expect 1 "verify catches a line for a file that is not there" sh "$repo/scripts/ci-checksums.sh" verify "$tmp/assets"
cp "$tmp/CHECKSUMS.txt" "$tmp/assets/"
echo tampered >"$tmp/assets/npad-v9.9.9-file-0.bin"
expect 1 "verify catches a digest that does not match" sh "$repo/scripts/ci-checksums.sh" verify "$tmp/assets"

# --- ci-installer-refs.sh -----------------------------------------------------

echo "ci-installer-refs.sh"
inst() {
	rm -rf "$tmp/inst"
	mkdir -p "$tmp/inst/installer" "$tmp/inst/src/platform"
	cp "$repo/installer/npad.wxs" "$repo/installer/npad.iss" "$repo/installer/fetch-fonts.ps1" "$tmp/inst/installer/"
	cp "$repo/LICENSE" "$repo/README.md" "$repo/DOCUMENTATION.md" "$tmp/inst/"
	cp "$repo"/src/platform/*.ico "$tmp/inst/src/platform/"
}
refs() {
	sh "$repo/scripts/ci-installer-refs.sh" "$tmp/inst/installer"
}
expect 0 "the real installer scripts resolve" sh "$repo/scripts/ci-installer-refs.sh" "$repo/installer"
inst
expect 0 "a copy with the referenced files resolves" refs
rm "$tmp/inst/DOCUMENTATION.md"
expect 1 "a renamed DOCUMENTATION.md fails both installers' check" refs
inst
rm "$tmp/inst/LICENSE"
expect 1 "a missing LICENSE (LicenseFile=, Source:, Source=) fails" refs
inst
rm "$tmp/inst/src/platform/npad-light.ico"
expect 1 "a deleted icon (the v0.28.0 failure) fails" refs
inst
sed 's/IntelOneMono-Bold.ttf"; DestDir/IntelOneMono-Heavy.ttf"; DestDir/' "$repo/installer/npad.iss" >"$tmp/inst/installer/npad.iss"
expect 1 "a font fetch-fonts.ps1 does not download fails" refs
inst
sed 's/fonts\\Roboto\\\*\.ttf/fonts\\Roboto\\*.woff/' "$repo/installer/npad.iss" >"$tmp/inst/installer/npad.iss"
expect 1 "a font wildcard that matches nothing fails" refs

echo
echo "release scripts: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
