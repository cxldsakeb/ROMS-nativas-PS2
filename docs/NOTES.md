# Bitácora técnica

Todo lo que averigüemos del juego se apunta aquí: una sola fuente de verdad para los tres.

## Build de referencia

**Todas las direcciones del proyecto valen solo para este ELF.**

| Campo | Valor |
|---|---|
| Juego | God of War (USA), `SYSTEM.CNF`: `VER = 1.00`, `VMODE = NTSC` |
| Serial | SCUS-97399 |
| ELF | `SCUS_973.99` (1 911 580 bytes) |
| SHA1 del ELF | `64B10DB280304115DA4BC2066238D29023DF0A06` |
| Entry point | `0x00100008` |
| CRC32 del ELF | `0x822956F8` |
| ISO | 8 522 792 960 bytes (4 161 520 sectores), DVD de doble capa |

## Datos del ELF y del disco

- **Sin símbolos** (stripped): hay que nombrar las funciones a mano o con Ghidra.
- Secciones: `.text` 0x00100000–0x0029BBA4 (unos 422 000 instrucciones), `.data` 0x0029BC00, `.rodata` 0x002B1300, `.bss` 0x002C7080.
- **24 secciones `.DVP.overlay.*`** más `.DVP.ovlytab`: son microprogramas de VU, el código de VU1 que dibuja la geometría. Las usará el área de gráficos.
- **Disco:** `PART1.PAK` (4.26 GB) + `GODOFWAR.TOC` + IRX propios (`989NOMID`, `SMPD_IOP`, `DBCMAN`, `LIBSD`, `MC2_*`, `SIO2*`, `DS*`) + `IOPRP300.IMG`.
- **El juego lee por sector (LBN)** usando `GODOFWAR.TOC` (entradas de 24 bytes: nombre de 12 bytes y valores u32, entre ellos el LBN). Muchos archivos (`*.PSS`, `*.VPK`, `R_PERM.WAD`…) no aparecen en el sistema de archivos visible.
- **Doble capa:** Windows solo monta la capa 0, así que `game/disc/` está incompleto. Por eso `overrides/gow_overrides.cpp` le pasa la **ISO completa** al runtime como imagen de CD (ruta en `game/iso-path.txt`).

## Pruebas del pipeline

**2026-10-14 (cxldsakeb):** `ps2_analyzer` (sin Ghidra) → `ps2_recomp` → 6426 archivos C++ (121 MB) en 17 s.

- 1756 errores `unhandled-instruction`, **todos** en 15 "funciones" falsas fuera de `.text` (`sub_0029CCE4`…`sub_002A5B20`), que en realidad son datos de `.data` (por ejemplo, `0x40490fdb` es π en float). Dentro de `.text`: **0 errores**.
- 2246 avisos, sobre todo `unresolved JR/JALR` (saltos indirectos o tablas). Con los límites de función de Ghidra deberían bajar.
- Primer arranque (con la config del analyzer): carga el ELF, aplica el override y monta la ISO. Se para en un constructor global sin registrar (ver Bloqueos).

**2026-10-14 (cxldsakeb):** Ghidra 12.1.4 headless + extensión EE: análisis + export en **1 minuto**.

- 5692 funciones + 25 548 etiquetas de código, 31 240 registros en el CSV. Detecta solas `0x29BB78`, `0x29BB90` y `0x26B6B0` (entradas de la tabla de constructores).
- Stubs: Ghidra reconoce 47 envoltorios de syscall (`CreateThread`, `WaitSema`, `FlushCache`…); el analyzer, 82 funciones del SDK (`sceCd*`, `sceSif*`, `sceMpeg*`, `sceDma*`, `sceGs*`…). No se solapan: las del analyzer van en `config/gow.manual.toml`.
- **Conflictos:** `StartThread`, `iWakeupThread` e `InitTLB` salen en direcciones distintas en cada herramienta. Nos quedamos con las de Ghidra (envoltorios de syscall) y quitamos las del analyzer (`0x294AF8`, `0x294990`, `0x29AA48`). **Pendiente:** averiguar qué son de verdad esas tres.
- Se descartan los 72 `[patches]` del analyzer (NOP a instrucciones marcadas como "posible código automodificable"): es demasiado arriesgado sin revisarlos uno a uno.
- Recompilación con Ghidra + manual: **0 errores**, 3587 avisos, 31 243 archivos C++.
- Compilación del runner (Debug): 5 min. **Tercer arranque:** sin funciones perdidas; el juego pasa los constructores, envía los primeros paquetes GIF al GS (`gs:reg 0x41/0x42`) con DMA activo y se queda en el bucle de `FUN_0013dc78`. Logs locales en `out/logs/boot3.*.log`.

## Funciones identificadas

Dirección = inicio de la función en el ELF de referencia.

| Dirección | Nombre | Qué hace | Estado | Quién |
|---|---|---|---|---|
| `0x0029BB78` | `FUN_0029bb78` | Constructor global de C++: `a0=0x2A618C`, `a1=0x32E830`, salta a `0x286F88` | lo detecta Ghidra | cxldsakeb |
| `0x0029BB90` | `FUN_0029bb90` | Destructor emparejado: `a0=0x2A618C`, salta a `0x286FB0` | lo detecta Ghidra | cxldsakeb |
| `0x0026B6B0` | `FUN_0026b6b0` | Otra entrada de la tabla de constructores (32 bytes) | lo detecta Ghidra | cxldsakeb |
| `0x0013DC78` | `heap_malloc` | **malloc(heap, bytes, alineación).** Recorre la lista circular de libres del heap (`+4` = primer libre) hasta encontrar un hueco o volver a la cabecera. Tamaño en palabras: `(bytes+3)/4 + 2`, mínimo 4. Si `bytes < 4` llama a `0x2897C4(heap+0x28, "Debug")`. **Es el bucle del arranque actual** (ver Bloqueos) | investigando | DaniSBR1 |
| `0x0013FCA8` | `FUN_0013fca8` | Constructor de un búfer doble (¿de paquetes DMA?): `new(0x678)` y luego `heap_malloc(heapActual, (a0 & ~0x3F)*2, 0x40)`; alineación de 64 bytes, la que pide el DMA. Guarda los punteros en `+0/+4/+8/+0xC` | investigando | DaniSBR1 |
| `0x0013D630` | `operator_new` | `heap_malloc(heapActual(), a0, 8)` | identificada | DaniSBR1 |
| `0x0013D668` | `operator_delete` | Si `a0 != 0`, llama a `heap_free` (`0x13DB98`) | identificada | DaniSBR1 |
| `0x0013DB98` | `heap_free` | Libera un bloque (sin analizar a fondo) | investigando | DaniSBR1 |
| `0x0013D6E0` | `heap_stack_init` | Inicializa la pila de heaps de `0x2CB940`: base `0x304728`, índice = límite = `0x80` (crece hacia abajo; vacía si índice == límite) | identificada | DaniSBR1 |
| `0x0013D710` | `heap_create` | `heap_create(mem, bytes, nombre, n)`: magic `0xC0DE1111` en `+0`, nombre (15 car.) en `+0x28`, la enlaza a la lista global de heaps (`0x29BEB0`), crea un único bloque libre con todo el espacio y apila el heap | identificada | DaniSBR1 |
| `0x0013E090` | `heap_current` | Devuelve el heap de la cima de la pila (`0` si está vacía) | identificada | DaniSBR1 |
| `0x0013E158` / `0x0013E180` | `heap_push` / `heap_pop` | Apilan y desapilan un heap en `0x2CB940` | identificada | DaniSBR1 |
| `0x0013E1C0` | `heap_find_by_addr` | Busca en la lista global (`0x29BEB0`) el heap que contiene la dirección `a0` | identificada | DaniSBR1 |
| `0x002A6170` | (tabla) | Tabla de constructores/destructores `{0x26B6B0, ctor, 0, 1, dtor, …}`, recorrida en `0x28374C` (JALR) | dato | cxldsakeb |

Estados: `investigando` · `binding temporal (ret0/ret1)` · `binding real` · `reimplementada`.

## Bloqueos

Los más recientes arriba. Un bloqueo = el primer error del log que impide avanzar.

| Fecha | Síntoma (línea del log) | Causa | Solución / PR | Quién |
|---|---|---|---|---|
| 2026-10-14 | **ABIERTO.** Tras arreglar el heap: el juego sube una textura de 114 KB, dibuja un sprite y presenta frames (`[frame:upload]` 512×448). Luego `[run:tick]` muestra `pc=0x27a768 ra=0x27a79c` | Bucle de espera en `FUN_0027a6b8`: cuenta atrás de `0x100000` (`0x27A768`) y reintenta `0x297290(a0, 0x80000593, 0)`. Por el patrón, es `sceSifBindRpc` al servidor RPC `0x80000593` de `libcdvd` en el IOP (¿`sceCdSearchFile`?): el juego espera a que el servidor de CD/DVD responda y el runtime no lo ofrece. **Sin confirmar** | Pendiente (fase 4): confirmar qué es `0x297290` y `FUN_0027a6b8`; ver si el runtime/`ps2xIOP` implementa el servidor `0x80000593` o si hay que bindear las funciones `sceCd*` del juego a los stubs de `CD.cpp` | — |
| 2026-10-14 | Tras los constructores y los primeros paquetes GIF al GS, `[run:tick]` muestra el PC alternando entre `0x13dcd8` y `0x13de00` (`ra=0x13fd58`) durante todo el minuto. `dma=2 gif=1 vif=0`, un solo hilo activo, sin errores en stderr | **El runtime escribe encima del heap del juego.** `FUN_0013dc78` es el **malloc** del juego y `0x13DCD8`↔`0x13DE00` es el recorrido de su lista de libres (`0x01FFFFFF` = máscara del tamaño, `0x40000000` = bit de "ocupado", no floats). En `0x138CB0` el juego crea el heap **"Root"** con toda la RAM: de `align16(fin de .bss) = 0x35C1B0` a `0x1FF7FF8`. El syscall `SetupHeap` fija el heap del runtime (`guestMalloc`) **también en `0x35C1B0`**. El stub HLE de `sceGsResetGraph` (`GS.cpp`, modo 0) reserva ahí 128 bytes para su paquete GIF y pisa la cabecera de "Root". Con `GOW_HEAP_DEBUG=1` se ve: tras `[gs:reg] reg=0x41/0x42`, el heap pasa a llamarse "B" (`0x42`), pierde el magic y su primer libre vale `0x10000000` (la mitad alta del GIFtag, `NREG=1`). El malloc de `FUN_0013fca8` recorre esa lista rota para siempre | **Resuelto:** `overrides/gow_memory.cpp` (TEMPORAL) recorta 1 MB del final de "Root" al crearlo y configura ahí el heap del runtime (`configureGuestHeap`). Diagnóstico: `overrides/gow_heap_debug.cpp` con `GOW_HEAP_DEBUG=1` | DaniSBR1 |
| 2026-10-14 | `[guest-branch:missing-target] kind=IndirectCall op=JALR source=0x28374c target=0x29bb78` (el juego se queda girando ahí) | Constructor global solo referenciado desde la tabla de datos `0x2A6170`; el analyzer heurístico no lo ve como función | **Resuelto con Ghidra:** su mapa de funciones incluye `0x29BB78`, `0x29BB90` y `0x26B6B0` (la otra entrada de la tabla, que fue el bloqueo siguiente con el analyzer) | cxldsakeb |
| 2026-10-14 | Miles de `LNK2019` al compilar el runner tras recompilar | MSBuild usa el `.vcxproj` viejo aunque CMake regenere el glob a mitad del build | `recompile.ps1` reconfigura CMake después de copiar los archivos | cxldsakeb |

## Cambios necesarios en PS2Recomp

Arreglos que no son específicos de God of War y deberían ir al runtime o al recompilador (y, si se puede, como PR al proyecto original).

- **`guestMalloc` toma memoria que el juego cree suya.** Los stubs HLE (`GS.cpp`, `SIF.cpp`, `MPEG.cpp`, `Font.cpp`, pilas en `Thread.cpp`…) reservan con `guestMalloc`, cuyo heap empieza donde dice `SetupHeap` (el final de .bss). Un juego que gestiona él mismo toda la RAM a partir de ahí, como God of War, sale corrupto. Propuesta: que el runtime tenga una zona propia y configurable para sus reservas, o que cada juego pueda declararla. Mientras tanto, `overrides/gow_memory.cpp`.
