# Makes a release in dist\ - nothing is uploaded or published:
#   BlinkBuddy.exe                      the program on its own (what the updater downloads)
#   BlinkBuddy-<version>-portable.zip   the program with its voice clips: unzip anywhere and run
#   BlinkBuddy-<version>-setup.exe      the installer (only when Inno Setup 6 is installed; see installer\BlinkBuddy.iss)
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

# The installer, if Inno Setup 6 is here.
$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc /Q "/DAppVersion=$version" (Join-Path $PSScriptRoot 'installer\BlinkBuddy.iss')
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)." }
} else {
    'Inno Setup 6 not found: the installer was skipped (install it from https://jrsoftware.org/isinfo.php, then run this again).'
}

''
Get-ChildItem -LiteralPath $dist | ForEach-Object { '{0,-40} {1,8:0} KB' -f $_.Name, ($_.Length / 1KB) }
