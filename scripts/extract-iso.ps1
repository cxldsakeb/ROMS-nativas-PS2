<#
.SYNOPSIS
    Extrae el disco de God of War a game/disc/ (las dos capas, con su mapa de sectores) y detecta el ELF.

.DESCRIPTION
    El runtime de la base de Waffle565 lee los datos del juego de los archivos extraídos junto al ELF, cada
    uno en su sector original: tools/iso_extract.py --layout escribe además game/disc/.disc_layout.
    Lee la ISO directamente, sin montarla, así que también saca la capa 1 del DVD (Windows solo monta la 0).
    Después copia los IRX a game/disc/IOP_MOD/, que es donde el juego los busca.

.EXAMPLE
    .\scripts\extract-iso.ps1 -IsoPath "E:\Juegos\God of War (USA).iso"
#>
param(
    [Parameter(Mandatory)][string]$IsoPath
)
. "$PSScriptRoot\_common.ps1"

$iso = (Resolve-Path $IsoPath).Path
New-Item -ItemType Directory -Force $DiscDir | Out-Null
Write-Host "Extrayendo $iso en $DiscDir (1–2 minutos) ..."
Invoke-Native 'py' @('-3', '-I', (Join-Path $RepoRoot 'tools\iso_extract.py'), $iso, $DiscDir, '--layout')

$cnfPath = Join-Path $DiscDir 'SYSTEM.CNF'
if (-not (Test-Path $cnfPath)) { throw "No hay SYSTEM.CNF en $DiscDir. ¿Seguro que es una ISO de PS2?" }
$cnf = Get-Content $cnfPath -Raw
if ($cnf -notmatch 'BOOT2\s*=\s*cdrom0:\\([^;\s]+)') { throw "No pude leer BOOT2 de SYSTEM.CNF:`n$cnf" }
$elfName = $Matches[1]

# El juego carga sus módulos como IOP_MOD/xxx.IRX relativo al ELF, pero en el disco están en la raíz
$iopMod = Join-Path $DiscDir 'IOP_MOD'
New-Item -ItemType Directory -Force $iopMod | Out-Null
Get-ChildItem $DiscDir -Filter '*.IRX' -File | Copy-Item -Destination $iopMod -Force

Set-Content -Path $ElfNameFile -Value $elfName -Encoding ascii
Write-Utf8NoBom $IsoPathFile $iso
$elfPath = Join-Path $DiscDir $elfName
$sha1 = (Get-FileHash $elfPath -Algorithm SHA1).Hash

Write-Host ''
Write-Host "Listo. ELF: $elfPath"
Write-Host "SHA1 del ELF: $sha1"
Write-Host 'Compara este SHA1 con el de docs/NOTES.md: todos tenemos que usar exactamente el mismo ELF.'
