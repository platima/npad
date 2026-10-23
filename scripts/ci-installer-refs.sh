#!/bin/sh
# Check that every file the two Windows installers package exists, without
# building them (that only happens on a Windows runner, at release time).
#
# Usage: scripts/ci-installer-refs.sh [installer-dir]   (default: installer)
#
# Reads every file reference in npad.wxs (SourceFile="..." and Source="...")
# and in npad.iss (Source: "..." and the [Setup] *File= directives such as
# LicenseFile), relative to the installer directory, as wix and ISCC resolve
# them. References made of build-time variables ($(var.NpadExe), {#NpadExe})
# are the freshly built exe and are skipped. The bundled fonts under fonts\
# are not in the tree - installer/fetch-fonts.ps1 downloads them - so a font
# reference must instead name a file fetch-fonts.ps1 produces.
#
# v0.28.0 shipped with no installers because npad.wxs still named a deleted
# icon; this used to check that one kind of reference only.
set -eu
dir=${1:-installer}
failed=0
checked=0

# Strip Windows line endings from a file
lf() {
	tr -d '\015' <"$1"
}

# "<Family>/<file>" for every file fetch-fonts.ps1 writes: its Files map's
# values, plus OFL.txt for a family with a separate License download
fonts=$(lf "$dir/fetch-fonts.ps1" | awk '
	/^[[:space:]]*Name[[:space:]]*=[[:space:]]*"/ { s = $0; sub(/^[^"]*"/, "", s); sub(/".*/, "", s); fam = s; next }
	/^[[:space:]]*License[[:space:]]*=/ { if (fam != "") print fam "/OFL.txt"; next }
	fam != "" && /^[[:space:]]*"[^"]+"[[:space:]]*=[[:space:]]*"[^"]+"/ {
		s = $0; sub(/^[^=]*=[^"]*"/, "", s); sub(/".*/, "", s); print fam "/" s
	}')
if [ -z "$fonts" ]; then
	echo "ERROR: could not read the font list from $dir/fetch-fonts.ps1" >&2
	exit 1
fi

check_ref() {
	src=$1
	ref=$(printf '%s' "$2" | tr '\\' '/')
	case $ref in
	*'$('* | *'{'*'}'*) return 0 ;; # a build-time variable
	esac
	checked=$((checked + 1))
	case $ref in
	fonts/*)
		rel=${ref#fonts/}
		fam=${rel%%/*}
		pat=${rel#*/}
		hit=0
		for f in $(printf '%s\n' "$fonts" | sed -n "s|^$fam/||p"); do
			# shellcheck disable=SC2254 # a wildcard reference is a pattern
			case $f in
			$pat) hit=1 ;;
			esac
		done
		if [ "$hit" -eq 1 ]; then
			printf 'ok: %s: %s (fetched by fetch-fonts.ps1)\n' "$src" "$2"
		else
			printf 'ERROR: %s: %s is not a file fetch-fonts.ps1 downloads\n' "$src" "$2" >&2
			failed=1
		fi
		;;
	*)
		if [ -e "$dir/$ref" ]; then
			printf 'ok: %s: %s\n' "$src" "$2"
		else
			printf 'ERROR: %s references a missing file: %s\n' "$src" "$2" >&2
			failed=1
		fi
		;;
	esac
}

# Every reference, as "<file> <path>" lines: npad.wxs's SourceFile="..." and
# Source="..." attributes on any element; npad.iss's [Files] Source: "..."
# entries (comment lines start with ';') and the [Setup] directives that name
# a file
setup_keys='LicenseFile|InfoBeforeFile|InfoAfterFile|SetupIconFile|WizardImageFile|WizardSmallImageFile'
refs=$(
	lf "$dir/npad.wxs" | grep -o '[[:space:]]Source\(File\)\{0,1\}="[^"]*"' |
		sed 's/^[[:space:]]*Source\(File\)\{0,1\}="//; s/"$//; s/^/npad.wxs /'
	lf "$dir/npad.iss" | grep -v '^[[:space:]]*;' | awk -v keys="^($setup_keys)\$" '
		/^[[:space:]]*Source:[[:space:]]*"/ {
			s = $0; sub(/^[^"]*"/, "", s); sub(/".*/, "", s); print "npad.iss " s; next
		}
		/^[[:space:]]*[A-Za-z]+[[:space:]]*=/ {
			k = $0; sub(/^[[:space:]]*/, "", k); sub(/[[:space:]]*=.*/, "", k)
			if (k ~ keys) {
				v = $0; sub(/^[^=]*=[[:space:]]*/, "", v); gsub(/"/, "", v); sub(/[[:space:]]*$/, "", v)
				print "npad.iss " v
			}
		}'
)
while read -r src ref; do
	[ -z "$src" ] || check_ref "$src" "$ref"
done <<EOF
$refs
EOF

if [ "$checked" -lt 5 ]; then
	echo "ERROR: found only $checked installer file references; has the parsing broken?" >&2
	exit 1
fi
if [ "$failed" -ne 0 ]; then
	echo "Installer source references FAILED" >&2
	exit 1
fi
echo "OK: all $checked installer file references resolve"
