# Builds BlinkBuddy.exe with the C# compiler that ships with Windows (.NET Framework 4.8) - nothing to install.
$ErrorActionPreference = 'Stop'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$src = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter *.cs | ForEach-Object { $_.FullName }
$out = Join-Path $PSScriptRoot 'BlinkBuddy.exe'
$ico = Join-Path $PSScriptRoot 'BlinkBuddy.ico'
# x64 is required: the Magnification API refuses to run under WOW64.
$flags = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/codepage:65001',
           '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Management.dll', '/r:System.Device.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll',('/r:' + (Join-Path (Split-Path $csc) 'WPF\System.Speech.dll')), "/out:$out")
# The translations travel inside the exe: src\lang\<code>.txt becomes the resource "lang.<code>".
$flags += Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src\lang') -Filter *.txt -ErrorAction SilentlyContinue |
    ForEach-Object { "/resource:$($_.FullName),lang.$($_.BaseName)" }

# A running exe can't be overwritten but can be renamed; the old copy is cleaned up on a later build.
Remove-Item -LiteralPath "$out.old" -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $out) {
    # If the .old copy is itself still running, it can't be replaced; then the exe is free to overwrite anyway.
    try { Rename-Item -LiteralPath $out -NewName 'BlinkBuddy.exe.old' -ErrorAction Stop } catch { }
}

if (-not (Test-Path -LiteralPath $ico)) {
    # First pass: build once so the app can draw its own icon, then rebuild with it embedded.
    & $csc @flags @src
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
    Start-Process -FilePath $out -ArgumentList '--write-icon', "`"$ico`"" -Wait
}
& $csc @flags "/win32icon:$ico" @src
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "Built $out ($([math]::Round((Get-Item -LiteralPath $out).Length / 1KB)) KB)"
