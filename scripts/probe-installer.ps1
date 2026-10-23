<#
.SYNOPSIS
  Walk npad's own setup wizard with window messages and capture its
  Components and Tasks pages, then leave Setup before anything is installed.

.DESCRIPTION
  Starts the given npad setup exe itself and only ever touches windows owned by
  that process or the processes it started (Inno's bootstrapper runs the
  wizard as a child, <name>.tmp), tracked by PID - never another installer
  that happens to be open. It clicks "I accept" / Next with BM_CLICK,
  captures the pages with PrintWindow, dumps the geometry of every control on
  them, then leaves through WM_CLOSE and the "Exit Setup?" box's Yes (exit
  code 2: nothing installed). Nothing is ever closed by name or force-killed:
  if Setup does not exit, it is left running and reported.

.EXAMPLE
  .\probe-installer.ps1 -Exe dist\npad-v0.36.0-setup-win-x64.exe -OutDir $env:TEMP\npad-setup-shots -SetupArgs '/CURRENTUSER'
#>
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$OutDir,
    [string]$SetupArgs = ''
)

. (Join-Path $PSScriptRoot 'probe-common.ps1')

if (-not (Test-Path -LiteralPath $Exe)) { throw "setup not found at: $Exe" }
if ([IO.Path]::GetExtension($Exe) -ne '.exe') { throw "expected an Inno Setup .exe, not: $Exe" }
# The probe walks the wizard and leaves before the Ready page; a silent setup
# has no wizard and would simply install
if ($SetupArgs -match '(?i)(^|\s)/(VERY)?SILENT\b') { throw "refusing -SetupArgs with /SILENT or /VERYSILENT: that installs without a wizard to probe" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Find-Child($top, $cls, $txt) {
    [NpadProbe.Win32]::Kids($top) | Where-Object { ([NpadProbe.Win32]::Cls($_)) -like $cls -and ([NpadProbe.Win32]::Txt($_)) -like $txt } | Select-Object -First 1
}
function Header($top) {
    [NpadProbe.Win32]::Kids($top) | Where-Object { ([NpadProbe.Win32]::Cls($_)) -eq 'TNewStaticText' } |
        ForEach-Object { [NpadProbe.Win32]::Txt($_) } |
        Where-Object { $_ -match 'Select|License|Welcome|Ready|Destination|Additional' } | Select-Object -First 1
}
function Dump($top, $name) {
    $cr = New-Object NpadProbe.RECT; [void][NpadProbe.Win32]::GetClientRect($top, [ref]$cr)
    $o = New-Object NpadProbe.POINT; [void][NpadProbe.Win32]::ClientToScreen($top, [ref]$o)
    "== $name : wizard client $($cr.R)x$($cr.B) at screen ($($o.X),$($o.Y))"
    foreach ($k in [NpadProbe.Win32]::Kids($top)) {
        if (-not [NpadProbe.Win32]::IsWindowVisible($k)) { continue }
        $r = New-Object NpadProbe.RECT; [void][NpadProbe.Win32]::GetWindowRect($k, [ref]$r)
        $c = [NpadProbe.Win32]::Cls($k); $t = ([NpadProbe.Win32]::Txt($k)) -replace "`r|`n", ' '
        if ($c -match 'CheckList|Static|Button|Combo|Panel|Notebook|Page') {
            "  {0,-22} x={1,5} y={2,5} w={3,5} h={4,5}  {5}" -f $c, ($r.L - $o.X), ($r.T - $o.Y), ($r.R - $r.L), ($r.B - $r.T), $t.Substring(0, [Math]::Min(60, $t.Length))
        }
    }
}
# Our own wizard only: TWizardForm among the windows our PIDs own
function Wizard { Get-ProbeWindows $probe 'TWizardForm' | Select-Object -First 1 }

$probeArgs = if ($SetupArgs) { @($SetupArgs) } else { @() }
$probe = Start-ProbeProcess -FilePath $Exe -ArgumentList $probeArgs
$top = Wait-ProbeWindow $probe 'TWizardForm' -TimeoutMs 30000
if (-not $top) {
    "no wizard window from PID $($probe.Id) or its children"
    [void](Close-ProbeGracefully $probe)
    exit 1
}

$captured = @{}
for ($i = 0; $i -lt 8; $i++) {
    $top = Wizard
    if (-not $top) { "wizard window went away"; break }
    $h = Header $top
    "page: $h"
    if ($h -like 'Select Components*' -and -not $captured['components']) { Save-ProbeSnapshot $top $OutDir 'components'; Dump $top 'components'; $captured['components'] = $true }
    if ($h -like 'Select Additional Tasks*' -and -not $captured['tasks']) { Save-ProbeSnapshot $top $OutDir 'tasks'; Dump $top 'tasks'; $captured['tasks'] = $true; break }
    # Never step past the tasks page: Next on Ready would install
    if ($h -like 'Ready*') { "reached the Ready page without seeing Tasks; stopping here"; break }
    $accept = Find-Child $top 'TNewRadioButton' 'I &accept*'
    if ($accept) { [void](Send-ProbeMessage $accept $BM_CLICK); Start-Sleep -Milliseconds 300 }
    $next = Find-Child $top 'TNewButton' '&Next*'
    if (-not $next -or -not [NpadProbe.Win32]::IsWindowEnabled($next)) { "Next not available on '$h'"; break }
    [void](Send-ProbeMessage $next $BM_CLICK)
    Start-Sleep -Milliseconds 700
}

# Leave without installing: WM_CLOSE brings up "Exit Setup?", answer Yes
$top = Wizard
if ($top) {
    Post-ProbeMessage $top $WM_CLOSE
    $box = Wait-ProbeWindow $probe '#32770' 'Exit Setup*' -TimeoutMs 5000
    if ($box) {
        $yes = Find-Child $box 'Button' '&Yes*'
        if ($yes) { [void](Send-ProbeMessage $yes $BM_CLICK) }
    } else { "no 'Exit Setup?' box appeared" }
}
# The bootstrapper waits for its wizard child, so its exit means both are gone;
# anything still running is reported, not killed
if ($probe.Process.WaitForExit(15000)) {
    "setup exit code: $($probe.Process.ExitCode) (2 = cancelled, nothing installed)"
}
[void](Close-ProbeGracefully $probe 5000)
"done"
