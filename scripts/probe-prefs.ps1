<#
.SYNOPSIS
  Capture npad's Preferences pages and Find / Replace dialogs without any real
  keyboard or mouse input.

.DESCRIPTION
  Starts its own npad (minimized, then shown without activation, so it never
  takes focus) against a throwaway profile, opens Preferences / Find / Replace
  by posting WM_COMMAND to that npad's own main window, walks the property
  sheet with PSM_SETCURSEL, captures each page with PrintWindow, and closes
  everything again: the sheet with PSM_PRESSBUTTON(PSBTN_CANCEL), the dialogs
  and npad with WM_CLOSE.

  It only ever looks at windows owned by the npad it started (by PID), never
  closes anything by name and never force-kills: if its npad does not exit,
  it is left running and reported. Your real settings.json and recovery
  folder are not touched (see probe-common.ps1).

.EXAMPLE
  .\probe-prefs.ps1 -Exe .\npad.exe -OutDir $env:TEMP\npad-shots -FindCmd 2003 -ReplaceCmd 2004
#>
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OutDir,
    [int]$PrefsCmd = 2007,
    [int]$FindCmd = 0,
    [int]$ReplaceCmd = 0,
    # A profile folder to reuse (kept afterwards); default is a fresh temp one
    [string]$ProfileDir
)

. (Join-Path $PSScriptRoot 'probe-common.ps1')

if (-not (Test-Path -LiteralPath $Exe)) { throw "npad not found at: $Exe" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$PSM_SETCURSEL = 0x0465; $PSM_GETCURRENTPAGEHWND = 0x0476; $PSM_PRESSBUTTON = 0x0471
# PSM_PRESSBUTTON's button index: 0 is BACK and 1 NEXT, which do nothing on a
# property sheet; CANCEL is 5 (OK 3, APPLYNOW 4)
$PSBTN_CANCEL = 5

$realSettings = Get-RealNpadSettingsPath
$realBefore = Get-ProbeFileHash $realSettings
$ownProfile = -not $ProfileDir
$probeProfile = New-ProbeProfile $ProfileDir
"profile: $probeProfile"

$probe = Start-ProbeProcess -FilePath $Exe -ProfileDir $probeProfile -WindowStyle Minimized
$main = Wait-ProbeWindow $probe 'NpadMainWindow' -TimeoutMs 30000
if (-not $main) {
    "no main window from PID $($probe.Id)"
    if (Close-ProbeGracefully $probe) { if ($ownProfile) { Remove-ProbeProfile $probeProfile } }
    exit 1
}
# Restore without activating: a minimized owner would hide the sheet too
[void][NpadProbe.Win32]::ShowWindow($main, $SW_SHOWNOACTIVATE)
Start-Sleep -Milliseconds 300

# Preferences sheet: walk every page, then Cancel (nothing is applied)
Post-ProbeMessage $main $WM_COMMAND $PrefsCmd 0
$sheet = Wait-ProbeWindow $probe '#32770' -TimeoutMs 5000
if (-not $sheet) { "no prefs sheet" } else {
    for ($i = 0; $i -lt 8; $i++) {
        $ok = Send-ProbeMessage $sheet $PSM_SETCURSEL $i 0
        if ($null -eq $ok -or $ok -eq [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 400
        $page = Send-ProbeMessage $sheet $PSM_GETCURRENTPAGEHWND 0 0
        $title = if ($page -and $page -ne [IntPtr]::Zero) { [NpadProbe.Win32]::Txt($page) } else { "page$i" }
        Save-ProbeSnapshot $sheet $OutDir ("prefs-$i-" + ($title -replace '[^A-Za-z]', ''))
    }
    [void](Send-ProbeMessage $sheet $PSM_PRESSBUTTON $PSBTN_CANCEL 0)
    if (-not (Wait-ProbeWindowGone $sheet)) { "prefs sheet did not close" }
}

foreach ($pair in @(@($FindCmd, 'find'), @($ReplaceCmd, 'replace'))) {
    if ($pair[0] -eq 0) { continue }
    Post-ProbeMessage $main $WM_COMMAND $pair[0] 0
    $dlg = Wait-ProbeWindow $probe '#32770' -TimeoutMs 5000
    if ($dlg) {
        Save-ProbeSnapshot $dlg $OutDir $pair[1]
        Post-ProbeMessage $dlg $WM_CLOSE
        if (-not (Wait-ProbeWindowGone $dlg)) { "$($pair[1]) dialog did not close" }
    } else { "no $($pair[1]) dialog" }
}

$exited = Close-ProbeGracefully $probe
$realAfter = Get-ProbeFileHash $realSettings
if ($realBefore -ne $realAfter) {
    Write-Warning "Your real settings.json ($realSettings) changed during the probe - by this npad or by another one you have open."
}
if ($exited -and -not (Test-Path -LiteralPath (Join-Path $probeProfile 'AppData\Roaming\Platima\npad\settings.json'))) {
    Write-Warning "The probe's npad wrote no settings.json in its private profile; it may not have been isolated."
}
if ($exited -and $ownProfile) { Remove-ProbeProfile $probeProfile }
"done"
