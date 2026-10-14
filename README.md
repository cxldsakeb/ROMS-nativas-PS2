# God of War: port nativo a PC (PS2Recomp)

Proyecto para recompilar estáticamente **God of War (PS2, 2005)** a un ejecutable nativo de PC con [PS2Recomp](https://github.com/ran-j/PS2Recomp).

El código MIPS del juego se traduce a C++, se compila con un runtime que emula el resto del hardware de la PS2 y se ejecuta como programa de Windows, sin emulador.

> **Estado:** fase 0, montando el entorno. Ver [docs/ROADMAP.md](docs/ROADMAP.md).

## ⚠️ Regla número uno

**Nunca se sube al repo nada del juego**: ni la ISO, ni el ELF, ni archivos del disco, ni el C++ generado (que deriva del ELF). Cada uno usa su propia copia, en `game/`, y Git la ignora. Al repo solo van nuestro código, la configuración y la documentación.

## Inicio rápido

```powershell
git clone --recurse-submodules https://github.com/cxldsakeb/ROMS-nativas-PS2.git
cd ROMS-nativas-PS2
.\scripts\extract-iso.ps1 -IsoPath "ruta\a\God of War.iso"   # copia el disco a game/
.\scripts\build-tools.ps1                                     # compila ps2_analyzer / ps2_recomp
# (Ghidra → config/gow.toml + config/gow_functions.csv, ver docs/SETUP.md paso 6)
.\scripts\recompile.ps1                                       # ELF → C++ → runner
.\scripts\run.ps1                                             # compila y arranca; log en out/logs/
```

La guía completa, con requisitos y Ghidra, está en [docs/SETUP.md](docs/SETUP.md).

## Estructura

```
PS2Recomp/      submódulo: recompilador + runtime. Fork DaniSBR1/PS2Recomp, rama godofwar (original: ran-j/PS2Recomp)
config/         gow.toml + gow_functions.csv (config del recompilador, export de Ghidra)
overrides/      parches en C++ específicos de God of War (se copian al runner)
scripts/        extract-iso, build-tools, ghidra-analyze, recompile, run
tools/          scripts de análisis en Python (desensamblar el ELF y los IRX, buscar funciones que Ghidra no ve)
docs/           SETUP (instalación), ROADMAP (fases y reparto), NOTES (bitácora técnica)
game/           [local, ignorado] tu copia del disco
generated/      [local, ignorado] C++ generado por el recompilador
out/            [local, ignorado] builds y logs
```

## Cómo trabajamos

1. **Una rama por tarea** (`fase3/syscall-0x3c`, `re/cd-functions`…) y un **Pull Request** a `main`. Otro de los tres lo revisa antes de hacer merge.
2. **Todo hallazgo va a [docs/NOTES.md](docs/NOTES.md)**: direcciones identificadas, bloqueos y su solución. Si no está apuntado, se pierde.
3. **Un problema a la vez:** se arregla el primer bloqueo del log, se vuelve a probar desde cero y se apunta.
4. **Los parches temporales** (`ret0`/`ret1`) llevan un comentario `TEMPORAL` y una fila en NOTES.md. Hay que convertirlos en implementaciones reales antes de dar una fase por terminada.
5. **Cambios en PS2Recomp:** no se edita el submódulo a lo loco. Los arreglos al runtime van a nuestro fork (`DaniSBR1/PS2Recomp`, rama `godofwar`), con un commit propio dentro de `PS2Recomp/`, y luego se actualiza el puntero del submódulo en este repo. Los que sirvan para todos los juegos se proponen al proyecto original.

## Licencia

PS2Recomp es GPLv3; el código de este repo que se enlaza con él también lo es. God of War es propiedad de Sony Interactive Entertainment. Este proyecto no contiene ni distribuye ningún archivo del juego.
