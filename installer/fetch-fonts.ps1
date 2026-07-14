# Downloads the fonts bundled by the npad installers into installer/fonts/
# (gitignored). Every download is pinned by SHA256; a hash mismatch aborts.
# Idempotent: already-extracted families are skipped.
#
# Families (all SIL OFL 1.1 licensed, license shipped alongside):
#   Intel One Mono  - intel/intel-one-mono V1.4.0
#   Roboto          - googlefonts/roboto-3-classic v3.016 (static weights)
#   OpenDyslexic    - antijingoist/opendyslexic v0.91.12

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$fontsDir = Join-Path $root "fonts"
$cacheDir = Join-Path $fontsDir ".cache"
New-Item -ItemType Directory -Force $fontsDir, $cacheDir | Out-Null

Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Pinned {
    param([string]$Url, [string]$Sha256, [string]$OutFile)
    if (Test-Path $OutFile) {
        if ((Get-FileHash $OutFile -Algorithm SHA256).Hash -eq $Sha256) { return }
        Remove-Item $OutFile -Force
    }
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -Headers @{ "User-Agent" = "npad-build" }
    $actual = (Get-FileHash $OutFile -Algorithm SHA256).Hash
    if ($actual -ne $Sha256) {
        Remove-Item $OutFile -Force
        throw "SHA256 mismatch for ${Url}: expected $Sha256, got $actual"
    }
}

function Extract-Entries {
    param([string]$Zip, [hashtable]$EntryMap, [string]$DestDir)
    New-Item -ItemType Directory -Force $DestDir | Out-Null
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entryPath in $EntryMap.Keys) {
            $entry = $archive.Entries | Where-Object { $_.FullName -eq $entryPath } | Select-Object -First 1
            if (-not $entry) { throw "Entry '$entryPath' not found in $Zip" }
            $dest = Join-Path $DestDir $EntryMap[$entryPath]
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dest, $true)
        }
    } finally { $archive.Dispose() }
}

$families = @(
    @{
        Name   = "IntelOneMono"
        Zip    = @{ Url = "https://github.com/intel/intel-one-mono/releases/download/V1.4.0/ttf.zip"
                    Sha256 = "54863552D25DCB9C3F5360B296FC980D6E1FBFD02E0D214224E8B78F0A2BCCF0" }
        Files  = @{
            "ttf/IntelOneMono-Regular.ttf"    = "IntelOneMono-Regular.ttf"
            "ttf/IntelOneMono-Bold.ttf"       = "IntelOneMono-Bold.ttf"
            "ttf/IntelOneMono-Italic.ttf"     = "IntelOneMono-Italic.ttf"
            "ttf/IntelOneMono-BoldItalic.ttf" = "IntelOneMono-BoldItalic.ttf"
            "ttf/OFL.txt"                     = "OFL.txt"
        }
    },
    @{
        Name   = "Roboto"
        Zip    = @{ Url = "https://github.com/googlefonts/roboto-3-classic/releases/download/v3.016/Roboto_v3.016.zip"
                    Sha256 = "1653DBE12F248DA8FB0B9920DB7B9496CD677ED3981154F6F15285C8BD4E334F" }
        Files  = @{
            "android/static/Roboto-Regular.ttf"    = "Roboto-Regular.ttf"
            "android/static/Roboto-Bold.ttf"       = "Roboto-Bold.ttf"
            "android/static/Roboto-Italic.ttf"     = "Roboto-Italic.ttf"
            "android/static/Roboto-BoldItalic.ttf" = "Roboto-BoldItalic.ttf"
        }
        # The release zip carries no license file; take it from the repo at the same tag
        License = @{ Url = "https://raw.githubusercontent.com/googlefonts/roboto-3-classic/v3.016/OFL.txt"
                     Sha256 = "061402327A96AADB0BFB694A960ED289ECD38D383E396243831AB81FEB109C41" }
    },
    @{
        Name   = "OpenDyslexic"
        Zip    = @{ Url = "https://github.com/antijingoist/opendyslexic/releases/download/v0.91.12/opendyslexic-0.910.12-rc2-2019.10.17.zip"
                    Sha256 = "B92D7FCB9409F2BCFD23B65AC71647256EB49C429F4FBB1CC870381FC93EB486" }
        Files  = @{
            "OpenDyslexic-Regular.otf"     = "OpenDyslexic-Regular.otf"
            "OpenDyslexic-Bold.otf"        = "OpenDyslexic-Bold.otf"
            "OpenDyslexic-Italic.otf"      = "OpenDyslexic-Italic.otf"
            "OpenDyslexic-Bold-Italic.otf" = "OpenDyslexic-BoldItalic.otf"
        }
        License = @{ Url = "https://raw.githubusercontent.com/antijingoist/opendyslexic/v0.91.12/OFL.txt"
                     Sha256 = "CAAFCCCFB70FC72458FCBDA812EC8F0A06CB300CBABB87EABBB30B946124394B" }
    }
)

foreach ($fam in $families) {
    $destDir = Join-Path $fontsDir $fam.Name
    $expected = @($fam.Files.Values)
    if ($fam.License) { $expected += "OFL.txt" }
    $missing = $expected | Where-Object { -not (Test-Path (Join-Path $destDir $_)) }
    if (-not $missing) {
        Write-Host "$($fam.Name): already present, skipping"
        continue
    }

    $zipPath = Join-Path $cacheDir ("{0}.zip" -f $fam.Name)
    Get-Pinned -Url $fam.Zip.Url -Sha256 $fam.Zip.Sha256 -OutFile $zipPath
    Extract-Entries -Zip $zipPath -EntryMap $fam.Files -DestDir $destDir

    if ($fam.License) {
        Get-Pinned -Url $fam.License.Url -Sha256 $fam.License.Sha256 -OutFile (Join-Path $destDir "OFL.txt")
    }
    Write-Host "$($fam.Name): fetched to $destDir"
}

Write-Host "Fonts ready under $fontsDir"
