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

- **Fase:** 4 (IOP, módulos y archivos). La fase 3 se considera hecha: el juego pasa toda su inicialización de EE e IOP hasta la primera RPC de sonido. Ver `docs/ROADMAP.md`.
- **Última sesión (2026-10-14, cxldsakeb):** resueltos dos bloqueos seguidos.
  1. **Bucle de `0x27A768`:** era `_CdCheckSCmd` de libcdvd esperando el servidor de CD `0x80000593`, que el runtime no emula. La única función que lo necesitaba era `sceCdReadDvdDualInfo` (`0x27AB00`), sin stub en el runtime: reimplementada en `overrides/gow_cd.cpp` (capa 1 en el sector 2 080 560, detectada en la ISO).
  2. **El juego no se creía arrancado desde el disco:** el `SetupThread` de PS2Recomp no rellena `argc`/`argv`, así que tomaba la ruta de desarrollo (`IOP_MOD/x.irx`), fallaban 4 IRX y `sceDbcInit` esperaba para siempre. `overrides/gow_boot.cpp` pasa `argv[0] = "cdrom0:\SCUS_973.99;1"` a `main`. **Ahora los 8 IRX se cargan desde el disco en el IOP emulado.**
  - **Nuevo bloqueo:** `pc=0x26c0e8`, en `FUN_0026bf28`, que espera la respuesta de un `sceSifCallRpc`. Antes, `989NOMID.IRX` (sonido) avisa en el IOP: `"failed to allocate memory, probably going to hang soon!"`. Detalles en `docs/NOTES.md`.
- **SIGUIENTE PASO CONCRETO:** averiguar qué RPC espera `FUN_0026bf28`.
  1. Ver a qué cliente/SID va el `sceSifCallRpc` de `0x26C0D4` y qué comprueba `FUN_0026bb98`.
  2. Ver por qué 989snd no puede reservar memoria en el IOP (`ps2xIOP`, `allocateMemory`/sysmem): ¿tamaño de la RAM del IOP o lo que ocupan los módulos cargados?
  3. Si es el sonido y no se arregla pronto, valorar un HLE mínimo de su RPC para seguir avanzando (TEMPORAL).
- **Herramienta útil:** los archivos de `generated/` llevan la dirección en el nombre (`FUN_0027a6b8_0x27a6b8.cpp`) y cada instrucción en un comentario `// 0x…`. Con `grep -E "^\s*// 0x"` se lee el ensamblador sin abrir Ghidra. Buscar en todo `generated/` tarda minutos: filtrad por prefijo de archivo.
- **Diagnóstico de memoria:** `$env:GOW_HEAP_DEBUG = "1"` antes de `run.ps1` activa `overrides/gow_heap_debug.cpp`, que registra cada `heap_create` y avisa si se rompe la lista de libres de un heap. Cualquier stub del runtime que use `guestMalloc` puede volver a pisar memoria del juego.
- **Build de referencia:** God of War USA, SCUS-97399 v1.00, ELF `SCUS_973.99`, SHA1 `64B10DB280304115DA4BC2066238D29023DF0A06`. Los datos completos están en `docs/NOTES.md`.
- **Importante:** el juego lee por sector (LBN) vía `GODOFWAR.TOC` y el DVD es de doble capa. El runtime usa la ISO completa como imagen de CD, configurada en `overrides/gow_overrides.cpp` a partir de `game/iso-path.txt`.
- **Overrides en unity build:** CMake junta los `.cpp` del runner en bloques (`unity_N_cxx.cxx`), así que los `namespace { }` de varios overrides pueden acabar en el mismo bloque. Usad nombres con prefijo por archivo (`kMem…`, `g_mem…`) para no chocar.
- **Hecho:**
  - Estructura del repo, scripts y docs. `build-tools.ps1` verificado (VS 2022 Build Tools, MSVC 14.44, CMake 3.31).
  - ISO extraída. Ghidra + extensión EE + JDK 21 instalados; `config/gow.toml` ya sale de Ghidra.
  - El runner compila (Debug, 5 min) y el juego arranca.
  - DaniSBR1 completó SETUP y lo reproduce igual: mismo SHA1, mismo export de Ghidra y misma recompilación (0 errores, 3587 avisos). Runner Debug en unos 20 min con 12 hilos; con `recompile.ps1 -SyncOnly`, si solo cambian los overrides, recompilar y enlazar tarda alrededor de 1 min. Ghidra funciona también con JDK 23 (`$env:JAVA_HOME`).
  - Sistema de memoria del juego identificado (`heap_create`, malloc, free, pila de heaps); ver NOTES.md.
  - Bucle de `FUN_0013dc78` resuelto (`overrides/gow_memory.cpp`).
  - Bucle de libcdvd en `0x27A768` resuelto (`overrides/gow_cd.cpp`) y arranque "desde disco" con los 8 IRX cargados (`overrides/gow_boot.cpp`).
- **Pendiente:**
  - Que ErickFRD20 complete SETUP y reproduzca el arranque.
  - Repartir las áreas: análisis, runtime y gráficos.
  - Espera de RPC en `0x26C0E8` (siguiente paso de arriba).
  - Proponer a PS2Recomp que `SetupThread` rellene `argc`/`argv` y añadir `sceCdReadDvdDualInfo` a `CD.cpp` (NOTES.md, "Cambios necesarios en PS2Recomp").
  - Revisar los tres stubs en conflicto (`StartThread`/`iWakeupThread`/`InitTLB`, ver NOTES.md).
  - Proponer a PS2Recomp que `guestMalloc` use una zona propia en lugar de la memoria del juego (NOTES.md, "Cambios necesarios en PS2Recomp").
  - Cambio pendiente de commit en el submódulo: ninguno (todo lo copiado al runner es local y está ignorado).
