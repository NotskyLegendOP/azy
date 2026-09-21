# Compiles resources/shaders/mirror.hlsl with fxc - the same compiler family
# (d3dcompiler) and the same entry points and profiles the application uses at
# runtime (see MirrorRenderer::compile_shaders) - so a shader that does not
# compile can never reach an installer again.
#
#   powershell -ExecutionPolicy Bypass -File scripts\check-shader.ps1
#
# Called by scripts\package.ps1 and by CI before the installer is built. The
# Linux-side structural scan in tools/check-mirror.py is the fast mirror of this
# check; this is the authoritative one, because only a real HLSL compiler knows
# which words are reserved (2.0.0 shipped a parameter named `point` and a local
# named `line` - both HLSL keywords - and the skin refused to start on every
# machine until fxc said so).
#
# Requires fxc.exe from the Windows SDK ("Desktop development with C++" tools).

[CmdletBinding()]
param(
    [string] $Shader = (Join-Path (Split-Path -Parent $PSScriptRoot) 'resources\shaders\mirror.hlsl')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Shader)) { throw "shader not found: $Shader" }

function Find-Fxc {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path $kits) {
        $fromKit = Get-ChildItem $kits -Directory |
            Where-Object { $_.Name -like '10.*' } |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\fxc.exe' } |
            Where-Object { Test-Path $_ } |
            Select-Object -First 1
        if ($fromKit) { return $fromKit }
    }
    $onPath = Get-Command fxc.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

$fxc = Find-Fxc
if (-not $fxc) {
    throw "fxc.exe not found. Install the Windows SDK (Visual Studio Installer -> Desktop development with C++)."
}

# The renderer compiles ps_main against ps_5_0 with a ps_4_0 fallback (and the
# same pair for vs_main); the shader's own contract is that the source is legal
# for both, so all four are required to compile.
$targets = @(
    @{ entry = 'ps_main'; profile = 'ps_5_0' },
    @{ entry = 'ps_main'; profile = 'ps_4_0' },
    @{ entry = 'vs_main'; profile = 'vs_5_0' },
    @{ entry = 'vs_main'; profile = 'vs_4_0' }
)

Write-Host "== Compiling the mirror shader with fxc ($fxc) =="
$failed = @()
foreach ($target in $targets) {
    $out = Join-Path $env:TEMP "azy-shader-check-$($target.profile).o"
    try {
        # fxc prints the X-errors (the exact format the runtime log shows) and
        # returns non-zero on failure.
        & $fxc /nologo /T $target.profile /E $target.entry /Fo $out $Shader 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            $failed += "$($target.profile) / $($target.entry)"
            Write-Host "FAILED: $($target.profile) $($target.entry)" -ForegroundColor Red
        } else {
            Write-Host "ok: $($target.profile) $($target.entry)"
        }
    } finally {
        Remove-Item $out -ErrorAction SilentlyContinue
    }
}

if ($failed.Count -gt 0) {
    throw "mirror.hlsl does not compile ($($failed -join ', ')) - the mirror would refuse to start at runtime"
}
Write-Host "mirror shader compiles as ps_5_0, ps_4_0, vs_5_0 and vs_4_0" -ForegroundColor Green
