/*
 * npad - Update check helpers
 * Pure parsing/comparison logic for the "Check for Updates" feature.
 * Networking lives in the platform layer; everything here is testable
 * without it. Updates are opt-in and off by default - npad never checks
 * automatically unless the user enables it in Preferences > Updates.
 *
 * Author: Platima
 * https://npad.dev
 */

#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <stdbool.h>
#include <stddef.h>

// Extract the "tag_name" value (e.g. "v0.15.0") from a GitHub releases API
// JSON response into out. Returns false when absent or too long.
bool update_extract_tag(const char *json, char *out, size_t out_cap);

// The newest release tag in a releases API response: either one release
// object (/releases/latest) or an array of them (/releases?per_page=N).
// Drafts are never picked; pre-releases only when include_prereleases. Tags
// that do not start with a version number ("nightly") are ignored. Returns
// false when nothing qualifies or the tag does not fit in out.
bool update_pick_release_tag(const char *json, bool include_prereleases, char *out, size_t out_cap);

// Whether a version string (update_current_version's form) is a published
// pre-release such as "v0.36.0-rc2". A development build ("-dev") is not:
// nothing is ever published under that name. Pre-release builds follow the
// pre-releases (GitHub's /releases/latest never names one), so a tester on
// rc2 is offered rc3 as well as the final release.
bool update_is_prerelease(const char *version);

// A user-facing sentence for a non-200 HTTP answer from the update server,
// e.g. GitHub's rate limit (403/429) is not "could not reach github.com".
void update_http_status_message(int status, char *out, size_t cap);

// Compare two dotted version strings: the numeric triple first (an optional
// leading 'v'), then, for the same triple, a pre-release suffix ("-rc2",
// "-dev") sorts BEFORE the plain release, as in semver - so a v0.36.0-rc2
// build is offered v0.36.0. Two suffixes compare with numbers as numbers
// (rc2 < rc10). Anything after a space (" (abc1234)") is ignored.
// Negative when a < b, 0 when equal, positive when a > b.
int update_version_compare(const char *a, const char *b);

// This build's version as the updater and the Updates page should show it,
// from the compile-time version string (NPAD_VERSION): "v0.36.0" for a
// release, "v0.36.0-rc2" for a pre-release, "v0.36.0-dev" for anything else
// (the " (abc1234)" commit note is dropped). Falls back to the numeric
// triple when the string does not start with one.
void update_current_version(const char *build_version, int major, int minor, int patch, char *out,
                            size_t cap);

// Parse the leading 64-hex-digit SHA-256 digest from a .sha256 file's text
// (formats like "<hex>  <filename>") into out[65], lowercased. Returns
// false when the text does not start with a valid digest.
bool update_parse_sha256(const char *text, char *out);

// Whether an available update should be surfaced: true when latest is
// version-newer than current AND latest != skipped. An empty/NULL latest
// (no successful check yet) yields false. Drives the Help-menu indicator
// and the notify/prompt/auto decision.
bool update_is_newer_unskipped(const char *current, const char *latest, const char *skipped);

// Find a release asset in a GitHub releases API JSON response by matching the
// END of its "name", and copy that asset's "browser_download_url" into out.
// Matching is case-insensitive, e.g. suffix "-setup-win-x64.exe" selects the
// installer while "-setup-win-x64.exe.sha256" selects its digest. The JSON is
// walked by structure: each asset's own "name" and URL are paired, in either
// order, and a "name" inside a nested object (the uploader's) is never taken
// for the asset's.
//
// Resolving the download from the response - rather than rebuilding the file
// name from the tag - means a future change to release asset naming does not
// strand the in-app updater, which is exactly what happened once before.
// Returns false when no asset matches, the URL is missing, or out is too small.
bool update_find_asset_url(const char *json, const char *name_suffix, char *out, size_t out_cap);

// The Windows architecture THIS build was compiled for, as release assets
// name it: "x64" or "arm64", or NULL for any other target. Decided by the
// compiler's target macros, not by asking Windows: an x64 npad running under
// emulation on an Arm PC is an x64 build and keeps updating to x64.
const char *update_build_arch(void);

// The release asset suffix of the installer for arch ("x64" or "arm64"), or of
// its .sha256 digest when checksum is true - e.g. "-setup-win-arm64.exe". NULL
// for any other arch, so a build of unknown architecture is offered nothing
// rather than the x64 installer.
const char *update_installer_suffix(const char *arch, bool checksum);

// --- Which Windows installer updates this copy ------------------------------

typedef enum {
    UPDATE_PACKAGE_SETUP, // The Inno setup exe (per-user by default)
    UPDATE_PACKAGE_MSI,   // The Windows Installer package
} UpdatePackage;

// update_installer_suffix for either package: the MSI's is "-msi-win-<arch>.msi"
const char *update_package_suffix(const char *arch, UpdatePackage package, bool checksum);

// Which package to run for an in-app update. The MSI only when Windows
// Installer has npad registered and the Inno setup does not own this copy:
// running the setup over an MSI install would add a second product to
// Add/Remove Programs whose uninstaller deletes the other's files.
UpdatePackage update_windows_package(bool setup_owns_this_copy, bool msi_installed);

// Whether exe_path is a file directly inside dir. Windows paths: '\\' and '/'
// are the same separator, case is ignored (ASCII), and a trailing separator on
// dir is optional - Inno records InstallLocation with one.
bool update_dir_holds_exe(const char *dir, const char *exe_path);

// --- Is the downloaded installer genuine? -----------------------------------

typedef enum {
    UPDATE_SIG_VALID, // Authenticode signature present and trusted
    UPDATE_SIG_NONE,  // No signature at all
    UPDATE_SIG_BAD,   // A signature that does not verify (tampered, untrusted, revoked)
} UpdateSignature;

// The reason an installer must not run, or NULL when it may. The SHA-256 from
// the same release is checked first and always; this adds the publisher.
// Releases are signed only when the release workflow has signing configured,
// so the rule follows THIS copy: a signed npad accepts only an installer
// validly signed by the same publisher, while an unsigned build (a dev build,
// or a release from before signing) accepts an unsigned installer - but never
// one whose signature is present and broken.
const char *update_signature_problem(UpdateSignature this_build, UpdateSignature installer,
                                     bool same_publisher);

// --- HTTP (Linux) -----------------------------------------------------------
//
// The Linux build has no WinHTTP and takes no HTTP library: it speaks
// HTTP/1.1 over a GIO TLS connection and reads until the server closes it.

// Split a complete raw HTTP/1.x response (status line, headers, body) into its
// status code and body. A body sent with "Transfer-Encoding: chunked" is
// de-chunked; otherwise it is everything after the headers (cut to
// Content-Length when one is given). *body is malloc'd and NUL-terminated
// (free()); *body_len may be NULL. Returns false for anything that is not a
// well-formed response, including a chunked body that ends early.
bool update_http_parse_response(const char *raw, size_t len, int *status, char **body,
                                size_t *body_len);

// --- How this copy was installed (Linux) ------------------------------------
//
// npad on Linux never installs an update: Check for Updates says how to update
// the way this copy was installed. The platform gathers the facts (which repo
// files exist, which package manager owns the executable) and these functions
// decide - so the decision is tested without any of those present.

typedef enum {
    UPDATE_OWNER_NONE, // No package manager owns the executable
    UPDATE_OWNER_DPKG,
    UPDATE_OWNER_RPM,
    UPDATE_OWNER_PACMAN,
} UpdatePackageOwner;

typedef enum {
    UPDATE_SOURCE_APT_REPO,    // A .deb, with the pkg.npad.dev apt source configured
    UPDATE_SOURCE_DNF_REPO,    // An rpm, with the pkg.npad.dev dnf repository configured
    UPDATE_SOURCE_PACMAN_REPO, // An Arch package, with the [npad] pacman repository
    UPDATE_SOURCE_AUR,         // An AUR package (npad-bin, npad-git)
    UPDATE_SOURCE_DEB_FILE,    // A .deb installed by hand, no npad apt source
    UPDATE_SOURCE_RPM_FILE,    // An rpm installed by hand, no npad dnf repository
    UPDATE_SOURCE_PACMAN_FILE, // An Arch package installed by hand, no [npad] repository
    UPDATE_SOURCE_PORTABLE,    // Not owned by a package: the portable tarball or a source build
} UpdateInstallSource;

typedef struct {
    UpdatePackageOwner owner;
    const char *package; // The owning package's name ("npad", "npad-bin"); NULL if unknown
    bool apt_repo;       // An apt source names pkg.npad.dev
    bool dnf_repo;       // A yum.repos.d file names pkg.npad.dev
    bool pacman_repo;    // pacman.conf has an [npad] section
} UpdateInstallFacts;

UpdateInstallSource update_install_source(const UpdateInstallFacts *facts);

// What to tell the user about updating a copy installed this way: a static,
// user-facing paragraph whose last line is the command to run (or the place to
// download from), so the dialog can show it on its own line
const char *update_install_advice(UpdateInstallSource source);

// Whether a package manager could own this executable at all: packages put
// npad in /usr/bin (or /bin). Anything else - ~/.local/bin, an unpacked
// npad-v*-portable-linux-* folder, /usr/local/bin from `make install` - is not worth
// asking dpkg, rpm or pacman about.
bool update_exe_may_be_packaged(const char *exe_path);

// Whether an apt source (.list or deb822 .sources) or a dnf .repo file's text
// points at pkg.npad.dev on a line that is not a comment
bool update_repo_text_names_npad(const char *text);

// Whether pacman.conf's text has an [npad] repository section (not commented out)
bool update_pacman_conf_has_npad(const char *text);

// The owning package's name from the output of the ownership query:
//   dpkg   "dpkg -S <path>"                       -> "npad: /usr/bin/npad"
//   rpm    "rpm -qf --queryformat %{NAME} <path>" -> "npad"
//   pacman "pacman -Qqo <path>"                   -> "npad-bin"
// dpkg's ":amd64" qualifier and diversion lines are dropped. Returns false when
// the output names no package (the query's "not owned" message, or empty).
bool update_parse_owner_output(UpdatePackageOwner owner, const char *output, char *pkg,
                               size_t pkg_cap);

#endif // UPDATE_CHECK_H
