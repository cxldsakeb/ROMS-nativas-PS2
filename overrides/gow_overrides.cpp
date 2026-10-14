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

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    // God of War lee casi todo por número de sector (LBN) usando GODOFWAR.TOC, y el DVD es de
    // doble capa: la carpeta game/disc/ solo tiene la capa 0. Por eso el runtime necesita la ISO
    // completa como imagen de CD. extract-iso.ps1 guarda su ruta en game/iso-path.txt.
    void configureCdImage()
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
        const std::filesystem::path pathFile = paths.elfDirectory.parent_path() / "iso-path.txt";

        std::ifstream in(pathFile);
        std::string isoPath;
        if (!in || !std::getline(in, isoPath) || isoPath.empty())
        {
            std::cerr << "[gow] Falta " << pathFile.string()
                      << ": las lecturas por sector fallarán. Vuelve a ejecutar extract-iso.ps1" << std::endl;
            return;
        }
        if (!isoPath.empty() && isoPath.back() == '\r')
        {
            isoPath.pop_back();
        }
        if (!std::filesystem::exists(isoPath))
        {
            std::cerr << "[gow] No existe la ISO indicada en iso-path.txt: " << isoPath << std::endl;
            return;
        }

        paths.cdImage = isoPath;
        PS2Runtime::setIoPaths(paths);
        std::cout << "[gow] Imagen de CD: " << isoPath << std::endl;
    }

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
        configureCdImage();

        // Ejemplos (descomentar y ajustar cuando conozcamos las direcciones reales):
        //
        // bindWithReturn(runtime, 0x00123456u, ps2_stubs::sceCdRead);           // [nombre] lectura de CD
        // ps2_game_overrides::bindAddressHandler(runtime, 0x0031D5F8u, "ret0"); // [nombre] TEMPORAL: ...
    }
}

// Solo se aplica al ELF de referencia (SCUS-97399 v1.00, ver docs/NOTES.md).
PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyGodOfWarOverrides);
