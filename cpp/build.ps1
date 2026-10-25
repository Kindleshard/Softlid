# Builds BlinkBuddy.exe into bin\ - finds Visual Studio's C++ tools by itself.
#   .\build.ps1            release build (small and fast)
#   .\build.ps1 -Debug     debug build, with symbols
param([switch]$Debug)
$ErrorActionPreference = 'Stop'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ build tools not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeDir = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$env:PATH = "$cmakeDir\CMake\bin;$cmakeDir\Ninja;$(Split-Path $vswhere);$env:PATH"   # (vcvars looks for vswhere too)

$config = if ($Debug) { 'Debug' } else { 'Release' }
$build = Join-Path $PSScriptRoot "build\$config"
$commands = "`"$vcvars`" >nul && cmake -S `"$PSScriptRoot`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$config >nul && cmake --build `"$build`""
cmd /c $commands
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

$bin = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
$exe = Join-Path $bin 'BlinkBuddy.exe'
# A running copy can't be overwritten, but it can be renamed out of the way.
if (Test-Path $exe) { Move-Item -Force $exe "$exe.old" -ErrorAction SilentlyContinue }
Copy-Item (Join-Path $build 'BlinkBuddy.exe') $exe
"Built $exe ({0:0} KB)" -f ((Get-Item $exe).Length / 1KB)
