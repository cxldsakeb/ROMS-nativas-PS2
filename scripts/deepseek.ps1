<#
.SYNOPSIS
    Encarga una tarea de solo lectura a DeepSeek (gratis, vía OpenCode) y guarda su respuesta.

.DESCRIPTION
    Copia la tarea y los archivos de entrada a una carpeta aislada fuera del repo (%TEMP%\gow-deepseek\<tarea>-<fecha>\) y ejecuta
    OpenCode en modo "plan" (solo lectura) dentro de ella. La respuesta queda en salida.md, en esa misma carpeta.
    Sirve para trabajo de mucho volumen (resumir, clasificar, cruzar documentos); el resultado se revisa siempre
    antes de usarlo. Ver "Tareas para DeepSeek" en CLAUDE.md.

    Regla 1: el servicio es externo y gratuito (puede guardar lo que recibe), así que el script se niega a enviar
    nada derivado del juego: game/, generated/, out/, ghidra/, la carpeta runner/ del
    runtime (lleva copias de generated/) ni volcados, imágenes o logs.

.PARAMETER Tarea
    Archivo .md con las instrucciones y el formato exacto de la respuesta.

.PARAMETER Archivos
    Archivos o carpetas de entrada (solo material público: nuestro repo, el fork, PCSX2, KIexster...).

.PARAMETER Modelo
    Modelo de OpenCode (por defecto, DeepSeek gratis). `opencode models` lista los disponibles.

.EXAMPLE
    .\scripts\deepseek.ps1 -Tarea tarea.md -Archivos docs\NOTES.md, D:\ref\ESTADO.md
#>
param(
    [Parameter(Mandatory)][string]$Tarea,
    [string[]]$Archivos = @(),
    [string]$Modelo = 'opencode/deepseek-v4.1-flash-free'
)
. "$PSScriptRoot\_common.ps1"

# npm deja opencode.cmd y opencode.ps1; Start-Process solo puede lanzar el .cmd (o un .exe)
$opencode = Get-Command opencode.cmd, opencode.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $opencode) { throw 'No se encontró OpenCode. Instálalo con: npm install -g opencode-ai (ver docs/SETUP.md).' }

# Rutas y extensiones que nunca salen del PC (regla 1)
$forbiddenDirs = @($GameDir, $GeneratedDir, (Join-Path $RepoRoot 'out'), (Join-Path $RepoRoot 'ghidra'), $RunnerSrcDir)
# Fuera del repo: dentro, OpenCode toma la raíz de git como proyecto y ve (y carga) el resto de archivos
$workRoot = Join-Path $env:TEMP 'gow-deepseek'
$forbiddenExt = @('.elf', '.irx', '.iso', '.bin', '.img', '.wad', '.pak', '.toc', '.log', '.png', '.jpg', '.bmp', '.raw', '.dmp')

function Assert-Public([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    foreach ($dir in $forbiddenDirs) {
        $root = [IO.Path]::GetFullPath($dir).TrimEnd('\')
        if ($full.TrimEnd('\') -eq $root -or $full.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or
            $root.StartsWith($full.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Regla 1: $full está en $dir y puede contener datos del juego. No se envía."
        }
    }
    $items = if (Test-Path $full -PathType Container) { Get-ChildItem $full -Recurse -File } else { Get-Item $full }
    foreach ($item in $items) {
        if ($forbiddenExt -contains $item.Extension.ToLowerInvariant() -or $item.Name -match '^[A-Z]{4}_\d{3}\.\d{2}$') {
            throw "Regla 1: $($item.FullName) parece un archivo del juego, un volcado o un log. No se envía."
        }
    }
}

if (-not (Test-Path $Tarea -PathType Leaf)) { throw "No existe la tarea $Tarea." }
Assert-Public $Tarea
foreach ($a in $Archivos) {
    if (-not (Test-Path $a)) { throw "No existe $a." }
    Assert-Public $a
}

$name = [IO.Path]::GetFileNameWithoutExtension($Tarea)
$work = Join-Path $workRoot ("{0}-{1:yyyyMMdd-HHmmss-fff}" -f $name, (Get-Date))
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item $Tarea (Join-Path $work 'TAREA.md')
foreach ($a in $Archivos) { Copy-Item $a $work -Recurse }

$salida = Join-Path $work 'salida.md'
$progreso = Join-Path $work 'progreso.txt'
$mensaje = 'Haz la tarea descrita en TAREA.md (adjunto). Los archivos de entrada están en el directorio actual.'
# Start-Process guarda la salida tal cual (UTF-8); un redirect de PowerShell 5.1 rompería los acentos.
$argList = "run `"$mensaje`" -m $Modelo --agent plan --dir `"$work`" -f `"$(Join-Path $work 'TAREA.md')`""
Write-Host "DeepSeek trabajando en $work ..."
$t0 = Get-Date
# Entrada vacía: OpenCode añade al mensaje lo que llegue por stdin, y en un Start-Job esa entrada nunca se cierra
$vacio = Join-Path $work 'stdin-vacio.txt'
New-Item -ItemType File $vacio | Out-Null
$p = Start-Process -FilePath $opencode.Source -ArgumentList $argList -WorkingDirectory $work -NoNewWindow -Wait -PassThru `
    -RedirectStandardInput $vacio -RedirectStandardOutput $salida -RedirectStandardError $progreso
$secs = [int]((Get-Date) - $t0).TotalSeconds
if ($p.ExitCode -ne 0 -or (Get-Item $salida).Length -eq 0) {
    throw "OpenCode terminó con código $($p.ExitCode) en $secs s. Mira $progreso."
}
Write-Host "Hecho en $secs s: $salida"
Write-Host 'Revisa el resultado antes de usarlo: DeepSeek puede equivocarse con seguridad.'
