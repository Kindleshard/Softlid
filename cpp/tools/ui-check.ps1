# Checks that the settings window looks exactly as it did: the panel and Options, each scrolled through to its end, compared pixel by pixel.
#   .\tools\ui-check.ps1 -Update   before a change: the screenshots become the baseline (cpp\build\ui-baseline)
#   .\tools\ui-check.ps1           after it: compare (exit code = number of views that changed)
# It runs a diagnostic copy of bin\BlinkBuddy.exe (--offscreen: off the screen, beside a running BlinkBuddy, which is
# left alone) with a settings file of its own (Advanced view, English, filter and reminders off), so your settings
# are never touched. Screenshots depend on the display's scale and fonts: compare with a baseline made on this PC.
param([switch]$Update, [int]$Tolerance = 0)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class UiCheck {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(CB cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  public struct RECT { public int L, T, R, B; } public struct PT { public int X, Y; }
  // The settings window of one process (the diagnostic copy, not a BlinkBuddy you have open).
  public static IntPtr SettingsOf(uint id) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid); var s = new StringBuilder(64); GetClassName(h, s, 64);
      if (pid == id && s.ToString() == "BlinkBuddy.Direct" && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
}
"@
[UiCheck]::SetProcessDPIAware() | Out-Null

$cpp = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $cpp 'bin\BlinkBuddy.exe'
$work = Join-Path $cpp 'build\ui-check'
$baseline = Join-Path $cpp 'build\ui-baseline'
$shots = if ($Update) { $baseline } else { Join-Path $work 'shots' }
New-Item -ItemType Directory -Force -Path $work, $shots | Out-Null
Get-ChildItem -LiteralPath $shots -Filter *.png | Remove-Item

# Its own settings: the Advanced view (everything shows), English, and nothing that changes the screen or pops up.
$ini = Join-Path $work 'settings.ini'
@('AdvancedView=True', 'Language=en', 'FilterEnabled=False', 'ToneEnabled=False', 'ScheduleEnabled=False',
  'NightLightSync=False', 'BlinkEnabled=False', 'BreakEnabled=False', 'LongBreakEnabled=False', 'MiniMode=False',
  'UpdateCheck=False') | Set-Content -LiteralPath $ini -Encoding UTF8

function L([int]$a, [int]$b) { [IntPtr](($b -shl 16) -bor ($a -band 0xFFFF)) }
# The client area only: the title bar is drawn grey or white by whether the window happens to be the active one.
function Shot([IntPtr]$h, [string]$out) {
    $r = New-Object UiCheck+RECT; [UiCheck]::GetWindowRect($h, [ref]$r) | Out-Null
    $c = New-Object UiCheck+RECT; [UiCheck]::GetClientRect($h, [ref]$c) | Out-Null
    $o = New-Object UiCheck+PT; [UiCheck]::ClientToScreen($h, [ref]$o) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [UiCheck]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
    $client = $bmp.Clone([System.Drawing.Rectangle]::new($o.X - $r.L, $o.Y - $r.T, $c.R, $c.B), $bmp.PixelFormat)
    $client.Save($out); $client.Dispose(); $g.Dispose(); $bmp.Dispose()
}
# A screenshot once the window has stopped changing: a capture taken while a scroll or a tab change is still being
# drawn is torn (part old, part new). Two captures in a row that are the same mean it is done.
function SteadyShot([IntPtr]$h, [string]$out) {
    $a = Join-Path $work 'steady-a.png'; $b = Join-Path $work 'steady-b.png'
    Shot $h $a
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 200
        Shot $h $b
        if ((Get-FileHash -LiteralPath $a).Hash -eq (Get-FileHash -LiteralPath $b).Hash) { Move-Item -Force -LiteralPath $b -Destination $out; return }
        Move-Item -Force -LiteralPath $b -Destination $a
    }
    throw "The window kept changing: $out"
}
function Click([IntPtr]$h, [int]$x, [int]$y) { foreach ($m in 0x200, 0x201, 0x202) { [UiCheck]::PostMessage($h, $m, [IntPtr]([int]($m -eq 0x201)), (L $x $y)) | Out-Null } }
function Wheel([IntPtr]$h, [int]$x, [int]$y, [int]$n) {
    $p = New-Object UiCheck+PT; $p.X = $x; $p.Y = $y; [UiCheck]::ClientToScreen($h, [ref]$p) | Out-Null
    for ($i = 0; $i -lt $n; $i++) { [UiCheck]::PostMessage($h, 0x20A, [IntPtr]((-120 -band 0xFFFF) -shl 16), (L $p.X $p.Y)) | Out-Null }
}

$p = Start-Process $exe -ArgumentList '--offscreen', '--settings', "`"$ini`"" -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 50 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $h = [UiCheck]::SettingsOf([uint32]$p.Id) }
    if ($h -eq [IntPtr]::Zero) { throw 'The settings window didn''t open.' }
    Start-Sleep -Seconds 2
    $s = [UiCheck]::GetDpiForWindow($h) / 96.0
    # The panel (every card: a diagnostic copy shows them all), then Options, behind the cog by the title bar.
    for ($t = 0; $t -lt 2; $t++) {
        if ($t -eq 1) { Click $h ([int](400 * $s)) ([int](27 * $s)); Start-Sleep -Milliseconds 300 }
        # Scrolled through to the end of the page: a scroll that changes nothing means the end was reached.
        $previous = $null
        for ($part = 0; $part -lt 20; $part++) {
            $out = Join-Path $shots "tab$t-$part.png"
            SteadyShot $h $out
            if ($previous -and (Get-FileHash -LiteralPath $out).Hash -eq (Get-FileHash -LiteralPath $previous).Hash) { Remove-Item -LiteralPath $out; break }
            $previous = $out
            Wheel $h ([int](200 * $s)) ([int](300 * $s)) 9; Start-Sleep -Milliseconds 300
        }
    }
} finally {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}

$total = (Get-ChildItem -LiteralPath $shots -Filter *.png).Count
if ($Update) { "Baseline: $total views in $baseline"; exit 0 }

$changed = 0
foreach ($f in Get-ChildItem -LiteralPath $shots -Filter *.png | Sort-Object Name) {
    $basePath = Join-Path $baseline $f.Name
    if (-not (Test-Path -LiteralPath $basePath)) { "{0}: no baseline (run with -Update first)" -f $f.Name; $changed++; continue }
    $a = [System.Drawing.Bitmap]::new($basePath); $b = [System.Drawing.Bitmap]::new($f.FullName)
    $n = 0; $y0 = -1; $y1 = -1
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) { $n = -1 }
    else {
        for ($y = 0; $y -lt $a.Height; $y++) { for ($x = 0; $x -lt $a.Width; $x++) {
            if ($a.GetPixel($x, $y).ToArgb() -ne $b.GetPixel($x, $y).ToArgb()) { $n++; if ($y0 -lt 0) { $y0 = $y }; $y1 = $y } } }
    }
    $a.Dispose(); $b.Dispose()
    if ($n -lt 0) { "{0}: a different size" -f $f.Name; $changed++ }
    elseif ($n -gt $Tolerance) { "{0}: {1} pixels changed (rows {2}-{3})" -f $f.Name, $n, $y0, $y1; $changed++ }
}
if ($changed -eq 0) { "All $total views match the baseline." } else { "$changed of $total views changed: see $shots" }
exit $changed
