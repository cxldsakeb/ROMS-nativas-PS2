# Contexto para Claude: port nativo de God of War (PS2 → PC)

Este archivo lo carga Claude Code automáticamente. Es el **hilo compartido** entre las sesiones de Claude de todo el equipo: lo que no esté aquí o en `docs/` no lo sabe el Claude de los demás.

## Proyecto

- **Objetivo:** recompilar estáticamente God of War (PS2, 2005) a un ejecutable nativo de Windows con [PS2Recomp](https://github.com/ran-j/PS2Recomp). Está en el submódulo `PS2Recomp/` y apunta al repo original.
- **Equipo:** @cxldsakeb (dueño del repo), @DaniSBR1, @ErickFRD20. Están aprendiendo desde cero: Claude hace de guía, así que explica el porqué de las cosas y no solo el qué.
- **Idioma:** todo en español (conversación, docs, comentarios y commits).
- **Repo:** privado, https://github.com/cxldsakeb/ROMS-nativas-PS2

## Reglas que no se rompen

1. **Nunca se sube nada del juego:** ni ISO, ni ELF, ni archivos del disco, ni el C++ de `generated/` (deriva del ELF). Todo eso es local y está en `.gitignore`. No propongas saltarte esta regla.
2. **Las direcciones solo valen para el ELF de referencia**, que se identifica por el SHA1 apuntado en `docs/NOTES.md`. Antes de dar por buena una dirección, comprueba que el SHA1 del ELF local coincide.
3. **Todo hallazgo va a `docs/NOTES.md`:** funciones identificadas, bloqueos y soluciones. Es la memoria compartida del proyecto. Al terminar una tarea, actualízalo en la misma rama.
4. **Los parches temporales** (`ret0`/`ret1`/`reta0`) llevan un comentario `TEMPORAL` en el código y una fila en NOTES.md.
5. **No se editan a mano archivos dentro de `PS2Recomp/`** sin avisar. Si hace falta cambiar el runtime, el plan es hacer un fork de PS2Recomp, apuntar el submódulo al fork y mandar al proyecto original los arreglos genéricos.

## Estructura y flujo

```
scripts/extract-iso.ps1 -IsoPath <iso>   ISO → game/disc/ (+ game/elf-name.txt, imprime el SHA1)
scripts/build-tools.ps1                  compila ps2_analyzer y ps2_recomp (Release, VS 2022)
scripts/recompile.ps1 [-SyncOnly]        config/gow.toml → generated/ → copia a PS2Recomp/ps2xRuntime/src/runner
scripts/run.ps1 [-Config] [-NoBuild]     compila ps2EntryRunner y arranca; log en out/logs/
```

- `config/gow.toml` y `config/gow_functions.csv` salen del export de Ghidra (`PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`) y **sí** se suben al repo. `recompile.ps1` reescribe sus rutas a relativas.
- Los parches específicos del juego van en `overrides/*.cpp`, con `PS2_REGISTER_GAME_OVERRIDE`. Mejor un archivo por área (`gow_cd.cpp`, `gow_gs.cpp`…) para evitar conflictos de merge.
- El runtime toma como `cdrom0:` la carpeta donde está el ELF (`game/disc/`).
- Guía de depuración de PS2Recomp para juegos sin símbolos: el wiki de PS2Recomp, página "PS2Recomp Stripped Game Walkthrough". El bucle es: ejecutar, arreglar el **primer** bloqueo del log y repetir.
- Los scripts están en PowerShell 5.1 y se guardan en UTF-8 **con BOM**; sin él, los acentos se rompen.

## Cómo colaboramos

- Una rama por tarea y PR a `main`, que revisa otro miembro del equipo.
- Antes de empezar: `git pull` y `git submodule update --init`, y lee `docs/NOTES.md` y `docs/ROADMAP.md` para saber en qué fase estamos y qué hicieron los demás.
- Al terminar: actualiza NOTES.md y, si cambia la fase, la sección **Estado actual** de abajo.

## Estado actual

> Actualizar esta sección al cerrar cada hito, para que la vean las sesiones de todos.

- **Fase:** 0, entorno (ver `docs/ROADMAP.md`).
- **Hecho:** estructura del repo, scripts y docs. `build-tools.ps1` verificado en el PC de cxldsakeb (VS 2022 Build Tools, MSVC 14.44, CMake 3.31).
- **Pendiente:**
  - Extraer la ISO y apuntar el SHA1 de referencia (región aún sin confirmar; USA = SCUS-97399).
  - Que DaniSBR1 y ErickFRD20 completen SETUP hasta el paso 5.
  - Repartir las áreas: análisis, runtime y gráficos.
  - Análisis en Ghidra para generar `config/gow.toml`.
