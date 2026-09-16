# Drives the npad setup wizard with window messages only (BM_CLICK / WM_CLOSE),
# captures the Components and Tasks pages with PrintWindow, dumps the geometry of
# every control on those pages, then exits Setup before anything is installed.
param([string]$Exe, [string]$OutDir, [string]$SetupArgs = '')

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public static List<IntPtr> Tops(uint pid) {
    var r = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if ((pid == 0 || p == pid) && IsWindowVisible(h)) r.Add(h); return true; }, IntPtr.Zero);
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
"@

$BM_CLICK = 0x00F5; $WM_CLOSE = 0x0010
$proc = if ($SetupArgs) { Start-Process -FilePath $Exe -ArgumentList $SetupArgs -PassThru } else { Start-Process -FilePath $Exe -PassThru }
Start-Sleep -Seconds 3

function Wizard { [W]::Tops(0) | Where-Object { ([W]::Cls($_)) -eq 'TWizardForm' } | Select-Object -First 1 }
function Find-Child($top, $cls, $txt) { [W]::Kids($top) | Where-Object { ([W]::Cls($_)) -like $cls -and ([W]::Txt($_)) -like $txt } | Select-Object -First 1 }
function Header($top) { ([W]::Kids($top) | Where-Object { ([W]::Cls($_)) -eq 'TNewStaticText' } | ForEach-Object { [W]::Txt($_) } | Where-Object { $_ -match 'Select|License|Welcome|Ready|Destination|Additional' } | Select-Object -First 1) }
function Snap($top, $name) {
  $r = New-Object W+RECT; [void][W]::GetWindowRect($top, [ref]$r)
  $bmp = New-Object System.Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [void][W]::PrintWindow($top, $dc, 2)  # PW_RENDERFULLCONTENT
  $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save((Join-Path $OutDir "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Dump($top, $name) {
  $cr = New-Object W+RECT; [void][W]::GetClientRect($top, [ref]$cr)
  $o = New-Object W+POINT; [void][W]::ClientToScreen($top, [ref]$o)
  "== $name : wizard client ${($cr.R)}x${($cr.B)} at screen ($($o.X),$($o.Y))"
  foreach ($k in [W]::Kids($top)) {
    if (-not [W]::IsWindowVisible($k)) { continue }
    $r = New-Object W+RECT; [void][W]::GetWindowRect($k, [ref]$r)
    $c = [W]::Cls($k); $t = ([W]::Txt($k)) -replace "`r|`n", ' '
    if ($c -match 'CheckList|Static|Button|Combo|Panel|Notebook|Page' ) {
      "  {0,-22} x={1,5} y={2,5} w={3,5} h={4,5}  {5}" -f $c, ($r.L - $o.X), ($r.T - $o.Y), ($r.R - $r.L), ($r.B - $r.T), $t.Substring(0, [Math]::Min(60, $t.Length))
    }
  }
}

$top = Wizard
if (-not $top) { "no wizard window"; $proc | Stop-Process -Force; exit 1 }
$captured = @{}
for ($i = 0; $i -lt 8; $i++) {
  $top = Wizard
  $h = Header $top
  "page: $h"
  if ($h -like 'Select Components*' -and -not $captured['components']) { Snap $top 'components'; Dump $top 'components'; $captured['components'] = $true }
  if ($h -like 'Select Additional Tasks*' -and -not $captured['tasks']) { Snap $top 'tasks'; Dump $top 'tasks'; $captured['tasks'] = $true; break }
  $accept = Find-Child $top 'TNewRadioButton' 'I &accept*'
  if ($accept) { [void][W]::SendMessage($accept, $BM_CLICK, 0, 0); Start-Sleep -Milliseconds 300 }
  $next = Find-Child $top 'TNewButton' '&Next*'
  if (-not $next -or -not [W]::IsWindowEnabled($next)) { "Next not available on '$h'"; break }
  [void][W]::SendMessage($next, $BM_CLICK, 0, 0)
  Start-Sleep -Milliseconds 700
}
# Leave without installing: WM_CLOSE brings up "Exit Setup?", answer Yes
[void][W]::PostMessage((Wizard), $WM_CLOSE, 0, 0); Start-Sleep -Milliseconds 800
$box = [W]::Tops(0) | Where-Object { ([W]::Cls($_)) -eq '#32770' -and ([W]::Txt($_)) -like 'Exit Setup*' } | Select-Object -First 1
if ($box) { $yes = Find-Child $box 'Button' '&Yes*'; if ($yes) { [void][W]::SendMessage($yes, $BM_CLICK, 0, 0) } }
Start-Sleep -Seconds 2
# The bootstrapper spawns <name>.tmp for the wizard; make sure both are gone
$stem = [IO.Path]::GetFileNameWithoutExtension($Exe)
Get-Process -Name "$stem.tmp", $stem -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
"done"
