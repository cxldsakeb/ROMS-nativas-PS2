// Memoria: reparto de la RAM entre el heap de God of War y el allocator del runtime.
//
// Problema (bloqueo de FUN_0013dc78, ver docs/NOTES.md): al arrancar, el juego crea su heap "Root"
// con TODA la RAM libre, desde el final de .bss (0x35C1B0) hasta justo debajo de la pila
// (0x1FF7FF8). Pero varios stubs HLE del runtime (sceGsResetGraph, SIF, MPEG, pilas de hilos...)
// reservan memoria del juego con guestMalloc(), cuyo heap también empieza en 0x35C1B0 (lo fija el
// syscall SetupHeap). El primer paquete GIF de sceGsResetGraph se escribía encima de la cabecera de
// "Root" y el siguiente malloc del juego recorría una lista de libres rota para siempre.
//
// Solución: cuando el juego crea "Root", se le quita 1 MB del final y ese hueco pasa a ser el heap
// del runtime. En la PS2 real esto no hace falta porque esas funciones son código del propio juego
// y usan sus búferes; aquí son C++ nativo y necesitan memoria aparte.
//
// [DaniSBR1] TEMPORAL: si en algún nivel el juego se queda sin memoria, revisar el tamaño de la
// reserva. La solución de fondo sería que el runtime no tomase memoria del juego (ver NOTES.md,
// "Cambios necesarios en PS2Recomp").
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"

#include <iomanip>
#include <iostream>

namespace
{
    // heap_create(mem, bytes, nombre, n)
    constexpr uint32_t kMemHeapCreateAddr = 0x0013D710u;
    // El juego lo crea en 0x138CB0 con mem = align16(fin de .bss) y bytes = 0x1FF7FF8 - mem
    constexpr uint32_t kMemRootHeapBase = 0x0035C1B0u;
    constexpr uint32_t kMemRuntimeReserve = 0x00100000u; // 1 MB para guestMalloc

    PS2Runtime::RecompiledFunction g_memOriginalHeapCreate = nullptr;
    bool g_memRootShrunk = false;

    void heapCreateReservingRuntimeMemory(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mem = getRegU32(ctx, 4);
        const uint32_t bytes = getRegU32(ctx, 5);

        if (g_memRootShrunk || mem != kMemRootHeapBase || bytes <= kMemRuntimeReserve * 2u)
        {
            g_memOriginalHeapCreate(rdram, ctx, runtime);
            return;
        }
        g_memRootShrunk = true;

        const uint32_t rootEnd = mem + bytes;
        // El hueco del runtime empieza alineado a 64 bytes (la alineación más estricta que pide el DMA)
        const uint32_t runtimeBase = (rootEnd - kMemRuntimeReserve + 0x3Fu) & ~0x3Fu;
        const uint32_t runtimeLimit = rootEnd & ~0xFu;

        // $a1 = nuevo tamaño de "Root". El R5900 guarda los valores de 32 bits extendidos a 64 con signo.
        ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(runtimeBase - mem)));
        g_memOriginalHeapCreate(rdram, ctx, runtime);

        // Lo que el runtime hubiera reservado antes de este punto (con base 0x35C1B0) se pierde;
        // en el arranque actual no hay ninguna reserva previa.
        runtime->configureGuestHeap(runtimeBase, runtimeLimit);

        std::cout << std::hex << std::setfill('0')
                  << "[gow:mem] Root reducido a 0x" << std::setw(8) << mem << "-0x" << std::setw(8) << runtimeBase
                  << "; heap del runtime en 0x" << std::setw(8) << runtime->guestHeapBase()
                  << "-0x" << std::setw(8) << runtime->guestHeapLimit()
                  << std::dec << std::setfill(' ') << std::endl;
    }

    void applyMemoryOverrides(PS2Runtime &runtime)
    {
        if (!runtime.hasFunction(kMemHeapCreateAddr))
        {
            std::cerr << "[gow:mem] falta heap_create en el código generado; no se reserva memoria para el runtime" << std::endl;
            return;
        }
        // Si el diagnóstico del heap está activo, esto devuelve su envoltorio y las dos capas se encadenan
        g_memOriginalHeapCreate = runtime.lookupFunction(kMemHeapCreateAddr);
        runtime.registerFunction(kMemHeapCreateAddr, &heapCreateReservingRuntimeMemory);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-memory",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyMemoryOverrides);
