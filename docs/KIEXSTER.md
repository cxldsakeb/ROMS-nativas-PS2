# Parches de KIexster: qué son y cuáles tenemos

[KIexster/god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp) (GPLv3) porta el mismo juego con PS2Recomp y aplica **118 parches** sobre `c5a9d02`. Este documento sirve para repartirlos por frentes (ver "Reparto de trabajo" en `CLAUDE.md`) y saber cuáles faltan por adaptar.

**Cómo se hizo (2026-10-15):** DeepSeek (`scripts/deepseek.ps1`) clasificó los parches y los cruzó con los 11 commits de nuestro fork (`godofwar` en `ed11610`). Lo comprobado:
- las 118 filas están completas y en orden;
- la columna "Recompilar todo" se calculó también por script y coincide;
- cada "sí" o "parcial" cita un commit nuestro que toca los mismos archivos;
- se revisaron a mano 9 filas (`vif1-unpack-formats`, `vu1-zero-factor`, `gs-host-timing`, `iop-sound`, `audio-pcm`, `vu1-sticky-preserve`, `gs-opengl`, `vu-jump` y `spr-chain`).

Las frases son resúmenes automáticos: antes de adaptar un parche, léelo.

- **Recompilar todo** = el parche toca `ps2xRecomp/` o `ps2_runtime_macros.h`, así que cambia el código generado y hace falta `recompile.ps1` completo. Son 8.
- **Sus overrides no están en los parches.** Viven en `src/gow_overrides.cpp` de su repo: el IPU (`gowIpuInit`, traído a `overrides/gow_fmv.cpp`), el salto de vídeos (`GOW_SKIP_FMV`), el mando por SIO2 (`gow_pad2_packet.h`), sondas de diagnóstico, etc.

## Prueba de su runtime con nuestro juego (2026-10-15)

Sus 118 parches, más los commits del original #271 y #274, compilados en la rama local `kiexster-base` del submódulo y con **nuestros** overrides:

| | Nuestro fork (`ed11610`) | Su runtime, GS por CPU | Su runtime, OpenGL (`PS2X_GS_GPU=1`) |
|---|---|---|---|
| Lecturas del mando por segundo (≈ FPS de la lógica) | 5,5 | ~2,8 | **41–71** |
| Llega al menú principal | en minutos | sí | **a los 8 s** |
| Kratos en el menú | blanco | blanco | **blanco** |
| Tras elegir la dificultad | — | parado en una escena con un árbol | parado en una escena con un árbol (desde los 24 s) |
| Ídem, con `overrides/gow_fmv.cpp` | sin probar | sin probar | **se reproduce la intro** (Kratos en el acantilado, a los 32 s) |

- **Kratos blanco no se arregla con sus parches del runtime.** La causa está en sus overrides o su configuración, o en algo nuestro.
- **El parón tras elegir la dificultad** era el primer vídeo. El `sceIpuInit` genérico del runtime usa direcciones de otro juego. Se arregla con `gow_fmv.cpp`, adaptado de su `gowIpuInit` (ver la fila en NOTES.md).
- Con `PS2X_VU1_HILO=1` y `PS2X_GS_FRENTE=1`, el resultado es el mismo.
- Su contador `GOW_PERF_DIAG` siempre da 0 con nuestros overrides, así que no sirve para medir.

## Resumen

| Frente | Parches | Ya los tenemos | Parcial | Faltan |
|---|---|---|---|---|
| Gráficos | 42 | 5 | 0 | 37 |
| Rendimiento | 47 | 0 | 0 | 47 |
| Sistema | 8 | 0 | 2 | 6 |
| Base (compartido) | 12 | 8 | 2 | 2 |
| Diagnóstico | 9 | 0 | 0 | 9 |

## Parches por frente

El número es el orden de aplicación (el de `scripts/compilar.ps1` en su repo).

### Gráficos

| # | Parche | Qué hace | ¿Lo tenemos? | Recompilar todo |
|---|---|---|---|---|
| 3 | `xgkick` | Vuelca opcionalmente el paquete XGKICK antes de que instrucciones VU posteriores reescriban el búfer de origen. | no | no |
| 4 | `vif-unpack` | Corrige el UNPACK de VIF1: V4-5 expande RGB/alpha y V2 escribe XYXY como el hardware. | sí (0007: V4-5 a 8 bits por canal y V2 escribe XYXY, igual que en 0007.) | no |
| 7 | `vu-jump` | Hace que JR/JALR de VU1 lean el VI actual y solo las ramas condicionales usen el valor retrasado. | sí (0006: JR/JALR leen el VI actual en ps2_vu1_lower.cpp.) | no |
| 8 | `vu-efu` | Corrige el coeficiente t^7 de EFU y EATAN de VU1 según PCSX2. | sí (0011: Coeficiente de t^7 -0.139085... idéntico.) | no |
| 15 | `gs-opengl` | Añade el backend OpenGL (gs/gpu) y la cola del fork SotC como renderizador del GS. | no | no |
| 17 | `gs-presentation` | Corrige la presentación (procedencia, campos/progresivo y páginas DISPFB). | no | no |
| 18 | `vif-direct` | Arregla el flujo DIRECT/IMAGE de VIF1: una IMAGE pendiente continúa con el siguiente DIRECT. | no | no |
| 19 | `vif-direct-fragments` | Conserva el tamaño de un DIRECT entre bloques DMA/FIFO y completa la IMAGE pendiente. | no | no |
| 20 | `gif-order` | Corrige el orden del árbitro GIF entre DIRECTHL/IMAGE y los FIFO de cada PATH. | no | no |
| 21 | `gif-tag-semantics` | Aplica la semántica de NLOOP=0 y PRE/PRIM en PACKED, REGLIST e IMAGE. | no | no |
| 22 | `gif-stream` | Reescribe el flujo GIF con un cursor por PATH sin retener paquetes completos. | no | no |
| 23 | `gif-image-order` | Clasifica el flujo GIF completo (payloads que parecen GIFtags) y la prioridad IMAGE. | no | no |
| 24 | `gif-image2` | Añade compatibilidad IMAGE2 (FLG=3) como GSState::Transfer de PCSX2. | no | no |
| 25 | `gif-native-tag` | Valida la cadena DMA completa antes de aplicar PRE/Q y conserva el atajo de texturas. | no | no |
| 26 | `gs-image-fragments` | Preserva el píxel CT24/Z24 que atraviesa dos bloques de IMAGE. | no | no |
| 27 | `gs-triangle-sampling` | Iguala el muestreo de triángulos del GS (centro entero y lados incluidos). | no | no |
| 28 | `gs-sprite-sampling` | Corrige el muestreo de sprites (ejes firmados y scissor que solo recorta cobertura). | no | no |
| 32 | `gs-triangle-constants` | Conserva atributos constantes entre vértices (un alpha 128 no cae a 127). | no | no |
| 33 | `gs-triangle-texcoords` | Interpola S/T/Q homogéneos por diferencias antes de dividir por Q. | no | no |
| 34 | `vu1-efu-opcodes` | Usa los códigos y latencias EFU reales de VU1 (LowerOP de PCSX2). | sí (0011: Códigos y latencias EFU (0x79/0x7C/0x7D/0x7E); está en 0011.) | no |
| 35 | `gs-cpu-state` | Permite exportar/importar el estado del backend CPU del GS para repetir comandos. | no | no |
| 36 | `gs-cpu-rounding` | Aísla el modo de redondeo SSE del GS del que usan EE/VU. | no | no |
| 44 | `gs-triangle-precision` | Conserva la precisión de los pesos de triángulo (recíproco double y bits de profundidad). | no | no |
| 45 | `gs-packed-depth` | Conserva Z32 en PACKED XYZ2/XYZ3 sin perder bits en float. | no | no |
| 46 | `gs-bilinear-precision` | Iguala las fracciones de 4 bits y el truncado por etapa del bilineal (PCSX2). | no | no |
| 47 | `gs-nearest-stq` | Convierte STQ por 16.16 antes de elegir el texel en nearest. | no | no |
| 48 | `gs-feedback-snapshot` | Configura una fuente disjunta para el feedback del GS sin releer la VRAM que escribe. | no | no |
| 49 | `gs-bilinear-stq` | Convierte STQ a 16.16 antes del medio texel también en bilineal. | no | no |
| 50 | `gs-region-pages` | Corrige REGION_CLAMP/REPEAT y la clave de la caché de páginas de lectura. | no | no |
| 51 | `gs-target-alias` | Ordena las primitivas cuando color y Z reinterpretan las mismas páginas de VRAM. | no | no |
| 55 | `vu1-sticky-preserve` | Evita que se pierdan los bits persistentes de status de VU1. | no | no |
| 63 | `gs-coordinate-alias` | Separa páginas de VRAM solapadas cuando XY deja de identificar un pixel único. | no | no |
| 64 | `gs-stq-hardware-test` | Repite las pruebas STQ hasta observar hardware (compilación asíncrona de shaders). | no | no |
| 75 | `gs-shared-host` | Desacopla la presentación del GS: host present compartido y captura explícita del CRTC. | no | no |
| 97 | `gs-source-address` | Normaliza las direcciones físicas de TEX0/DISPFB para comparar fuentes de textura. | no | no |
| 104 | `gs-fog-latch` | XYZF escribe el atributo FOG aunque no dibuje y XYZ lo conserva. | no | no |
| 105 | `gs-fog-math` | Corrige la niebla: pesos que suman 256 y un solo truncado. | no | no |
| 106 | `gs-zte` | ZTE=0 dibuja sin prueba ni escritura Z, ignorando ZTST. | sí (0008: ZTE=0 sin test ni escritura Z en el backend CPU; la parte del shader GPU no existe en nuestro fork.) | no |
| 107 | `gs-cpu-colclamp` | COLCLAMP=0 envuelve a ocho bits el resultado de ALPHA, incluidos negativos. | no | no |
| 108 | `gs-texa-depth` | Aplica la misma semántica TEXA/TA0/TA1/AEM a las texturas depth alias. | no | no |
| 109 | `gs-pcrtc-blend` | Iguala el mezclado del compositor PCRTC (RC1/RC2, BGCOLOR y ALP255). | no | no |
| 111 | `gs-reset-crtc` | Programa el banco CRTC bajo el mutex de snapshot sin escribir ALPHA_1. | no | no |

### Rendimiento

| # | Parche | Qué hace | ¿Lo tenemos? | Recompilar todo |
|---|---|---|---|---|
| 14 | `perf` | Añade un perfil de tiempos exclusivos por hilo (ps2_perf). | no | no |
| 29 | `iop-fast` | Añade caminos rápidos en línea al intérprete del IOP (RAM alineada y stubs «jr ra»). | no | no |
| 37 | `getenv-hot` | Lee una sola vez getenv (XGKICK y traza del IOP) para no recorrer el entorno en bucles calientes. | no | no |
| 38 | `vu1-perf` | Optimiza VU1: bitmasks de colas pendientes, normalizeOperand en línea y recorrido de bits activos. | no | no |
| 52 | `vu1-direct` | VU1 escribe VF/VI/ACC al ejecutar en vez de encolarlos con latencia. | no | no |
| 53 | `vu1-compiled` | Añade el microcódigo de VU1 compilado a C++ (tools/vu1) con su despachador. | no | no |
| 56 | `vu1-blocks` | Divide el código VU1 compilado en bloques con funciones expandibles siempre. | no | no |
| 57 | `vu1-runner` | Enlaza el microcódigo VU1 compilado y lo usa en el despachador (CMake). | no | no |
| 58 | `iop-scheduler` | Optimiza el planificador del IOP con una sola pasada y versionado de la lista. | no | no |
| 59 | `vif1-unpack-fast` | Añade un atajo al UNPACK de VIF1 sin máscara ni fila (STMOD 0/3). | no | no |
| 60 | `ee-timers-fast` | Guarda GIF_STAT en m_ioRegisters para acelerar advanceEeTimers. | no | no |
| 61 | `vu1-simd` | Añade carriles SSE4.1 al código VU1 compilado (normalizeOperand y máscaras DEST). | no | no |
| 62 | `vu1-lazy-flags` | Confirma los flags MAC/estado/clip de VU1 de forma perezosa cuando se leen. | no | no |
| 65 | `gs-finish-async` | Encola FINISH en el hilo del GS sin esperar ni glFinish (GOW_GS_FINISH_ASINCRONO). | no | no |
| 66 | `gs-hardware-profile` | Adjunta el profiler de hardware del GS antes de profEnd y deja fuera el trabajo CPU. | no | no |
| 67 | `gs-profile-async` | Recolector propio de timestamps OpenGL sin esperar QUERY_RESULT hasta el cierre. | no | no |
| 68 | `vu1-fast-flags` | Calcula MAC/status de FMAC desde movemask en el código VU1 compilado. | no | no |
| 69 | `gs-upload-ring` | Añade un anillo de uploads persistentes protegidos por fences al backend GPU. | no | no |
| 70 | `vu1-zero-factor` | Calcula directamente el producto con factor cero exacto en VU1. | no | no |
| 71 | `vu1-direct-store` | Escribe en el acto el store de VU1 con DEST constante. | no | no |
| 72 | `vu1-cold-scalar` | Aparta la ruta escalar de VU1 en una función no inline para el código compilado. | no | no |
| 73 | `vu1-simd-zero` | Extiende los casos de factor cero o ±1 a los carriles SIMD de VU1. | no | no |
| 74 | `vu1-xgkick-flush` | Vuelca de una vez los ciclos restantes de PATH1 (XGKICK) al terminar un programa. | no | no |
| 76 | `vu1-generator-dir` | Pasa la carpeta del generador VU1 en vez de cada archivo (límite de línea de órdenes). | no | no |
| 77 | `gs-discard-uncovered` | Descarta los fragmentos fuera del rectángulo conservador de triángulos finos (variante opcional). | no | no |
| 78 | `dma-iop-timers-cache` | Cachea el próximo evento de los temporizadores del IOP y los búferes de cadenas DMA. | no | no |
| 79 | `vif1-ee-timers-fast` | Acelera los temporizadores EE/BUSCLK y añade un atajo al UNPACK de VIF1. | no | no |
| 80 | `vu1-thread` | Procesa VIF1/VU1 en un hilo propio (PS2X_VU1_HILO). | no | no |
| 81 | `gs-front-thread` | Procesa los paquetes GIF de PATH1/PATH2 en otro hilo (PS2X_GS_FRENTE). | no | no |
| 82 | `vif1-unpack-formats` | Reescribe UNPACK de VIF1 con un bucle por formato (mismo resultado). | no | no |
| 83 | `vu1-block-check-cache` | Recuerda el resultado de comparar cada bloque contra la micromemoria. | no | no |
| 84 | `vu1-chained-steps` | Trata los pares encadenados de VU1 sin recomprobar precondiciones. | no | no |
| 85 | `game-speed` | Mide e imprime la velocidad del juego (tiempo de juego / tiempo real). | no | no |
| 86 | `vu1-fast-pairs` | Optimiza los pares de VU1: un store de 16 bytes y tabla de MAC/status. | no | no |
| 87 | `vu1-path1-events` | Avanza PATH1 de VU1 por eventos en lugar de ciclo a ciclo. | no | no |
| 88 | `vu1-lazy-decode` | Decodifica cada par de VU1 la primera vez que se pide (caché perezosa). | no | no |
| 89 | `gif-arbiter-buffers` | Reutiliza búferes de paquetes ya procesados en el árbitro GIF. | no | no |
| 90 | `vif1-unpack-mask-sse` | Añade una versión SSE al UNPACK con máscara o fila (STMOD 1/2). | no | no |
| 91 | `vu1-xgkick-bulk` | Copia de una vez la carga útil de una GIFtag en XGKICK. | no | no |
| 92 | `vu1-advance-events` | Salta de evento en evento durante las esperas de VU1. | no | no |
| 93 | `vu1-lower-specials` | Incluye en línea en el código compilado los lectores de flags, BAL, JR y JALR de VU1. | no | no |
| 94 | `vu1-static-stores` | Traduce en línea en el código compilado las inferiores que escriben VF (LQ, MOVE, LQI…). | no | no |
| 95 | `vu1-simd-specials` | Hace ITOF, FTOI y ABS en los cuatro carriles SIMD y el DEST parcial por mezcla. | no | no |
| 110 | `gs-host-timing` | Instrumenta los tiempos del host (HostWait) con un reloj inyectable. | no | no |
| 115 | `spu2-perf` | Mide por secciones el tiempo del SPU2 (voces, envolventes, ADPCM y salida). | no | no |
| 117 | `sampler` | Perfilador por muestreo de pila de hilos (GOW_SAMPLER, solo Windows). | no | no |
| 118 | `checkpoint-fast` | Salta los ciclos ociosos del IOP hasta el primer evento (idleUntil). | no | no |

### Sistema

| # | Parche | Qué hace | ¿Lo tenemos? | Recompilar todo |
|---|---|---|---|---|
| 9 | `mpeg-nodata` | Registra y ejecuta el callback sceMpegCbNodata en el hilo actual y reevalúa GetPicture. | no | no |
| 10 | `spu2` | Implementa el núcleo SPU2 (DMA MADR-TSA, voces, ADSR y fin de bloque). | no | no |
| 12 | `spu2-output` | Saca la salida estéreo del SPU2 a 48 kHz hacia el audio del host. | no | no |
| 13 | `sio2` | Implementa SIO2 y la memory card (DMA 11/12 y comandos del mando). | no | no |
| 40 | `mpeg-create` | Conserva la cabecera de secuencia ya demultiplexada al crear el MPEG. | no | no |
| 42 | `iop-sound` | Arregla la espera de semáforos fuera de hilo para que 989snd no se bloquee. | parcial (0005: 0005 sí espera en WaitSema sin hilo (syncWaitPump), pero falta sceSifGetOtherData (copia EE→IOP).) | no |
| 43 | `memcard` | Corrige LWL/LWR y la espera de eventos de la memory card desde el servidor RPC. | parcial (0005: 0005 sí espera WaitEventFlag sin hilo; faltan LWL/LWR combinadas, sceSifSetDmaIntr y sceCdReadClock.) | no |
| 116 | `mpeg-sync-callbacks` | Ejecuta los callbacks de flujo del MPEG antes de volver al llamador (no diferidos). | no | no |

### Base (compartido)

| # | Parche | Qué hace | ¿Lo tenemos? | Recompilar todo |
|---|---|---|---|---|
| 1 | `runtime` | Arranque del runtime para GoW: heap del IOP y del runtime, sprintf del IOP, argumentos de los módulos, asentamiento de hilos tras RPC, heap de SetupHeap, pila de interrupciones e IRQ del GS. | parcial (0003, 0004, 0005: Coinciden el heap del IOP (0x70000), printf/sprintf/vsprintf, las esperas síncronas IOP/EE, la IRQ del GS y las pilas de interrupción. Faltan gow_stub_services.cpp, dbcman.cpp, iop_subsystem/module_factories y demás pegamento del port.) | no |
| 2 | `checkpoint` | Cuenta los puntos de control del ejecutor del EE para no colapsar un call que cae en el mismo punto. | sí (0010: m_eeCheckpointSerial y su comprobación en el despachador están en 0010.) | no |
| 6 | `heap` | Fija el heap interno del runtime (guestMalloc) en un rango aparte del heap del juego. | no | no |
| 11 | `fpu-roots` | Corrige SQRT.S/RSQRT.S de la FPU del R5900 (radicando en FT, FS/sqrt). | sí (0010: SQRT.S lee FT y RSQRT.S = FS/sqrt(FT) (ps2FpuSqrt/ps2FpuRsqrt).) | si |
| 16 | `ee-fixes` | Corrige FPU/MMI/VU del EE: comparaciones de 64 bits, sin inf/NaN y VDIV/VSQRT/VRSQRT. | sí (0010: FPU/VU0 sin infinitos, GPR_S64 en ramas, LQ/SQ alineadas: todo en 0010.) | si |
| 30 | `vu0-macro` | Corrige VU0 en modo macro (VRNEXT/LFSR, VLQI/VSQI y cadenas GIF largas). | sí (0010: VLQI/VSQI/VLQD/VSQD/VILWR/VISWR en memoria VU0, R/LFSR, VFTOI/VABS y cadenas DMA >4096: está en 0010.) | si |
| 31 | `mmi` | Corrige la saturación de PADDUH/PSUBUH y la permutación MMI según PCSX2. | sí (0010: PADDUH/PSUBUH y permutaciones MMI; está en 0010.) | si |
| 39 | `spr-chain` | Implementa las cadenas DMA del scratchpad (canales 8/9) que GoW usa para la paleta de huesos. | parcial (0007: 0007 sí hace la cadena de destino fromSPR (la que usa GoW) y el intercalado, pero no la cadena de origen toSPR.) | no |
| 96 | `ee-cop0-count` | Actualiza COP0 Count por ciclos del EE y lo comparte entre hilos. | no | si |
| 102 | `ee-pmulth` | Calcula PMULTH con ocho productos separados y ambos pares HI/LO. | sí (0010: PMULTH con ocho productos y ambos HI/LO; está en 0010.) | si |
| 112 | `vu0-vf0` | Trata VF0 de VU0 como registro constante (no se escribe). | sí (0010: VF0 constante (PS2_VU0_VF_WRITE, LDC2 rt=0); está en 0010.) | si |
| 113 | `vu0-vi0` | Trata VI0 de VU0 como registro constante cero. | sí (0010: VI0 constante cero (VMTIR rt=0); está en 0010.) | si |

### Diagnóstico

| # | Parche | Qué hace | ¿Lo tenemos? | Recompilar todo |
|---|---|---|---|---|
| 5 | `vif-diagnostic` | Añade trazas a VIF1 y VU1 para depurar el UNPACK y el flujo VIF. | no | no |
| 41 | `audio-pcm` | GOW_AUDIO_PCM vuelca a fichero las muestras emuladas del SPU2. | no | no |
| 54 | `vu1-budget-diag` | Observa las ejecuciones de VU1 cortadas por presupuesto (GOW_VU1_BUDGET_DIAG). | no | no |
| 98 | `gs-host-frame-diag` | Observa la textura adquirida del host sin otra lectura ni barrera. | no | no |
| 99 | `gs-host-diag-window` | Añade una ventana opcional del registro de adquisiciones del host (no controla render). | no | no |
| 100 | `gs-acquired-mode-diag` | Registra el modo del request que produjo cada textura adquirida. | no | no |
| 101 | `gs-host-acquired-capture` | Captura explícita y acotada de la textura adquirida (desactivada por defecto). | no | no |
| 103 | `gs-external-observer` | Enlaza un observador externo opcional al GS que recibe estado coherente del EE. | no | no |
| 114 | `stall-diag` | Vuelca hilos, semáforos y flags del IOP/EE que están esperando (GOW_STALL_DIAG). | no | no |
