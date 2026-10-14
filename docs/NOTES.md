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
| `0x0013DC78` | `FUN_0013dc78` | **Bucle donde se queda el arranque actual** (ver Bloqueos) | investigando | — |
| `0x0013FCA8` | `FUN_0013fca8` | Llama a `FUN_0013dc78` en bucle | investigando | — |
| `0x002A6170` | (tabla) | Tabla de constructores/destructores `{0x26B6B0, ctor, 0, 1, dtor, …}`, recorrida en `0x28374C` (JALR) | dato | cxldsakeb |

Estados: `investigando` · `binding temporal (ret0/ret1)` · `binding real` · `reimplementada`.

## Bloqueos

Los más recientes arriba. Un bloqueo = el primer error del log que impide avanzar.

| Fecha | Síntoma (línea del log) | Causa | Solución / PR | Quién |
|---|---|---|---|---|
| 2026-10-14 | **ABIERTO.** Tras los constructores y los primeros paquetes GIF al GS, `[run:tick]` muestra el PC alternando entre `0x13dcd8` y `0x13de00` (`ra=0x13fd58`) durante todo el minuto. `dma=2 gif=1 vif=0`, un solo hilo activo, sin errores en stderr | Bucle dentro de `FUN_0013dc78` (`0x13DC78`–`0x13DE24`), llamado desde `FUN_0013fca8` (`0x13FCA8`–`0x13FE0C`). Llama a `0x2897C4`, carga `0x01FF....` y constantes float (`0x40000000` = 2.0). **No se sabe aún qué espera** (¿un DMA/GIF que el runtime no completa? ¿vsync? ¿una lectura de CD?) | Pendiente: ver ambas funciones en el descompilador de Ghidra, poner breakpoint en `0x13DC78` en PCSX2 con el juego real y comparar qué condición sale del bucle | — |
| 2026-10-14 | `[guest-branch:missing-target] kind=IndirectCall op=JALR source=0x28374c target=0x29bb78` (el juego se queda girando ahí) | Constructor global solo referenciado desde la tabla de datos `0x2A6170`; el analyzer heurístico no lo ve como función | **Resuelto con Ghidra:** su mapa de funciones incluye `0x29BB78`, `0x29BB90` y `0x26B6B0` (la otra entrada de la tabla, que fue el bloqueo siguiente con el analyzer) | cxldsakeb |
| 2026-10-14 | Miles de `LNK2019` al compilar el runner tras recompilar | MSBuild usa el `.vcxproj` viejo aunque CMake regenere el glob a mitad del build | `recompile.ps1` reconfigura CMake después de copiar los archivos | cxldsakeb |

## Cambios necesarios en PS2Recomp

Arreglos que no son específicos de God of War y deberían ir al runtime o al recompilador (y, si se puede, como PR al proyecto original).

-
