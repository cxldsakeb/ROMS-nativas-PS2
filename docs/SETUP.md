# Instalación del entorno (Windows)

Hay que seguir estos pasos **una vez por persona**. Todos los comandos se ejecutan en PowerShell, desde la raíz del repo.

## 1. Clonar el repo (con el submódulo)

```powershell
git clone --recurse-submodules https://github.com/cxldsakeb/ROMS-nativas-PS2.git
cd ROMS-nativas-PS2
```

Si ya lo tenías clonado sin submódulos: `git submodule update --init --recursive`.

**El submódulo apunta a nuestro fork** (`DaniSBR1/PS2Recomp`, rama `godofwar`), que lleva arreglos del IOP que el original no tiene. Si clonaste antes del 2026-10-14, cuando apuntaba a `ran-j/PS2Recomp`, actualiza la URL una vez:

```powershell
git pull
git submodule sync --recursive
git submodule update --init --recursive
```

Sin esto el juego se queda colgado en la RPC de sonido (`0x26C0E8`).

## 2. Compilador de C++ y CMake

Hace falta **Visual Studio 2022** (Community o Build Tools) con la carga de trabajo *Desarrollo para el escritorio con C++*, que incluye MSVC y CMake:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Los scripts encuentran solos el CMake que trae Visual Studio. Si usas otra versión de VS, pásala con `.\scripts\build-tools.ps1 -Generator "<generador>"`.

## 3. Permitir ejecutar los scripts

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

## 4. Extraer tu copia del juego

```powershell
.\scripts\extract-iso.ps1 -IsoPath "ruta\a\God of War.iso"
```

- Extrae las dos capas del DVD a `game/disc/` con `tools/iso_extract.py --layout` (necesita Python 3), escribe el mapa de sectores `game/disc/.disc_layout`, copia los IRX a `game/disc/IOP_MOD/` y detecta el ELF principal (en la versión USA es `SCUS_973.99`).
- Al final imprime el **SHA1 del ELF**. Compáralo con el de [NOTES.md](NOTES.md). Si no coincide, tienes otra región o revisión y las direcciones del proyecto **no te van a servir**.
- Solo funciona con `.iso`. Si tu copia es `.bin/.cue` u otro formato, conviértela antes a ISO.
- El juego lee los datos de `game/disc/`, no de la ISO. Si tenías el disco extraído con el formato anterior (sin `.disc_layout`), vuelve a ejecutar el script.

## 5. Compilar las herramientas de PS2Recomp

```powershell
.\scripts\build-tools.ps1
```

La primera vez tarda varios minutos. Genera `ps2_analyzer.exe` y `ps2_recomp.exe` en `out/build/`.

## 6. Ghidra + extensión Emotion Engine (opcional)

**Desde el 2026-10-15 ya no hace falta para compilar:** la configuración del recompilador es `config/recomp.template.toml` + `config/funcmap.csv` (de KIexster). Ghidra sigue sirviendo para investigar funciones, y lo que salía de él está en `config/anterior/`.

1. **Java 21**: `winget install EclipseAdoptium.Temurin.21.JDK`
2. **Ghidra 12.1.4**: descárgalo de https://github.com/NationalSecurityAgency/ghidra/releases y descomprímelo (por ejemplo en `C:\ghidra`).
3. **Extensión Emotion Engine** (el procesador de PS2, con soporte de MMI/VU0, que el MIPS normal de Ghidra no decodifica bien): descarga de https://github.com/chaoticgd/ghidra-emotionengine-reloaded/releases el zip **de tu versión exacta de Ghidra** (`ghidra_12.1.4_PUBLIC_..._ghidra-emotionengine-reloaded.zip`). En Ghidra: *File → Install Extensions → +*, elige el zip y reinicia Ghidra.
   La forma rápida: descomprime el zip directamente en `C:\ghidra\ghidra_12.1.4_PUBLIC\Ghidra\Extensions\`.
4. Analiza el ELF y exporta la config **sin abrir la interfaz** (tarda alrededor de 1 minuto):

   ```powershell
   .\scripts\ghidra-analyze.ps1          # Ghidra en otra carpeta: -GhidraDir "ruta" o $env:GHIDRA_INSTALL_DIR
   ```

   El script crea el proyecto en `ghidra/` (ignorado por Git) y escribe `config/gow.toml` + `config/gow_functions.csv`. Si el proyecto ya existe, solo vuelve a exportar; `-Reanalyze` empieza de cero.
5. Para investigar funciones con el descompilador, abre la interfaz (`C:\ghidra\ghidra_12.1.4_PUBLIC\ghidraRun.bat`) y en ella el proyecto `ghidra/GodOfWar.gpr`. Si corriges límites de función, vuelve a ejecutar `ghidra-analyze.ps1` (con Ghidra cerrado) para reexportar.

## 7. Recompilar y arrancar

```powershell
.\scripts\recompile.ps1                        # ELF → C++ (generated/) → runner de PS2Recomp
.\scripts\run.ps1 -Config RelWithDebInfo       # compila ps2EntryRunner y arranca el juego; log en out/logs/
```

- La primera compilación del runner con todo el código de God of War **va a tardar bastante** (20–40 min).
- Si solo cambiaste algo en `overrides/`: `.\scripts\recompile.ps1 -SyncOnly` y luego `.\scripts\run.ps1`.

## 7b. VU1 e IOP nativos (de 7–8 a ~60 FPS)

Sin esto el juego funciona, pero a 7–8 FPS: la VU1 se interpreta. Los datos salen del juego, así que cada uno los captura en su PC y **nunca se suben**.

```powershell
# 1. Capturar (una partida de ~3 min: menú → New Game → barco). Las carpetas tienen que existir.
New-Item -ItemType Directory -Force out\capturas\vu1, out\capturas\iop
$env:GOW_VU1_CAPTURA = "$PWD\out\capturas\vu1"       # microprogramas de la VU1
$env:GOW_VU1_NATIVE_OFF = '1'                         # sin esto no captura nada
$env:PS2X_IOP_DUMP_MODULES = "$PWD\out\capturas\iop"  # los 8 IRX del juego
$env:GOW_PAD_TEST = '1'                               # mando automático: START y ✕ hasta la partida
.\scripts\run.ps1 -Config RelWithDebInfo -NoBuild    # ciérralo a los ~3 min
Remove-Item Env:GOW_VU1_CAPTURA, Env:GOW_VU1_NATIVE_OFF, Env:PS2X_IOP_DUMP_MODULES, Env:GOW_PAD_TEST

# 2. Generar y compilar con ellos
$env:GOW_VU1_MICROCODIGO = "$PWD\out\capturas\vu1"
$env:GOW_IOP_MODULOS = "$PWD\out\capturas\iop"
.\scripts\recompile.ps1 -SyncOnly
.\scripts\run.ps1 -Config RelWithDebInfo
```

- Con `$env:GOW_VU1_STATS = '1'`, el log dice cuántos pares de la VU1 se interpretan: con todo capturado, 0.
- Memory card: `$env:GOW_MC0 = "$PWD\game\disc\play.mcd"` (se crea sola). Ver "Bloqueos" en NOTES.md: el primer guardado tras formatear dejó el juego parado, aunque la partida se guardó bien.

## 8. Herramientas de apoyo recomendadas

- **PCSX2** (con su debugger): ejecuta el juego real, pon breakpoints en direcciones y mira qué devuelve una función. Es la forma más rápida de saber cómo *debería* comportarse algo que en el port falla.
- **ripgrep** (`winget install BurntSushi.ripgrep.MSVC`): sirve para buscar direcciones en `generated/`, que tiene miles de archivos.
- **Python 3 + `capstone` + `pyelftools`** (`py -3 -m pip install capstone pyelftools`): para los scripts de `tools/` (ver `tools/README.md`).
- **OpenCode** (`npm install -g opencode-ai`, necesita Node.js): para encargar a DeepSeek, gratis, tareas de lectura de mucho volumen con `scripts/deepseek.ps1`. Solo material público; ver "Tareas para DeepSeek" en `CLAUDE.md`.
- **Discord de PS2Recomp**: https://discord.gg/JQ8mawxUEf
- **PS2Tek** (documentación del hardware): https://psi-rockin.github.io/ps2tek/
