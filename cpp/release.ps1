# Makes a release in dist\ - nothing is uploaded or published:
#   BlinkBuddy.exe                      the program on its own (what the updater downloads)
#   BlinkBuddy-<version>-portable.zip   the program with its voice clips: unzip anywhere and run
#   BlinkBuddy-<version>-setup.exe      the installer, with the voice in one language or all (only when NSIS 3 is
#                                       here; see installer\BlinkBuddy.nsi)
# The version comes from res\BuildVersion.h.
$ErrorActionPreference = 'Stop'

$header = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'res\BuildVersion.h') -Raw
if ($header -notmatch '#define BB_VERSION_TEXT "([^"]+)"') { throw 'No version in res\BuildVersion.h.' }
$version = $Matches[1]
"BlinkBuddy $version"

& (Join-Path $PSScriptRoot 'build.ps1')
$bin = Join-Path $PSScriptRoot 'bin'
$dist = Join-Path $PSScriptRoot 'dist'
if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist | Out-Null

# The program on its own.
Copy-Item -LiteralPath (Join-Path $bin 'BlinkBuddy.exe') -Destination $dist

# Portable: a folder with the program and its voice clips, zipped.
$stage = Join-Path $dist "BlinkBuddy-$version"
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $bin 'BlinkBuddy.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $bin 'voice') -Destination $stage -Recurse
$zip = Join-Path $dist "BlinkBuddy-$version-portable.zip"
# Entry by entry, with forward slashes: Windows PowerShell's own zipping writes backslashes some unzip tools mishandle.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
        $name = (Split-Path $stage -Leaf) + '/' + $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
Remove-Item -LiteralPath $stage -Recurse -Force

# The installer, if NSIS 3 is here (installed, or unzipped into C:\Tools\nsis).
$makensis = @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe", 'C:\Tools\nsis\makensis.exe') |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($makensis) {
    # Each language's voice in KB, for the installer to show beside the choice.
    $sizes = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'res\voice') -Directory | ForEach-Object {
        '/DKB_{0}={1}' -f $_.Name, [math]::Ceiling((Get-ChildItem -LiteralPath $_.FullName -File | Measure-Object Length -Sum).Sum / 1KB)
    }
    & $makensis /V2 "/DVERSION=$version" @sizes (Join-Path $PSScriptRoot 'installer\BlinkBuddy.nsi')
    if ($LASTEXITCODE -ne 0) { throw "NSIS failed ($LASTEXITCODE)." }
} else {
    'NSIS not found: the installer was skipped (install it from https://nsis.sourceforge.io, then run this again).'
}

''
Get-ChildItem -LiteralPath $dist | ForEach-Object { '{0,-40} {1,8:0} KB' -f $_.Name, ($_.Length / 1KB) }
