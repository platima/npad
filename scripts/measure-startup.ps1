<#
.SYNOPSIS
  Measure npad's real launch time, including the part its own Debug page cannot see.

.DESCRIPTION
  npad's built-in startup profile begins at the process-creation timestamp, so
  it cannot account for anything the system does BEFORE the process object
  exists: the shell resolving the launch, a SmartScreen reputation check (which
  can make a network call for an unsigned binary), and anti-malware scanning the
  image while its section is created.

  This script brackets the whole thing. It records a timestamp before calling
  CreateProcess and stops when npad's window is up and pumping messages, then
  subtracts the process's own reported lifetime to show the split:

      total (what you feel)  =  pre-creation (shell / SmartScreen / AV)
                                + in-process (npad's own startup)

  A large "pre-creation" number means the delay is not npad's code.

  Each run's npad is the script's own: it is found by its PID, closed with
  WM_CLOSE to its own window, and runs against a throwaway profile (so your
  settings.json, window position and crash-recovery folder are untouched;
  see probe-common.ps1). The script refuses to start while npad is already
  open - it never closes your windows for you - and never force-kills: if a
  run's npad does not exit, the measurement stops and that PID is reported.

.EXAMPLE
  .\measure-startup.ps1 -Runs 15
  .\measure-startup.ps1 -Exe "C:\Users\me\AppData\Local\Programs\npad\npad.exe" -Runs 30 -DelaySeconds 20
#>
[CmdletBinding()]
param(
    [string]$Exe = "$env:LOCALAPPDATA\Programs\npad\npad.exe",
    [int]$Runs = 15,
    # Idle gap between runs. Raise this (e.g. 60+) to let caches and
    # reputation results go cold, which is when slow launches happen.
    [int]$DelaySeconds = 2,
    # Measure even though npad is already open. Your windows are still left
    # alone, but a running copy keeps the image warm, so the numbers are not
    # cold starts.
    [switch]$AllowRunning,
    # A profile folder to reuse (kept afterwards); default is a fresh temp one
    [string]$ProfileDir
)

. (Join-Path $PSScriptRoot 'probe-common.ps1')

if (-not (Test-Path -LiteralPath $Exe)) { throw "npad not found at: $Exe  (pass -Exe <path>)" }

# Read-only check, only to refuse: nothing here is ever closed by name
$stem = [IO.Path]::GetFileNameWithoutExtension($Exe)
$already = @(Get-Process -Name $stem -ErrorAction SilentlyContinue)
if ($already.Count -gt 0 -and -not $AllowRunning) {
    throw ("$stem is already running (PID " + (($already | ForEach-Object { $_.Id }) -join ', ') +
        "). This script never closes it for you: save your work and close it yourself, or pass -AllowRunning to measure anyway (warm, not cold, starts).")
}

function Wait-Ready($probe, [int]$timeoutMs = 30000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        # Only this run's own process: no window of any other npad counts
        foreach ($h in [NpadProbe.Win32]::TopsOf(@([int]$probe.Id))) {
            if ([NpadProbe.Win32]::Cls($h) -ne 'NpadMainWindow') { continue }
            # SMTO_ABORTIFHUNG: only counts once the UI thread is pumping
            if ($null -ne (Send-ProbeMessage $h 0 0 0 5000)) { return $true }
        }
        if ($probe.Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 2
    }
    return $false
}

$realSettings = Get-RealNpadSettingsPath
$realBefore = Get-ProbeFileHash $realSettings
$ownProfile = -not $ProfileDir
$probeProfile = New-ProbeProfile $ProfileDir

"npad startup measurement"
"  exe     : $Exe"
"  runs    : $Runs (idle $DelaySeconds s between)"
"  profile : $probeProfile (private; your settings are not used)"
"  NOTE    : leave the machine otherwise idle for clean numbers."
""
"{0,4}  {1,10}  {2,12}  {3,12}" -f "run", "total ms", "pre-create", "in-process"
"{0,4}  {1,10}  {2,12}  {3,12}" -f "----", "--------", "----------", "----------"

$rows = @()
$allClosed = $true
for ($r = 1; $r -le $Runs; $r++) {
    Start-Sleep -Seconds $DelaySeconds

    $t0 = Get-Date
    $probe = Start-ProbeProcess -FilePath $Exe -ProfileDir $probeProfile
    $ok = Wait-Ready $probe
    $t1 = Get-Date

    $total = ($t1 - $t0).TotalMilliseconds
    # Everything from the kernel creating the process to the window being ready
    $inProc = ($t1 - $probe.StartTime).TotalMilliseconds
    # The gap the Debug page can never show you
    $pre = $total - $inProc
    if (-not $ok) { $total = -1 }

    $rows += [pscustomobject]@{ Run=$r; Total=$total; Pre=$pre; In=$inProc }
    "{0,4}  {1,10:N1}  {2,12:N1}  {3,12:N1}" -f $r, $total, $pre, $inProc

    # A graceful close of this run's own window; a run that does not exit
    # ends the measurement rather than stacking up npads (and is left alone)
    if (-not (Close-ProbeGracefully $probe)) {
        $allClosed = $false
        "stopping after run ${r}: its npad did not exit"
        break
    }
}

$realAfter = Get-ProbeFileHash $realSettings
if ($realBefore -ne $realAfter) {
    Write-Warning "Your real settings.json ($realSettings) changed during the measurement - by a probe npad or by another one you have open."
}
if ($allClosed -and $ownProfile) { Remove-ProbeProfile $probeProfile }

$good = $rows | Where-Object { $_.Total -ge 0 }
if (-not $good) { "no successful runs"; return }
$sorted = $good.Total | Sort-Object
""
"summary over $($good.Count) runs"
"  total      min {0,8:N1}   median {1,8:N1}   max {2,8:N1} ms" -f $sorted[0], $sorted[[int]($sorted.Count/2)], $sorted[-1]
"  pre-create min {0,8:N1}   median {1,8:N1}   max {2,8:N1} ms" -f ($good.Pre | Measure-Object -Minimum).Minimum, (($good.Pre | Sort-Object)[[int]($good.Count/2)]), ($good.Pre | Measure-Object -Maximum).Maximum
"  in-process min {0,8:N1}   median {1,8:N1}   max {2,8:N1} ms" -f ($good.In | Measure-Object -Minimum).Minimum, (($good.In | Sort-Object)[[int]($good.Count/2)]), ($good.In | Measure-Object -Maximum).Maximum
""
$worst = $good | Sort-Object Total -Descending | Select-Object -First 1
if ($worst.Total -gt 400) {
    if ($worst.Pre -gt ($worst.Total * 0.5)) {
        "VERDICT: the slow launches are mostly BEFORE the process exists"
        "  ({0:N0} ms of the worst {1:N0} ms run). That is the shell, SmartScreen" -f $worst.Pre, $worst.Total
        "  reputation checking, or anti-malware scanning the image - not npad's code."
        "  Code signing is the fix that actually removes it."
    } else {
        "VERDICT: the slow launches are INSIDE npad ({0:N0} ms of {1:N0} ms)." -f $worst.In, $worst.Total
        "  Open npad, press Ctrl+Shift+. and send the startup profile."
    }
} else {
    "VERDICT: no slow launch reproduced in this sample. Try -DelaySeconds 120"
    "  (or run it after not using npad for a while) - the slow case is a cold one."
}
