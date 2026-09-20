# Builds Azy Skin on Windows (MSVC or MinGW) and runs the core unit tests.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Arch x64 -Config Release
#
# Prerequisites: CMake 3.20+ and either Visual Studio 2019/2022 or MinGW-w64 with
# the Windows SDK. No other dependencies: Azy Skin has no third-party libraries.

[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')] [string] $Arch = 'x64',
    [ValidateSet('Debug', 'Release')] [string] $Config = 'Release',
    [switch] $SkipTests,
    [string] $BuildDir = 'build'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$generatorArgs = @()
if ($Arch -eq 'ARM64') { $generatorArgs += @('-A', 'ARM64') }

Write-Host "== Configuring ($Config, $Arch) ==" -ForegroundColor Cyan
cmake -S $root -B (Join-Path $root $BuildDir) @generatorArgs -DAZY_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

Write-Host "== Building ==" -ForegroundColor Cyan
cmake --build (Join-Path $root $BuildDir) --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if (-not $SkipTests) {
    Write-Host "== Running core tests ==" -ForegroundColor Cyan
    ctest --test-dir (Join-Path $root $BuildDir) -C $Config --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
}

$exe = Join-Path $root "$BuildDir\$Config\AzySkin.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $root "$BuildDir\AzySkin.exe" }
if (Test-Path $exe) {
    $size = [math]::Round((Get-Item $exe).Length / 1KB)
    Write-Host "== Built $exe ($size KB) ==" -ForegroundColor Green
    Write-Host "   Run it:  $exe" -ForegroundColor Gray
    Write-Host "   It starts in the system tray; right-click the icon for the menu." -ForegroundColor Gray
} else {
    Write-Warning "AzySkin.exe was not found in $BuildDir - check the build output above."
}
