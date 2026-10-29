# Films Softlid on the staged desktop (stage.html, full screen in a Chrome window of its own), for the README's
# feature loops. Everything it makes goes in cpp\build\media; the screen is taken over while it runs.
#   .\record.ps1 -Name exercises -Seconds 24 -Run '--preview-exercises'
#   .\record.ps1 -Name mini -Seconds 12 -Run '--offscreen --no-screen' -Set 'MiniMode=True'
#   .\record.ps1 -Name strip -Seconds 8 -Run '--offscreen --no-screen' -Set 'StripEnabled=True' -Path '0:.3:.3;3:.3:.5'
# -Run is Softlid's (a preview, or --offscreen for a whole copy beside your own); -Set adds settings to the demo
# ones (English, no sounds or voice, nothing else on the screen). Your own settings are never read or changed.
# -Path moves the pointer while filming (it isn't filmed): "seconds:x:y" points after Softlid starts, x and y as
# fractions of the screen, eased from one to the next.
param([Parameter(Mandatory)][string]$Name, [double]$Seconds = 10, [string]$Run = '', [string[]]$Set = @(),
      [double]$Lead = 1.0, [string]$Path = '')
$ErrorActionPreference = 'Stop'
$cpp = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$work = Join-Path $cpp 'build\media'
New-Item -ItemType Directory -Force -Path (Join-Path $work 'raw') | Out-Null
$ffmpeg = if ($env:FFMPEG) { $env:FFMPEG } else { 'C:\Tools\ffmpeg\ffmpeg.exe' }
$chrome = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe", "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
            "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

# The demo settings: nothing that sounds, speaks, filters the screen or pops up on its own.
$ini = Join-Path $work 'settings.ini'
@('Language=en', 'TourDone=True', 'Sounds=False', 'ExerciseVoice=False', 'FilterEnabled=False', 'ToneEnabled=False',
  'ScheduleEnabled=False', 'NightLightSync=False', 'BlinkEnabled=False', 'BreakEnabled=False', 'LongBreakEnabled=False',
  'MiniMode=False', 'StripEnabled=False', 'UpdateCheck=False') + $Set | Set-Content -LiteralPath $ini -Encoding UTF8

Add-Type @"
using System; using System.Runtime.InteropServices;
public class MediaInput {
  [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy, data, flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public int type; public MOUSEINPUT mi; }   // (40 bytes, as on 64-bit Windows)
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  // The pointer to x, y (fractions of the primary screen), as a mouse move.
  public static void MoveTo(double x, double y) {
    var i = new INPUT { type = 0, mi = new MOUSEINPUT { dx = (int)(x * 65535), dy = (int)(y * 65535), flags = 0x8001 } };
    SendInput(1, new[] { i }, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
[MediaInput]::MoveTo(0.999, 0.999)

# The stage, full screen.
$chromeProfile = Join-Path $work 'chrome-profile'
$stageUrl = 'file:///' + (Join-Path $PSScriptRoot 'stage.html').Replace('\', '/')
$stage = Start-Process -FilePath $chrome -PassThru -ArgumentList "--user-data-dir=`"$chromeProfile`"", '--kiosk', '--no-first-run',
    '--no-default-browser-check', '--disable-features=Translate', '--hide-crash-restore-bubble', $stageUrl
Start-Sleep -Seconds 3

# Filming, then Softlid a moment later.
$out = Join-Path $work "raw\$Name.mp4"
# (The desktop through the GPU - Desktop Duplication, encoded by NVENC - keeps a steady 60 fps at full resolution,
# which the GDI grabber can't.)
$film = Start-Process -FilePath $ffmpeg -PassThru -WindowStyle Hidden -ArgumentList '-v', 'error', '-y', '-f', 'lavfi',
    '-i', 'ddagrab=output_idx=0:framerate=60:draw_mouse=0', '-t', ($Seconds + $Lead).ToString([cultureinfo]::InvariantCulture),
    '-c:v', 'h264_nvenc', '-preset', 'p4', '-rc', 'constqp', '-qp', '14', "`"$out`""
Start-Sleep -Seconds $Lead
$app = Start-Process -FilePath (Join-Path $cpp 'bin\Softlid.exe') -PassThru -ArgumentList "$Run --settings `"$ini`""
if ($Path) {
    $points = $Path.Split(';') | ForEach-Object { , ([double[]]($_.Split(':') | ForEach-Object { [double]::Parse($_, [cultureinfo]::InvariantCulture) })) }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $film.HasExited) {
        $t = $clock.Elapsed.TotalSeconds
        $i = 1
        while ($i -lt $points.Count - 1 -and $t -gt $points[$i][0]) { $i++ }
        $a = $points[$i - 1]; $b = $points[[math]::Min($i, $points.Count - 1)]
        $k = if ($b[0] -gt $a[0]) { [math]::Min(1, [math]::Max(0, ($t - $a[0]) / ($b[0] - $a[0]))) } else { 1 }
        $k = $k * $k * (3 - 2 * $k)
        # (as real mouse input, which Softlid's pointer watching sees; just placing the pointer isn't)
        [MediaInput]::MoveTo($a[1] + ($b[1] - $a[1]) * $k, $a[2] + ($b[2] - $a[2]) * $k)
        Start-Sleep -Milliseconds 12
    }
}
$film.WaitForExit()
if (-not $app.HasExited) { Stop-Process -Id $app.Id -Force }
Get-CimInstance Win32_Process -Filter "Name = 'chrome.exe'" | Where-Object { $_.CommandLine -like "*$chromeProfile*" } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
"$out"
