# Builds Azy Skin and produces the installer in dist\.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#
# Requires Inno Setup 6 (https://jrsoftware.org/isdl.php). The script looks for
# ISCC.exe on the PATH and in the two standard install locations.

[CmdletBinding()]
param(
    [ValidateSet('x64', 'ARM64')] [string] $Arch = 'x64',
    [string] $BuildDir = 'build',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# The mirror shader is embedded as text and only compiled at runtime, so a shader
# that does not compile builds and packages without complaint - and then refuses
# to start on the user's machine. Run the runtime's compiler first; a package with
# a broken shader must not exist.
& (Join-Path $PSScriptRoot 'check-shader.ps1')

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build-windows.ps1') -Arch $Arch -Config Release -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

function Find-Iscc {
    $candidates = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }
    return $candidates | Select-Object -First 1
}

$iscc = Find-Iscc
if (-not $iscc) {
    throw "Inno Setup 6 not found. Install it from https://jrsoftware.org/isdl.php (or 'choco install innosetup')."
}

# The .iss expects the built executable at build\Release\AzySkin.exe.
$release = Join-Path $root "$BuildDir\Release"
if (-not (Test-Path $release)) { New-Item -ItemType Directory -Path $release | Out-Null }
$built = Join-Path $root "$BuildDir\AzySkin.exe"
$builtRelease = Join-Path $release 'AzySkin.exe'
if ((Test-Path $built) -and -not (Test-Path $builtRelease)) { Copy-Item $built $builtRelease }

Write-Host "== Building installer ==" -ForegroundColor Cyan
& $iscc (Join-Path $root 'packaging\AzySkin.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }

Get-ChildItem (Join-Path $root 'dist') -Filter '*.exe' |
    ForEach-Object { Write-Host "== $($_.FullName) ($([math]::Round($_.Length / 1MB, 1)) MB) ==" -ForegroundColor Green }
