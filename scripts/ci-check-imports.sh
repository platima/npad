#!/bin/sh
# Check that a built npad.exe delay-loads the DLLs it does not need to open a
# window (winhttp, bcrypt, comdlg32, msimg32; DELAY_DLLS in the Makefile)
# instead of binding them at load time, which costs a cold start.
#
# Usage: scripts/ci-check-imports.sh <npad.exe>
#
# The two Windows linkers leave different evidence (LESSONS.md, "Windows on
# Arm"), so the check follows the exe's PE machine field:
#   x64 (GNU ld, dlltool -y): the delay-load stubs do not fill the PE
#     delay-import directory, so "delay-loaded" is "absent from the import
#     table, yet the DLL's name is in the file" (its stub names it).
#     Uses $OBJDUMP, default x86_64-w64-mingw32-objdump (MSYS2: objdump).
#   arm64 (lld, --delayload): each DLL must be listed under DelayImport and
#     not under Import. Uses $READOBJ, default llvm-readobj.
# A matching -l flag added to the link, or a change in dlltool's -y, binds the
# DLL statically again; nothing else would notice, since the exe still works.
set -eu
exe=$1
dlls="winhttp bcrypt comdlg32 msimg32"

[ -f "$exe" ] || {
	echo "ERROR: $exe not found" >&2
	exit 1
}
off=$(od -An -t u4 -j 60 -N 4 "$exe" | tr -d ' ')
machine=$(od -An -t x2 -j $((off + 4)) -N 2 "$exe" | tr -d ' ')
failed=0

case $machine in
8664)
	objdump=${OBJDUMP:-x86_64-w64-mingw32-objdump}
	bound=$($objdump -p "$exe" | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr -d '\015')
	[ -n "$bound" ] || {
		echo "ERROR: $objdump -p listed no imports for $exe" >&2
		exit 1
	}
	echo "Bound at load: $(echo $bound)"
	for dll in $dlls; do
		if printf '%s\n' "$bound" | grep -qix "$dll.dll"; then
			echo "ERROR: $dll.dll is bound at load time; it should be delay-loaded" >&2
			failed=1
		elif ! LC_ALL=C grep -aqi "$dll\.dll" "$exe"; then
			echo "ERROR: $dll.dll is not referenced at all; is the delay-import library still linked?" >&2
			failed=1
		else
			echo "OK: $dll.dll is delay-loaded"
		fi
	done
	;;
aa64)
	readobj=${READOBJ:-llvm-readobj}
	imports=$($readobj --coff-imports "$exe")
	bound=$(printf '%s\n' "$imports" | grep -A1 '^Import {' | sed -n 's/^ *Name: //p')
	delayed=$(printf '%s\n' "$imports" | grep -A1 '^DelayImport {' | sed -n 's/^ *Name: //p')
	echo "Bound at load: $(echo $bound)"
	echo "Delay-loaded:  $(echo $delayed)"
	for dll in $dlls; do
		if printf '%s\n' "$bound" | grep -qix "$dll.dll"; then
			echo "ERROR: $dll.dll is bound at load time; it should be delay-loaded" >&2
			failed=1
		elif ! printf '%s\n' "$delayed" | grep -qix "$dll.dll"; then
			echo "ERROR: $dll.dll is not delay-loaded" >&2
			failed=1
		else
			echo "OK: $dll.dll is delay-loaded"
		fi
	done
	;;
*)
	echo "ERROR: $exe has PE machine '$machine', neither x64 (8664) nor arm64 (aa64)" >&2
	exit 1
	;;
esac
[ "$failed" -eq 0 ] || exit 1
echo "OK: $exe delay-loads $dlls"
