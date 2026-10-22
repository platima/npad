#!/bin/sh
# CHECKSUMS.txt for a release, from the release's own <asset>.sha256 files.
#
#   scripts/ci-checksums.sh write <dir> <tag> [repo] [commit] > CHECKSUMS.txt
#   scripts/ci-checksums.sh verify <dir>
#
# write: <dir> holds every asset and its <asset>.sha256 (as release.yml has them
# before uploading). Every asset must have a .sha256 naming it, and every
# .sha256 its asset; one line per asset, sorted by name. Fails, and writes
# nothing useful, if any is missing: a CHECKSUMS.txt that silently leaves
# files out is worse than none (v0.36.0-rc3's lacked the three .deb files).
# Comment lines start with '#', which sha256sum -c skips.
#
# verify: <dir> holds a release as downloaded, CHECKSUMS.txt included. Every
# asset in it (other than CHECKSUMS.txt and the .sha256 files) must have
# exactly one line, every line must name a file that is there, and every
# digest must match.
set -eu

usage() {
	echo "usage: $0 write <dir> <tag> [repo] [commit] | verify <dir>" >&2
	exit 2
}

# The assets: everything except the checksum files themselves
assets_in() {
	for f in "$1"/*; do
		[ -f "$f" ] || continue
		n=$(basename "$f")
		case $n in
		*.sha256 | CHECKSUMS.txt) ;;
		*) printf '%s\n' "$n" ;;
		esac
	done | LC_ALL=C sort
}

cmd=${1:-}
[ $# -ge 2 ] || usage
dir=$2
[ -d "$dir" ] || {
	echo "ci-checksums: $dir is not a directory" >&2
	exit 1
}
failed=0
fail() {
	echo "ERROR: $*" >&2
	failed=1
}

case $cmd in
write)
	[ $# -ge 3 ] || usage
	tag=$3
	repo=${4:-platima/npad}
	commit=${5:-}
	lines=$(mktemp)
	trap 'rm -f "$lines"' EXIT
	count=0
	for n in $(assets_in "$dir"); do
		if [ ! -f "$dir/$n.sha256" ]; then
			fail "$n has no $n.sha256"
			continue
		fi
		# "<sha256>  <name>" (or " *<name>", sha256sum's binary marker)
		read -r hash listed <"$dir/$n.sha256" || true
		listed=${listed#\*}
		if [ "$listed" != "$n" ]; then
			fail "$n.sha256 names '$listed', not $n"
			continue
		fi
		if ! printf '%s\n' "$hash" | grep -Eqx '[0-9a-fA-F]{64}'; then
			fail "$n.sha256 does not start with a SHA-256 digest"
			continue
		fi
		printf '%s  %s\n' "$(printf '%s' "$hash" | tr 'A-F' 'a-f')" "$n" >>"$lines"
		count=$((count + 1))
	done
	for s in "$dir"/*.sha256; do
		[ -f "$s" ] || continue
		n=$(basename "$s" .sha256)
		[ -f "$dir/$n" ] || fail "$(basename "$s") has no asset $n"
	done
	[ "$count" -gt 0 ] || fail "no assets with checksums in $dir"
	if [ "$failed" -ne 0 ]; then
		echo "CHECKSUMS.txt NOT written" >&2
		exit 1
	fi
	echo "# npad Release Checksums - $tag"
	echo "# Generated: $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
	echo "# Release: https://github.com/$repo/releases/tag/$tag"
	[ -n "$commit" ] && echo "# Commit: $commit"
	echo "# $count files. To check the ones you downloaded, from their folder:"
	echo "#   Linux:   sha256sum -c --ignore-missing CHECKSUMS.txt"
	echo "#   Windows: Get-FileHash -Algorithm SHA256 <file> (PowerShell), or"
	echo "#            certutil -hashfile <file> SHA256 (Command Prompt)"
	cat "$lines"
	;;
verify)
	sums=$dir/CHECKSUMS.txt
	[ -f "$sums" ] || {
		echo "ERROR: $sums is missing" >&2
		exit 1
	}
	listed=$(grep -v '^#' "$sums" | grep -v '^[[:space:]]*$' | sed 's/^[0-9a-fA-F]\{64\}  \*\{0,1\}//' | LC_ALL=C sort)
	dups=$(printf '%s\n' "$listed" | uniq -d)
	[ -z "$dups" ] || fail "CHECKSUMS.txt lists more than once: $(echo $dups)"
	for n in $(assets_in "$dir"); do
		printf '%s\n' "$listed" | grep -Fqx "$n" || fail "CHECKSUMS.txt has no line for $n"
	done
	for n in $listed; do
		[ -f "$dir/$n" ] || fail "CHECKSUMS.txt lists $n, which is not in the release"
	done
	(cd "$dir" && sha256sum -c --strict --quiet CHECKSUMS.txt) || fail "a digest in CHECKSUMS.txt does not match"
	if [ "$failed" -ne 0 ]; then
		echo "CHECKSUMS.txt verification FAILED" >&2
		exit 1
	fi
	echo "OK: CHECKSUMS.txt covers all $(printf '%s\n' "$listed" | grep -c .) assets and every digest matches"
	;;
*) usage ;;
esac
