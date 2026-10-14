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
| `0x00100008` | `_start` (crt0) | Limpia `.bss`, `SetupThread(gp=0x2CF070, stack=0x1FF8000, 0x8000, args=0x2C7080, …)`, `SetupHeap(0x35C1A8)`, `0x138CB0` (init) y `main(_args.argc, _args.argv)`; al volver, `exit` (`0x2996B0`) | identificada | cxldsakeb |
| `0x002C7080` | `_args` (dato) | `{int argc; char *argv[16]; char payload[256]}` que rellena el kernel en `SetupThread`. **PS2Recomp no lo rellena** (ver `overrides/gow_boot.cpp`) | dato | cxldsakeb |
| `0x00138D48` | `main` | Guarda argc/argv en `0x32F3F8`/`0x32F3FC` y llama a `boot_mode_init(argc, argv, *(0x304724))` | identificada | cxldsakeb |
| `0x0017AA88` | `boot_mode_init` | Fija el modo de arranque (`0x29C4E4`, `-1` en el ELF): `argc != 0` y `argv[0]` empieza por `"cdrom"` → 0; `argc != 0` y no → 1 (`host0:`); `argc == 0` → 2 (prefijo copiado de `a2[1]` a `0x304C68`) | identificada | cxldsakeb |
| `0x0017AA78` | `boot_mode_get` | Devuelve el modo de arranque (`0x29C4E4`) | identificada | cxldsakeb |
| `0x0017AB28` | `boot_make_path` | `boot_make_path(nombre, out, modo)` (`modo == -1` → el global). Si `nombre` empieza por `host0:` lo deja igual. Modo 0 → `cdrom0:\NOMBRE;1` (en mayúsculas y con `\`); 1 → `host0:%s`; 2 → `%s%s` con el prefijo de `0x304C68`. `0x17AC88` es el atajo con `out` = búfer estático `0x304A68` | identificada | cxldsakeb |
| `0x0027AD00` | `sceDbcInit` (libdbc) | Hace bind a DBCMAN `0x80001300` y comprueba la versión (`0x27ACA8`). Después hace bind en bucle a `0x8000131C`, `0x8000131E` y `0x8000131F`, los servidores de los drivers del mando. Cadenas `"libdbc: Module version mismatch"` en `0x2C4D20` | identificada | cxldsakeb |
| `0x0017BC80` | `iop_init` | Inicialización del IOP del juego: `sceSifInitRpc`, `sceCdInit(0)`, `sceCdMmode(2)`; si hace falta, reinicia el IOP (`sceSifRebootIop`/`sceSifSyncIop`); **repite `sceCdReadDvdDualInfo` hasta que devuelve ≠ 0** y guarda `on_dual`/`layer1_start` en `0x3053E4`/`0x3053E8`; luego `sceFsReset`, `sceSifLoadFileReset`, `sceSifInitIopHeap` y carga 8 IRX (tabla de pares en `0x2FD970`, vía `0x17BBC8`) | identificada | cxldsakeb |
| `0x0027A340` | `_CdSemaInit` | libcdvd: crea los semáforos de los comandos S y N | identificada (por patrón) | cxldsakeb |
| `0x0027A6B8` | `_CdCheckSCmd` | libcdvd: `PollSema`, `_CdSyncS(1)`, `sceSifInitRpc` y bucle `sceSifBindRpc(&clientSCmd, 0x80000593, 0)` hasta que `clientSCmd.server != 0`. Es igual que en ps2sdk (`scmd.c`). Lo llaman todos los comandos S | identificada | cxldsakeb |
| `0x0027AB00` | `sceCdReadDvdDualInfo` | Comando S `0x27`; `*a0 = on_dual`, `*a1 = layer1_start`, devuelve 1 si va bien. Su único llamador es `iop_init` | reimplementada (`overrides/gow_cd.cpp`) | cxldsakeb |
| `0x00293C40` / `0x00293C70` | `SignalSema` / `PollSema` | Envoltorios de syscall (van detrás de `CreateSema`/`DeleteSema` en `0x293C20`/`0x293C30`) | identificada | cxldsakeb |
| `0x002963C0` | `printf` | Recibe las cadenas de libcdvd `0x2C4BE0` (`"Scmd fail sema…"`) y `0x2C4C08` (`"Libcdvd bind err S cmd"`), comprobadas en el ELF | identificada | cxldsakeb |
| `0x002A6170` | (tabla) | Tabla de constructores/destructores `{0x26B6B0, ctor, 0, 1, dtor, …}`, recorrida en `0x28374C` (JALR) | dato | cxldsakeb |
| `0x0026BF28` | `snd_send_command` | Cliente EE de 989snd: copia el comando a `0x305640` (los "external", `fno=0x68`, con cabecera de 12 bytes y máx. 500 bytes), arma la respuesta con `0x26BC40` y hace `sceSifCallRpc(0x3055C8, fno, NOWAIT, 0x305640, …, 0x305600, 12)`; luego espera con `while (!0x26BB98());` | identificada | DaniSBR1 |
| `0x0026BC40` | `snd_arm_return` | `(buf, n)`: pone a 0 `buf[0]` y `buf[n+1]` y guarda `buf` en `0x2A1338` y `n` en `0x2A133C` | identificada | DaniSBR1 |
| `0x0026BB98` | `snd_poll_return` | Devuelve 1 cuando `buf[0] == buf[n+1] == 0xFFFFFFFF` (las marcas que pone el IOP) o si no hay espera pendiente. Si la RPC ya no está ocupada y no hay marcas, imprime "Sif says RPC isn't busy, but we still don't have returns from the IOP!" | identificada | DaniSBR1 |
| `0x0026B6D0` | `snd_init` | Bind (en `0x26B77C`) del cliente `0x3055C8` al SID `0x123456` (989snd); `0x30A200` se conecta al `0x123457` | dato | DaniSBR1 |
| `989NOMID+0x1878` | (IOP) `snd_rpc_dispatch` | Servidor RPC de 989snd: `tabla[fno](buf)` (tabla en `.data+0x1DE88`), luego `res[2] = -1`; `res[0] = -1` desde la inicialización. `fno=0` → `+0x310`; `fno=104` → `+0x1410` → `+0x250` (manejadores "external" que registran otros módulos, como SMPD_IOP) | identificada | DaniSBR1 |
| `0x00185F28` | `asset_load` | Carga un recurso por nombre (`a1`, p. ej. `"R_Perm"`) y flags (`a2`); abre con `0x17AD70` y espera en un bucle asíncrono (`0x26B918`, `0x173090`, `0x1733D8`, `0x185A20`). Contiene la trampa fatal de `0x185F90` (si `*0x29C4DC < 0` o `*0x29C4E0 != 0`) | investigando | DaniSBR1 |
| `0x00283570` | `load_embedded_irx` | `(int *res)`: copia el IRX de `.data+0x2A3650` (857 bytes) al IOP y lo carga con `sceSifLoadModuleBuffer`; `*res &= ~3`. Devuelve el id del módulo (`0x29C4DC`) | identificada | DaniSBR1 |
| `0x0026CA18` / `0x0026CA90` | `smpd_command` / `smpd_command_async` | Envían un comando externo (`fno=0x68`) a 989snd para SMPD_IOP: cabecera `{"SMPD", cmd, tamaño}` + datos | identificada | DaniSBR1 |
| `0x001BABE8` | `config_get_int` | `(nombre, ?, sección)`: lee un valor del objeto de configuración `*0x29CC58` (p. ej. `HERO_HEAP_SIZE` de `Hero`) y crea un heap con él | investigando | DaniSBR1 |
| `0x00175AB0` / `0x00175A70` | `dict_get` / `dict_has` | Envolturas de `0x175890` (busca en el árbol de `obj+4`) | identificada | DaniSBR1 |
| `0x001769F8` | `tree_find_or_insert` | Árbol binario de nodos de 16 bytes (pool fijo de 8000, creado en `0x175D30`): clave `+0`, hijos u16 `+0xA`/`+0xC` desde la base `*0x29C4BC`; nodo vacío `*0x29C4B4` | identificada | DaniSBR1 |
| `SMPD_IOP+0x6CC` | (IOP) `FindResource`? | Registra/busca extensiones `.PAK`/`.TOC` en una tabla por nombre (`+0x40`, `+0x150`); de ahí sale el -1 de abrir `R_Perm` | investigando | DaniSBR1 |

Estados: `investigando` · `binding temporal (ret0/ret1)` · `binding real` · `reimplementada`.

## Bloqueos

Los más recientes arriba. Un bloqueo = el primer error del log que impide avanzar.

| Fecha | Síntoma (línea del log) | Causa | Solución / PR | Quién |
|---|---|---|---|---|
| 2026-10-14 | **ABIERTO.** Bucle en `pc=0x176A80`–`0x176B00` (`ra=0x1758C0`): búsqueda en un árbol binario que nunca termina | **No es un árbol roto: es un puntero nulo.** Backtrace (`GOW_HEAP_DEBUG=1`): `main` → `0x17A994` → `0x21CA60` → `0x1BAED0` → `0x1BABE8` → `0x18673C` → `dict_get` (`0x175AB0`). `0x1BABE8` pide `HERO_HEAP_SIZE` de la sección `Hero` al objeto de configuración global `*0x29CC58`, cuyo diccionario (`+0x4C`) es NULL: la raíz se lee de la dirección `0x4` (zona del kernel) y la basura de ahí forma el ciclo. El objeto sale de cargar el recurso **`R_Perm`** (`R_PERM.WAD`) con `FUN_00185F28`. Los archivos del juego **no se leen con libcdvd del EE**: van como "comandos externos" de 989snd (`fno=104`) al módulo **SMPD_IOP** (id `0x534D5044` = "SMPD"), que los busca en `GODOFWAR.TOC` y lee `PART1.PAK` desde el IOP. El comando 1 (init) va bien; el **comando 0xF (abrir `"R_Perm"`, flags `0x10000210`) devuelve -1** | Pendiente: averiguar por qué falla el `FindResource` de SMPD (`SMPD_IOP+0x6CC`: tabla de extensiones `.PAK`/`.TOC`/`.WAD`). SMPD importa `cdvdman` 6/8/11/13/28/37 (implementados en `iop_cdvd.cpp`) e `ioman` 4/5/6/8 (open/close/read/lseek: **no implementados** en `iop_ioman.cpp`, aunque en el log aún no se ven llamadas). Mirar en PCSX2 qué lee SMPD antes de abrir `R_Perm` | — |
| 2026-10-14 | Trampa fatal en `0x185F90` (bucle de `nop`; `ra=0x1BAE50`) | `iop_init` (`0x17BDB8`) carga un IRX que va embebido en `.data` (`0x2A3650`, 857 bytes, importa cdvdman) con `FUN_00283570`: lo copia al IOP (`sceSifAllocIopHeap` + `sceSifSetDma`) y llama a `sceSifLoadModuleBuffer` con la dirección **del IOP**. El runtime (`IopEmulator::loadModuleBuffer` → `readElfFromGuest`) lo lee de la memoria **del EE**, no encuentra un ELF y devuelve -1; `0x29C4DC < 0` dispara la trampa | **Resuelto:** `overrides/gow_iop.cpp` envuelve `_sceSifLoadModuleBuffer` (`0x298AD8`): copia el ELF de la memoria del IOP a un búfer del EE y llama a `loadIopModuleBuffer`. Log: `[gow:iop] IRX desde el IOP 0x192340 (857 bytes): id=9 arranque=1` | DaniSBR1 |
| 2026-10-14 | `[guest-branch:missing-target] kind=DirectCall op=JAL source=0x23fbc4 target=0x259480` | Efecto en cadena del anterior: el código nuevo (`vfn_0023fae0`) llama a funciones que Ghidra tampoco vio. Escaneo de todos los `J`/`JAL` de `.text`: **13 destinos** caen en huecos entre funciones (ninguno a mitad de una) | **Resuelto:** añadidos como `fn_XXXXXXXX` en `extra_entry_points` de `config/gow.manual.toml` | DaniSBR1 |
| 2026-10-14 | Tras arreglar el sonido: `[guest-branch:missing-target] kind=IndirectCall op=JALR source=0x180d30 target=0x239a60` | `0x239A60` es un método virtual trivial (`jr ra; nop`) que solo se llama por su vtable; Ghidra no lo ve como función. Un escaneo de punteros de `.data`/`.rodata` a `.text` fuera de toda función del CSV da **621 casos parecidos** (423 empiezan por `jr ra`) | **Resuelto:** los 621 añadidos como `extra_entry_points` (`vfn_XXXXXXXX`) en `config/gow.manual.toml` | DaniSBR1 |
| 2026-10-14 | Tras cargar los 8 IRX: 2 hilos activos, `[run:tick]` fijo en `pc=0x26c0e8` y el juego sigue presentando frames. En el IOP, `989NOMID.IRX` imprime `"failed to allocate memory, probably going to hang soon!"` y 15 `"989snd Error: cause …"` (el printf del IOP no formatea los `%d`) | **Dos límites del IOP emulado (ps2xIOP).** (1) `SMPD_IOP.IRX` pide con `AllocSysMemory(1, 0x11ADF8)` ~1,1 MB al arrancar y el heap del IOP solo tiene 832 KB (`IopMemory::HeapBase = 0x120000`–`HeapLimit = 0x1F0000`; los módulos van aparte, en `0x10000`–`0x120000`, y solo llegan a ~`0x5A000`). El mensaje "failed to allocate memory" es de SMPD, no de 989snd. (2) La RPC de 989snd (SID `0x123456`) responde `{0xFFFFFFFF, resultado, 0xFFFFFFFF}` y el EE espera esas marcas en el búfer de recepción (`0x305600`, armado por `0x26BC40`, sondeado por `0x26BB98`). El comando 104 ("external command") lo reenvía 989snd a un manejador de SMPD_IOP que tarda más de **2 millones de instrucciones** (busca nombres en una tabla de 152 entradas, `SMPD_IOP+0x18C`); `callFunction` del IOP corta a los 2 M (`kMaxCallInstructions`) y devuelve basura (`ret=0x98`), sin las marcas | **Resuelto en el fork de PS2Recomp** (`DaniSBR1/PS2Recomp`, rama `godofwar`, commit `cdd8e0b`): `HeapBase = 0x70000` (heap de 1,5 MB) y `kMaxCallInstructions = 100'000'000`. Con ellos, `fno=104` devuelve `FFFFFFFF 00000000 FFFFFFFF` y el juego sigue | DaniSBR1 |
| 2026-10-14 | Tras pasar el bind de CD, `[run:tick]` fijo en `pc=0x27ae08 ra=0x27ae3c`. 4 de los 8 IRX fallan: `failed to open IRX 'IOP_MOD/mc2_d.irx'` (también `ds2u_d`, `989nomid` y `smpd_iop`) | **El juego cree que no arrancó desde el disco.** `SetupThread` de PS2Recomp no rellena `_args` (`0x2C7080`), así que `main` recibe `argc = 0` y `boot_mode_init` (`0x17AA88`) elige el modo 2 (desarrollo): no reinicia el IOP con `IOPRP300.IMG` y pide los módulos como `IOP_MOD/x.irx`. Sin `DS2U_D.IRX`, `sceDbcInit` (`0x27AD00`) espera para siempre el servidor `0x8000131C` | **Resuelto:** `overrides/gow_boot.cpp` envuelve `main` y rellena `_args` con `argc = 1, argv[0] = "cdrom0:\SCUS_973.99;1"` (el `BOOT2` de `SYSTEM.CNF`). Ahora los 8 IRX se cargan desde `cdrom0:` en el IOP emulado y se pasa libdbc | cxldsakeb |
| 2026-10-14 | Bucle de espera en `0x27A768` (`FUN_0027a6b8`) reintentando `0x297290(…, 0x80000593, 0)` | `0x27A6B8` es `_CdCheckSCmd` de libcdvd: espera a que el bind al servidor de comandos S de CD (`0x80000593`) devuelva `server != 0`. El runtime no emula ese servidor, solo da stubs HLE de `sceCd*`. La única función de comandos S que usa el juego es `sceCdReadDvdDualInfo` (`0x27AB00`, comando `0x27`), y no tenía stub. La llama `iop_init` en bucle | **Resuelto:** `overrides/gow_cd.cpp` reimplementa `sceCdReadDvdDualInfo`: `on_dual = 1`, `layer1_start = 2080560`, detectado en la ISO como en PCSX2 (PVD de la capa 1 en el sector del tamaño del volumen de la capa 0) | cxldsakeb |
| 2026-10-14 | Tras arreglar el heap: el juego sube una textura de 114 KB, dibuja un sprite y presenta frames (`[frame:upload]` 512×448). Luego `[run:tick]` muestra `pc=0x27a768 ra=0x27a79c` | Bucle de espera en `FUN_0027a6b8`: cuenta atrás de `0x100000` (`0x27A768`) y reintenta `0x297290(a0, 0x80000593, 0)`. Por el patrón, es `sceSifBindRpc` al servidor RPC `0x80000593` de `libcdvd` en el IOP (¿`sceCdSearchFile`?): el juego espera a que el servidor de CD/DVD responda y el runtime no lo ofrece. **Sin confirmar** | Diagnóstico inicial; la causa y la solución están en la fila del bucle de `0x27A768` (arriba) | DaniSBR1 |
| 2026-10-14 | Tras los constructores y los primeros paquetes GIF al GS, `[run:tick]` muestra el PC alternando entre `0x13dcd8` y `0x13de00` (`ra=0x13fd58`) durante todo el minuto. `dma=2 gif=1 vif=0`, un solo hilo activo, sin errores en stderr | **El runtime escribe encima del heap del juego.** `FUN_0013dc78` es el **malloc** del juego y `0x13DCD8`↔`0x13DE00` es el recorrido de su lista de libres (`0x01FFFFFF` = máscara del tamaño, `0x40000000` = bit de "ocupado", no floats). En `0x138CB0` el juego crea el heap **"Root"** con toda la RAM: de `align16(fin de .bss) = 0x35C1B0` a `0x1FF7FF8`. El syscall `SetupHeap` fija el heap del runtime (`guestMalloc`) **también en `0x35C1B0`**. El stub HLE de `sceGsResetGraph` (`GS.cpp`, modo 0) reserva ahí 128 bytes para su paquete GIF y pisa la cabecera de "Root". Con `GOW_HEAP_DEBUG=1` se ve: tras `[gs:reg] reg=0x41/0x42`, el heap pasa a llamarse "B" (`0x42`), pierde el magic y su primer libre vale `0x10000000` (la mitad alta del GIFtag, `NREG=1`). El malloc de `FUN_0013fca8` recorre esa lista rota para siempre | **Resuelto:** `overrides/gow_memory.cpp` (TEMPORAL) recorta 1 MB del final de "Root" al crearlo y configura ahí el heap del runtime (`configureGuestHeap`). Diagnóstico: `overrides/gow_heap_debug.cpp` con `GOW_HEAP_DEBUG=1` | DaniSBR1 |
| 2026-10-14 | `[guest-branch:missing-target] kind=IndirectCall op=JALR source=0x28374c target=0x29bb78` (el juego se queda girando ahí) | Constructor global solo referenciado desde la tabla de datos `0x2A6170`; el analyzer heurístico no lo ve como función | **Resuelto con Ghidra:** su mapa de funciones incluye `0x29BB78`, `0x29BB90` y `0x26B6B0` (la otra entrada de la tabla, que fue el bloqueo siguiente con el analyzer) | cxldsakeb |
| 2026-10-14 | Miles de `LNK2019` al compilar el runner tras recompilar | MSBuild usa el `.vcxproj` viejo aunque CMake regenere el glob a mitad del build | `recompile.ps1` reconfigura CMake después de copiar los archivos | cxldsakeb |

## Cambios necesarios en PS2Recomp

Arreglos que no son específicos de God of War y deberían ir al runtime o al recompilador (y, si se puede, como PR al proyecto original).

- **`loadModuleBuffer` lee el IRX de la memoria del EE.** `IopEmulator::loadModuleBuffer` usa `readElfFromGuest`, pero `sceSifLoadModuleBuffer` recibe una dirección del IOP (el juego copia el módulo allí con `sceSifSetDma`). Debería leer de `IopMemory`. Parche temporal: `overrides/gow_iop.cpp`.
- **`printf` del IOP sin formato:** **hecho en el fork** (`iop_stdio.cpp`, `%d %i %u %x %X %o %c %p %s` con anchos); queda proponerlo al original.
- **`ioman` del IOP sin `open`/`close`/`read`/`lseek`** (`iop_ioman.cpp` solo tiene `AddDrv`/`DelDrv`). SMPD_IOP los importa.
- **El heap del IOP es demasiado pequeño.** `ps2xIOP/src/emulator/core/iop_memory.h`: `HeapBase = 0x120000` deja 832 KB de heap y reserva ~1 MB solo para módulos, que en God of War usan ~300 KB. En la PS2 real, módulos y heap comparten los 2 MB a través de sysmem. Propuesta: que el cargador de módulos reserve con el mismo allocator (o que `HeapBase` arranque tras el último módulo). **Hecho en el fork** (`0x70000`); queda proponerlo al original.
- **Presupuesto de las llamadas síncronas del IOP.** `kMaxCallInstructions = 2'000'000` (~54 ms de IOP real) corta manejadores RPC legítimos y devuelve basura sin avisar. **Hecho en el fork:** 100 M y aviso `[IOP] call to 0x… did not return` en el log; queda proponerlo al original.
- **El allocator del IOP no reutiliza memoria liberada.** `IopMemory::allocate` siempre busca a partir de `m_heapCursor`, que solo avanza; `FreeSysMemory` no devuelve el hueco. Tarde o temprano, un juego que reserve y libere se quedará sin memoria.
- **`guestMalloc` toma memoria que el juego cree suya.** Los stubs HLE (`GS.cpp`, `SIF.cpp`, `MPEG.cpp`, `Font.cpp`, pilas en `Thread.cpp`…) reservan con `guestMalloc`, cuyo heap empieza donde dice `SetupHeap` (el final de .bss). Un juego que gestiona él mismo toda la RAM a partir de ahí, como God of War, sale corrupto. Propuesta: que el runtime tenga una zona propia y configurable para sus reservas, o que cada juego pueda declararla. Mientras tanto, `overrides/gow_memory.cpp`.
- **`SetupThread` no rellena los argumentos de arranque.** El kernel de la PS2 copia `argc`/`argv` (la ruta de `BOOT2` de `SYSTEM.CNF`, por ejemplo `cdrom0:\SCUS_973.99;1`) en el búfer `_args` que el crt0 le pasa en `$a3`. El de PS2Recomp (`Kernel/Syscalls/System.cpp`) lo ignora y `main` recibe `argc = 0`. Los juegos que deciden desde dónde cargar mirando `argv[0]`, como God of War, toman la ruta de desarrollo. Propuesta: rellenar `_args` con `cdrom0:\<ELF>;1`. Mientras tanto, `overrides/gow_boot.cpp`.
- **Falta `sceCdReadDvdDualInfo` en `Stubs/CD.cpp`.** Cualquier juego en DVD de doble capa que la llame se queda en el bucle de bind de `_CdCheckSCmd`. Nuestra versión (`overrides/gow_cd.cpp`) detecta la capa 1 en la ISO igual que PCSX2 y se puede pasar tal cual al runtime.
