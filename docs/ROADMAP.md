# Hoja de ruta

## Antes de empezar: qué esperar

- **PS2Recomp es experimental.** Su propio README dice que la emulación de hardware es parcial, que hay muchas rutas con stubs y que el rendimiento de VU/GS es muy malo. No es un botón de "convertir a PC": es una base sobre la que vamos a tener que programar mucho.
- **God of War es un juego exigente.** Usa a fondo VU1 (microprogramas para la geometría), streaming desde el DVD, módulos IRX propios en el IOP y vídeos MPEG por el IPU.
- **El objetivo realista para empezar** es que arranque y llegue a los logos o al menú. Que sea jugable vendrá después y puede llevar meses.
- **Lo que hay que aprender por el camino:** C++, lo básico del ensamblador MIPS y cómo funciona la PS2 (EE, IOP, GS, VIF/VU, DMA). PS2Tek y el código de PCSX2 son las referencias.

## Fases

Cada fase tiene un **criterio de "hecho"** comprobable. No hay que pasar a la siguiente sin cumplirlo.

### Fase 0: Entorno (todos)

- [ ] Cada uno sigue [SETUP.md](SETUP.md) hasta el paso 5 (`build-tools.ps1` compila).
- [ ] Todos tenemos el **mismo SHA1 del ELF**, apuntado en [NOTES.md](NOTES.md).

**Hecho cuando:** los 3 compilamos las herramientas y tenemos el ELF de referencia.

### Fase 1: Análisis en Ghidra

- [x] Proyecto de Ghidra con la extensión Emotion Engine y el análisis automático terminado.
- [x] Exportar `config/gow.toml` y `config/gow_functions.csv` y subirlos.
- [ ] Revisar cuántas funciones quedan como `untracked_stubs` (funciones de librería sin handler en el runtime).

**Hecho cuando:** `recompile.ps1` genera C++ sin errores.

### Fase 2: Que el código generado compile

- [x] `run.ps1` compila `ps2EntryRunner` con todo el código generado.
- [ ] Apuntar en NOTES.md los errores del recompilador o del compilador que haga falta arreglar en PS2Recomp.

**Hecho cuando:** existe `ps2EntryRunner.exe` con las funciones de God of War registradas.

### Fase 3: Primer arranque (EE)

Seguir el bucle de la [guía de PS2Recomp para juegos sin símbolos](https://github.com/ran-j/PS2Recomp/wiki/PS2Recomp-Stripped-Game-Walkthrough-For-LLMs): ejecutar, leer el log, arreglar el **primer** bloqueo y repetir.

- [x] `Function at address 0x... not found` → mejorar límites de función en Ghidra o hacer un binding. (636 funciones en `gow.manual.toml`: `vfn_`/`fn_`/`cfn_`)
- [ ] `[Syscall TODO]` → implementar la syscall.
- [x] `Unimplemented PS2 stub` → binding a un handler, o `ret0`/`ret1` temporal. (`sceCdReadDvdDualInfo` reimplementada)
- [x] `PC not updating` → buscar el bucle de espera y averiguar qué espera. (heap, libcdvd, libdbc, sonido, `R_Perm`, GS…; ver NOTES.md)

**Hecho cuando:** el juego llega a su bucle principal sin quedarse colgado al arrancar. ✅ **Hecho el 2026-10-14**: llega al bucle de frames (`0x17A0E0`–`0x17A36C`).

### Fase 4: IOP, módulos y archivos

- [x] Que se carguen los IRX del juego (en `ps2xIOP`) o tengan un HLE que funcione. (los 8 IRX + el embebido, desde `cdrom0:`)
- [x] Lecturas de `cdrom0:` correctas (los archivos de `game/disc/`). (SMPD_IOP lee la ISO completa por LBN; `R_PERM.WAD` llega entero)
- [x] RPCs SIF del juego con el IOP sin bloqueos. (989snd/SMPD; quedan ~27 esperas del IOP que agotan el tiempo, ver NOTES.md)

**Hecho cuando:** el juego lee sus archivos de datos y avanza más allá de la inicialización. ✅ **Hecho el 2026-10-14.**

### Fase 5: Primeras imágenes

- [x] Ruta GIF → GS: algo aparece en pantalla (logos, pantallas 2D). **2026-10-14:** en `RelWithDebInfo` se ven la pantalla legal de Sony y la de título (logo de God of War, fondo del menú, "New Game").
- [x] Texto de la fuente completo. **2026-10-14:** tras el arreglo de la VU1 y del FLG 3 del GIF, el texto de los menús sale completo. **2026-10-15:** confirmado en capturas del menú principal ("New Game", "Select") y del de dificultad ("Choose Difficulty" y las cuatro opciones): sin bloques grises.
- [ ] VIF1 / VU1: los microprogramas de geometría de God of War. **Es el mayor riesgo técnico.** **2026-10-14:** el intérprete de VU1 se corrigió contra PCSX2 (fork `7eb4a1b`) y ya termina todos los microprogramas (205 000 sin atascos). Con el DMA de la scratchpad en modo cadena (fork `86d6fb9`), las matrices ya llegan y desaparecen los triángulos basura. Comparado con el juego real, falta **Kratos** (manchas negras en su lugar) y sobran unas astillas grises y un tramado: es el siguiente frente.
- [ ] Vídeos MPEG (IPU, con FFmpeg).
- [x] Mando: entrada con raylib (mando y teclado), retención de pulsaciones, conexión en caliente, puerto 2 y vibración XInput (`overrides/gow_pad.cpp`, PR #9). Probado por DaniSBR1 con un mando de Xbox el 2026-10-15.
- [ ] Velocidad. **2026-10-15:** la lógica iba a 1,8 FPS; el perfilador (`GOW_PROFILE=1`) mostró ~76 % en el rasterizador del GS por software. Con el GS en paralelo (fork `1e3161e`), 3,9 FPS. Pendiente: medir con `AGRESSIVE_LOGS` apagado, y después el planificador del IOP y la VU1 (PR #10).

**Hecho cuando:** se ve **correctamente** el menú principal (la escena 3D de Kratos y el texto) y se llega a él **sin pulsar nada**. *(2026-10-14: se llega al menú principal y al de opciones, y el texto sale completo; falta la geometría 3D del fondo. Referencia para corregirlo: PCSX2.)*

### Fase 6: Hacia jugable

- [x] Mando (pad), de forma TEMPORAL: `overrides/gow_pad.cpp` conecta el mando o el teclado del PC sustituyendo 4 funciones de la librería del DualShock (sin presión ni vibración; la solución completa sería emular el SIO2 en el IOP). Antes bloqueaba: La pantalla de título pide "PLEASE INSERT A DUALSHOCK®2 ANALOG CONTROLLER INTO CONTROLLER PORT 1". El juego usa su propio driver en el IOP (`DBCMAN`/`DS2U_D`, RPC `0x80001300`/`0x8000131C`…).
- [ ] Tarjeta de memoria (guardar y cargar) y audio (SPU2/libsd).
- [ ] Rendimiento: pasar de las rutas de VU/GS por CPU a algo más rápido.

**Hecho cuando:** se puede jugar el primer nivel (el barco) de principio a fin.

### Fase 7: Mejoras de PC (opcional, al final)

Resolución nativa, 60 FPS, configuración de controles y el resto de lo que hace "nativo" a un port.

## Reparto

Somos dos, así que cada uno lleva una o dos áreas, para no pisarnos en los mismos archivos. Acordado el 2026-10-14:

| Área | Qué hace | Quién |
|---|---|---|
| **Gráficos / Media** | GIF/GS, VIF1/VU1, IPU/vídeos, audio (fases 5–6), y las compilaciones pesadas | @cxldsakeb |
| **Mando / Entrada** | `overrides/gow_pad.cpp`: mandos de PC reales, conexión en caliente, puerto 2, vibración y, más adelante, SIO2 en el IOP. **Hecho lo básico (2026-10-15)**; queda la vibración de DualShock 4/DualSense y el SIO2 | @DaniSBR1 |
| **Rendimiento** | Perfilar (`GOW_PROFILE=1`) y acelerar lo que frena el juego: GS por CPU, IOP, VU1 | @DaniSBR1 (cxldsakeb compila) |
| **Análisis / RE** | Ghidra: límites de función, nombrar funciones, averiguar qué hace cada `sub_xxx`, mantener `config/gow.toml` | según haga falta |
| **Runtime / Sistema** | Syscalls, IOP/IRX, SIF/RPC, archivos, overrides de arranque (fases 3–4) | según haga falta |

Quien vaya a cambiar el fork (`godofwar`) lo avisa antes, porque los dos suben a la misma rama.

Colaboradores: @cxldsakeb y @DaniSBR1.
