#!/bin/sh
# Read a GitHub release - a draft included - through the API, every page of it.
#
#   scripts/ci-release.sh state <tag>                  prints none, draft or published
#   scripts/ci-release.sh download <tag> <dir> [glob]  every asset (or those matching glob)
#
# Needs gh with GH_TOKEN; the repository is GITHUB_REPOSITORY (CI) or NPAD_REPO,
# default platima/npad. A draft is visible only to a token that can write to
# the repository, and only through the release list (releases/tags/<tag>
# never returns one), so the release is found by listing.
#
# Why not `curl .../releases/<id>/assets` or a one-page listing: the API
# returns 30 items per page unless told otherwise, and a release has had more
# than 30 assets since v0.36.0. CHECKSUMS.txt for v0.36.0-rc3 silently lost
# the three .deb files to exactly that. Every listing here uses --paginate,
# and download checks that it got as many files as the listing named.
set -eu
repo=${GITHUB_REPOSITORY:-${NPAD_REPO:-platima/npad}}

usage() {
	echo "usage: $0 state <tag> | download <tag> <dir> [glob]" >&2
	exit 2
}

[ $# -ge 2 ] || usage
cmd=$1
tag=$2
# The tag is spliced into a jq filter below, so it must be a plain tag
if ! printf '%s\n' "$tag" | grep -Eqx 'v[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?'; then
	echo "ci-release: '$tag' is not a release tag" >&2
	exit 2
fi

# "<id> <draft>" for each release with this tag (normally at most one)
releases=$(gh api --paginate "repos/$repo/releases?per_page=100" \
	--jq ".[] | select(.tag_name == \"$tag\") | \"\(.id) \(.draft)\"")
count=$(printf '%s' "$releases" | grep -c . || true)

case $cmd in
state)
	if [ "$count" -eq 0 ]; then
		echo none
	elif [ "$count" -gt 1 ]; then
		echo "ci-release: $count releases have the tag $tag; delete the stray drafts first" >&2
		exit 1
	elif [ "${releases#* }" = true ]; then
		echo draft
	else
		echo published
	fi
	;;
download)
	[ $# -ge 3 ] || usage
	dir=$3
	glob=${4:-*}
	if [ "$count" -ne 1 ]; then
		echo "ci-release: expected one release tagged $tag, found $count" >&2
		exit 1
	fi
	id=${releases%% *}
	mkdir -p "$dir"
	list=$(gh api --paginate "repos/$repo/releases/$id/assets?per_page=100" --jq '.[] | "\(.id) \(.name)"')
	want=0
	got=0
	# Asset names are spelled by our own build; anything else is refused
	# rather than written to a path it names
	while read -r aid name; do
		[ -n "$aid" ] || continue
		# shellcheck disable=SC2254 # glob is a pattern on purpose
		case $name in
		$glob) ;;
		*) continue ;;
		esac
		want=$((want + 1))
		if ! printf '%s\n' "$name" | grep -Eqx '[A-Za-z0-9][A-Za-z0-9._+~-]*'; then
			echo "ci-release: refusing the asset name '$name'" >&2
			exit 1
		fi
		gh api -H "Accept: application/octet-stream" "repos/$repo/releases/assets/$aid" >"$dir/$name"
		got=$((got + 1))
	done <<EOF
$list
EOF
	if [ "$got" -ne "$want" ] || [ "$(find "$dir" -maxdepth 1 -type f | wc -l | tr -d ' ')" -lt "$want" ]; then
		echo "ci-release: listed $want assets but have $got" >&2
		exit 1
	fi
	echo "Downloaded $got assets of $tag (release $id) to $dir"
	;;
*) usage ;;
esac
