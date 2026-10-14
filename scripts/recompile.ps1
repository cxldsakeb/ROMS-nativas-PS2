<#
.SYNOPSIS
    Recompila el ELF del juego a C++ usando config/gow.toml y copia el resultado al runner de PS2Recomp.

.DESCRIPTION
    1. Normaliza las rutas del TOML a rutas relativas al repo (el export de Ghidra las escribe absolutas).
    2. Ejecuta ps2_recomp -> generated/
    3. Copia generated/*.cpp + overrides/*.cpp a PS2Recomp/ps2xRuntime/src/runner/
       y generated/*.h a PS2Recomp/ps2xRuntime/include/

.PARAMETER SyncOnly
    No recompila: solo vuelve a copiar generated/ y overrides/ al runner (útil tras editar un override).
#>
param(
    [switch]$SyncOnly
)
. "$PSScriptRoot\_common.ps1"

$elfPath = Get-GameElfPath
$elfName = Split-Path $elfPath -Leaf

if (-not $SyncOnly) {
    $recomp = Join-Path $BuildDir 'ps2xRecomp\Release\ps2_recomp.exe'
    if (-not (Test-Path $recomp)) { throw 'Falta ps2_recomp.exe. Ejecuta primero .\scripts\build-tools.ps1' }
    if (-not (Test-Path $ConfigToml)) { throw "Falta $ConfigToml. Genéralo con Ghidra (docs/SETUP.md, paso 5)." }

    # Rutas relativas al repo para que el mismo TOML funcione en el PC de todos
    $toml = Get-Content $ConfigToml -Raw
    $toml = $toml -replace '(?m)^input\s*=.*$', "input = `"game/disc/$elfName`""
    $toml = $toml -replace '(?m)^output\s*=.*$', 'output = "generated"'
    # Sin CSV (TOML sacado de ps2_analyzer) el recompilador usa sus propias heurísticas
    $csvValue = if (Test-Path (Join-Path $RepoRoot 'config\gow_functions.csv')) { 'config/gow_functions.csv' } else { '' }
    $toml = $toml -replace '(?m)^ghidra_output\s*=.*$', "ghidra_output = `"$csvValue`""
    Write-Utf8NoBom $ConfigToml $toml

    if (Test-Path $GeneratedDir) { Remove-Item $GeneratedDir -Recurse -Force }
    New-Item -ItemType Directory -Force $GeneratedDir | Out-Null

    Push-Location $RepoRoot
    try { Invoke-Native $recomp @($ConfigToml) }
    finally { Pop-Location }
}

# ── Sincronizar con el runner ──────────────────────────────────────────────
if (-not (Test-Path $GeneratedDir)) { throw 'No hay nada en generated/. Ejecuta sin -SyncOnly.' }

# Borrar lo que copiamos la vez anterior (por si cambiaron nombres de funciones)
if (Test-Path $SyncManifest) {
    Get-Content $SyncManifest | Where-Object { $_ -and (Test-Path $_) } | Remove-Item -Force
}
New-Item -ItemType Directory -Force (Split-Path $SyncManifest) | Out-Null

$copied = New-Object System.Collections.Generic.List[string]
$sources = @(Get-ChildItem $GeneratedDir -File) + @(Get-ChildItem $OverridesDir -Filter *.cpp -File)
foreach ($file in $sources) {
    $destDir = if ($file.Extension -eq '.h') { $RunnerIncDir } else { $RunnerSrcDir }
    $dest = Join-Path $destDir $file.Name
    Copy-Item $file.FullName $dest -Force
    $copied.Add($dest)
}
Set-Content -Path $SyncManifest -Value $copied -Encoding utf8

Write-Host "Copiados $($copied.Count) archivos al runner de PS2Recomp."
Write-Host 'Siguiente paso: .\scripts\run.ps1'
