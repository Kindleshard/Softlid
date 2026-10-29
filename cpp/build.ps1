# Builds Softlid.exe into bin\ - finds Visual Studio's C++ tools by itself.
#   .\build.ps1                  release build (small and fast)
#   .\build.ps1 -Debug           debug build, with symbols
#   .\build.ps1 -Arch arm64      for ARM64 PCs, into bin-arm64\ (needs Visual Studio's ARM64 tools; built on an x64
#                                PC its tests can't run here, so they are only built)
param([switch]$Debug, [ValidateSet('x64', 'arm64')][string]$Arch = 'x64')
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools not found.' }
$cross = $Arch -eq 'arm64' -and $env:PROCESSOR_ARCHITECTURE -ne 'ARM64'
$vcvars = Join-Path $vs ('VC\Auxiliary\Build\' + $(if ($Arch -eq 'x64') { 'vcvars64.bat' } elseif ($cross) { 'vcvarsamd64_arm64.bat' } else { 'vcvarsarm64.bat' }))
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:PATH = "$cmakeDir\CMake\bin;$cmakeDir\Ninja;$(Split-Path $vswhere);$env:PATH"   # (vcvars looks for vswhere too)

$config = if ($Debug) { 'Debug' } else { 'Release' }
$suffix = if ($Arch -eq 'x64') { '' } else { "-$Arch" }
$build = Join-Path $PSScriptRoot "build\$config$suffix"
$options = "-DCMAKE_BUILD_TYPE=$config"
if ($cross) {
    # The translations are packed by a tool that runs while building: the x64 build's (made first if it isn't there).
    $langPack = Join-Path $PSScriptRoot "build\$config\LangPack.exe"
    if (-not (Test-Path -LiteralPath $langPack)) { & $PSCommandPath -Debug:$Debug | Out-Null }
    $options += " -DLANGPACK=`"$($langPack.Replace('\', '/'))`""
}
$commands = "`"$vcvars`" >nul && cmake -S `"$PSScriptRoot`" -B `"$build`" -G Ninja $options >nul && cmake --build `"$build`""
cmd /c $commands
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }


# The tests run before anything is copied: a failing test stops the build.
if (-not $cross) {
    & (Join-Path $build 'SoftlidTests.exe')
    if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)." }
}

$bin = Join-Path $PSScriptRoot "bin$suffix"
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$exe = Join-Path $bin 'Softlid.exe'
# A running copy can't be overwritten, but it can be renamed out of the way.
if (Test-Path $exe) { Move-Item -Force $exe "$exe.old" -ErrorAction SilentlyContinue }
Copy-Item (Join-Path $build 'Softlid.exe') $exe
# The spoken steps of the guided breaks sit next to the exe, a folder per language (without them, a Windows voice for
# the language reads the steps). Clips from before the folders (voice\*.mp3) are cleared away.
$voice = Join-Path $bin 'voice'
New-Item -ItemType Directory -Force -Path $voice | Out-Null
Get-ChildItem -LiteralPath $voice -File -Filter *.mp3 | Remove-Item -Force
# Only the clips that changed (a running Softlid may hold one it played open: an unchanged one is left be).
robocopy (Join-Path $PSScriptRoot 'res\voice') $voice /E /NJH /NJS /NFL /NDL /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying the voice clips failed ($LASTEXITCODE)." }
$global:LASTEXITCODE = 0
"Built $exe ({0:0} KB)" -f ((Get-Item $exe).Length / 1KB)
