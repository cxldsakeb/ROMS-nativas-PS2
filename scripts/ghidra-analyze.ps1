<#
.SYNOPSIS
    Analiza el ELF con Ghidra en modo headless (sin interfaz) y exporta config/gow.toml + config/gow_functions.csv.

.DESCRIPTION
    Importa game/disc/<ELF> con el procesador Emotion Engine (r5900), ejecuta el análisis automático
    y corre ExportPS2Functions.java de PS2Recomp. El proyecto de Ghidra queda en ghidra/ (ignorado
    por Git) y se puede abrir luego con la interfaz para investigar funciones.

    OJO: sobrescribe config/gow.toml. Los cambios hechos a mano (entry_points, stubs, patches)
    hay que volver a añadirlos después: revisa "git diff config/gow.toml".

.PARAMETER GhidraDir
    Carpeta de instalación de Ghidra. Por defecto $env:GHIDRA_INSTALL_DIR o C:\ghidra\ghidra_12.1.4_PUBLIC.

.PARAMETER Reanalyze
    Borra el proyecto de Ghidra existente y vuelve a importar y analizar desde cero.
#>
param(
    [string]$GhidraDir = $(if ($env:GHIDRA_INSTALL_DIR) { $env:GHIDRA_INSTALL_DIR } else { 'C:\ghidra\ghidra_12.1.4_PUBLIC' }),
    [switch]$Reanalyze
)
. "$PSScriptRoot\_common.ps1"

$headless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'
if (-not (Test-Path $headless)) { throw "No encuentro Ghidra en $GhidraDir (ver docs/SETUP.md, paso 6)." }

if (-not $env:JAVA_HOME) {
    $jdk = Get-ChildItem 'C:\Program Files\Eclipse Adoptium' -Directory -Filter 'jdk-21*' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($jdk) { $env:JAVA_HOME = $jdk.FullName }
}

$elfPath = Get-GameElfPath
$elfName = Split-Path $elfPath -Leaf
$projectDir = Join-Path $RepoRoot 'ghidra'
$projectName = 'GodOfWar'
$scriptDir = Join-Path $RecompDir 'ps2xRecomp\tools\ghidra'
$csvPath = Join-Path $RepoRoot 'config\gow_functions.csv'
New-Item -ItemType Directory -Force $projectDir, $LogDir | Out-Null

$projectExists = Test-Path (Join-Path $projectDir "$projectName.gpr")
if ($Reanalyze -and $projectExists) {
    Remove-Item (Join-Path $projectDir "$projectName.gpr"), (Join-Path $projectDir "$projectName.rep") -Recurse -Force
    $projectExists = $false
}

$log = Join-Path $LogDir 'ghidra-headless.log'
if ($projectExists) {
    Write-Host 'Proyecto de Ghidra existente: solo se vuelve a exportar (usa -Reanalyze para empezar de cero).'
    $ghidraArgs = @($projectDir, $projectName, '-process', $elfName, '-noanalysis')
}
else {
    Write-Host 'Importando y analizando el ELF (la primera vez tarda bastante) ...'
    $ghidraArgs = @($projectDir, $projectName, '-import', $elfPath, '-processor', 'r5900:LE:32:default')
}
# En headless, los askFile() del script de export se responden con estos argumentos, en orden
$ghidraArgs += @('-scriptPath', $scriptDir, '-postScript', 'ExportPS2Functions.java', $ConfigToml, $csvPath, '-log', $log)

Invoke-Native $headless $ghidraArgs

Write-Host ''
Write-Host "Exportados: $ConfigToml y $csvPath"
Write-Host 'Siguiente paso: revisa "git diff config/gow.toml", vuelve a añadir los cambios manuales y ejecuta .\scripts\recompile.ps1'
