<#
.SYNOPSIS
  Shared helpers for npad's window-message probes: probe-prefs.ps1,
  probe-installer.ps1 and measure-startup.ps1. Dot-source it:
      . (Join-Path $PSScriptRoot 'probe-common.ps1')

.DESCRIPTION
  These scripts run on desktops where other work is open, so the helpers
  enforce three rules rather than leaving them to each script:

  - A probe touches only the process it started and that process's own
    children, tracked by PID. Nothing is ever found, closed or stopped by
    process name, window class or title alone: every window lookup is limited
    to the probe's PIDs, and there is deliberately no "all processes" mode.
  - A probe closes what it started gracefully (WM_CLOSE to the windows its
    PIDs own) and never force-kills. A process that does not exit in time is
    left running and reported with its PID, for the user to deal with.
  - npad runs against a throwaway profile, never the real settings.json or
    crash-recovery folder. npad finds its settings through
    SHGetFolderPath(CSIDL_APPDATA), which follows USERPROFILE (APPDATA= alone
    does not move it) unless the user's AppData is redirected; that is
    checked before anything starts, and the real settings.json's hash is
    compared before and after as a tripwire.
#>

if (-not ('NpadProbe.Win32' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
namespace NpadProbe {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static class Win32 {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint timeoutMs, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);

    public static int OwnerPid(IntPtr h) { uint p; GetWindowThreadProcessId(h, out p); return (int) p; }

    // Visible top-level windows owned by one of these processes. An empty or
    // null list matches nothing - there is no way to ask for every window.
    public static List<IntPtr> TopsOf(int[] pids) {
      var r = new List<IntPtr>();
      if (pids == null || pids.Length == 0) return r;
      EnumWindows((h, l) => {
        if (IsWindowVisible(h) && Array.IndexOf(pids, OwnerPid(h)) >= 0) r.Add(h);
        return true;
      }, IntPtr.Zero);
      return r;
    }
    public static List<IntPtr> Kids(IntPtr h) {
      var r = new List<IntPtr>();
      EnumChildWindows(h, (c, l) => { r.Add(c); return true; }, IntPtr.Zero);
      return r;
    }
    public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
    public static string Txt(IntPtr h) { var s = new StringBuilder(1024); GetWindowText(h, s, 1024); return s.ToString(); }
  }
}
"@
}

$script:WM_CLOSE = 0x0010
$script:WM_COMMAND = 0x0111
$script:BM_CLICK = 0x00F5
$script:SW_SHOWNOACTIVATE = 4

# SendMessage with a bound: SMTO_ABORTIFHUNG, and a timeout so that a message
# whose handler opens a modal loop (a click that raises a message box) cannot
# block the probe forever. Returns $null when it timed out or failed.
function Send-ProbeMessage([IntPtr]$Hwnd, [uint32]$Msg, [long]$WParam = 0, [long]$LParam = 0, [uint32]$TimeoutMs = 5000) {
    $out = [IntPtr]::Zero
    $ok = [NpadProbe.Win32]::SendMessageTimeout($Hwnd, $Msg, [IntPtr]$WParam, [IntPtr]$LParam, 0x0002, $TimeoutMs, [ref]$out)
    if ($ok -eq [IntPtr]::Zero) { return $null }
    return $out
}

function Post-ProbeMessage([IntPtr]$Hwnd, [uint32]$Msg, [long]$WParam = 0, [long]$LParam = 0) {
    [void][NpadProbe.Win32]::PostMessage($Hwnd, $Msg, [IntPtr]$WParam, [IntPtr]$LParam)
}

# Refuse to isolate by USERPROFILE when it would not work: a redirected AppData
# (folder redirection, a roaming profile on a share) is an absolute path that
# does not follow USERPROFILE, so npad would read and write the real settings.
function Assert-ProbeProfileIsolatable {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders'
    $raw = $null
    try {
        # Read-only, and unexpanded so the %USERPROFILE% prefix is visible
        $raw = (Get-Item -LiteralPath $key -ErrorAction Stop).GetValue('AppData', $null, 'DoNotExpandEnvironmentNames')
    } catch {
        $raw = $null
    }
    if ($raw -and ($raw -notlike '%USERPROFILE%*')) {
        throw "AppData is redirected to '$raw', so a private USERPROFILE would not keep npad off your real settings. Not starting npad."
    }
}

# A fresh profile root for a probe's npad, with the folders npad expects
# (it exits with "Failed to initialize settings" if the settings folder's
# parents are missing). Returns its path.
function New-ProbeProfile([string]$Path) {
    Assert-ProbeProfileIsolatable
    if (-not $Path) {
        $Path = Join-Path ([IO.Path]::GetTempPath()) ('npad-probe-' + [guid]::NewGuid().ToString('N').Substring(0, 12))
    }
    foreach ($d in @('AppData\Roaming\Platima\npad', 'AppData\Local')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Path $d) | Out-Null
    }
    return (Resolve-Path -LiteralPath $Path).ProviderPath
}

# Remove a profile New-ProbeProfile made itself (a temp folder named
# npad-probe-*), never a folder the caller passed in.
function Remove-ProbeProfile([string]$Path) {
    if (-not $Path) { return }
    $tmp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith($tmp, [StringComparison]::OrdinalIgnoreCase) -and ((Split-Path -Leaf $full) -like 'npad-probe-*')) {
        Remove-Item -LiteralPath $full -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# The real settings.json, resolved in this (unmodified) process
function Get-RealNpadSettingsPath {
    Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'Platima\npad\settings.json'
}

function Get-ProbeFileHash([string]$Path) {
    if (Test-Path -LiteralPath $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
    return $null
}

# Start a process the probe then owns. With -ProfileDir, the child gets that
# folder as USERPROFILE (plus matching APPDATA / LOCALAPPDATA); this process's
# own environment is restored straight afterwards. Returns a probe record:
# Process, Id, StartTime, ProfileDir.
function Start-ProbeProcess {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList,
        [string]$ProfileDir,
        [ValidateSet('Normal', 'Minimized', 'Maximized', 'Hidden')][string]$WindowStyle = 'Normal'
    )
    $vars = @{}
    if ($ProfileDir) {
        $vars['USERPROFILE'] = $ProfileDir
        $vars['APPDATA'] = Join-Path $ProfileDir 'AppData\Roaming'
        $vars['LOCALAPPDATA'] = Join-Path $ProfileDir 'AppData\Local'
    }
    $saved = @{}
    foreach ($k in $vars.Keys) {
        $saved[$k] = [Environment]::GetEnvironmentVariable($k)
        [Environment]::SetEnvironmentVariable($k, $vars[$k])
    }
    try {
        $sp = @{ FilePath = $FilePath; PassThru = $true; WindowStyle = $WindowStyle }
        if ($ArgumentList) { $sp['ArgumentList'] = $ArgumentList }
        $p = Start-Process @sp
    } finally {
        foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) }
    }
    $null = $p.Handle # Hold a handle: ExitCode and StartTime stay readable after it exits
    return [pscustomobject]@{ Process = $p; Id = $p.Id; StartTime = $p.StartTime; ProfileDir = $ProfileDir }
}

# The probe's root process plus every process it (transitively) started. The
# Win32_Process query is read-only; the creation-time filter stops a recycled
# parent PID from adopting an unrelated, older process.
function Get-ProbeProcessIds($Probe) {
    $ids = [System.Collections.Generic.List[int]]::new()
    $ids.Add([int]$Probe.Id)
    $all = @(Get-CimInstance -ClassName Win32_Process -Property ProcessId, ParentProcessId, CreationDate -ErrorAction SilentlyContinue)
    $grew = $true
    while ($grew) {
        $grew = $false
        foreach ($p in $all) {
            $id = [int]$p.ProcessId
            if ($ids.Contains($id)) { continue }
            if ($ids.Contains([int]$p.ParentProcessId) -and $p.CreationDate -and $p.CreationDate -ge $Probe.StartTime.AddSeconds(-1)) {
                $ids.Add($id)
                $grew = $true
            }
        }
    }
    return , $ids.ToArray()
}

# Visible top-level windows owned by the probe's processes, optionally of one
# window class and with a title matching a -like pattern
function Get-ProbeWindows($Probe, [string]$Class, [string]$TitleLike = '*') {
    $ids = Get-ProbeProcessIds $Probe
    [NpadProbe.Win32]::TopsOf($ids) | Where-Object {
        ((-not $Class) -or ([NpadProbe.Win32]::Cls($_) -eq $Class)) -and ([NpadProbe.Win32]::Txt($_) -like $TitleLike)
    }
}

# Poll for one of the probe's windows; $null if none appeared in time
function Wait-ProbeWindow($Probe, [string]$Class, [string]$TitleLike = '*', [int]$TimeoutMs = 15000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    do {
        $h = Get-ProbeWindows $Probe $Class $TitleLike | Select-Object -First 1
        if ($h) { return $h }
        if ($Probe.Process.HasExited) { return $null }
        Start-Sleep -Milliseconds 100
    } while ($sw.ElapsedMilliseconds -lt $TimeoutMs)
    return $null
}

# Poll until a window is gone (closed or hidden); $true if it went
function Wait-ProbeWindowGone([IntPtr]$Hwnd, [int]$TimeoutMs = 5000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if (-not [NpadProbe.Win32]::IsWindow($Hwnd) -or -not [NpadProbe.Win32]::IsWindowVisible($Hwnd)) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

# Close the probe's processes gracefully: WM_CLOSE to every visible top-level
# window they own, then wait. Never force-kills: whatever is still running
# afterwards is reported with its PID and left alone. A save prompt answered
# by WM_CLOSE is a Cancel, so this can never discard anything. $true when
# every probe process has exited.
function Close-ProbeGracefully($Probe, [int]$TimeoutMs = 10000) {
    $ids = Get-ProbeProcessIds $Probe
    foreach ($h in [NpadProbe.Win32]::TopsOf($ids)) { Post-ProbeMessage $h $script:WM_CLOSE }
    [void]$Probe.Process.WaitForExit($TimeoutMs)
    $left = @()
    foreach ($id in $ids) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($p -and -not $p.HasExited) { $left += "PID $id ($($p.ProcessName))" }
    }
    if ($left.Count -eq 0) { return $true }
    Write-Warning ("Still running after a graceful close, and left alone: " + ($left -join ', ') + ".")
    Write-Warning "Close its window yourself, or end that exact PID in Task Manager once you are sure it holds nothing you need."
    return $false
}

# PrintWindow capture of one window into <OutDir>\<Name>.png
function Save-ProbeSnapshot([IntPtr]$Hwnd, [string]$OutDir, [string]$Name) {
    Add-Type -AssemblyName System.Drawing
    $r = New-Object NpadProbe.RECT
    [void][NpadProbe.Win32]::GetWindowRect($Hwnd, [ref]$r)
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($w -le 0 -or $h -le 0) { "skipped $Name (window has no size)"; return }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][NpadProbe.Win32]::PrintWindow($Hwnd, $dc, 2) # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($dc); $g.Dispose()
    $bmp.Save((Join-Path $OutDir "$Name.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "captured $Name (${w}x${h})"
}
