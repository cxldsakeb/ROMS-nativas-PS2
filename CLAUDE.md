# Contexto para Claude: port nativo de God of War (PS2 → PC)

Este archivo lo carga Claude Code automáticamente. Es el **hilo compartido** entre las sesiones de Claude de todo el equipo: lo que no esté aquí o en `docs/` no lo sabe el Claude de los demás.

## Proyecto

- **Objetivo:** recompilar estáticamente God of War (PS2, 2005) a un ejecutable nativo de Windows con [PS2Recomp](https://github.com/ran-j/PS2Recomp).
- **Submódulo `PS2Recomp/`:** desde el 2026-10-14 apunta a **nuestro fork, [DaniSBR1/PS2Recomp](https://github.com/DaniSBR1/PS2Recomp), rama `godofwar`**, que lleva arreglos del IOP que el original no tiene. Si alguien clonó antes, tiene que ejecutar `git submodule sync --recursive` y `git submodule update --init --recursive`. Sin eso, el juego se cuelga en la RPC de sonido (`0x26C0E8`).
- **Equipo:** @cxldsakeb (dueño del repo), @DaniSBR1 y @Alejrr2 (se une el 2026-10-15). Están aprendiendo desde cero: Claude hace de guía, así que explica el porqué de las cosas y no solo el qué.
- **Para quien se une:** sigue [docs/SETUP.md](docs/SETUP.md) y lee `docs/NOTES.md` y `docs/ROADMAP.md`. Para subir al fork hay que ser colaborador de `DaniSBR1/PS2Recomp` (lo invita DaniSBR1). El reparto de trabajo entre los tres está en "Reparto de trabajo", más abajo.
- **Idioma:** todo en español (conversación, docs, comentarios y commits). Excepción: los comentarios de código dentro del fork de PS2Recomp van en inglés, como el resto de ese proyecto, para poder proponer los arreglos al original.
- **Repo:** público desde el 2026-10-15 (GPLv3), https://github.com/cxldsakeb/ROMS-nativas-PS2. Por eso la regla 1 importa todavía más: nada del juego, ni en commits, ni en issues, ni en capturas.

## Reglas que no se rompen

1. **Nunca se sube nada del juego:** ni ISO, ni ELF, ni archivos del disco, ni el C++ de `generated/` (deriva del ELF). Todo eso es local y está en `.gitignore`. No propongas saltarte esta regla.
2. **Las direcciones solo valen para el ELF de referencia**, que se identifica por el SHA1 apuntado en `docs/NOTES.md`. Antes de dar por buena una dirección, comprueba que el SHA1 del ELF local coincide.
3. **Todo hallazgo va a `docs/NOTES.md`:** funciones identificadas, bloqueos y soluciones. Es la memoria compartida del proyecto. Al terminar una tarea, actualízalo en la misma rama.
4. **Los parches temporales** (`ret0`/`ret1`/`reta0`, y cualquier apaño marcado así) llevan un comentario `TEMPORAL` en el código y una fila en NOTES.md.
5. **Cambios en `PS2Recomp/`:** primero intenta arreglarlo desde nuestro repo con un override (`overrides/`), como `gow_iop.cpp`. Si de verdad hay que cambiar el runtime: commit en la rama `godofwar` **dentro** de `PS2Recomp/`, push a `DaniSBR1/PS2Recomp` (remoto `fork`) y luego commit del nuevo puntero del submódulo en este repo. **Nunca subas trazas de diagnóstico al fork**: se pueden añadir en local para investigar, pero se quitan antes del commit.

## Estructura y flujo

```
scripts/extract-iso.ps1 -IsoPath <iso>   ISO → game/disc/ con .disc_layout e IOP_MOD/ (tools/iso_extract.py; imprime el SHA1)
scripts/build-tools.ps1                  compila ps2_analyzer y ps2_recomp (Release, VS 2022)
scripts/recompile.ps1 [-SyncOnly]        config/recomp.template.toml + funcmap.csv → out/gow.effective.toml → generated/ → runner; reconfigura CMake
scripts/run.ps1 [-Config] [-NoBuild]     compila ps2EntryRunner y arranca; log en out/logs/
scripts/deepseek.ps1 -Tarea -Archivos    tarea de solo lectura para DeepSeek → %TEMP%\gow-deepseek\ (ver "Tareas para DeepSeek")
tools/iso_extract.py, tools/iop, tools/vu1   extractor del disco y generadores del IOP y la VU1 nativos (de Waffle565, vía KIexster)
tools/*.py                               análisis en Python sin Ghidra (ver tools/README.md)
```

- **Desde el 2026-10-15 la base es el runtime de Waffle565** (ver "Estado actual"). La configuración del recompilador es la de KIexster: `config/funcmap.csv` (6 414 funciones, nombres `sub_XXXXXXXX`) y `config/recomp.template.toml` (stubs, entry points y la tabla `[mmio]` que convierte accesos a registros del hardware en llamadas nativas). Los cambios se hacen ahí, a mano.
- **Lo anterior queda como referencia, sin compilarse:** `config/anterior/` (el export de Ghidra y `gow.manual.toml`, con sus 634 funciones añadidas) y `overrides/anterior/` (nuestros overrides). `scripts/ghidra-analyze.ps1` y `tools/scan_missing_functions.py` siguen funcionando, pero ya no forman parte del flujo.
- **IOP y VU1 nativos (opcionales, la gran ganancia de velocidad):** se generan a partir de datos del juego capturados en local (regla 1: nunca se suben).
  - IOP: ejecuta una vez con `$env:PS2X_IOP_DUMP_MODULES = '<carpeta>'` y luego `recompile.ps1` con `$env:GOW_IOP_MODULOS = '<carpeta>'`.
  - VU1: crea la carpeta y ejecuta con `$env:GOW_VU1_CAPTURA = '<carpeta>'` (sin la carpeta creada no guarda nada); luego `recompile.ps1` con `$env:GOW_VU1_MICROCODIGO = '<carpeta>'`.
- **Al copiar overrides a mano, actualiza su fecha** (`(Get-Item $destino).LastWriteTime = Get-Date`): `Copy-Item` conserva la del original y, si se editó durante un build, MSBuild no lo recompila.
- Si solo cambian overrides, basta con `recompile.ps1 -SyncOnly` y compilar.
- Los parches específicos del juego van en `overrides/*.cpp` (con sus cabeceras `*.h`, que también se copian al runner), con `PS2_REGISTER_GAME_OVERRIDE`. Hoy hay uno, `gow_overrides.cpp`, de KIexster.
- **Unity build:** CMake junta los `.cpp` del runner en bloques (`unity_N_cxx.cxx`), así que varios overrides comparten unidad de compilación. Usa nombres con prefijo por archivo (`kIop…`, `g_iop…`), y comprueba que `PS2_REGISTER_GAME_OVERRIDE` no cae en la misma línea que en otro override: la macro usa `__LINE__` para sus nombres.
- El juego lee sus datos de los archivos extraídos junto al ELF (`game/disc/`), cada uno en su sector original según `game/disc/.disc_layout`. Si falta, vuelve a ejecutar `extract-iso.ps1`.
- Guía de depuración de PS2Recomp para juegos sin símbolos: el wiki de PS2Recomp, página "PS2Recomp Stripped Game Walkthrough". El bucle es: ejecutar, arreglar el **primer** bloqueo del log y repetir.
- Los scripts están en PowerShell 5.1 y se guardan en UTF-8 **con BOM**; sin él, los acentos se rompen.
- Si VS Code tiene la extensión C/C++, excluye `generated/` y `PS2Recomp/ps2xRuntime/src/runner/` en `.vscode/settings.json` (`C_Cpp.files.exclude`). Si no, la indexación bloquea archivos y `recompile.ps1` falla al borrarlos.

## Cómo colaboramos

- Una rama por tarea y PR a `main`, que revisa otro miembro del equipo.
- Antes de empezar: `git pull`, `git submodule sync --recursive` y `git submodule update --init --recursive`. Lee `docs/NOTES.md` y `docs/ROADMAP.md` para saber en qué fase estamos y qué hicieron los demás.
- Al terminar: actualiza NOTES.md y, si cambia la fase, la sección **Estado actual** de abajo.

## Herramientas de diagnóstico

- **`$env:GOW_HEAP_DEBUG = "1"`** antes de arrancar activa `overrides/gow_heap_debug.cpp`, que:
  - registra cada `heap_create` y los primeros mallocs, y avisa si se rompe la lista de libres de un heap;
  - vigila el árbol de `0x1769F8` y el pool de nodos de `0x175D88`;
  - cuando algo se rompe, imprime un **backtrace heurístico** de la pila del EE (`[gow:heap]   backtrace: … <- … <- …`): las direcciones de retorno guardadas en la pila. Con eso se sabe quién llamó a quién sin depurador.
- **`$env:GOW_PROFILE = "1"`** activa `overrides/gow_profile.cpp`, un perfilador de muestreo: cada `GOW_PROFILE_SECONDS` (15) imprime, por hilo, qué parte del tiempo trabaja, en qué funciones (propio e inclusivo, con las `FUN_…` del juego) y las pilas más frecuentes (`GOW_PROFILE_DEPTH`, `GOW_PROFILE_FILTER=texto`). Empieza tras `GOW_PROFILE_DELAY` (20 s). Necesita el PDB (`RelWithDebInfo`). Fue lo que encontró por qué el juego iba a 1,8 FPS.
- **`tools/`**: `elf_dis.py` (desensamblar y leer cadenas), `elf_xref.py` (quién llama a qué o quién toca una global), `irx_dis.py` (módulos del IOP, con nombres de imports) y `scan_missing_functions.py`.
- **IOP:** el fork ya formatea los `printf` de los IRX (antes salían con `%d` literal) y avisa con `[IOP] call to 0x… did not return` cuando una llamada síncrona se corta. Para investigar RPCs sirvieron trazas locales en `iop_rpc.cpp` (`handleRpc`: SID, `fno`, datos recibidos y devueltos) y `RPC.cpp` (`pushSifRpcDebugEventLocked`); se pueden volver a añadir en local, pero no se suben.
- Los archivos de `generated/` llevan la dirección en el nombre (`FUN_0027a6b8_0x27a6b8.cpp`) y cada instrucción en un comentario `// 0x…`.

## Tareas para DeepSeek (OpenCode)

Desde el 2026-10-15 usamos **DeepSeek V4.1 Flash, gratis en OpenCode**, como ayudante externo para el trabajo de mucho volumen. Así se ahorra uso de Claude y se pueden lanzar varias tareas a la vez. **Claude: cuando una tarea encaje aquí, propón delegarla, lánzala con el script y revisa siempre el resultado antes de usarlo.**

- **Instalación** (una vez): `npm install -g opencode-ai`. No hace falta cuenta. `opencode models` lista los modelos gratuitos.
- **Uso:** `.\scripts\deepseek.ps1 -Tarea tarea.md -Archivos <archivos o carpetas>`. La respuesta queda en `%TEMP%\gow-deepseek\<tarea>-<fecha>\salida.md`. El script trabaja en esa carpeta aislada, fuera del repo (dentro, OpenCode vería el resto de archivos y cargaría este CLAUDE.md), y en modo solo lectura (`--agent plan`).
- **Cómo escribir la tarea:** contexto en dos líneas, qué hacer, reglas ("no inventes", "si dudas, pon `?`") y **un formato de salida exacto**, preferiblemente una tabla con una fila por elemento. Sin formato fijo no se puede comprobar.
- **Si la entrada es grande** (más de ~70 KB), pártela y lanza un DeepSeek por trozo. Funcionan bien en paralelo.
- **Sí:** resumir, clasificar o cruzar documentos públicos, como los parches y `docs/ESTADO.md` de KIexster frente a nuestro fork y NOTES.md. También fichas de parches grandes, guías de estudio del runtime, borradores de PR en inglés para el PS2Recomp original y primeras pasadas contra PCSX2 (como lista de candidatos, no como veredicto).
- **No:** depurar ni decidir arreglos (un modelo "flash" se equivoca con seguridad), ni lo que un script hace mejor (contar, buscar cadenas).
- **Regla 1, también aquí:** es un servicio externo y gratuito que puede guardar lo que recibe. **Solo material público:** nuestro repo, el fork, PCSX2 y KIexster. Nunca `generated/`, desensamblados del ELF, volcados de la VU, logs de ejecución ni capturas. El script rechaza esas rutas, pero la responsabilidad es de quien lo lanza.
- **Cómo se revisa:** las columnas mecánicas se comprueban con un script; el contenido, abriendo una muestra (sobre todo lo dudoso). Las direcciones que dé, una a una contra el ELF de referencia (regla 2).
- **Prueba piloto:** clasificó los 118 parches de KIexster por frente en 3,5 min. Salieron las 118 filas, la columna calculable acertada al 100 % y las 7 revisadas a mano, correctas.

## Estado actual

> Actualizar esta sección al cerrar cada hito, para que la vean las sesiones de todos.

### GoW1 jugable: base de Waffle565 (cxldsakeb, 2026-10-15, rama `fase5/base-waffle`)

- **El runtime pasa a ser el de [Waffle565/God-Of-War-Native-Port](https://github.com/Waffle565/God-Of-War-Native-Port)** (rama `runtime`, GPL-3.0), montado como hace KIexster. **Fork `godofwar` en `083a286`**: `c5a9d02` + sus 7 parches + `ReleaseMode` con las opciones de Release también en `RelWithDebInfo` (sin `/GL`/`/LTCG`) + `.gitignore` del IOP nativo generado. El merge conserva nuestra historia hasta `ed11610`.
- **Configuración y overrides de KIexster** (`config/funcmap.csv`, `config/recomp.template.toml`, `overrides/gow_overrides.cpp` + cabeceras). Lo nuestro queda en `config/anterior/` y `overrides/anterior/`.
- **Resultado** (compilación de KIexster en `D:\gowport-kx`, equivalente archivo por archivo a la USA de Waffle565): menú, vídeos con sonido, partida a **53–60 FPS** con VU1 e IOP nativos, combate, HUD, DualSense, guardar y cargar. Dos fallos abiertos en NOTES.md (parada tras formatear la tarjeta; algunos sonidos de golpes).
- **Para reproducirlo (todos):** `git pull`, `git submodule sync --recursive`, `git submodule update --init --recursive`; `.\scripts\build-tools.ps1`; **`.\scripts\extract-iso.ps1` otra vez** (formato nuevo con `.disc_layout`); `.\scripts\recompile.ps1`; `.\scripts\run.ps1 -Config RelWithDebInfo`; y luego la VU1/IOP nativos (`docs/SETUP.md`, 7b). **Los scripts adaptados no se han probado aún de principio a fin en este repo** (sí el flujo de KIexster en el que se basan): si algo falla, apúntalo en NOTES.md.
- **Licencias:** su `runtime` es GPL-3.0; su `main` no tiene licencia, así que de ahí solo usamos lo que KIexster redistribuye con permiso del autor. Pedido en [su issue #2](https://github.com/Waffle565/God-Of-War-Native-Port/issues/2).
- **Siguiente:** God of War II en un repo aparte, sobre esta misma base.

### La intro se reproduce con el runtime de KIexster (cxldsakeb, 2026-10-15, rama `fase5/fmv`)

- **Prueba en local de su runtime entero** (los 118 parches + #271/#274, rama local `kiexster-base` del submódulo, **no subida**) con nuestros overrides. Con OpenGL (`PS2X_GS_GPU=1`) la lógica va a **41–71 FPS** (nosotros, 5,5) y llega al menú en 8 s. **Kratos sigue blanco**, así que la causa no está en sus parches del runtime.
- Tras elegir la dificultad se quedaba parado: el `sceIpuInit` genérico del runtime trae direcciones de otro juego. **`overrides/gow_fmv.cpp`** (adaptado de su `gowIpuInit`) lo arregla y **la intro se reproduce** (Kratos en el acantilado). `GOW_SKIP_FMV=1` salta los vídeos.
- **`docs/KIEXSTER.md`**: sus 118 parches por frente y cuáles tenemos ya (5 de Gráficos, 8 de Base y algunos parciales; ninguno de Rendimiento). Sale de las primeras tareas con DeepSeek.
- NOTES.md: 15 funciones con nombre sacadas de su bitácora (`vid::Flip`, `CalcSkinHierarchy`, `renModelServer::ProcessServer`…) y las dos filas de bloqueos.
- **Siguiente:** decidir si adoptamos su runtime como base del fork (la ganancia de velocidad y la intro lo piden) o traemos parche a parche (`mpeg-nodata` hará falta para el vídeo en nuestro fork); y seguir con Kratos blanco mirando sus overrides y su configuración.

### Kratos aparece: arreglos traídos de KIexster (cxldsakeb, 2026-10-15, rama `fase5/kratos`)

- **Referencia nueva: [KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp)**, otro port del mismo juego con PS2Recomp (GPLv3), bastante más avanzado: llega a la partida a 12–25 FPS. Aplica 118 parches sobre PS2Recomp `c5a9d02`, que ya está en la historia de nuestro fork, y su `docs/ESTADO.md` es una bitácora muy detallada. **Antes de investigar un bloqueo, mira si ellos ya lo resolvieron.** Lo que se adapte va con comentario en inglés y citando la fuente; los créditos están en el README.
- **Fork, `godofwar` en `ed11610`** (revisado por DaniSBR1 y fusionado en `main` el 2026-10-15; dos commits encima de `1e3161e`):
  - `2fa41fb`, EE: FPU (`SQRT.S`/`RSQRT.S` leían el registro equivocado, y sin NaN ni infinitos), ramas de 64 bits, VU0 en modo macro, MMI y el dispatcher que dejaba funciones a medias al ceder el turno. **Cambia el código generado: hace falta `recompile.ps1` completo** (sin `-SyncOnly`) y luego compilar (~45 min con 32 hilos).
  - `ed11610`, VU1: códigos de la EFU (`ERSQRT`/`ESIN`/`EATAN`/`EEXP`) mal numerados.
- **Resultado:** la Z del 3D deja de ser constante y **Kratos sale en el menú** (hombro y cuello, delante del fuego). Además, el texto del menú sale completo, las astillas grises desaparecen y `gow_state.cpp` deja de actuar. **Pero Kratos sale blanco y sin textura** (primera fila de "Bloqueos"), y la lógica baja de 5,5 a 1,8 FPS (sin perfilar aún).
- **Para reproducirlo (todos, después de este merge):** `git pull`, `git submodule sync --recursive` y `git submodule update --init --recursive` (el fork debe quedar en `ed11610`). Luego **`.\scripts\build-tools.ps1` y `.\scripts\recompile.ps1` completo** (cambia el recompilador, así que el `-SyncOnly` no basta) y `.\scripts\run.ps1 -Config RelWithDebInfo`.
- **Siguiente:** (1) la textura de Kratos: estado del GS de sus triángulos y parches del GS por CPU de KIexster; (2) perfilar la caída de FPS; (3) a medio plazo, su renderizador por OpenGL y la VU1 compilada (la gran ganancia de velocidad); (4) borrar `gow_state.cpp` cuando se confirme.
- Copias locales de trabajo (no se suben): `D:\gowref\theirs` (su árbol: `c5a9d02` + los 118 parches) y `D:\gowref\ours` (rama `godofwar-kiexster`).

### Traspaso para cxldsakeb: compilación completa y medir los FPS (DaniSBR1, 2026-10-15). **Hecho:** 5,5 FPS (NOTES.md)

DaniSBR1 subió el trabajo de la lentitud y **deja la compilación completa a cxldsakeb**, que tiene la CPU más rápida. Pasos, en orden:

1. **Ponerse al día.** Revisar los PR de `fase5/mando` (#9) y `fase5/lentitud` (este va encima del otro). Luego:
   ```powershell
   git checkout main; git pull
   git submodule sync --recursive; git submodule update --init --recursive   # el fork debe quedar en 1e3161e
   ```
   El fork `1e3161e` va **encima de tu `431aca1`** (TEST.ZTE), así que lo incluye. Si mergeas `fase5/kratos` después, quédate con el puntero `1e3161e` del submódulo.
2. **Compilación completa** (una vez): `AGRESSIVE_LOGS` pasa a estar apagado, y ese `define` llega a todo el código generado.
   ```powershell
   .\scripts\recompile.ps1 -SyncOnly          # copia gow_profile.cpp (nuevo) y reconfigura CMake con -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF
   .\scripts\run.ps1 -Config RelWithDebInfo
   ```
   Para volver a tener las trazas `[gs:*]`, `[padread]`…: `.\scripts\recompile.ps1 -SyncOnly -AggressiveLogs` (otra compilación completa). El `ps2_log.txt` viejo de la raíz (~180 MB) se puede borrar.
3. **Medir y apuntarlo en NOTES.md** (sustituir el "pendiente de medir" de la entrada del 2026-10-15):
   ```powershell
   $env:GOW_PAD_DEBUG = "1"; .\scripts\run.ps1 -Config RelWithDebInfo -NoBuild
   ```
   En el log, `[gow:pad] lectura N: X lecturas/s` son los FPS de la lógica del juego: **1,8** antes y **3,9** con el GS en paralelo (aún con `AGRESSIVE_LOGS`). Con los logs apagados debería subir más (en el perfil eran el 11 %).
4. **Opcional, para ver qué queda:** `$env:GOW_PROFILE = "1"` (`overrides/gow_profile.cpp`; informe por hilo cada 15 s desde el segundo 20). Lo siguiente según el último perfil: el planificador del IOP (~9 %) y la VU1 (~15 %).
5. Si algo se ve distinto en pantalla, `$env:PS2X_GS_THREADS = "0"` vuelve al rasterizador de un solo hilo para comparar.

### Reparto de trabajo (acordado el 2026-10-14)

Desde aquí se trabaja **en paralelo**, con objetivos distintos:

- **cxldsakeb: gráficos (VU1/GS) y compilaciones pesadas.** Rama `fase5/vu1`, y las que sigan para el mismo frente.
- **DaniSBR1: mando del port.** Rama `fase5/mando`. Punto de partida: `overrides/gow_pad.cpp`. Por orden:
  1. probar un mando de PlayStation real (DualShock 4/DualSense) en el PC y ajustar la zona muerta;
  2. quitar el aviso "DUALSHOCK®2 HAS BEEN REMOVED" que sale un momento al arrancar (seguramente el estado se pide antes de crear el socket o mientras se recrea);
  3. conectar y desconectar en caliente, y el puerto 2;
  4. vibración: raylib/GLFW no tiene vibración en escritorio, así que haría falta XInput o SDL en el runtime (fork);
  5. a largo plazo, emular el SIO2 en `ps2xIOP` para que los IRX del juego funcionen tal cual.
  Referencia: `pcsx2/SIO/Pad/` en la copia de PCSX2.
  **Estado (2026-10-15, DaniSBR1):** punto 2 hecho (el aviso ya no sale; ver NOTES.md). `gow_pad.cpp` lee ya la entrada con raylib: mando 1 y teclado a la vez en el puerto 1, mando 2 en el puerto 2 con conexión en caliente, zona muerta radial (`GOW_PAD_DEADZONE`), gatillos analógicos y presión. Vibración con XInput hecha (mandos de Xbox; `GOW_PAD_RUMBLE_TEST=1` la prueba al arrancar). Probado con el mando de Xbox de DaniSBR1: vibra. Los toques cortos se perdían porque la lógica del juego va a ~1,8 FPS y solo lee el mando una vez por frame; ahora un hilo retiene las pulsaciones entre lecturas (ver NOTES.md) y START, ✕, △ y el stick responden en el menú. Confirmado por DaniSBR1 con el mando (2026-10-15): mando terminado, PR #9. Pendiente: vibración para DualShock 4/DualSense (órdenes HID propias) y, a largo plazo, el SIO2.
- **Regla para no pisarse:** quien vaya a cambiar el fork (`godofwar`) lo avisa antes, porque los dos subimos a la misma rama. Si uno cambia `config/gow.manual.toml` o el fork, el otro tiene que hacer `git pull`, `git submodule update` y recompilar.

### VU1 arreglada (cxldsakeb, 2026-10-14, rama `fase5/vu1`)

Traspaso de DaniSBR1 cumplido: `main` al día y `-SyncOnly`. La primera compilación `RelWithDebInfo` tardó **31 min**; las siguientes, si solo cambia el runtime, 30 s.

- **Cómo se hizo:** tres auditorías en paralelo (unidad superior, inferior y núcleo) del intérprete de VU del runtime contra PCSX2 (`VUops.cpp`, `VU1microInterp.cpp`, `x86/microVU_Analyze.inl`, `Gif_Unit.h`). Se añadió `/pcsx2/x86/microVU*` al sparse checkout: microVU es lo que de verdad ejecuta los juegos en PCSX2 y sirve para desempatar con su intérprete.
- **Fork, commit `7eb4a1b`:** retardo de VI en los saltos (la causa principal), `vf00` fijo para la inferior del par, salto en la ranura de otro salto, `XGKICK` demasiado grande sin parar el programa y FLG 3 = IMAGE. Detalle en NOTES.md.
- **Resultado:** 205 000 microprogramas sin un solo atasco (antes, el 3,7 %), y el texto de los menús sale completo. El menú principal y el de opciones se manejan con el teclado.
- **Sigue mal:** el fondo 3D, con triángulos enormes estirados hasta los bordes. Es la primera fila de "Bloqueos" en NOTES.md.
- **Para reproducirlo:** `git pull`, `git submodule sync --recursive` y `git submodule update --init --recursive` (el fork debe quedar en `7eb4a1b`). Luego `.\scripts\run.ps1 -Config RelWithDebInfo`. Solo cambió el runtime: no hace falta `recompile.ps1`.
- **Copia de PCSX2:** cada uno la tiene en `%USERPROFILE%\ref\pcsx2`. Desde esta sesión el sparse checkout incluye también `/pcsx2/x86/microVU*`, `/pcsx2/Dmac.*`, `/pcsx2/Hw*` y `/pcsx2/SPR.*` (`git sparse-checkout add …`).

### Matrices a cero: DMA de la scratchpad (cxldsakeb, 2026-10-14, rama `fase5/geometria`)

- **Causa de los triángulos gigantes:** God of War calcula las matrices de cada objeto en la scratchpad y las copia a la RAM con un DMA `fromSPR` **en modo cadena**. El runtime solo hacía los DMA de la scratchpad en modo normal, así que esas copias se perdían. La cadena del VIF1 subía a la VU1 matrices a cero, la división de perspectiva daba `Q = FLT_MAX` y salían abanicos de triángulos basura.
- **Fork, commit `86d6fb9`:** `fromSPR` en cadena, intercalado en los dos sentidos, el bit SPR de las direcciones DMA y el UNPACK del VIF1 como PCSX2.
- **Resultado:** 0 vértices basura; los triángulos gigantes bajan del 0,45 % al 0,17 % (son tiras normales que recorta el scissor) y ya no salen formas erráticas al cargar.
- **Comparado con el juego real (YouTube):** el fuego, el logo, "New Game" y "Select" salen bien. **Falta Kratos** (cara y hombro a la derecha): salen manchas negras dentadas en su lugar. Hay también astillas grises en los bordes y un tramado sobre el fuego. Es el bloqueo abierto (primera fila de "Bloqueos" en NOTES.md).
- **Cómo se encontró** (método reutilizable, detalle en NOTES.md): contar en el rasterizador los triángulos más grandes que la pantalla → ver sus vértices → repetir **en otra VU1, sin dibujar**, el microprograma que los genera, con una línea por instrucción (`tools/vu_dis.py --trace` lo anota) → seguir el valor malo hacia atrás: la VU1, el VIF1 (registro de VIFcodes y DMAtags), el DMA y la scratchpad.

### Primeras imágenes (DaniSBR1, 2026-10-14, rama `fase5/primeras-imagenes`)

Traspaso de cxldsakeb cumplido: con `main` al día, el fork en `a0e3c5f` y una recompilación completa (636 funciones añadidas, 0 errores), todo se reproduce igual (27 `synchronous wait timed out`, 0 `missing-target`, `pc=0x17a154`).

**Se miró la ventana por primera vez, con `.\scripts\run.ps1 -Config RelWithDebInfo`:**
- ✅ **Pantalla legal de Sony**, perfecta.
- ✅ **Pantalla de título:** logo de GOD OF WAR, fondo del menú y "New Game".
- ✅ **Mando conectado** con `overrides/gow_pad.cpp` (mando o teclado del PC: flechas/WASD, X = ✕, C = ○, Z = □, V = △, Enter = START). Para pruebas sin nadie delante: `$env:GOW_PAD_PRESS = "start@150,start@450"`.
- ✅ **Se llega al menú principal** ("NEW GAME"), con su escena 3D animada de fondo (Kratos), pero con **la geometría deformada** (mosaico de triángulos con texturas correctas): casi seguro la VU1.
- ⚠️ **TEMPORAL:** `overrides/gow_state.cpp` ignora un cambio a un estado nulo que, sin el parche, salta a `pc=0`. El nombre `"Animation"` aparece como `"Animao"` en un búfer de la pila principal: algo pisa 2 bytes de un marco activo. Causa sin encontrar (NOTES.md).
- ❌ **Faltan letras en el texto** del menú, y dos líneas salen como bloques grises.
- `RelWithDebInfo` va más rápido que Debug (`dma=453` frente a ~300 en 2 min), pero **la primera compilación tarda ~42 min** con 12 hilos. Las siguientes, si solo cambian overrides, son rápidas.
- Para "mirar" la ventana desde un script, sin estar delante: capturarla con `PrintWindow` (`PW_RENDERFULLCONTENT`) a partir del `MainWindowHandle` del proceso. Las capturas **no se suben** (son imágenes del juego).

- **Fase:** 5, primeras imágenes: **ya se ven la pantalla legal, la de título y el menú principal** (este último con la geometría 3D deformada). La 3 y la 4 se cerraron el 2026-10-14, ver `docs/ROADMAP.md`.
- **Objetivo acordado para cerrar la fase 5:** que el **menú principal se vea correctamente** (la escena 3D de Kratos bien dibujada y el texto completo) y que se llegue a él **sin pulsar nada**. El soporte de mando "de verdad" para el port (configuración, mandos de PC) queda para más adelante; de momento basta con `gow_pad.cpp`.
- **Referencia: PCSX2** (https://github.com/pcsx2/pcsx2, GPLv3 como PS2Recomp). Su VU (`pcsx2/VU*.cpp`, `VUops.cpp`), su VIF (`Vif_Unpack.cpp`, `Vif_Codes.cpp`), su GS (`pcsx2/GS/`) y su mando (`pcsx2/SIO/Pad/`) son la referencia para corregir el runtime. Se puede consultar y adaptar código, citando de dónde sale y respetando la licencia. El juego pasa toda la inicialización, **carga `R_PERM.WAD` entero y llega a su bucle principal de frames** (envía frames al GS, cambia de framebuffer y espera la VBlank), pero va lento: en Debug, unos pocos DMA cada pocos segundos. Ver `docs/ROADMAP.md`.
- **Sesión del 2026-10-14 (cxldsakeb, rama `fase4/r-perm-stream`).** Cuatro bloqueos más, tres de ellos en el runtime (fork). Detalles en `docs/NOTES.md`.
  1. **El hilo principal no esperaba: moría.** En `ps2xIOP`, `WaitSema`/`WaitEventFlag` dentro de un manejador RPC (llamada síncrona, sin hilo) no bloqueaban, así que SMPD mandaba al EE el bloque anterior del disco. El stream de `R_PERM` llegaba desfasado, una cabecera basura provocaba un `memcpy` de ~200 MB y la pila se rompía. **Fork:** esas esperas ejecutan ahora los hilos del IOP hasta cumplirse.
  2. **Interrupción del GS (INTC 0).** El runtime nunca la lanzaba y el juego esperaba para siempre el `FINISH` del GS (`0x183894`). **Fork:** `EeScheduler::pollGsInterrupt`.
  3. **Pilas de interrupción encima de la pila principal.** El runtime las tomaba desde `0x2000000`, donde God of War tiene la pila de `main`. **Fork:** `SetupThread` las baja por debajo.
  4. **2 callbacks que Ghidra no ve** (punteros con `lui`+`addiu`). `tools/scan_missing_functions.py` busca ahora también esos (`cfn_`); añadidos a `config/gow.manual.toml`.
- **Bloqueo actual:** en el menú principal no se ve a Kratos (salen manchas negras dentadas en su lugar), y hay astillas grises y un tramado sobre el fuego. La VU1 ya no se atasca (fork `7eb4a1b`), las matrices de los objetos ya llegan (fork `86d6fb9`) y `TEST.ZTE` del GS está corregido (fork `431aca1`). Sigue además la lentitud, ya explicada: el hilo del juego va al 100 %, sobre todo pintando el GS por software dentro de `0x17A154` → `0x1403C8` → `sceDmaSend` (de 1,8 a 3,9 FPS con el GS en paralelo; ver el traspaso de arriba). En el arranque hay 27 `[IOP] synchronous wait timed out`.
- **SIGUIENTE PASO CONCRETO** (frentes que se pueden repartir; el mando ya está):
  1. **Kratos en el menú (cxldsakeb, rama `fase5/kratos`):** en el menú solo hay tres entradas de microprograma (`0x0`/`0x18`/`0x30`), y las matrices ya llegan. Se arregló `TEST.ZTE` en el GS (fork `431aca1`): las partes grises salen más nítidas, pero Kratos sigue sin verse. **Causa probable, medida:** todos los triángulos 3D del menú llevan **la misma `z` (`0x81D4B8`)** con `ZTST = GEQUAL`, así que gana lo último que se dibuja y el fuego tapa a Kratos a trozos. La Z constante ya sale de la VU1 (el programa convierte float a entero sumando `2^23`, `I = 8388863`). **Siguiente:** seguir la Z de un vértice en la traza de la VU1 y comparar con PCSX2 (detalle en NOTES.md). Trazas locales que sirvieron: `[gs-diag]`, `[vu-trace]` (ahora con disparo `GOW_VU_TRACE_PC`/`GOW_VU_TRACE_SKIP` y recuento de `MSCAL` por `startPc`), `[vif-diag]` y `[vu1-diag]`.
  2. **Corrupción de la pila** que tapa `gow_state.cpp`: alguien escribe 2 bytes en un marco activo de la pila principal (`0x1FFEDA0`). Vigilar las escrituras a esa dirección en las macros `WRITE*` de `ps2_runtime_macros.h` (en local; obliga a una compilación completa) e imprimir el PC del que escribe.
  3. **Texto:** desde el arreglo de la VU1 y del FLG 3 sale completo. Falta confirmar si siguen las dos líneas en bloques grises.
  4. **Lentitud (DaniSBR1, rama `fase5/lentitud`):** medido con `GOW_PROFILE=1`. El hilo del juego estaba ocupado el 100 %: ~76 % en el rasterizador del GS por software (dentro de `sceDmaSend`, que es lo que hay detrás de `0x17A154` → `0x1403C8`), ~12 % en la VU1 y ~5 % en `AGRESSIVE_LOGS`. Hecho: GS en paralelo por filas (fork, commit `1e3161e`, encima de `431aca1`) y `AGRESSIVE_LOGS` apagado desde los scripts (`-AggressiveLogs` para activarlo; obliga a una compilación completa). Resultado: pendiente de medir: la compilación completa la hace cxldsakeb (ver el traspaso en CLAUDE.md). Siguiente: el planificador del IOP (~9 %) y la VU1.
- **Trazas locales útiles (no se suben):** `[run:threads]` en `ps2_runtime.cpp` (junto a `[run:tick]`: PC, estado y espera de **cada** hilo del EE, con `eeScheduler().snapshot()`); `[ee:pc0]` en `EeScheduler.cpp` (cuando un hilo salta a `pc=0`, imprime `branch_pc`: la última instrucción de salto); `[sif:dma]` en `iop_rpc.cpp` (cada copia IOP→EE); `[cdvd:read]` en `iop_cdvd.cpp`; y un override local que envuelva una función del juego (`lookupFunction` + `registerFunction`) para imprimir sus argumentos.
  - En la sesión de `R_Perm`: en `iop_cdvd.cpp` (`readSectors`: LSN y primeros bytes) y en `IopEmulator::step` (al pasar por `SMPD base 0x4C900 + offset`: argumentos de `add_file`, `find_file`, `OpenResourceFile`…). Con eso se ve qué hace un IRX sin depurador.
- **Sesión del 2026-10-14 (DaniSBR1, rama `fase4/rpc-sonido`).** Se resolvieron cuatro bloqueos seguidos; los detalles están en `docs/NOTES.md`, en "Bloqueos" y "Funciones identificadas".
  1. **RPC de sonido (`0x26C0E8`).** El juego envía comandos a 989snd (SID `0x123456`) y espera la respuesta `{0xFFFFFFFF, resultado, 0xFFFFFFFF}` en `0x305600`. Fallaban dos límites del IOP emulado:
     - `SMPD_IOP.IRX` pide 1,1 MB con `AllocSysMemory` y el heap solo tenía 832 KB (el mensaje "failed to allocate memory" era de SMPD, no de 989snd);
     - el comando 104 de 989snd (un "comando externo" que atiende SMPD) tarda más de los 2 M de instrucciones que el emulador permite a una llamada síncrona, así que se cortaba y devolvía basura.
     **Arreglado en el fork** (commit `cdd8e0b`): `HeapBase = 0x70000` y `kMaxCallInstructions = 100'000'000`.
  2. **Funciones que Ghidra no ve** (`missing-target` en `0x239A60` y luego en `0x259480`). Se añadieron 621 métodos virtuales (`vfn_`) y 13 destinos de `JAL`/`J` en huecos (`fn_`) a `config/gow.manual.toml`, encontrados con `tools/scan_missing_functions.py`.
  3. **Trampa fatal en `0x185F90`.** `iop_init` carga un IRX que va embebido en el propio ELF (`0x2A3650`) mediante `sceSifLoadModuleBuffer`, que recibe una dirección del IOP, pero el runtime lo buscaba en la memoria del EE. Arreglado en **`overrides/gow_iop.cpp`**, que copia el módulo de la memoria del IOP al EE antes de cargarlo.
  4. **`printf` del IOP con formato** (en el fork). Fue clave para entender SMPD.
  5. Con eso se pasó el bucle de `0x176A80` (un puntero nulo porque `R_Perm` no cargaba), que llevó hasta SMPD.
- **Cómo carga archivos God of War:** no usa la libcdvd del EE. Manda "comandos externos" de 989snd (`fno=104`, cabecera `{"SMPD", cmd, tamaño}`) al módulo **SMPD_IOP** (base `0x4C900` en el IOP), que al iniciar recorre el ISO 9660 de la capa 0, lee `GODOFWAR.TOC` y registra cada recurso con su LBN dentro de `PART1.PAK`/`PART2.PAK`. Comando 1 = Init, `0xF` = abrir recurso.
- **Build de referencia:** God of War USA, SCUS-97399 v1.00, ELF `SCUS_973.99`, SHA1 `64B10DB280304115DA4BC2066238D29023DF0A06`. Los datos completos están en `docs/NOTES.md`.
- **Hecho:**
  - Estructura del repo, scripts y docs. `build-tools.ps1` verificado (VS 2022 Build Tools, MSVC 14.44, CMake 3.31).
  - Ghidra + extensión EE; `config/gow.toml` sale de Ghidra.
  - DaniSBR1 reproduce todo igual que cxldsakeb: mismo SHA1, mismo export de Ghidra y misma recompilación.
  - Heap del juego pisado por `guestMalloc` (`overrides/gow_memory.cpp`), bucle de libcdvd (`gow_cd.cpp`), arranque "desde disco" con los 8 IRX (`gow_boot.cpp`), RPC de sonido (fork), 634 funciones que faltaban (`gow.manual.toml`), IRX embebido (`gow_iop.cpp`), `R_Perm` abierto (`sprintf` del IOP en el fork + capa 1 corregida en `gow_cd.cpp`), stream de recursos correcto, interrupción del GS y pilas de interrupción (fork) y el bucle principal alcanzado. Fase 5: mando terminado (`gow_pad.cpp`: raylib, retención de pulsaciones, vibración XInput; PR #9), menú principal y de opciones, intérprete de la VU1 corregido contra PCSX2 (fork `7eb4a1b`), DMA de la scratchpad (fork `86d6fb9`), perfilador `GOW_PROFILE=1` y GS en paralelo (fork `1e3161e`, PR #10).
  - Fork de PS2Recomp creado y submódulo apuntando a él.
- **Pendiente:**
  - Velocidad: medir con `AGRESSIVE_LOGS` apagado (traspaso de arriba) y seguir con el planificador del IOP (~9 %) y la VU1 (~15 %).
  - Proponer al PS2Recomp original los arreglos de "Cambios necesarios en PS2Recomp" (NOTES.md): heap y presupuesto del IOP, `printf`/`sprintf`, esperas en llamadas síncronas del IOP, interrupción del GS, pilas de interrupción, intérprete de la VU1, GS en paralelo, `AGRESSIVE_LOGS` en OFF por defecto, `loadModuleBuffer` desde el IOP, `SetupThread` con `argc`/`argv`, `sceCdReadDvdDualInfo` y `guestMalloc` con zona propia.
  - Revisar los tres stubs en conflicto (`StartThread`/`iWakeupThread`/`InitTLB`, ver NOTES.md).
  - Cambio pendiente de commit en el submódulo: ninguno (cxldsakeb ya es colaborador del fork y puede subir a `godofwar`). `register_functions.cpp` y los archivos copiados al runner cambian con cada sync y se ignoran (`ignore = dirty`).
