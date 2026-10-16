// Vídeos (FMV): inicialización del IPU y, opcionalmente, salto de los vídeos.
//
// Al elegir la dificultad en "New Game", el juego pone el primer vídeo y llama a sceIpuInit (0x279600).
// El stub genérico del runtime (PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/IPU.cpp) lleva direcciones de
// otro juego: llama a "SetD4_CHCR" en 0x126428, que en este ELF cae en medio de otra función
// (FilteredCopyTile), y carga las tablas del IPU desde 0x1721E0/0x172230. El juego se quedaba parado en la
// pantalla de carga. El sceIpuInit real llama a SetD4_CHCR (0x279588) y carga las tablas desde 0x2A1610
// (IQ) y 0x2A1660 (VQ): aquí se hace lo mismo escribiendo directamente en los registros del DMA y del IPU.
//
// Adaptado de KIexster/god-of-war-recomp (GPLv3), src/gow_overrides.cpp: gowIpuInit y gowSkipMovie*.
//
// GOW_SKIP_FMV=1 salta los vídeos en su API de carga, antes de que reserven búferes o hagan RPC. Sirve
// para investigar lo que viene después mientras el decodificador no esté listo; según KIexster, con él
// el framebuffer de la partida queda negro, así que no vale para jugar.
//
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint32_t kFmvIpuInitAddr = 0x00279600u;
    constexpr uint32_t kFmvIqTableAddr = 0x002A1610u;
    constexpr uint32_t kFmvVqTableAddr = 0x002A1660u;

    constexpr uint32_t kFmvD4Chcr = 0x1000B400u;   // DMA canal 4 (memoria → IPU)
    constexpr uint32_t kFmvIpuCmd = 0x10002000u;
    constexpr uint32_t kFmvIpuCtrl = 0x10002010u;
    constexpr uint32_t kFmvIpuInFifo = 0x10007010u;

    // API de vídeo del juego (solo con GOW_SKIP_FMV=1)
    constexpr uint32_t kFmvMovieLoadAddr = 0x00188CA0u;      // a0 = nombre del vídeo
    constexpr uint32_t kFmvMovieNoopAddrs[] = {0x00188E80u, 0x00188F20u};
    constexpr uint32_t kFmvMovieReadyAddrs[] = {0x00188ED8u, 0x00188EF0u};
    constexpr uint32_t kFmvMovieStateAddrs[] = {0x0029C838u, 0x0029C870u, 0x0029C840u};

    void fmvIpuInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        PS2Memory &mem = runtime->memory();
        mem.write32(kFmvD4Chcr, 1u); // lo que hace SetD4_CHCR(1)
        mem.write32(kFmvIpuCtrl, 0x40000000u); // reset del IPU
        mem.write32(kFmvIpuCmd, 0u);
        // Tabla IQ: 4 qwords y el quinto repetido 4 veces, como el sceIpuInit del juego
        for (uint32_t offset : {0u, 16u, 32u, 48u, 64u, 64u, 64u, 64u})
            mem.write128(kFmvIpuInFifo, runtime->Load128(rdram, ctx, kFmvIqTableAddr + offset));
        mem.write32(kFmvIpuCmd, 0x50000000u); // SETIQ
        mem.write32(kFmvIpuCmd, 0x58000000u);
        for (uint32_t offset : {0u, 16u})
            mem.write128(kFmvIpuInFifo, runtime->Load128(rdram, ctx, kFmvVqTableAddr + offset));
        mem.write32(kFmvIpuCmd, 0x60000000u); // SETVQ
        mem.write32(kFmvIpuCmd, 0x90000000u); // SETTH
        mem.write32(kFmvIpuCtrl, 0x40000000u);
        mem.write32(kFmvIpuCmd, 0u);
        std::fprintf(stderr, "[gow:fmv] IPU inicializado con las tablas del juego (0x%X, 0x%X)\n",
                     kFmvIqTableAddr, kFmvVqTableAddr);
        setReturnS32(ctx, 0);
    }

    void fmvSkipMovieLoad(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t nameAddr = getRegU32(ctx, 4) & 0x01FFFFFFu;
        char name[128] = "<sin nombre>";
        if (nameAddr != 0u && nameAddr + sizeof(name) <= 0x02000000u)
        {
            std::memcpy(name, rdram + nameAddr, sizeof(name) - 1u);
            name[sizeof(name) - 1u] = '\0';
        }
        std::fprintf(stderr, "[gow:fmv] vídeo saltado (GOW_SKIP_FMV): %s\n", name);
        for (uint32_t addr : kFmvMovieStateAddrs)
            WRITE32(addr, 0u);
        setReturnS32(ctx, 0);
    }

    void fmvSkipMovieNoop(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, 0); }
    void fmvSkipMovieReady(uint8_t *, R5900Context *ctx, PS2Runtime *) { setReturnS32(ctx, 1); }

    void applyFmvOverrides(PS2Runtime &runtime)
    {
        if (!runtime.registerFunction(kFmvIpuInitAddr, &fmvIpuInit))
            std::fprintf(stderr, "[gow:fmv] no se pudo enlazar sceIpuInit\n");

        const char *skip = std::getenv("GOW_SKIP_FMV");
        if (skip && std::strcmp(skip, "1") == 0)
        {
            runtime.registerFunction(kFmvMovieLoadAddr, &fmvSkipMovieLoad);
            for (uint32_t addr : kFmvMovieNoopAddrs)
                runtime.registerFunction(addr, &fmvSkipMovieNoop);
            for (uint32_t addr : kFmvMovieReadyAddrs)
                runtime.registerFunction(addr, &fmvSkipMovieReady);
            std::fprintf(stderr, "[gow:fmv] GOW_SKIP_FMV=1: los vídeos se saltan\n");
        }
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-fmv",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyFmvOverrides);
