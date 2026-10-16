<#
.SYNOPSIS
    Recompila el ELF del juego a C++ y lo copia, con los overrides, al runner de PS2Recomp.

.DESCRIPTION
    Flujo de la base de Waffle565 (vía KIexster/god-of-war-recomp, ver docs/KIEXSTER.md):
    1. Rellena config/recomp.template.toml (@ELF@, @MAP@, @OUT@) -> out/gow.effective.toml
       y ejecuta ps2_recomp con él -> generated/
    2. IOP nativo (opcional): si $env:GOW_IOP_MODULOS apunta a una carpeta con modules.txt (sale de una
       ejecución con $env:PS2X_IOP_DUMP_MODULES), tools/iop/iopn_gen.py compila los IRX a C++ dentro de
       PS2Recomp/ps2xIOP/src/native (ignorado por git: son datos derivados del juego).
    3. Copia generated/* + overrides/*.cpp + overrides/*.h a PS2Recomp/ps2xRuntime/src/runner/
    4. Reconfigura CMake. VU1 nativa (opcional): si $env:GOW_VU1_MICROCODIGO apunta a una carpeta con
       micromemorias capturadas con $env:GOW_VU1_CAPTURA, CMake genera y compila la VU1 nativa.

.PARAMETER SyncOnly
    No recompila: solo vuelve a copiar generated/ y overrides/ al runner (útil tras editar un override).

.PARAMETER AggressiveLogs
    Reconfigura CMake con AGRESSIVE_LOGS activado (por defecto, desactivado; ver build-tools.ps1).
    Cambiarlo obliga a una compilación completa.
#>
param(
    [switch]$SyncOnly,
    [switch]$AggressiveLogs
)
. "$PSScriptRoot\_common.ps1"

$elfPath = Get-GameElfPath
function ConvertTo-ForwardSlash([string]$Path) { return $Path -replace '\\', '/' }

if (-not $SyncOnly) {
    $recomp = Join-Path $BuildDir 'ps2xRecomp\Release\ps2_recomp.exe'
    if (-not (Test-Path $recomp)) { throw 'Falta ps2_recomp.exe. Ejecuta primero .\scripts\build-tools.ps1' }

    # ps2_recomp no abre rutas con caracteres no ASCII (acentos en la carpeta del repo, por ejemplo)
    if ("$RepoRoot" -match '[^\x00-\x7F]') { throw "La ruta del repo tiene caracteres no ASCII: $RepoRoot" }

    $toml = [IO.File]::ReadAllText($TemplateToml)
    $toml = $toml.Replace('@ELF@', (ConvertTo-ForwardSlash $elfPath)).
                  Replace('@MAP@', (ConvertTo-ForwardSlash $FuncMapCsv)).
                  Replace('@OUT@', (ConvertTo-ForwardSlash $GeneratedDir) + '/')
    New-Item -ItemType Directory -Force (Split-Path $EffectiveToml) | Out-Null
    Write-Utf8NoBom $EffectiveToml $toml

    if (Test-Path $GeneratedDir) { Remove-Item $GeneratedDir -Recurse -Force }
    New-Item -ItemType Directory -Force $GeneratedDir | Out-Null

    Push-Location $RepoRoot
    try { Invoke-Native $recomp @($EffectiveToml) }
    finally { Pop-Location }
}

# ── IOP nativo (opcional) ──────────────────────────────────────────────────
$iopNativeDir = Join-Path $RecompDir 'ps2xIOP\src\native'
Get-ChildItem $iopNativeDir -Filter 'iopn_*.cpp' -File -ErrorAction SilentlyContinue | Remove-Item -Force
if ($env:GOW_IOP_MODULOS) {
    if (-not (Test-Path (Join-Path $env:GOW_IOP_MODULOS 'modules.txt'))) { throw "GOW_IOP_MODULOS no tiene modules.txt: $env:GOW_IOP_MODULOS" }
    Write-Host "IOP nativo desde $env:GOW_IOP_MODULOS"
    Invoke-Native 'py' @('-3', '-I', (Join-Path $RepoRoot 'tools\iop\iopn_gen.py'), $env:GOW_IOP_MODULOS, $iopNativeDir)
}

# ── Sincronizar con el runner ──────────────────────────────────────────────
if (-not (Test-Path $GeneratedDir)) { throw 'No hay nada en generated/. Ejecuta sin -SyncOnly.' }

# Borrar lo que copiamos la vez anterior (por si cambiaron nombres de funciones)
if (Test-Path $SyncManifest) {
    Get-Content $SyncManifest | Where-Object { $_ -and (Test-Path $_) } | Remove-Item -Force
}
New-Item -ItemType Directory -Force (Split-Path $SyncManifest) | Out-Null

# El código generado incluye <ps2_recompiled_functions.h> desde src/runner, así que todo va ahí
$copied = New-Object System.Collections.Generic.List[string]
$sources = @(Get-ChildItem $GeneratedDir -File) + @(Get-ChildItem $OverridesDir -File | Where-Object { $_.Extension -in '.cpp', '.h' })
foreach ($file in $sources) {
    $dest = Join-Path $RunnerSrcDir $file.Name
    Copy-Item $file.FullName $dest -Force
    (Get-Item $dest).LastWriteTime = Get-Date   # que MSBuild no lo dé por compilado
    $copied.Add($dest)
}
Set-Content -Path $SyncManifest -Value $copied -Encoding utf8
Write-Host "Copiados $($copied.Count) archivos al runner de PS2Recomp."

# Reconfigurar CMake para que el proyecto del runner incluya los archivos nuevos. Si se deja
# a la regeneración automática durante el build, MSBuild compila con el proyecto viejo y
# el enlazado falla con miles de LNK2019.
$cmakeArgs = @('-S', $RecompDir, '-B', $BuildDir, (Get-AggressiveLogsDefine $AggressiveLogs))
$vu1Micro = if ($env:GOW_VU1_MICROCODIGO) { ConvertTo-ForwardSlash $env:GOW_VU1_MICROCODIGO } else { '' }
if ($vu1Micro) { Write-Host "VU1 nativa desde $vu1Micro" }
$cmakeArgs += "-DPS2X_VU1_MICROCODE_DIR=$vu1Micro"
$cmakeArgs += "-DPS2X_VU1_GENERATOR_SOURCE=$(if ($vu1Micro) { ConvertTo-ForwardSlash (Join-Path $RepoRoot 'tools\vu1\generar_vu1.cpp') })"
$cmakeArgs += "-DPS2X_VU1N_GEN_OVERRIDE=$(if ($vu1Micro) { ConvertTo-ForwardSlash (Join-Path $RepoRoot 'tools\vu1\vu1n_gen.py') })"
$cmakeArgs += "-DPS2X_VU1N_HEADS_FILE=$(if ($vu1Micro) { ConvertTo-ForwardSlash (Join-Path $RepoRoot 'tools\vu1\vu1n_heads.txt') })"
if (Test-Path (Join-Path $BuildDir 'CMakeCache.txt')) {
    Invoke-Native (Get-CMake) $cmakeArgs | Out-Null
}
Write-Host 'Siguiente paso: .\scripts\run.ps1 -Config RelWithDebInfo'
