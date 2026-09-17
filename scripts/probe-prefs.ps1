# Opens npad's Preferences sheet and Find/Replace dialogs by window message,
# captures each page with PrintWindow, then closes everything. No real input.
param([string]$Exe, [string]$OutDir, [int]$PrefsCmd = 2007, [int]$FindCmd = 0, [int]$ReplaceCmd = 0)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static List<IntPtr> Tops(uint pid) {
    var r = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) r.Add(h); return true; }, IntPtr.Zero);
    return r;
  }
  public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
  public static string Txt(IntPtr h) { var s = new StringBuilder(1024); GetWindowText(h, s, 1024); return s.ToString(); }
}
"@
$WM_COMMAND = 0x0111; $WM_CLOSE = 0x0010
$PSM_SETCURSEL = 0x0465; $PSM_GETCURRENTPAGEHWND = 0x0476; $PSM_PRESSBUTTON = 0x0471

function Snap($h, $name) {
  $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
  $bmp = New-Object System.Drawing.Bitmap(($r.R - $r.L), ($r.B - $r.T))
  $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
  [void][W]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
  $bmp.Save((Join-Path $OutDir "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  "captured $name ($($r.R - $r.L)x$($r.B - $r.T))"
}
function TopOf($procId, $cls, $titleLike) { [W]::Tops($procId) | Where-Object { ([W]::Cls($_)) -eq $cls -and ([W]::Txt($_)) -like $titleLike } | Select-Object -First 1 }

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 2
$main = TopOf $proc.Id 'NpadMainWindow' '*'
if (-not $main) { "no main window"; $proc | Stop-Process -Force; exit 1 }

# Preferences sheet: walk every page
[void][W]::PostMessage($main, $WM_COMMAND, $PrefsCmd, 0); Start-Sleep -Milliseconds 1500
$sheet = TopOf $proc.Id '#32770' '*'
if (-not $sheet) { "no prefs sheet" } else {
  for ($i = 0; $i -lt 8; $i++) {
    $ok = [W]::SendMessage($sheet, $PSM_SETCURSEL, $i, 0)
    if ($ok -eq 0) { break }
    Start-Sleep -Milliseconds 400
    $page = [W]::SendMessage($sheet, $PSM_GETCURRENTPAGEHWND, 0, 0)
    $title = if ($page -ne 0) { [W]::Txt($page) } else { "page$i" }
    Snap $sheet ("prefs-$i-" + ($title -replace '[^A-Za-z]', ''))
  }
  [void][W]::SendMessage($sheet, $PSM_PRESSBUTTON, 0, 0)  # PSBTN_CANCEL
  Start-Sleep -Milliseconds 500
}

foreach ($pair in @(@($FindCmd, 'find'), @($ReplaceCmd, 'replace'))) {
  if ($pair[0] -eq 0) { continue }
  [void][W]::PostMessage($main, $WM_COMMAND, $pair[0], 0); Start-Sleep -Milliseconds 1000
  $dlg = TopOf $proc.Id '#32770' '*'
  if ($dlg) { Snap $dlg $pair[1]; [void][W]::PostMessage($dlg, $WM_CLOSE, 0, 0); Start-Sleep -Milliseconds 500 } else { "no $($pair[1]) dialog" }
}

[void][W]::PostMessage($main, $WM_CLOSE, 0, 0); Start-Sleep -Seconds 2
if (-not $proc.HasExited) { "npad still running - killing"; $proc | Stop-Process -Force }
"done"
