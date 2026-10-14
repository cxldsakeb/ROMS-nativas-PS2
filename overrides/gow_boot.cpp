// Arranque: argumentos de main() como los pone el kernel de la PS2 al arrancar desde el disco.
//
// En la PS2, el crt0 del juego (0x100008) llama al syscall SetupThread con $a3 = &_args (0x2C7080,
// al principio de .bss) y el kernel rellena ahí argc/argv con la ruta de arranque de SYSTEM.CNF
// (BOOT2 = cdrom0:\SCUS_973.99;1). Luego el crt0 llama a main(_args.argc, _args.argv).
// El SetupThread de PS2Recomp no rellena _args, así que main recibe argc = 0.
//
// Con argc = 0, FUN_0017aa88 (lo llama main) pone el modo de arranque (0x29C4E4) a 2, el de
// "PC de desarrollo con prefijo", en lugar de 0 ("cdrom"). Consecuencias:
//   - iop_init (0x17BC80) no reinicia el IOP con IOPRP300.IMG.
//   - Los IRX se piden como "IOP_MOD/sio2man.irx" en vez de "cdrom0:\SIO2MAN.IRX;1": fallan
//     MC2_D, DS2U_D, 989NOMID y SMPD_IOP, y libdbc (0x27AD00) se queda esperando para siempre al
//     servidor RPC 0x8000131C del driver del mando.
//
// [cxldsakeb] La solución de fondo es que SetupThread rellene _args (ver NOTES.md, "Cambios
// necesarios en PS2Recomp"). Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstring>
#include <iostream>

namespace
{
    constexpr uint32_t kBootMainAddr = 0x00138D48u;
    // struct _args { int argc; char *argv[16]; char payload[256]; } del crt0
    constexpr uint32_t kBootArgsAddr = 0x002C7080u;
    constexpr uint32_t kBootArgvAddr = kBootArgsAddr + 4u;
    constexpr uint32_t kBootPayloadAddr = kBootArgvAddr + 16u * 4u;
    constexpr uint32_t kBootPayloadSize = 256u;
    constexpr char kBootPath[] = "cdrom0:\\SCUS_973.99;1"; // BOOT2 de SYSTEM.CNF

    PS2Runtime::RecompiledFunction g_bootOriginalMain = nullptr;

    void mainWithDiscBootArgs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static_assert(sizeof(kBootPath) <= kBootPayloadSize, "la ruta no cabe en _args.payload");

        // Solo si el runtime no los ha puesto ya (por si algún día SetupThread lo hace)
        if (getRegU32(ctx, 4) == 0u)
        {
            std::memcpy(rdram + (kBootPayloadAddr & PS2_RAM_MASK), kBootPath, sizeof(kBootPath));
            WRITE32(kBootArgsAddr, 1u);
            WRITE32(kBootArgvAddr, kBootPayloadAddr);
            for (uint32_t i = 1; i < 16u; ++i)
            {
                WRITE32(kBootArgvAddr + i * 4u, 0u);
            }

            // main(argc, argv). El R5900 guarda los valores de 32 bits extendidos a 64 con signo.
            ctx->r[4] = _mm_set_epi64x(0, 1);
            ctx->r[5] = _mm_set_epi64x(0, static_cast<int64_t>(static_cast<int32_t>(kBootArgvAddr)));
            std::cout << "[gow:boot] main(argc=1, argv[0]=\"" << kBootPath << "\")" << std::endl;
        }

        g_bootOriginalMain(rdram, ctx, runtime);
    }

    void applyBootOverrides(PS2Runtime &runtime)
    {
        if (!runtime.hasFunction(kBootMainAddr))
        {
            std::cerr << "[gow:boot] falta main (0x138D48) en el código generado; argc seguirá a 0" << std::endl;
            return;
        }
        g_bootOriginalMain = runtime.lookupFunction(kBootMainAddr);
        runtime.registerFunction(kBootMainAddr, &mainWithDiscBootArgs);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-boot",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyBootOverrides);
