// Overrides específicos de God of War.
//
// Este archivo se copia a PS2Recomp/ps2xRuntime/src/runner/ con scripts/recompile.ps1
// (o "recompile.ps1 -SyncOnly" si solo cambiaste overrides).
//
// Aquí van los parches que solo valen para NUESTRA build del ELF:
//   - enlazar direcciones a handlers del runtime (sceCdRead, SifLoadModule, ...)
//   - stubs temporales ret0/ret1/reta0 para clasificar funciones
//   - reemplazos en C++ de funciones del juego
//
// Regla: cada binding lleva un comentario con QUIÉN, POR QUÉ y si es TEMPORAL.
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"

namespace
{
    // Llama a un handler del runtime y, si no movió el PC, vuelve a $ra
    // (evita bucles infinitos re-despachando la misma dirección).
    template <typename Handler>
    void bindWithReturn(PS2Runtime &runtime, uint32_t address, Handler handler)
    {
        runtime.registerFunction(address,
            [handler](uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
            {
                const uint32_t entryPc = ctx->pc;
                handler(rdram, ctx, rt);
                if (ctx->pc == entryPc)
                {
                    ctx->pc = getRegU32(ctx, 31);
                }
            });
    }

    void applyGodOfWarOverrides(PS2Runtime &runtime)
    {
        (void)runtime;

        // Ejemplos (descomentar y ajustar cuando conozcamos las direcciones reales):
        //
        // bindWithReturn(runtime, 0x00123456u, ps2_stubs::sceCdRead);           // [nombre] lectura de CD
        // ps2_game_overrides::bindAddressHandler(runtime, 0x0031D5F8u, "ret0"); // [nombre] TEMPORAL: ...
    }
}

// entry = 0 y crc32 = 0 significan "cualquiera": el override aplica a cualquier ELF con ese nombre.
// Cuando tengamos el ELF de referencia, rellenar el entry point y el CRC32 para no aplicarlo a otra región.
PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war",
    "SCUS_973.99",
    0u,
    0u,
    applyGodOfWarOverrides);
