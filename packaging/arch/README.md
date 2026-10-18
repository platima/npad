# npad on Arch Linux

Three things ship for Arch, Arch Linux ARM (aarch64) and the Arch RISC-V port
(riscv64):

| What | Where | Built by |
|------|-------|----------|
| `npad-<version>-1-<arch>.pkg.tar.zst` | the GitHub release | `packages.yml` (nfpm, same `packaging/linux/nfpm.yaml` as the .deb and .rpm) |
| signed pacman repository `[npad]` | `https://pkg.npad.dev/arch/<arch>` | `platima/npad-packages`, from the release's packages |
| `npad-bin` | the AUR | this directory: `aur/npad-bin/PKGBUILD` and `.SRCINFO` |

`npad-bin` is built from the release's **portable tarball**
(`npad-v<version>-portable-linux-<x86_64|aarch64|riscv64>.tar.gz`), the same binary the
packages carry, so an AUR helper does not need to compile anything. It
`provides=npad` and `conflicts=npad`, so it and the repository package are
alternatives, not companions.

Do not edit `pkgver` or the sums by hand: `scripts/aur-update.sh v<version>`
sets them from the release's own `.sha256` files and rewrites `.SRCINFO`
(it needs `makepkg`, so run it on Arch or in an `archlinux` container as an
ordinary user). The PKGBUILD in the repository carries placeholder zero sums
until a release exists to take them from.

## Publishing to the AUR (one time, by the maintainer)

Nothing here is published automatically until you have done steps 1-4.

1. **Create an AUR account** at <https://aur.archlinux.org/register>. Confirm
   the email address.
2. **Make an SSH key for it** (a dedicated one, so it can be a CI secret later):

   ```bash
   ssh-keygen -t ed25519 -C "npad AUR" -f ~/.ssh/aur
   ```

   Paste `~/.ssh/aur.pub` into *My Account > SSH Public Key* on the AUR site.
   Add a host entry so ssh uses it:

   ```
   # ~/.ssh/config
   Host aur.archlinux.org
     IdentityFile ~/.ssh/aur
     User aur
   ```

   The first connection asks you to trust the host; its ED25519 fingerprint is
   `SHA256:RFzBCUItH9LZS0cKB5UE6ceAYhBD5C8GeOBip8Z11+4` (published at
   <https://archlinux.org/news/aur-migration-new-ssh-hostkeys/>; compare it).
3. **Wait for a release that has the Arch assets**, i.e. the first npad
   release built from this branch (the tarballs `npad-v<version>-portable-linux-*.tar.gz`
   must exist on GitHub). Then, on an Arch machine (or WSL/container), in a
   checkout of npad at that release:

   ```bash
   sh scripts/aur-update.sh v<version>      # e.g. v0.36.0; fills pkgver, sums, .SRCINFO
   ```

   Check it builds before pushing: `cd packaging/arch/aur/npad-bin && makepkg -si`
   (and `namcap PKGBUILD *.pkg.tar.zst` if you have it).
4. **First push.** The AUR creates the package when you push to a repository
   that does not exist yet. The branch must be `master`:

   ```bash
   git clone ssh://aur@aur.archlinux.org/npad-bin.git aur-npad-bin   # warns "empty repository", fine
   cd aur-npad-bin
   git checkout -B master
   cp /path/to/npad/packaging/arch/aur/npad-bin/{PKGBUILD,.SRCINFO} .
   git add PKGBUILD .SRCINFO
   git commit -m "Initial import: npad-bin <version>"
   git push origin master
   ```

   The package appears at <https://aur.archlinux.org/packages/npad-bin>.
   (Only `PKGBUILD`, `.SRCINFO` and any helper files belong in that repository.)

## Letting releases update the AUR (optional)

`release.yml` has a `publish-aur` job that runs after each final release and
pushes the new version. It does nothing unless the repository secret
`AUR_SSH_KEY` exists, so skipping this step is fine: you can run steps 3-4 for
each release by hand instead.

1. After the first manual push works, add the **private** key as a secret:
   `gh secret set AUR_SSH_KEY --repo platima/npad < ~/.ssh/aur`
   (or *Settings > Secrets and variables > Actions > New repository secret*).
2. From then on a release (a `v*.*.*` tag without `-rc` style suffix) runs
   `scripts/aur-update.sh` in an `archlinux` container, clones
   `ssh://aur@aur.archlinux.org/npad-bin.git`, copies `PKGBUILD` and `.SRCINFO`
   in, commits `Update to <version>` and pushes. The AUR host key is pinned in
   the job (the fingerprint above), not trusted on first use. If the AUR
   already has that version it does nothing.

To stop it, delete the secret.

## Checking it locally

```bash
make pacman                               # in a Debian 12 container for release-like binaries
sudo pacman -U dist/npad-*-1-x86_64.pkg.tar.zst
npad --version
```

`scripts/ci-aur-check.sh` (run by `packages.yml`'s `aur-check` job) is the
test for the AUR files: `.SRCINFO` must be what `makepkg --printsrcinfo` makes
of the PKGBUILD, and `aur-update.sh` must rewrite both consistently.
