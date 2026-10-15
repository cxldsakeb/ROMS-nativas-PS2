// Máquina de estados del juego (pantallas, menús, animaciones): cambios a un estado nulo.
//
// Los estados forman un árbol con nombres. FUN_00180E90(máquina, "ruta/con/barras") lo recorre con
// FUN_0017FD10 (buscar hijo por nombre) y FUN_00180D08(máquina, estado), que guarda el estado y
// llama a su método "entrar" (vtable +0x34). Si un nombre no existe, el estado es NULL:
// FUN_00180D08 lee la vtable de la dirección 0xC y salta a 0.
//
// Pasa al conectar el mando, en la transición del título al menú: tras ~455 cambios correctos a
// "/Animation/goHero" (la animación de Kratos), el componente "Animation" del búfer de la ruta, en la
// pila principal (0x1FFEDA0), aparece como "Animao": alguien pisa dos bytes ("ti" -> "o\0") de un
// marco activo, unos 0x50 bytes por encima del sp. Causa aún sin encontrar (ver docs/NOTES.md).
//
// [DaniSBR1] TEMPORAL: aquí solo se ignoran los cambios a un estado nulo, con un aviso en el log,
// para no saltar a 0. Tapa el síntoma; hay que encontrar quién corrompe la pila.
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"

#include <atomic>
#include <iomanip>
#include <iostream>

namespace
{
    constexpr uint32_t kStateMachineSetAddr = 0x00180D08u;

    PS2Runtime::RecompiledFunction g_stateOriginalSet = nullptr;
    std::atomic<uint32_t> g_stateIgnoredNulls{0};

    void stateSetIgnoringNull(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (getRegU32(ctx, 5) == 0u)
        {
            const uint32_t count = g_stateIgnoredNulls.fetch_add(1) + 1u;
            if (count <= 8u || (count % 1000u) == 0u)
            {
                std::cout << "[gow:state] TEMPORAL: ignorado un cambio a estado nulo (máquina 0x" << std::hex
                          << std::setw(8) << std::setfill('0') << getRegU32(ctx, 4) << ", desde 0x" << std::setw(8)
                          << getRegU32(ctx, 31) << std::dec << std::setfill(' ') << ", van " << count << ")" << std::endl;
            }
            return; // dispatchGuestBranch vuelve solo a la instrucción siguiente al jal
        }
        g_stateOriginalSet(rdram, ctx, runtime);
    }

    void applyStateOverrides(PS2Runtime &runtime)
    {
        if (!runtime.hasFunction(kStateMachineSetAddr))
        {
            std::cerr << "[gow:state] falta FUN_00180D08 en el código generado" << std::endl;
            return;
        }
        g_stateOriginalSet = runtime.lookupFunction(kStateMachineSetAddr);
        runtime.registerFunction(kStateMachineSetAddr, &stateSetIgnoringNull);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-state",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyStateOverrides);
