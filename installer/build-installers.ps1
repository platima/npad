# Builds both npad Windows installers into dist/:
#   npad-v<version>-setup-win-x64.exe     (Inno Setup, interactive)
#   npad-v<version>-msi-win-x64.msi       (WiX, silent-install oriented)
#   npad-v<version>-portable-win-x64.exe  (the same npad.exe, on its own)
# plus .sha256 files for each.
#
# Requirements (Windows): Inno Setup 6 (ISCC.exe), WiX (dotnet tool: wix),
# and a built npad.exe in the repo root (make windows, or pass -NpadExe).
# The bundled fonts are fetched (SHA256-pinned) by fetch-fonts.ps1.
#
# Code signing is optional and external: pass -SignCmd with a script or
# executable that signs the single file given as its first argument (CI
# generates one that wraps signtool with Azure Trusted Signing). When set,
# npad.exe is signed BEFORE packaging so the binary inside both installers
# carries the signature, Inno signs its uninstaller through the same command,
# and the finished setup exe and MSI are signed last. Checksums are computed
# after all of that, since signing changes the bytes.

param(
    [string]$NpadExe = "",     # Path to npad.exe; defaults to <repo>\npad.exe
    [string]$Version = "",     # Override; defaults to parsing src/main.h
    [string]$SignCmd = ""      # Path to a "sign one file" command; empty = unsigned
)

$ErrorActionPreference = "Stop"
$installerDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $installerDir
$dist = Join-Path $repo "dist"
New-Item -ItemType Directory -Force $dist | Out-Null

function Invoke-Signing([string]$Path) {
    if (-not $SignCmd) { return }
    Write-Host "Signing $(Split-Path -Leaf $Path)..."
    & $SignCmd $Path
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $Path (exit code $LASTEXITCODE)" }
}

# --- Version from src/main.h unless overridden ---
if (-not $Version) {
    $mainH = Get-Content (Join-Path $repo "src\main.h") -Raw
    $maj = [regex]::Match($mainH, '#define NPAD_VERSION_MAJOR (\d+)').Groups[1].Value
    $min = [regex]::Match($mainH, '#define NPAD_VERSION_MINOR (\d+)').Groups[1].Value
    $pat = [regex]::Match($mainH, '#define NPAD_VERSION_PATCH (\d+)').Groups[1].Value
    if (-not ($maj -and $min -and $pat)) { throw "Could not parse version from src/main.h" }
    $Version = "$maj.$min.$pat"
}
Write-Host "Building installers for npad $Version$(if ($SignCmd) { ' (signed)' } else { ' (unsigned)' })"

# --- npad.exe ---
if (-not $NpadExe) { $NpadExe = Join-Path $repo "npad.exe" }
if (-not (Test-Path $NpadExe)) {
    throw "npad.exe not found at $NpadExe - build it first (make windows) or pass -NpadExe"
}
if ((Resolve-Path $NpadExe).Path -ne (Join-Path $repo "npad.exe")) {
    Copy-Item $NpadExe (Join-Path $repo "npad.exe") -Force
}
# Signed first, so every package below embeds a signed binary
Invoke-Signing (Join-Path $repo "npad.exe")

# --- Tools ---
$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue)?.Source }
if (-not $iscc) { throw "Inno Setup 6 (ISCC.exe) not found - winget install JRSoftware.InnoSetup" }
if (-not (Get-Command wix -ErrorAction SilentlyContinue)) {
    throw "WiX not found - dotnet tool install --global wix"
}

# --- Fonts (pinned downloads, idempotent) ---
& (Join-Path $installerDir "fetch-fonts.ps1")

# --- Inno Setup ---
Write-Host "Compiling Inno Setup installer..."
$isccArgs = @("/Q", "/DAppVersion=$Version")
if ($SignCmd) {
    # npad.iss enables SignTool=npadsign and SignedUninstaller=yes under
    # #ifdef Sign; the tool itself is defined here on the command line so the
    # script carries no machine-specific path. $f is Inno's file placeholder.
    # SignCmd is a .ps1 (CI writes one); Inno runs it through pwsh rather than
    # cmd.exe /c, because Inno substitutes $f as a QUOTED path and cmd's
    # quote-stripping rule then breaks a command with four quotes in it. pwsh
    # rather than powershell.exe: launched from a pwsh parent, Windows
    # PowerShell 5.1 inherits a PSModulePath it cannot load modules from.
    # Both failed here in dry runs. $q is Inno's escape for a quote.
    $isccArgs += "/DSign=1"
    $isccArgs += "/Snpadsign=pwsh.exe -NoProfile -File `$q$SignCmd`$q `$f"
}
& $iscc @isccArgs (Join-Path $installerDir "npad.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
$setupExe = Join-Path $dist "npad-v$Version-setup-win-x64.exe"
if (-not (Test-Path $setupExe)) { throw "Expected output missing: $setupExe" }
# Not signed here: with SignTool set, Inno signs the finished setup exe itself
# (as well as the uninstaller) - a second pass here just spent a signing call

# --- MSI ---
Write-Host "Building MSI..."
$msi = Join-Path $dist "npad-v$Version-msi-win-x64.msi"
# wix resolves the .wxs's relative Source paths against the current directory
Push-Location $installerDir
try {
    wix build npad.wxs -arch x64 -d "Version=$Version" -o $msi
    if ($LASTEXITCODE -ne 0) { throw "wix build failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
Invoke-Signing $msi

# --- Portable: the very same binary the installers carry ---
$portable = Join-Path $dist "npad-v$Version-portable-win-x64.exe"
Copy-Item (Join-Path $repo "npad.exe") $portable -Force

# --- Checksums (after signing: the signature is part of the bytes) ---
foreach ($f in @($setupExe, $msi, $portable)) {
    $hash = (Get-FileHash $f -Algorithm SHA256).Hash.ToLower()
    # Trailing newline matters: these get concatenated into CHECKSUMS.txt
    "$hash  $(Split-Path -Leaf $f)`n" | Set-Content -NoNewline "$f.sha256"
    Write-Host ("{0}  {1}  ({2:N0} bytes)" -f $hash, (Split-Path -Leaf $f), (Get-Item $f).Length)
}
Write-Host "Installers ready in $dist"
