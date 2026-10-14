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
scripts/ghidra-analyze.ps1 [-Reanalyze]  Ghidra headless (r5900) → config/gow.toml + gow_functions.csv (~1 min)
scripts/recompile.ps1 [-SyncOnly]        gow.toml + gow.manual.toml → out/gow.effective.toml → generated/ → runner; reconfigura CMake
scripts/run.ps1 [-Config] [-NoBuild]     compila ps2EntryRunner y arranca; log en out/logs/
```

- `config/gow.toml` y `config/gow_functions.csv` salen del export de Ghidra y **no se editan a mano**. Los cambios manuales (stubs, entry points, MMIO, parches) van en `config/gow.manual.toml`. Los tres archivos se suben al repo.
- Ghidra 12.1.4 + extensión ghidra-emotionengine-reloaded (procesador `r5900:LE:32:default`) + JDK 21. El proyecto vive en `ghidra/` (ignorado).
- Un ciclo completo (recompilar + compilar el runner Debug + arrancar) tarda unos 7–10 min en el PC de cxldsakeb (32 hilos).
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

- **Fase:** 3, primer arranque (las fases 1 y 2 están hechas en el PC de cxldsakeb; ver `docs/ROADMAP.md`).
- **Última sesión (2026-10-14, cxldsakeb):** pipeline completo funcionando: Ghidra → recompile (0 errores) → runner Debug → arranque. El juego carga, ejecuta los constructores, envía los primeros paquetes al GS y **se queda en un bucle en `FUN_0013dc78` (0x13DC78), llamado desde `FUN_0013fca8`**. No hay errores en el log, solo el PC dando vueltas. Detalles en `docs/NOTES.md` (Bloqueos).
- **SIGUIENTE PASO CONCRETO:** averiguar qué espera ese bucle.
  1. Abrir `FUN_0013dc78` y `FUN_0013fca8` en el descompilador de Ghidra (proyecto `ghidra/GodOfWar.gpr`, o crearlo con `scripts/ghidra-analyze.ps1`).
  2. Comparar con el juego real en el debugger de PCSX2: breakpoint en `0x13DC78` y ver qué valor hace salir del bucle.
  3. Mirar en el runtime (`PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/DMA.cpp`, `GS.cpp`) si falta completar el DMA/GIF o una interrupción (vsync, DMAC) que el juego espera.
  4. Arreglarlo con un override en `overrides/` o un binding en `config/gow.manual.toml`, repetir el ciclo y apuntar el resultado.
- **Build de referencia:** God of War USA, SCUS-97399 v1.00, ELF `SCUS_973.99`, SHA1 `64B10DB280304115DA4BC2066238D29023DF0A06`. Los datos completos están en `docs/NOTES.md`.
- **Importante:** el juego lee por sector (LBN) vía `GODOFWAR.TOC` y el DVD es de doble capa. El runtime usa la ISO completa como imagen de CD, configurada en `overrides/gow_overrides.cpp` a partir de `game/iso-path.txt`.
- **Hecho:**
  - Estructura del repo, scripts y docs. `build-tools.ps1` verificado (VS 2022 Build Tools, MSVC 14.44, CMake 3.31).
  - ISO extraída. Ghidra + extensión EE + JDK 21 instalados; `config/gow.toml` ya sale de Ghidra.
  - El runner compila (Debug, 5 min) y el juego arranca hasta el bucle de arriba.
- **Pendiente:**
  - Que DaniSBR1 y ErickFRD20 completen SETUP: pasos 1–5, Ghidra (paso 6) y un primer `recompile.ps1` + `run.ps1`. Lo deben reproducir igual: mismo SHA1, mismo bucle.
  - Repartir las áreas: análisis, runtime y gráficos.
  - Resolver el bucle de `FUN_0013dc78` (siguiente paso de arriba).
  - Revisar los tres stubs en conflicto (`StartThread`/`iWakeupThread`/`InitTLB`, ver NOTES.md).
  - Cambio pendiente de commit en el submódulo: ninguno (todo lo copiado al runner es local y está ignorado).
