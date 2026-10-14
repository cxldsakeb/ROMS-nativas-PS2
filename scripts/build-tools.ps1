<#
.SYNOPSIS
    Configura CMake y compila las herramientas de PS2Recomp (ps2_analyzer y ps2_recomp) en Release.

.PARAMETER Generator
    Generador de CMake. Por defecto VS 2022; cámbialo si usas otra versión de Visual Studio.
#>
param(
    [string]$Generator = 'Visual Studio 17 2022'
)
. "$PSScriptRoot\_common.ps1"

$cmake = Get-CMake
Write-Host "Usando CMake: $cmake"

if (-not (Test-Path (Join-Path $RecompDir 'CMakeLists.txt'))) {
    Write-Host 'Descargando el submódulo PS2Recomp ...'
    Invoke-Native git @('-C', $RepoRoot, 'submodule', 'update', '--init', '--recursive')
}

Invoke-Native $cmake @(
    '-S', $RecompDir, '-B', $BuildDir, '-G', $Generator, '-A', 'x64',
    '-DPS2X_BUILD_TEST=OFF', '-DPS2X_BUILD_STUDIO=OFF'
)
Invoke-Native $cmake @('--build', $BuildDir, '--config', 'Release', '--target', 'ps2_analyzer', 'ps2_recomp', '--parallel')

Write-Host ''
Write-Host 'Herramientas listas:'
Write-Host "  $BuildDir\ps2xAnalyzer\Release\ps2_analyzer.exe"
Write-Host "  $BuildDir\ps2xRecomp\Release\ps2_recomp.exe"
