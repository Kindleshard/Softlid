# A screenshot of BlinkBuddy's settings window with the given settings, for the loops: a diagnostic copy opens it off
# the screen (--offscreen --no-screen: nothing is put on your screen, your own BlinkBuddy is left alone).
#   .\shot.ps1 -Name filter-warm -Set 'FilterEnabled=True', 'BlueFilter=60'
#   .\shot.ps1 -Name strip -Set 'StripEnabled=True' -Scroll 18      (wheel steps down the page first)
# Saved as cpp\build\media\ui\<name>.png (the window's inside, at the display's scale).
param([Parameter(Mandatory)][string]$Name, [string[]]$Set = @(), [int]$Scroll = 0, [switch]$Options, [switch]$Frame)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class MediaShot {
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
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int size);
  public struct RECT { public int L, T, R, B; } public struct PT { public int X, Y; }
  public static IntPtr SettingsOf(uint id) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => { uint pid; GetWindowThreadProcessId(h, out pid); var s = new StringBuilder(64); GetClassName(h, s, 64);
      if (pid == id && s.ToString() == "BlinkBuddy.Direct" && IsWindowVisible(h)) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
}
"@
[MediaShot]::SetProcessDPIAware() | Out-Null
$cpp = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$work = Join-Path $cpp 'build\media'
$ui = Join-Path $work 'ui'
New-Item -ItemType Directory -Force -Path $ui | Out-Null
$ini = Join-Path $work "shot-settings.ini"
@('Language=en', 'TourDone=True', 'Sounds=False', 'ScheduleEnabled=False', 'NightLightSync=False', 'BlinkEnabled=False',
  'BreakEnabled=False', 'LongBreakEnabled=False', 'MiniMode=False', 'UpdateCheck=False') + $Set |
    Set-Content -LiteralPath $ini -Encoding UTF8

function L([int]$a, [int]$b) { [IntPtr](($b -shl 16) -bor ($a -band 0xFFFF)) }
function Shot([IntPtr]$h, [string]$out) {
    $r = New-Object MediaShot+RECT; [MediaShot]::GetWindowRect($h, [ref]$r) | Out-Null
    $c = New-Object MediaShot+RECT; [MediaShot]::GetClientRect($h, [ref]$c) | Out-Null
    $o = New-Object MediaShot+PT; [MediaShot]::ClientToScreen($h, [ref]$o) | Out-Null
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [MediaShot]::PrintWindow($h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
    # The inside, or with -Frame the window as it looks, title bar and all (its visible frame: without the resize
    # borders Windows keeps around it, which aren't drawn).
    $part = [System.Drawing.Rectangle]::new($o.X - $r.L, $o.Y - $r.T, $c.R, $c.B)
    if ($Frame) {
        $f = New-Object MediaShot+RECT; [MediaShot]::DwmGetWindowAttribute($h, 9, [ref]$f, 16) | Out-Null   # (DWMWA_EXTENDED_FRAME_BOUNDS)
        $part = [System.Drawing.Rectangle]::new($f.L - $r.L, $f.T - $r.T, $f.R - $f.L, $f.B - $f.T)
    }
    $client = $bmp.Clone($part, $bmp.PixelFormat)
    $client.Save($out); $client.Dispose(); $g.Dispose(); $bmp.Dispose()
}

$p = Start-Process (Join-Path $cpp 'bin\BlinkBuddy.exe') -ArgumentList '--offscreen', '--no-screen', '--settings', "`"$ini`"" -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 50 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $h = [MediaShot]::SettingsOf([uint32]$p.Id) }
    if ($h -eq [IntPtr]::Zero) { throw 'The settings window didn''t open.' }
    Start-Sleep -Milliseconds 1500
    $s = [MediaShot]::GetDpiForWindow($h) / 96.0
    if ($Options) {
        foreach ($m in 0x200, 0x201, 0x202) { [MediaShot]::PostMessage($h, $m, [IntPtr]([int]($m -eq 0x201)), (L ([int](400 * $s)) ([int](27 * $s)))) | Out-Null }
        [MediaShot]::PostMessage($h, 0x200, [IntPtr]0, (L ([int](250 * $s)) ([int](27 * $s)))) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    if ($Scroll -gt 0) {
        $pt = New-Object MediaShot+PT; $pt.X = [int](200 * $s); $pt.Y = [int](300 * $s); [MediaShot]::ClientToScreen($h, [ref]$pt) | Out-Null
        for ($i = 0; $i -lt $Scroll; $i++) { [MediaShot]::PostMessage($h, 0x20A, [IntPtr]((-120 -band 0xFFFF) -shl 16), (L $pt.X $pt.Y)) | Out-Null }
    }
    # Once the window has stopped changing (two captures in a row the same), as tools\ui-check.ps1 does.
    $out = Join-Path $ui "$Name.png"
    $a = Join-Path $work 'shot-a.png'; $b = Join-Path $work 'shot-b.png'
    Start-Sleep -Milliseconds 600
    Shot $h $a
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 250
        Shot $h $b
        if ((Get-FileHash -LiteralPath $a).Hash -eq (Get-FileHash -LiteralPath $b).Hash) { break }
        Move-Item -Force -LiteralPath $b -Destination $a
    }
    Move-Item -Force -LiteralPath $b -Destination $out
    Remove-Item -LiteralPath $a -ErrorAction SilentlyContinue
    $out
} finally {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
