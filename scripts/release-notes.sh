#!/bin/sh
# Write the GitHub release notes for a tag to stdout: the version's
# "### ✨ Highlights" from CHANGELOG.md, the downloads and install routes, and
# a link to the full changelog entry. Used by release.yml for both the initial
# and the final release body, and run before anything is published so a
# release without highlights fails instead of shipping a bare asset list.
#
# Usage: scripts/release-notes.sh <tag> [repo] [commit] [run-url]
#   e.g. scripts/release-notes.sh v0.34.0 platima/npad "$GITHUB_SHA" "$RUN_URL"
set -eu

tag=$1
repo=${2:-platima/npad}
commit=${3:-}
run_url=${4:-}
version=${tag#v}
base=${version%%-*} # A pre-release (v0.35.0-rc1) uses its version's entry

# This version's entry: from its "## [x.y.z]" heading to the next "## ["
entry=$(awk -v v="$base" '
    index($0, "## [" v "]") == 1 { on = 1; print; next }
    on && /^## \[/ { exit }
    on { print }
' CHANGELOG.md)
if [ -z "$entry" ]; then
    echo "release-notes: CHANGELOG.md has no '## [$base]' entry" >&2
    exit 1
fi
heading=$(printf '%s\n' "$entry" | head -n 1)

# Its highlights: from "### ✨ Highlights" to the next "### "
highlights=$(printf '%s\n' "$entry" | awk '
    /^### / { if (on) exit; if (index($0, "Highlights")) { on = 1; next } }
    on { print }
')
if [ -z "$(printf '%s' "$highlights" | tr -d '[:space:]')" ]; then
    echo "release-notes: the [$base] entry in CHANGELOG.md has no '### ✨ Highlights' section" >&2
    exit 1
fi

# GitHub's anchor for "## [0.34.0] - 2026-10-17" is "0340---2026-10-17"
anchor=$(printf '%s' "${heading#\#\# }" | tr -d '[].' | tr ' ' '-' | tr '[:upper:]' '[:lower:]')

cat <<EOF
## npad $tag

$(printf '%s\n' "$highlights" | sed -e '/./,$!d')

Every change in this release: [CHANGELOG.md](https://github.com/$repo/blob/$tag/CHANGELOG.md#$anchor)

### Downloads

**Windows** (\`x64\` for most PCs, \`arm64\` for Windows on Arm)
- \`npad-$tag-setup-win-x64.exe\` / \`npad-$tag-setup-win-arm64.exe\` - installer (per-user by default; system-wide available)
- \`npad-$tag-msi-win-x64.msi\` / \`npad-$tag-msi-win-arm64.msi\` - MSI for silent or managed deployment (\`msiexec /i <file> /qn /norestart\`)
- \`npad-$tag-portable-win-x64.exe\` / \`npad-$tag-portable-win-arm64.exe\` - portable, no installation

**Linux** (GTK 3 desktop app for X11 and Wayland; amd64, arm64 and riscv64, where riscv64 needs Debian 13 / Ubuntu 24.04 or newer)
- Recommended: the signed apt, dnf and pacman repositories at https://pkg.npad.dev, so npad updates with the rest of your system
- \`npad_${version}_<arch>.deb\` (\`amd64\`, \`arm64\`, \`riscv64\`) - Debian, Ubuntu, Mint (\`sudo apt install ./npad_${version}_amd64.deb\`)
- \`npad-${version}-1.<arch>.rpm\` (\`x86_64\`, \`aarch64\`, \`riscv64\`) - Fedora, openSUSE (\`sudo dnf install ./npad-${version}-1.x86_64.rpm\`)
- \`npad-${version}-1-<arch>.pkg.tar.zst\` (\`x86_64\`, \`aarch64\`, \`riscv64\`) - Arch Linux, Arch Linux ARM and Arch RISC-V (\`sudo pacman -U ./npad-${version}-1-x86_64.pkg.tar.zst\`)
- \`npad-$tag-portable-linux-<arch>.tar.gz\` (\`x86_64\`, \`aarch64\`, \`riscv64\`) - portable, for any glibc distro with GTK 3 and no package for it: unpack and run \`./npad\` (its README.txt adds a menu entry under \`~/.local\`; no automatic updates)

**Verification:** \`CHECKSUMS.txt\` lists the SHA-256 of every file. On Linux, run \`sha256sum -c --ignore-missing CHECKSUMS.txt\` in the folder you downloaded to, which checks just the files you have; on Windows, compare \`Get-FileHash <file>\` with its line.

> **SmartScreen warning?** Check the file's Properties > Digital Signatures tab: a signed release names the publisher there, and SmartScreen can still warn while a publisher's reputation is new. If unsure, verify the SHA-256 against \`CHECKSUMS.txt\`, then choose "More info" > "Run anyway".
EOF

if [ -n "$commit$run_url" ]; then
    printf '\n---\n\n'
    [ -n "$commit" ] && printf 'Built from `%s`' "$commit"
    [ -n "$run_url" ] && printf ' by [this workflow run](%s)' "$run_url"
    printf ', after CI validation passed.\n'
fi
