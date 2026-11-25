#!/bin/sh
# Version-consistency gate: every place that spells npad's version agrees.
#
# Usage: scripts/check-release-version.sh [<tag>]   (run from the repo root)
#
# With a tag (v0.36.0, or a pre-release such as v0.36.0-rc2), as release.yml
# runs it before anything is built or published:
#   - the tag is well formed: v<major>.<minor>.<patch>, optionally followed by
#     -<identifiers> made of letters, digits and dots (it ends up in file
#     names, shell commands and the release title, so nothing else is let in)
#   - src/main.h is the tag's version (a pre-release uses its base version)
#   - src/platform/npad.rc's FILEVERSION, PRODUCTVERSION and the "FileVersion"
#     and "ProductVersion" strings are that version
#   - CHANGELOG.md's newest entry is that version, and has the
#     "### ✨ Highlights" the release notes are made from
#   - for a final tag only: that entry is dated ("## [x.y.z] - YYYY-MM-DD",
#     not "- Unreleased"), and the AppStream metainfo's newest <release> is
#     this version
# Without a tag (CI, on every change): the same agreement between main.h,
# npad.rc and CHANGELOG.md for the version being worked on, so a forgotten
# half of a bump fails the pull request rather than the release.
#
# NPAD_ROOT points it at another tree (the self-test does).
set -eu
root=${NPAD_ROOT:-.}
tag=${1:-}
failed=0

fail() {
	echo "ERROR: $*" >&2
	failed=1
}

# A tag must be one line of the allowed characters before it is used anywhere
if [ -n "$tag" ]; then
	if [ "$(printf '%s' "$tag" | wc -l | tr -d ' ')" != 0 ] ||
		! printf '%s\n' "$tag" | grep -Eqx 'v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?'; then
		echo "ERROR: '$tag' is not a release tag: expected v<major>.<minor>.<patch>[-<pre-release>], e.g. v0.36.0 or v0.36.0-rc2" >&2
		exit 1
	fi
fi

# src/main.h: the three NPAD_VERSION_* numbers
header=$(tr -d '\r' <"$root/src/main.h" | awk '
	$1 == "#define" && $2 == "NPAD_VERSION_MAJOR" { ma = $3 }
	$1 == "#define" && $2 == "NPAD_VERSION_MINOR" { mi = $3 }
	$1 == "#define" && $2 == "NPAD_VERSION_PATCH" { pa = $3 }
	END { if (ma != "" && mi != "" && pa != "") print ma "." mi "." pa }')
if ! printf '%s\n' "$header" | grep -Eqx '[0-9]+\.[0-9]+\.[0-9]+'; then
	echo "ERROR: could not read NPAD_VERSION_MAJOR/MINOR/PATCH from src/main.h" >&2
	exit 1
fi

if [ -n "$tag" ]; then
	version=${tag#v}
	base=${version%%-*}
	if [ "$base" != "$header" ]; then
		fail "tag $tag is version $base, but src/main.h says $header - bump src/main.h (and npad.rc)?"
	fi
else
	version=$header
	base=$header
	tag=v$header
fi
echo "Checking version $base (tag $tag)"

# src/platform/npad.rc: the numeric resource version and its two strings
rc=$(tr -d '\r' <"$root/src/platform/npad.rc")
rc_commas=$(printf '%s' "$base" | tr '.' ',')",0"
for key in FILEVERSION PRODUCTVERSION; do
	got=$(printf '%s\n' "$rc" | sed -n "s/^[[:space:]]*$key[[:space:]]\{1,\}//p" | tr -d ' \t' | head -n 1)
	[ "$got" = "$rc_commas" ] || fail "src/platform/npad.rc $key is '$got', expected $rc_commas"
done
for key in FileVersion ProductVersion; do
	got=$(printf '%s\n' "$rc" | sed -n "s/.*VALUE[[:space:]]*\"$key\"[[:space:]]*,[[:space:]]*\"\([^\"]*\)\".*/\1/p" | head -n 1)
	[ "$got" = "$base.0" ] || fail "src/platform/npad.rc \"$key\" is '$got', expected $base.0"
done

# CHANGELOG.md: the newest entry is this version
newest=$(tr -d '\r' <"$root/CHANGELOG.md" | grep -m 1 '^## \[' || true)
case $newest in
"## [$base]"*) ;;
*) fail "CHANGELOG.md's newest entry is '$newest', expected '## [$base] - ...'" ;;
esac
# ... with highlights, exactly as the release notes will read them
if ! (cd "$root" && sh scripts/release-notes.sh "$tag" >/dev/null); then
	fail "scripts/release-notes.sh cannot write the notes for $tag (see above)"
fi

# A final release: a dated entry, and the metainfo AppStream shows in software
# centres names this release
if [ "$version" = "$base" ] && [ -n "${1:-}" ]; then
	if ! printf '%s\n' "$newest" | grep -Eqx "## \[$(printf '%s' "$base" | sed 's/\./\\./g')\] - [0-9]{4}-[0-9]{2}-[0-9]{2}"; then
		fail "CHANGELOG.md's entry for a final release must be dated: '## [$base] - YYYY-MM-DD', not '$newest'"
	fi
	meta=$root/packaging/linux/io.github.platima.npad.metainfo.xml
	got=$(tr -d '\r' <"$meta" | grep -o '<release [^>]*' | head -n 1 | sed -n 's/.*version="\([^"]*\)".*/\1/p')
	[ "$got" = "$base" ] || fail "$(basename "$meta")'s newest <release> is '$got', expected $base (add <release version=\"$base\" date=\"...\"/>)"
fi

if [ "$failed" -ne 0 ]; then
	echo "Version check FAILED for $tag" >&2
	exit 1
fi
echo "OK: src/main.h, npad.rc and CHANGELOG.md agree on $base"
