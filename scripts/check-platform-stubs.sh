#!/bin/sh
# Check that every stub UI backend defines each ui_platform_* function the
# shared code calls, so a backend that is never compiled here (Cocoa: there is
# no macOS runner) cannot silently stop linking again.
#
# Usage: scripts/check-platform-stubs.sh <shared.o> <backend source>...
#   <shared.o>  a relocatable link of the core, ui_interface.c and main.c
#               (the Makefile's test-platform-stubs target builds it), whose
#               undefined ui_platform_* symbols are what a backend must supply
#
# A definition is a line that starts in column 0 and names the function
# followed by "(" without ending in ";" - the project's clang-format style
# puts a definition's name on its first line. The list comes from the host
# build, so a call made only under another platform's #ifdef is not seen; the
# Windows terminal stub is also linked by `make windows`, which catches those.
set -eu

if [ $# -lt 2 ]; then
	echo "usage: $0 <shared.o> <backend source>..." >&2
	exit 2
fi
obj=$1
shift
[ -f "$obj" ] || { echo "$obj not found" >&2; exit 2; }

# nm prints Mach-O symbols with a leading underscore; strip it either way
needed=$(nm -u "$obj" | awk '{ print $NF }' | sed 's/^_//' | grep '^ui_platform_' | sort -u)
if [ -z "$needed" ]; then
	echo "no ui_platform_* references found in $obj - is it the right object?" >&2
	exit 2
fi

status=0
for src in "$@"; do
	missing=""
	for fn in $needed; do
		if ! grep -E "^[A-Za-z_].*[^A-Za-z0-9_]$fn\(" "$src" | grep -qv ';[[:space:]]*$'; then
			missing="$missing $fn"
		fi
	done
	if [ -n "$missing" ]; then
		echo "FAIL: $src does not define:$missing"
		status=1
	else
		echo "ok: $src defines all $(echo "$needed" | wc -l | tr -d ' ') ui_platform_* functions the shared code calls"
	fi
done
exit $status
