# Builds both npad Windows installers into dist/:
#   npad-v<version>-setup-win-x64.exe  (Inno Setup, interactive)
#   npad-v<version>-msi-win-x64.msi    (WiX, silent-install oriented)
# plus .sha256 files for each.
#
# Requirements (Windows): Inno Setup 6 (ISCC.exe), WiX (dotnet tool: wix),
# and a built npad.exe in the repo root (make windows, or pass -NpadExe).
# The bundled fonts are fetched (SHA256-pinned) by fetch-fonts.ps1.

param(
    [string]$NpadExe = "",     # Path to npad.exe; defaults to <repo>\npad.exe
    [string]$Version = ""      # Override; defaults to parsing src/main.h
)

$ErrorActionPreference = "Stop"
$installerDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $installerDir
$dist = Join-Path $repo "dist"
New-Item -ItemType Directory -Force $dist | Out-Null

# --- Version from src/main.h unless overridden ---
if (-not $Version) {
    $mainH = Get-Content (Join-Path $repo "src\main.h") -Raw
    $maj = [regex]::Match($mainH, '#define NPAD_VERSION_MAJOR (\d+)').Groups[1].Value
    $min = [regex]::Match($mainH, '#define NPAD_VERSION_MINOR (\d+)').Groups[1].Value
    $pat = [regex]::Match($mainH, '#define NPAD_VERSION_PATCH (\d+)').Groups[1].Value
    if (-not ($maj -and $min -and $pat)) { throw "Could not parse version from src/main.h" }
    $Version = "$maj.$min.$pat"
}
Write-Host "Building installers for npad $Version"

# --- npad.exe ---
if (-not $NpadExe) { $NpadExe = Join-Path $repo "npad.exe" }
if (-not (Test-Path $NpadExe)) {
    throw "npad.exe not found at $NpadExe - build it first (make windows) or pass -NpadExe"
}
if ((Resolve-Path $NpadExe).Path -ne (Join-Path $repo "npad.exe")) {
    Copy-Item $NpadExe (Join-Path $repo "npad.exe") -Force
}

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
& $iscc /Q "/DAppVersion=$Version" (Join-Path $installerDir "npad.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }
$setupExe = Join-Path $dist "npad-v$Version-setup-win-x64.exe"
if (-not (Test-Path $setupExe)) { throw "Expected output missing: $setupExe" }

# --- MSI ---
Write-Host "Building MSI..."
$msi = Join-Path $dist "npad-v$Version-msi-win-x64.msi"
# wix resolves the .wxs's relative Source paths against the current directory
Push-Location $installerDir
try {
    wix build npad.wxs -arch x64 -d "Version=$Version" -o $msi
    if ($LASTEXITCODE -ne 0) { throw "wix build failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }

# --- Checksums ---
foreach ($f in @($setupExe, $msi)) {
    $hash = (Get-FileHash $f -Algorithm SHA256).Hash.ToLower()
    # Trailing newline matters: these get concatenated into CHECKSUMS.txt
    "$hash  $(Split-Path -Leaf $f)`n" | Set-Content -NoNewline "$f.sha256"
    Write-Host ("{0}  {1}  ({2:N0} bytes)" -f $hash, (Split-Path -Leaf $f), (Get-Item $f).Length)
}
Write-Host "Installers ready in $dist"
