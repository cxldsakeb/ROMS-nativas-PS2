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

- Copia el DVD entero a `game/disc/` (unos 4–8 GB) y detecta el ELF principal (en la versión USA es `SCUS_973.99`).
- Al final imprime el **SHA1 del ELF**. Compáralo con el de [NOTES.md](NOTES.md). Si no coincide, tienes otra región o revisión y las direcciones del proyecto **no te van a servir**.
- Solo funciona con `.iso`. Si tu copia es `.bin/.cue` u otro formato, conviértela antes a ISO.
- **No muevas ni borres la ISO después.** God of War lee sectores del DVD en crudo y es de doble capa, así que el runtime necesita la ISO completa. Su ruta se guarda en `game/iso-path.txt`; si la mueves, vuelve a ejecutar el script.

## 5. Compilar las herramientas de PS2Recomp

```powershell
.\scripts\build-tools.ps1
```

La primera vez tarda varios minutos. Genera `ps2_analyzer.exe` y `ps2_recomp.exe` en `out/build/`.

## 6. Ghidra + extensión Emotion Engine

Basta con que lo instale **una persona** (quien se encargue del análisis), aunque viene bien que lo tengáis todos para investigar funciones.

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

Los cambios manuales a la config **no** se hacen en `gow.toml`, sino en `config/gow.manual.toml` (ver [config/README.md](../config/README.md)).

## 7. Recompilar y arrancar

```powershell
.\scripts\recompile.ps1     # ELF → C++ (generated/) → runner de PS2Recomp
.\scripts\run.ps1           # compila ps2EntryRunner y arranca el juego; log en out/logs/
```

- La primera compilación del runner con todo el código de God of War **va a tardar bastante**.
- Si solo cambiaste algo en `overrides/`: `.\scripts\recompile.ps1 -SyncOnly` y luego `.\scripts\run.ps1`.

## 8. Herramientas de apoyo recomendadas

- **PCSX2** (con su debugger): ejecuta el juego real, pon breakpoints en direcciones y mira qué devuelve una función. Es la forma más rápida de saber cómo *debería* comportarse algo que en el port falla.
- **ripgrep** (`winget install BurntSushi.ripgrep.MSVC`): sirve para buscar direcciones en `generated/`, que tiene miles de archivos.
- **Python 3 + `capstone` + `pyelftools`** (`py -3 -m pip install capstone pyelftools`): para los scripts de `tools/` (ver `tools/README.md`).
- **Discord de PS2Recomp**: https://discord.gg/JQ8mawxUEf
- **PS2Tek** (documentación del hardware): https://psi-rockin.github.io/ps2tek/
