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
    [int]$DelaySeconds = 2
)

if (-not (Test-Path $Exe)) { throw "npad not found at: $Exe  (pass -Exe <path>)" }

Add-Type @"
using System; using System.Runtime.InteropServices;
public class NP {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
}
"@

function Wait-Ready([int]$procId, [int]$timeoutMs = 30000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        $h = [IntPtr]::Zero
        for ($i = 0; $i -lt 64; $i++) {
            $h = [NP]::FindWindowExW([IntPtr]::Zero, $h, "NpadMainWindow", [NullString]::Value)
            if ($h -eq [IntPtr]::Zero) { break }
            $q = 0; [void][NP]::GetWindowThreadProcessId($h, [ref]$q)
            if ($q -eq $procId -and [NP]::IsWindowVisible($h)) {
                $o = [IntPtr]::Zero
                # SMTO_ABORTIFHUNG: only counts once the UI thread is pumping
                if ([NP]::SendMessageTimeoutW($h, 0, [IntPtr]0, [IntPtr]0, 0x0002, 5000, [ref]$o) -ne [IntPtr]::Zero) {
                    return $true
                }
            }
        }
        Start-Sleep -Milliseconds 2
    }
    return $false
}

"npad startup measurement"
"  exe   : $Exe"
"  runs  : $Runs (idle $DelaySeconds s between)"
"  NOTE  : leave the machine otherwise idle for clean numbers."
""
"{0,4}  {1,10}  {2,12}  {3,12}" -f "run", "total ms", "pre-create", "in-process"
"{0,4}  {1,10}  {2,12}  {3,12}" -f "----", "--------", "----------", "----------"

$rows = @()
for ($r = 1; $r -le $Runs; $r++) {
    Get-Process -Name npad -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds $DelaySeconds

    $t0 = Get-Date
    $p = Start-Process $Exe -PassThru
    $ok = Wait-Ready $p.Id
    $t1 = Get-Date

    $total = ($t1 - $t0).TotalMilliseconds
    $p.Refresh()
    # Everything from the kernel creating the process to the window being ready
    $inProc = ($t1 - $p.StartTime).TotalMilliseconds
    # The gap the Debug page can never show you
    $pre = $total - $inProc
    if (-not $ok) { $total = -1 }

    $rows += [pscustomobject]@{ Run=$r; Total=$total; Pre=$pre; In=$inProc }
    "{0,4}  {1,10:N1}  {2,12:N1}  {3,12:N1}" -f $r, $total, $pre, $inProc

    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}

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
