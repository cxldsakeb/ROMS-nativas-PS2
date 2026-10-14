<#
.SYNOPSIS
    Monta la ISO de God of War, copia el disco entero a game/disc/ y detecta el ELF principal.

.EXAMPLE
    .\scripts\extract-iso.ps1 -IsoPath "E:\Juegos\God of War (USA).iso"
#>
param(
    [Parameter(Mandatory)][string]$IsoPath
)
. "$PSScriptRoot\_common.ps1"

$iso = (Resolve-Path $IsoPath).Path
Write-Host "Montando $iso ..."
$image = Mount-DiskImage -ImagePath $iso -PassThru
try {
    $drive = ($image | Get-Volume).DriveLetter
    if (-not $drive) { throw 'Windows no asignó letra a la ISO montada.' }
    $src = "${drive}:\"

    $cnfPath = Join-Path $src 'SYSTEM.CNF'
    if (-not (Test-Path $cnfPath)) { throw "No hay SYSTEM.CNF en $src. ¿Seguro que es una ISO de PS2?" }
    $cnf = Get-Content $cnfPath -Raw
    if ($cnf -notmatch 'BOOT2\s*=\s*cdrom0:\\([^;\s]+)') { throw "No pude leer BOOT2 de SYSTEM.CNF:`n$cnf" }
    $elfName = $Matches[1]

    Write-Host "ELF principal: $elfName"
    Write-Host "Copiando el disco a $DiscDir (puede tardar varios minutos) ..."
    New-Item -ItemType Directory -Force $DiscDir | Out-Null
    robocopy $src $DiscDir /E /NFL /NDL /NJH /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy falló (código $LASTEXITCODE)" }
    $global:LASTEXITCODE = 0
}
finally {
    Dismount-DiskImage -ImagePath $iso | Out-Null
}

# Los archivos copiados de una ISO quedan como solo-lectura
Get-ChildItem $DiscDir -Recurse -File | ForEach-Object { $_.IsReadOnly = $false }

Set-Content -Path $ElfNameFile -Value $elfName -Encoding ascii
# God of War lee por sector y el DVD es de doble capa (Windows solo monta la capa 0):
# el runtime necesita la ISO completa, y el override la busca aquí.
Write-Utf8NoBom $IsoPathFile $iso
$elfPath = Join-Path $DiscDir $elfName
$sha1 = (Get-FileHash $elfPath -Algorithm SHA1).Hash

Write-Host ''
Write-Host "Listo. ELF: $elfPath"
Write-Host "SHA1 del ELF: $sha1"
Write-Host 'Compara este SHA1 con el de docs/NOTES.md: todos tenemos que usar exactamente el mismo ELF.'
