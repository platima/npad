#!/bin/sh
# Check a commit is ready to be tagged as a release, then (only if asked) tag
# it. The release itself is built, validated and published by
# .github/workflows/release.yml when the tag is pushed; this runs the same
# gate first, locally, so a mistake costs a minute instead of a release run.
#
# Usage: scripts/release.sh <tag> [--push]      e.g. scripts/release.sh v0.36.0
#
#   1. The version gate release.yml runs (scripts/check-release-version.sh):
#      the tag is well formed; src/main.h, src/platform/npad.rc and the newest
#      CHANGELOG.md entry (with its "### ✨ Highlights") name its version;
#      a final release also has a dated entry and the AppStream metainfo's
#      newest <release> is this version.
#   2. The checkout is main, clean, level with origin/main, and the tag is new.
#   3. The local checks CI runs: make lint, format-check, test, and the
#      Windows x64 and Linux GTK builds, all with WERROR=1 (needs the MinGW
#      cross compiler, GTK 3 headers, cppcheck 2.13 and clang-format 18:
#      scripts/setup-runner.sh).
#   4. Without --push: prints the dry-run and tag commands. With --push:
#      creates the annotated tag and pushes it, which starts the release.
#
# Run the dry run before tagging (it builds, signs and validates every asset,
# and publishes nothing):
#   gh workflow run release.yml --ref main -f version=<tag>
# The release then starts as a draft and is published only once every asset
# is attached and validated. Its downloads (CLAUDE.md, "Release asset names"):
#   Windows  npad-<tag>-setup-win-<x64|arm64>.exe, -msi-win-<arch>.msi,
#            -portable-win-<arch>.exe
#   Linux    npad_<version>_<amd64|arm64|riscv64>.deb,
#            npad-<version>-1.<x86_64|aarch64|riscv64>.rpm,
#            npad-<version>-1-<arch>.pkg.tar.zst,
#            npad-<tag>-portable-linux-<x86_64|aarch64|riscv64>.tar.gz
#   and a .sha256 for each, plus CHECKSUMS.txt listing them all.
set -eu

info() { printf '[INFO] %s\n' "$*"; }
die() {
	printf '[ERROR] %s\n' "$*" >&2
	exit 1
}

[ $# -ge 1 ] || die "usage: $0 <tag> [--push]   (e.g. $0 v0.36.0)"
tag=$1
push=false
[ "${2:-}" = --push ] && push=true
cd "$(dirname "$0")/.."

info "Checking the version sources for $tag"
sh scripts/check-release-version.sh "$tag" || die "the version gate failed (see above); release.yml would stop at the same place"

branch=$(git rev-parse --abbrev-ref HEAD)
[ "$branch" = main ] || die "releases are tagged on main (this is $branch); main takes releases through pull requests"
git diff-index --quiet HEAD -- || die "the working tree has uncommitted changes"
[ -z "$(git ls-files --others --exclude-standard)" ] || die "the working tree has untracked files"
git fetch --quiet origin main
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || die "HEAD is not origin/main; pull (or push the merge) first"
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null || git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
	die "the tag $tag already exists"
fi

info "Running the local checks (lint, format, unit tests, Windows x64 and Linux GTK builds)"
make lint || die "make lint failed"
make format-check || die "make format-check failed; run make format"
make test WERROR=1 || die "make test failed"
make windows WERROR=1 || die "the Windows build failed"
make linux-gtk WERROR=1 || die "the Linux GTK build failed"

if [ "$push" != true ]; then
	info "Ready. Rehearse the release first (publishes nothing):"
	echo "  gh workflow run release.yml --ref main -f version=$tag"
	info "Then tag it, which starts the real release:"
	echo "  $0 $tag --push"
	echo "  (or: git tag -a $tag -m 'Release $tag' && git push origin $tag)"
	exit 0
fi

if [ -t 0 ]; then
	printf 'Tag %s at %s and push it, starting the release? [y/N] ' "$tag" "$(git rev-parse --short HEAD)"
	read -r answer
	case $answer in
	y | Y | yes) ;;
	*) die "aborted; nothing was tagged" ;;
	esac
fi
git tag -a "$tag" -m "Release $tag"
git push origin "$tag"
info "Pushed $tag. release.yml now builds it as a draft and publishes it once every asset is validated:"
echo "  https://github.com/platima/npad/actions/workflows/release.yml"
info "Tag the next release only after this run has finished (concurrent runs queue)."
