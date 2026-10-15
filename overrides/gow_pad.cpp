// Mando: conecta el mando o el teclado del PC al juego.
//
// God of War no usa libpad: habla con su propio driver de mando en el IOP (SIO2MAN, SIO2D, DBCMAN,
// DS2U_D) a través de una librería del EE (0x27B828–0x27C380, encima de libdbc). El IOP emulado
// ejecuta esos IRX, pero no imita el hardware SIO2, así que nunca ven un mando y el juego muestra
// "PLEASE INSERT A DUALSHOCK®2 ANALOG CONTROLLER INTO CONTROLLER PORT 1".
//
// El código del juego (FUN_0017C130, una vez por frame y por puerto; estado en 0x2FD9C0, 0x1C0 bytes
// por puerto) usa cuatro funciones de esa librería. Aquí se sustituyen para el puerto 1:
//   0x27BB98 estado(socket)         -> 1 = conectado (0 = desconectado; otro valor = ocupado)
//   0x27BAC8 info(socket, buf)      -> buf[0] = 0xFF: el juego apunta el tipo 7, DualShock analógico
//   0x27BF90 actuadores(socket,buf) -> 0: sin vibración, así no intenta mandar comandos al motor
//   0x27B9F0 leer(socket, buf)      -> buf[0..1] botones (0 = pulsado, orden estándar del DualShock)
//                                      y buf[2..5] sticks; devuelve 3 = botones + sticks, sin presión
// Los datos salen de PSPadBackend del runtime: un mando del PC si hay, o el teclado
// (flechas/WASD, X/Espacio = ✕, C/Esc = ○, Z = □, V = △, Q/E = L1/R1, Enter = START, Tab = SELECT).
//
// [DaniSBR1] TEMPORAL: solo hay un mando y sin presión ni vibración. La solución completa sería
// emular el SIO2 en ps2xIOP para que los IRX del juego funcionen tal cual.
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_pad.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t kPadGetStatusAddr = 0x0027BB98u;
    constexpr uint32_t kPadGetInfoAddr = 0x0027BAC8u;
    constexpr uint32_t kPadGetActuatorsAddr = 0x0027BF90u;
    constexpr uint32_t kPadReadAddr = 0x0027B9F0u;

    constexpr int32_t kPadStatusConnected = 1;
    constexpr uint8_t kPadInfoDualShock = 0xFF;
    constexpr int32_t kPadReadButtonsAndSticks = 3;

    // Socket del puerto 1. El juego lo crea con 0x27B890(params, búfer), con params = {2, puerto, ranura},
    // y puede cerrarlo y volver a crearlo (FUN_0017C050): por eso se apunta en cada creación.
    constexpr uint32_t kPadCreateSocketAddr = 0x0027B890u;
    PS2Runtime::RecompiledFunction g_padOriginalCreateSocket = nullptr;
    std::atomic<uint32_t> g_padPort1Socket{0xFFFFFFFFu};
    std::atomic<bool> g_padAnnounced{false};
    std::atomic<uint32_t> g_padUnknownSocketLogs{0};

    void padCreateSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t params = getRegU32(ctx, 4);
        const uint32_t port = params != 0u ? Ps2FastRead32(rdram, params + 4u) : 0xFFFFFFFFu;
        g_padOriginalCreateSocket(rdram, ctx, runtime);
        const uint32_t socket = getRegU32(ctx, 2);
        std::cout << "[gow:pad] socket del puerto " << (port + 1u) << ": 0x" << std::hex << socket << std::dec << std::endl;
        if (port == 0u)
        {
            g_padPort1Socket = socket;
        }
    }

    bool padIsPort1(uint32_t socket)
    {
        const bool port1 = socket == g_padPort1Socket.load();
        if (!port1 && g_padUnknownSocketLogs.fetch_add(1) < 4u)
        {
            std::cout << "[gow:pad] estado pedido para el socket 0x" << std::hex << socket << " (puerto 1 = 0x"
                      << g_padPort1Socket.load() << ")" << std::dec << std::endl;
        }
        return port1;
    }

    void padGetStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const bool port1 = padIsPort1(getRegU32(ctx, 4));
        if (port1 && !g_padAnnounced.exchange(true))
        {
            std::cout << "[gow:pad] DualShock 2 conectado en el puerto 1 (mando del PC o teclado)" << std::endl;
        }
        setReturnS32(ctx, port1 ? kPadStatusConnected : 0);
    }

    void padGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t info = getRegU32(ctx, 5);
        if (info != 0u)
        {
            rdram[info & PS2_RAM_MASK] = kPadInfoDualShock;
        }
        setReturnS32(ctx, 1);
    }

    void padGetActuators(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    // Pulsaciones programadas para pruebas sin nadie delante:
    //   $env:GOW_PAD_PRESS = "start@600,cross@1200"
    // pulsa START en la lectura 600 del mando y ✕ en la 1200 (unas 60 lecturas por segundo a velocidad
    // normal; aquí va más lento). Cada pulsación dura kPadPressReads lecturas.
    struct PadScriptedPress
    {
        uint32_t read;
        uint16_t mask; // bit de scePadRead (orden de PSPadBackend), activo a nivel bajo
    };
    constexpr uint32_t kPadPressReads = 8u;
    std::vector<PadScriptedPress> g_padScript;
    std::atomic<uint32_t> g_padReads{0};

    void padLoadScript()
    {
        const char *env = std::getenv("GOW_PAD_PRESS");
        if (env == nullptr)
            return;
        static const std::pair<const char *, uint16_t> kButtons[] = {
            {"select", 0x0001}, {"l3", 0x0002}, {"r3", 0x0004}, {"start", 0x0008},
            {"up", 0x0010}, {"right", 0x0020}, {"down", 0x0040}, {"left", 0x0080},
            {"l2", 0x0100}, {"r2", 0x0200}, {"l1", 0x0400}, {"r1", 0x0800},
            {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000}, {"square", 0x8000}};
        std::string spec(env);
        size_t pos = 0;
        while (pos < spec.size())
        {
            const size_t comma = spec.find(',', pos);
            const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            const size_t at = item.find('@');
            if (at != std::string::npos)
            {
                const std::string name = item.substr(0, at);
                for (const auto &button : kButtons)
                {
                    if (name == button.first)
                    {
                        g_padScript.push_back({static_cast<uint32_t>(std::strtoul(item.c_str() + at + 1, nullptr, 10)), button.second});
                        std::cout << "[gow:pad] pulsación programada: " << name << " en la lectura " << g_padScript.back().read << std::endl;
                    }
                }
            }
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }

    void padRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t buffer = getRegU32(ctx, 5);
        std::array<uint8_t, 32> state{};
        if (buffer == 0u || !runtime->padBackend().readState(0, 0, state.data(), state.size()))
        {
            setReturnS32(ctx, -1);
            return;
        }
        const uint32_t read = g_padReads.fetch_add(1);
        if (read % 300u == 0u)
            std::cout << "[gow:pad] lectura " << read << std::endl;
        for (const PadScriptedPress &press : g_padScript)
        {
            if (read >= press.read && read < press.read + kPadPressReads)
            {
                state[2] &= static_cast<uint8_t>(~(press.mask & 0xFFu));
                state[3] &= static_cast<uint8_t>(~(press.mask >> 8));
                if (read == press.read)
                    std::cout << "[gow:pad] pulsando 0x" << std::hex << press.mask << std::dec << " (lectura " << read << ")" << std::endl;
            }
        }
        // state = formato de scePadRead: [2..3] botones, [4..7] sticks (der. X/Y, izq. X/Y)
        for (uint32_t i = 0; i < 6u; ++i)
        {
            rdram[(buffer + i) & PS2_RAM_MASK] = state[2u + i];
        }
        setReturnS32(ctx, kPadReadButtonsAndSticks);
    }

    void applyPadOverrides(PS2Runtime &runtime)
    {
        for (uint32_t address : {kPadGetStatusAddr, kPadGetInfoAddr, kPadGetActuatorsAddr, kPadReadAddr})
        {
            if (!runtime.hasFunction(address))
            {
                std::cerr << "[gow:pad] falta la función 0x" << std::hex << address << std::dec
                          << " en el código generado; el mando no se conecta" << std::endl;
                return;
            }
        }
        padLoadScript();
        if (runtime.hasFunction(kPadCreateSocketAddr))
        {
            g_padOriginalCreateSocket = runtime.lookupFunction(kPadCreateSocketAddr);
            runtime.registerFunction(kPadCreateSocketAddr, &padCreateSocket);
        }
        runtime.registerFunction(kPadGetStatusAddr, &padGetStatus);
        runtime.registerFunction(kPadGetInfoAddr, &padGetInfo);
        runtime.registerFunction(kPadGetActuatorsAddr, &padGetActuators);
        runtime.registerFunction(kPadReadAddr, &padRead);
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-pad",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyPadOverrides);
