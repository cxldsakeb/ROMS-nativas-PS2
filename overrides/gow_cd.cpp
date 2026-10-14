// CD/DVD: funciones de libcdvd del juego que el runtime no implementa.
//
// God of War lleva enlazada su propia libcdvd (la parte EE de la librería de Sony). Cada función
// pública habla con el IOP por SIF RPC: los "comandos S" van al servidor 0x80000593 (CDVDFSV) y
// los "comandos N" al 0x80000595. PS2Recomp no emula ese servidor; en su lugar ofrece stubs HLE
// (PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/CD.cpp) a los que se enlazan las funciones sceCd*
// del juego en config/gow.manual.toml. Si una se queda sin enlazar, la libcdvd del juego intenta
// el sceSifBindRpc de verdad, el runtime le contesta "servidor no disponible" (client.server == 0)
// y el juego reintenta para siempre en _CdCheckSCmd (0x27A6B8). Ese era el bloqueo de 0x27A768.
//
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
    // sceCdReadDvdDualInfo(int *on_dual, u32 *layer1_start): comando S 0x27. Solo lo llama
    // FUN_0017bc80 (la inicialización del IOP), en un bucle hasta que devuelve distinto de 0,
    // y guarda los resultados en 0x3053E4 / 0x3053E8.
    constexpr uint32_t kCdReadDvdDualInfoAddr = 0x0027AB00u;

    constexpr uint32_t kCdSectorSize = 2048u;
    constexpr uint32_t kCdPvdSector = 16u;        // descriptor de volumen primario (ISO 9660)
    constexpr uint32_t kCdPvdVolumeSizeOffset = 80u; // tamaño del volumen en sectores (u32 LE)

    struct CdDualInfo
    {
        bool detected = false;
        int32_t onDual = 0;
        uint32_t layer1Start = 0;
    };

    CdDualInfo g_cdDualInfo;

    bool cdReadSector(std::ifstream &iso, uint32_t lbn, std::array<uint8_t, kCdSectorSize> &out)
    {
        iso.clear();
        iso.seekg(static_cast<std::streamoff>(lbn) * kCdSectorSize, std::ios::beg);
        return static_cast<bool>(iso.read(reinterpret_cast<char *>(out.data()), out.size()));
    }

    bool cdIsPrimaryVolumeDescriptor(const std::array<uint8_t, kCdSectorSize> &sector)
    {
        return sector[0] == 0x01 && std::memcmp(sector.data() + 1, "CD001", 5) == 0;
    }

    // Mismo criterio que PCSX2 (InputIsoFile::FindLayer1Start): en una ISO de doble capa, la capa 1
    // empieza justo donde acaba el volumen de la capa 0, y ese sector contiene su propio PVD.
    // En nuestra ISO: volumen de la capa 0 = 2 080 560 sectores, PVD de la capa 1 en el 2 080 560.
    CdDualInfo cdDetectDualInfo()
    {
        CdDualInfo info;
        info.detected = true;

        const std::filesystem::path isoPath = PS2Runtime::getIoPaths().cdImage;
        std::ifstream iso(isoPath, std::ios::binary);
        if (isoPath.empty() || !iso)
        {
            std::cerr << "[gow:cd] sin imagen de CD; sceCdReadDvdDualInfo responde 'una capa'" << std::endl;
            return info;
        }

        iso.seekg(0, std::ios::end);
        const uint64_t totalSectors = static_cast<uint64_t>(iso.tellg()) / kCdSectorSize;

        std::array<uint8_t, kCdSectorSize> sector{};
        if (!cdReadSector(iso, kCdPvdSector, sector) || !cdIsPrimaryVolumeDescriptor(sector))
        {
            std::cerr << "[gow:cd] la imagen no tiene un PVD ISO 9660 en el sector 16" << std::endl;
            return info;
        }

        uint32_t layer0Size = 0;
        std::memcpy(&layer0Size, sector.data() + kCdPvdVolumeSizeOffset, sizeof(layer0Size));

        // Como PCSX2: con una sola capa, layer1_start = número de sectores del disco
        info.layer1Start = static_cast<uint32_t>(totalSectors);
        if (totalSectors > layer0Size && cdReadSector(iso, layer0Size, sector) && cdIsPrimaryVolumeDescriptor(sector))
        {
            info.onDual = 1;
            info.layer1Start = layer0Size;
        }

        std::cout << "[gow:cd] DVD " << (info.onDual ? "de doble capa" : "de una capa")
                  << ": capa 1 en el sector " << info.layer1Start
                  << " (" << totalSectors << " sectores en total)" << std::endl;
        return info;
    }

    void cdReadDvdDualInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        if (!g_cdDualInfo.detected)
        {
            g_cdDualInfo = cdDetectDualInfo();
        }

        const uint32_t onDualAddr = getRegU32(ctx, 4);
        const uint32_t layer1StartAddr = getRegU32(ctx, 5);
        if (onDualAddr != 0u)
        {
            WRITE32(onDualAddr, static_cast<uint32_t>(g_cdDualInfo.onDual));
        }
        if (layer1StartAddr != 0u)
        {
            WRITE32(layer1StartAddr, g_cdDualInfo.layer1Start);
        }
        // 1 = comando aceptado. Con 0 el juego vuelve a preguntar en bucle.
        // No se toca el PC: dispatchGuestBranch vuelve solo a la instrucción siguiente al jal.
        setReturnS32(ctx, 1);
    }

    void applyCdOverrides(PS2Runtime &runtime)
    {
        // [cxldsakeb] Sin esto el arranque se queda en el bucle de _CdCheckSCmd (0x27A768).
        if (!runtime.registerFunction(kCdReadDvdDualInfoAddr, &cdReadDvdDualInfo))
        {
            std::cerr << "[gow:cd] no se pudo enlazar sceCdReadDvdDualInfo" << std::endl;
        }
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-cd",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyCdOverrides);
