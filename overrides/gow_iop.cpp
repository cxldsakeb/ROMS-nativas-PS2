// IOP: carga de módulos IRX desde un búfer en la memoria del IOP.
//
// Al final de iop_init (0x17BDB8), el juego carga un IRX pequeño que lleva embebido en su .data
// (0x2A3650, 0x359 bytes, importa cdvdman): FUN_00283570 lo copia con sceSifAllocIopHeap +
// sceSifSetDma a la memoria del IOP y llama a sceSifLoadModuleBuffer con esa dirección del IOP,
// como manda la API de Sony. Si la carga devuelve < 0, o el resultado de arranque tiene bits de
// error, el juego entra en la trampa fatal de 0x185F90 (bucle infinito de nops).
//
// El stub del runtime (sceSifLoadModuleBuffer en Kernel/Syscalls/System.cpp) llama a
// IopEmulator::loadModuleBuffer, que lee el ELF con readElfFromGuest: de la memoria del EE, no
// de la del IOP. En la dirección del IOP (0x192340), en la RAM del EE hay código del juego, así
// que no encuentra un ELF y devuelve -1. Aquí se copia el módulo de la memoria del IOP a un búfer
// temporal del EE y se le pasa esa dirección al runtime, que es donde la espera.
//
// [DaniSBR1] La solución de fondo es que loadModuleBuffer lea de la memoria del IOP (ver NOTES.md,
// "Cambios necesarios en PS2Recomp"). Las direcciones solo valen para el ELF cuyo SHA1 está en
// docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
    // _sceSifLoadModuleBuffer(void *iopAddr, int argLen, const char *args, int *result)
    constexpr uint32_t kIopLoadModuleBufferAddr = 0x00298AD8u;
    constexpr uint32_t kIopMaxModuleSize = 2u * 1024u * 1024u; // la RAM entera del IOP

    PS2Runtime::RecompiledFunction g_iopOriginalLoadModuleBuffer = nullptr;

    template <typename T>
    T iopReadLe(const std::vector<uint8_t> &bytes, size_t offset)
    {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    // Lee un ELF entero de la memoria del IOP. El tamaño sale de las cabeceras: el final de la tabla
    // de secciones, de la de segmentos o del contenido de la última sección o segmento con datos.
    bool iopReadElf(const PS2Runtime &runtime, uint32_t iopAddr, std::vector<uint8_t> &image)
    {
        std::vector<uint8_t> header(52);
        if (!runtime.readIopMemory(iopAddr, header.data(), header.size()) ||
            std::memcmp(header.data(), "\x7F" "ELF", 4) != 0)
        {
            return false;
        }

        const uint32_t phoff = iopReadLe<uint32_t>(header, 28);
        const uint32_t shoff = iopReadLe<uint32_t>(header, 32);
        const uint16_t phentsize = iopReadLe<uint16_t>(header, 42);
        const uint16_t phnum = iopReadLe<uint16_t>(header, 44);
        const uint16_t shentsize = iopReadLe<uint16_t>(header, 46);
        const uint16_t shnum = iopReadLe<uint16_t>(header, 48);

        uint64_t required = std::max<uint64_t>({52u,
                                                uint64_t(phoff) + uint64_t(phentsize) * phnum,
                                                uint64_t(shoff) + uint64_t(shentsize) * shnum});
        if (required > kIopMaxModuleSize)
        {
            return false;
        }
        image.resize(static_cast<size_t>(required));
        if (!runtime.readIopMemory(iopAddr, image.data(), image.size()))
        {
            return false;
        }

        // Contenido de secciones (salvo SHT_NOBITS = 8) y segmentos que acabe después de las tablas
        for (uint16_t i = 0; i < shnum && shentsize >= 24; ++i)
        {
            const size_t sh = size_t(shoff) + size_t(i) * shentsize;
            if (iopReadLe<uint32_t>(image, sh + 4) != 8u)
            {
                required = std::max<uint64_t>(required, uint64_t(iopReadLe<uint32_t>(image, sh + 16)) + iopReadLe<uint32_t>(image, sh + 20));
            }
        }
        for (uint16_t i = 0; i < phnum && phentsize >= 20; ++i)
        {
            const size_t ph = size_t(phoff) + size_t(i) * phentsize;
            required = std::max<uint64_t>(required, uint64_t(iopReadLe<uint32_t>(image, ph + 4)) + iopReadLe<uint32_t>(image, ph + 16));
        }
        if (required > kIopMaxModuleSize)
        {
            return false;
        }
        if (required > image.size())
        {
            image.resize(static_cast<size_t>(required));
            if (!runtime.readIopMemory(iopAddr, image.data(), image.size()))
            {
                return false;
            }
        }
        return true;
    }

    void iopLoadModuleBufferFromIop(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t iopAddr = getRegU32(ctx, 4);
        const uint32_t argLen = getRegU32(ctx, 5);
        const uint32_t argAddr = getRegU32(ctx, 6);
        const uint32_t resultAddr = getRegU32(ctx, 7);

        std::vector<uint8_t> image;
        if (!runtime->isIopMemoryRange(iopAddr, 52u) || !iopReadElf(*runtime, iopAddr, image))
        {
            // No es un ELF en la memoria del IOP: que lo intente el stub del runtime tal cual
            g_iopOriginalLoadModuleBuffer(rdram, ctx, runtime);
            return;
        }

        const uint32_t eeCopy = runtime->guestMalloc(static_cast<uint32_t>(image.size()), 64u);
        if (eeCopy == 0u)
        {
            std::cerr << "[gow:iop] sin memoria del runtime para copiar el IRX de 0x" << std::hex << iopAddr << std::dec << std::endl;
            setReturnS32(ctx, -1);
            return;
        }
        std::memcpy(rdram + (eeCopy & PS2_RAM_MASK), image.data(), image.size());

        std::vector<uint8_t> args;
        if (argAddr != 0u && argLen != 0u && argLen <= 0x10000u)
        {
            args.assign(rdram + (argAddr & PS2_RAM_MASK), rdram + (argAddr & PS2_RAM_MASK) + argLen);
        }
        const ps2x::iop::ModuleLoadResult loaded =
            runtime->loadIopModuleBuffer(eeCopy, args.empty() ? nullptr : args.data(), static_cast<uint32_t>(args.size()));
        runtime->guestFree(eeCopy);

        const int32_t moduleId = (loaded.handled && loaded.moduleId > 0) ? loaded.moduleId : -1;
        if (resultAddr != 0u)
        {
            // Resultado del arranque del módulo: 0 = RESIDENT_END, 1 = NO_RESIDENT_END, 2 = REMOVABLE_RESIDENT_END
            WRITE32(resultAddr, static_cast<uint32_t>(std::max(loaded.startResult, 0)));
        }
        std::cout << "[gow:iop] IRX desde el IOP 0x" << std::hex << iopAddr << std::dec << " (" << image.size()
                  << " bytes): id=" << moduleId << " arranque=" << loaded.startResult << std::endl;
        setReturnS32(ctx, moduleId);
    }

    void applyIopOverrides(PS2Runtime &runtime)
    {
        if (!runtime.hasFunction(kIopLoadModuleBufferAddr))
        {
            std::cerr << "[gow:iop] falta _sceSifLoadModuleBuffer (0x298AD8); no se corrige la carga desde el IOP" << std::endl;
            return;
        }
        g_iopOriginalLoadModuleBuffer = runtime.lookupFunction(kIopLoadModuleBufferAddr);
        runtime.registerFunction(kIopLoadModuleBufferAddr, &iopLoadModuleBufferFromIop);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-iop",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyIopOverrides);
