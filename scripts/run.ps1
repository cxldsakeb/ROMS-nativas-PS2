<#
.SYNOPSIS
    Compila el runner (ps2EntryRunner) con el código generado y arranca el juego, guardando el log en out/logs/.

.PARAMETER Config
    Debug (por defecto, para depurar), RelWithDebInfo o Release.

.PARAMETER NoBuild
    Arranca el último ejecutable compilado sin recompilar.
#>
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')][string]$Config = 'Debug',
    [switch]$NoBuild
)
. "$PSScriptRoot\_common.ps1"

$elfPath = Get-GameElfPath
$runner = Join-Path $BuildDir "ps2xRuntime\$Config\ps2EntryRunner.exe"

if (-not $NoBuild) {
    $cmake = Get-CMake
    Invoke-Native $cmake @('--build', $BuildDir, '--config', $Config, '--target', 'ps2EntryRunner', '--parallel')
}
if (-not (Test-Path $runner)) { throw "No existe $runner. Ejecuta sin -NoBuild." }

New-Item -ItemType Directory -Force $LogDir | Out-Null
$log = Join-Path $LogDir ("run-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
Write-Host "Arrancando $elfPath"
Write-Host "Log: $log"

# Mezcla stdout+stderr del runner; Tee muestra en consola y guarda en el log.
# Con 'Stop', PowerShell 5.1 abortaría en la primera línea de stderr.
# Valores de juego de play.cmd (Waffle565): GS en su hilo y FINISH asíncrono. Se respetan si ya están definidos.
foreach ($pair in @(@('PS2X_GS_WORKER', '1'), @('GOW_GS_FINISH_ASINCRONO', '1'), @('GOW_AUDIO_DEVICE', 'default'))) {
    if (-not [Environment]::GetEnvironmentVariable($pair[0])) { Set-Item -Path ('env:' + $pair[0]) -Value $pair[1] }
}
if (-not (Test-Path (Join-Path (Split-Path $elfPath) '.disc_layout'))) {
    Write-Warning 'No hay game/disc/.disc_layout: vuelve a extraer el disco con .\scripts\extract-iso.ps1 (formato nuevo).'
}
$ErrorActionPreference = 'Continue'
Push-Location $RepoRoot
try { & $runner $elfPath 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log }
finally { Pop-Location }
Write-Host "Salida: código $LASTEXITCODE. Log guardado en $log"
