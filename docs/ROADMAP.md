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

- [ ] `Function at address 0x... not found` → mejorar límites de función en Ghidra o hacer un binding.
- [ ] `[Syscall TODO]` → implementar la syscall.
- [ ] `Unimplemented PS2 stub` → binding a un handler, o `ret0`/`ret1` temporal.
- [ ] `PC not updating` → buscar el bucle de espera y averiguar qué espera.

**Hecho cuando:** el juego llega a su bucle principal sin quedarse colgado al arrancar.

### Fase 4: IOP, módulos y archivos

- [ ] Que se carguen los IRX del juego (en `ps2xIOP`) o tengan un HLE que funcione.
- [ ] Lecturas de `cdrom0:` correctas (los archivos de `game/disc/`).
- [ ] RPCs SIF del juego con el IOP sin bloqueos.

**Hecho cuando:** el juego lee sus archivos de datos y avanza más allá de la inicialización.

### Fase 5: Primeras imágenes

- [ ] Ruta GIF → GS: algo aparece en pantalla (logos, pantallas 2D).
- [ ] VIF1 / VU1: los microprogramas de geometría de God of War. **Es el mayor riesgo técnico.**
- [ ] Vídeos MPEG (IPU, con FFmpeg).

**Hecho cuando:** se ve el menú principal.

### Fase 6: Hacia jugable

- [ ] Mando (pad), tarjeta de memoria (guardar y cargar) y audio (SPU2/libsd).
- [ ] Rendimiento: pasar de las rutas de VU/GS por CPU a algo más rápido.

**Hecho cuando:** se puede jugar el primer nivel (el barco) de principio a fin.

### Fase 7: Mejoras de PC (opcional, al final)

Resolución nativa, 60 FPS, configuración de controles y el resto de lo que hace "nativo" a un port.

## Reparto propuesto

Al principio es mejor que cada uno tenga **un área**, para no pisarnos en los mismos archivos:

| Área | Qué hace | Quién |
|---|---|---|
| **Análisis / RE** | Ghidra: límites de función, nombrar funciones, averiguar qué hace cada `sub_xxx`, mantener `config/gow.toml` | _por decidir_ |
| **Runtime / Sistema** | Syscalls, IOP/IRX, SIF/RPC, archivos, overrides de arranque (fases 3–4) | _por decidir_ |
| **Gráficos / Media** | GIF/GS, VIF1/VU1, IPU/vídeos, audio (fases 5–6) | _por decidir_ |

Colaboradores: @cxldsakeb, @DaniSBR1, @ErickFRD20. En la fase 0 todos hacen lo mismo; el reparto empieza a contar en la fase 1.
