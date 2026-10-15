# Rutas y helpers compartidos por todos los scripts. No se ejecuta solo: se carga con ". _common.ps1".
$ErrorActionPreference = 'Stop'

$RepoRoot     = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$RecompDir    = Join-Path $RepoRoot 'PS2Recomp'
$BuildDir     = Join-Path $RepoRoot 'out\build'
$LogDir       = Join-Path $RepoRoot 'out\logs'
$GameDir      = Join-Path $RepoRoot 'game'
$DiscDir      = Join-Path $GameDir 'disc'
$ElfNameFile  = Join-Path $GameDir 'elf-name.txt'
$IsoPathFile  = Join-Path $GameDir 'iso-path.txt'
$ConfigToml   = Join-Path $RepoRoot 'config\gow.toml'
$ManualToml   = Join-Path $RepoRoot 'config\gow.manual.toml'
$EffectiveToml = Join-Path $RepoRoot 'out\gow.effective.toml'
$GeneratedDir = Join-Path $RepoRoot 'generated'
$OverridesDir = Join-Path $RepoRoot 'overrides'
$RunnerSrcDir = Join-Path $RecompDir 'ps2xRuntime\src\runner'
$RunnerIncDir = Join-Path $RecompDir 'ps2xRuntime\include'
$SyncManifest = Join-Path $RepoRoot 'out\synced-files.txt'

# AGRESSIVE_LOGS del runtime: apagado salvo que se pida (cuesta ~11 % del tiempo del juego)
function Get-AggressiveLogsDefine([bool]$Enabled) {
    if ($Enabled) { return '-DPS2X_ENABLE_AGRESSIVE_LOGS=ON' }
    return '-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF'
}

function Get-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    # CMake que viene con Visual Studio / Build Tools (componente "C++ CMake tools for Windows")
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $installs = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        foreach ($path in $installs) {
            $exe = Join-Path $path 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $exe) { return $exe }
        }
    }
    throw 'No se encontró CMake. Instálalo siguiendo docs/SETUP.md.'
}

function Get-GameElfPath {
    if (-not (Test-Path $ElfNameFile)) {
        throw 'No hay juego extraído. Ejecuta primero: .\scripts\extract-iso.ps1 -IsoPath <ruta al .iso>'
    }
    $name = (Get-Content $ElfNameFile -TotalCount 1).Trim()
    return Join-Path $DiscDir $name
}

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object Text.UTF8Encoding $false))
}

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    # Solo cuenta el código de salida: en PowerShell 5.1 los avisos en stderr abortarían con 'Stop'
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Falló: $Exe $($Arguments -join ' ') (código $LASTEXITCODE)" }
}
