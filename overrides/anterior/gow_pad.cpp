// Mando: conecta los mandos y el teclado del PC al juego.
//
// God of War no usa libpad: habla con su propio driver de mando en el IOP (SIO2MAN, SIO2D, DBCMAN,
// DS2U_D) a través de una librería del EE (0x27B828–0x27C380, encima de libdbc). El IOP emulado
// ejecuta esos IRX, pero no imita el hardware SIO2, así que nunca ven un mando y el juego muestra
// "PLEASE INSERT A DUALSHOCK®2 ANALOG CONTROLLER INTO CONTROLLER PORT 1".
//
// El código del juego (FUN_0017C130, una vez por frame y por puerto; estado en 0x2FD9C0, 0x1C0 bytes
// por puerto) usa cinco funciones de esa librería, que aquí se sustituyen:
//   0x27B890 crear(params, búfer)   -> se deja la original; solo se apunta qué socket es de qué puerto
//   0x27BB98 estado(socket)         -> 1 = conectado, 0 = desconectado (el juego reinicia el puerto)
//   0x27BAC8 info(socket, buf)      -> buf[0] = 0xFF: el juego apunta el tipo 7, DualShock analógico
//   0x27BF90 actuadores(socket,buf) -> 1 si el puerto tiene un mando XInput (vibra), 0 si no
//   0x27C010 vibrar(socket,..,datos)-> datos = (motor grande << 1) | motor pequeño -> XInputSetState
//   0x27B9F0 leer(socket, buf)      -> buf[0..1] botones (0 = pulsado, orden estándar del DualShock),
//                                      buf[2..5] sticks y buf[6..17] presión; devuelve 9 (con presión)
//
// Entrada (raylib, leída aquí y no con PSPadBackend para tener zona muerta, puerto 2 y teclado):
//   puerto 1 = mando 1 del PC y el teclado a la vez. Puerto 2 = mando 2 del PC, solo si está
//   conectado: enchufarlo o quitarlo con el juego en marcha es una conexión en caliente.
//   Mando (botones en la posición del DualShock): cara inferior = ✕, derecha = ○, izquierda = □,
//   superior = △; LB/L1 = L1, RB/R1 = R1, gatillos = L2/R2 (analógicos); Back/Share = SELECT,
//   Start/Options = START; clic en los sticks = L3/R3.
//   Teclado: WASD = stick izquierdo (Kratos se mueve con el stick), flechas = cruceta,
//   IJKL = stick derecho (esquivar), Espacio/X = ✕, C/Esc = ○, Z = □, V = △, Q/E = L1/R1,
//   Mayús. izq./der. = L2/R2, Enter = START, Tab = SELECT.
//
// Variables de entorno:
//   GOW_PAD_DEADZONE=0.15   zona muerta radial de los sticks (0..0.9)
//   GOW_PAD_KEYBOARD=0      el puerto 1 es solo el mando 1, sin teclado (para probar a quitarlo)
//   GOW_PAD_PRESS=...       pulsaciones programadas en el puerto 1 (ver padLoadScript)
//   GOW_PAD_RUMBLE=0        sin vibración
//   GOW_PAD_RUMBLE_TEST=1   vibra ~1 s al arrancar, para comprobar que llega al mando
//   GOW_PAD_DEBUG=1         trazas del mando
//
// Retención de pulsaciones: el juego lee el mando una vez por frame de su lógica, y hoy va a menos de
// 2 frames por segundo. Un hilo muestrea la entrada cada 4 ms y acumula lo que pasa entre dos lecturas
// (ver padLatch), para que un toque corto no se pierda.
//
// Vibración: con XInput (mandos de Xbox y compatibles), cargado en tiempo de ejecución; el motor grande
// del DualShock es el izquierdo (baja frecuencia) y el pequeño, el derecho. Sin XInput, no vibra.
//
// [DaniSBR1] TEMPORAL: la solución completa sería emular el SIO2 en ps2xIOP para que
// los IRX del juego funcionen tal cual. Las direcciones solo valen para el ELF cuyo SHA1 está en
// docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t kPadCreateSocketAddr = 0x0027B890u;
    constexpr uint32_t kPadGetStatusAddr = 0x0027BB98u;
    constexpr uint32_t kPadGetInfoAddr = 0x0027BAC8u;
    constexpr uint32_t kPadGetActuatorsAddr = 0x0027BF90u;
    constexpr uint32_t kPadReadAddr = 0x0027B9F0u;
    // FUN_0017C0F8(puerto): el juego reinicia un puerto que ha dejado de responder (tipo +0x08 = 0,
    // paso +0x7C = 0). Solo se envuelve para la traza.
    constexpr uint32_t kPadPortResetAddr = 0x0017C0F8u;

    constexpr int32_t kPadStatusConnected = 1;
    constexpr uint8_t kPadInfoDualShock = 0xFF;
    // FUN_0017C130 copia los sticks si leer devuelve >= 3 y la presión de 12 botones si devuelve >= 7
    constexpr int32_t kPadReadWithPressure = 9;
    constexpr uint32_t kPadPorts = 2u;
    constexpr uint32_t kPadNoSocket = 0xFFFFFFFFu;

    // Bits de los dos bytes de botones del DualShock (activos a nivel bajo en el búfer)
    enum PadButton : uint16_t
    {
        kPadSelect = 0x0001, kPadL3 = 0x0002, kPadR3 = 0x0004, kPadStart = 0x0008,
        kPadUp = 0x0010, kPadRight = 0x0020, kPadDown = 0x0040, kPadLeft = 0x0080,
        kPadL2 = 0x0100, kPadR2 = 0x0200, kPadL1 = 0x0400, kPadR1 = 0x0800,
        kPadTriangle = 0x1000, kPadCircle = 0x2000, kPadCross = 0x4000, kPadSquare = 0x8000,
    };

    // Orden de los 12 bytes de presión del DualShock 2
    constexpr std::array<uint16_t, 12> kPadPressureOrder = {
        kPadRight, kPadLeft, kPadUp, kPadDown, kPadTriangle, kPadCircle,
        kPadCross, kPadSquare, kPadL1, kPadR1, kPadL2, kPadR2};

    enum class PadSource : uint8_t
    {
        None,
        Gamepad,
        Keyboard,
    };

    struct PadState
    {
        bool connected = false;
        PadSource source = PadSource::None;
        uint16_t pressed = 0;                       // bits PadButton pulsados
        std::array<uint8_t, 4> sticks{0x80, 0x80, 0x80, 0x80}; // der. X/Y, izq. X/Y (0x80 = centro)
        std::array<uint8_t, 12> pressure{};         // 0..255 en el orden de kPadPressureOrder
    };

    // ── Configuración ──────────────────────────────────────────────────────────────────────────

    bool padEnvFlag(const char *name, bool fallback)
    {
        const char *value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
            return fallback;
        return value[0] == '1';
    }

    bool padDebugEnabled()
    {
        static const bool enabled = padEnvFlag("GOW_PAD_DEBUG", false);
        return enabled;
    }

    float padDeadzone()
    {
        static const float deadzone = []
        {
            const char *value = std::getenv("GOW_PAD_DEADZONE");
            const float parsed = value != nullptr ? std::strtof(value, nullptr) : 0.15f;
            return std::clamp(parsed, 0.0f, 0.9f);
        }();
        return deadzone;
    }

    bool padKeyboardEnabled()
    {
        static const bool enabled = padEnvFlag("GOW_PAD_KEYBOARD", true);
        return enabled;
    }

    // ── Lectura de la entrada del PC ───────────────────────────────────────────────────────────

    uint8_t padAxisToByte(float value)
    {
        return static_cast<uint8_t>(std::lround(std::clamp(128.0f + value * 127.0f, 0.0f, 255.0f)));
    }

    // Zona muerta radial: por debajo de la zona muerta el stick está centrado; por encima, se reescala
    // para que el recorrido útil siga yendo de 0 a 1 sin salto. Radial y no por eje, para no deformar
    // las diagonales.
    void padApplyDeadzone(float &x, float &y)
    {
        const float magnitude = std::sqrt(x * x + y * y);
        const float deadzone = padDeadzone();
        if (magnitude <= deadzone || magnitude == 0.0f)
        {
            x = y = 0.0f;
            return;
        }
        const float scaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
        x = x / magnitude * scaled;
        y = y / magnitude * scaled;
    }

    void padReadGamepad(int gamepad, PadState &state)
    {
        static const std::pair<int, uint16_t> kButtons[] = {
            {GAMEPAD_BUTTON_LEFT_FACE_UP, kPadUp}, {GAMEPAD_BUTTON_LEFT_FACE_DOWN, kPadDown},
            {GAMEPAD_BUTTON_LEFT_FACE_LEFT, kPadLeft}, {GAMEPAD_BUTTON_LEFT_FACE_RIGHT, kPadRight},
            {GAMEPAD_BUTTON_RIGHT_FACE_DOWN, kPadCross}, {GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, kPadCircle},
            {GAMEPAD_BUTTON_RIGHT_FACE_LEFT, kPadSquare}, {GAMEPAD_BUTTON_RIGHT_FACE_UP, kPadTriangle},
            {GAMEPAD_BUTTON_LEFT_TRIGGER_1, kPadL1}, {GAMEPAD_BUTTON_RIGHT_TRIGGER_1, kPadR1},
            {GAMEPAD_BUTTON_MIDDLE_LEFT, kPadSelect}, {GAMEPAD_BUTTON_MIDDLE_RIGHT, kPadStart},
            {GAMEPAD_BUTTON_LEFT_THUMB, kPadL3}, {GAMEPAD_BUTTON_RIGHT_THUMB, kPadR3}};
        // L2/R2 no van aquí: raylib los saca del eje del gatillo (> 0,1), y se leen abajo
        for (const auto &[button, bit] : kButtons)
        {
            if (IsGamepadButtonDown(gamepad, button))
                state.pressed |= bit;
        }

        float lx = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_X);
        float ly = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_LEFT_Y);
        float rx = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_X);
        float ry = GetGamepadAxisMovement(gamepad, GAMEPAD_AXIS_RIGHT_Y);
        padApplyDeadzone(lx, ly);
        padApplyDeadzone(rx, ry);
        state.sticks = {padAxisToByte(rx), padAxisToByte(ry), padAxisToByte(lx), padAxisToByte(ly)};

        // Gatillos: raylib da -1 (suelto) .. 1 (a fondo). Con 1/4 de recorrido cuentan como pulsados,
        // y su presión sale del recorrido real (en los mandos sin gatillo analógico sale de L2/R2).
        // Un gatillo solo se usa después de verlo una vez en reposo (-1): raylib dice siempre que hay 6
        // ejes, y en un mando sin mapeo los da a 0, que con esta fórmula sería medio gatillo pulsado.
        static std::array<std::array<std::atomic<bool>, 2>, kPadPorts> triggerSeenAtRest{};
        auto trigger = [&](int axis, std::atomic<bool> &seenAtRest)
        {
            const float value = GetGamepadAxisMovement(gamepad, axis);
            if (value < -0.9f)
                seenAtRest = true;
            return seenAtRest ? std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f) : 0.0f;
        };
        auto &seen = triggerSeenAtRest[static_cast<size_t>(gamepad) % kPadPorts];
        const float lt = trigger(GAMEPAD_AXIS_LEFT_TRIGGER, seen[0]);
        const float rt = trigger(GAMEPAD_AXIS_RIGHT_TRIGGER, seen[1]);
        if (lt > 0.25f)
            state.pressed |= kPadL2;
        if (rt > 0.25f)
            state.pressed |= kPadR2;
        for (size_t i = 0; i < kPadPressureOrder.size(); ++i)
        {
            const uint16_t bit = kPadPressureOrder[i];
            float amount = (state.pressed & bit) ? 1.0f : 0.0f;
            if (bit == kPadL2)
                amount = std::max(amount, lt);
            if (bit == kPadR2)
                amount = std::max(amount, rt);
            state.pressure[i] = static_cast<uint8_t>(std::lround(std::clamp(amount, 0.0f, 1.0f) * 255.0f));
        }
    }

    void padReadKeyboard(PadState &state)
    {
        static const std::pair<int, uint16_t> kKeys[] = {
            {KEY_UP, kPadUp}, {KEY_DOWN, kPadDown}, {KEY_LEFT, kPadLeft}, {KEY_RIGHT, kPadRight},
            {KEY_SPACE, kPadCross}, {KEY_X, kPadCross}, {KEY_C, kPadCircle}, {KEY_ESCAPE, kPadCircle},
            {KEY_Z, kPadSquare}, {KEY_V, kPadTriangle}, {KEY_Q, kPadL1}, {KEY_E, kPadR1},
            {KEY_LEFT_SHIFT, kPadL2}, {KEY_RIGHT_SHIFT, kPadR2}, {KEY_ENTER, kPadStart}, {KEY_TAB, kPadSelect}};
        for (const auto &[key, bit] : kKeys)
        {
            if (IsKeyDown(key))
                state.pressed |= bit;
        }
        auto axis = [](int negative, int positive)
        { return (IsKeyDown(positive) ? 1.0f : 0.0f) - (IsKeyDown(negative) ? 1.0f : 0.0f); };
        float lx = axis(KEY_A, KEY_D), ly = axis(KEY_W, KEY_S);
        float rx = axis(KEY_J, KEY_L), ry = axis(KEY_I, KEY_K);
        // Las diagonales del teclado no deben salir del círculo del stick
        for (float *x : {&lx, &rx})
        {
            float *y = (x == &lx) ? &ly : &ry;
            const float magnitude = std::sqrt(*x * *x + *y * *y);
            if (magnitude > 1.0f)
            {
                *x /= magnitude;
                *y /= magnitude;
            }
        }
        state.sticks = {padAxisToByte(rx), padAxisToByte(ry), padAxisToByte(lx), padAxisToByte(ly)};
        for (size_t i = 0; i < kPadPressureOrder.size(); ++i)
            state.pressure[i] = (state.pressed & kPadPressureOrder[i]) ? 0xFF : 0x00;
    }

    void padMerge(PadState &into, const PadState &sample);

    // Puerto 1: el mando 1 y el teclado a la vez (botones sumados, el stick más desviado); si no hay
    // mando, solo el teclado. Puerto 2: el mando 2.
    PadState padReadPort(uint32_t port)
    {
        PadState state;
        const int gamepad = static_cast<int>(port);
        const bool keyboard = port == 0u && padKeyboardEnabled();
        if (keyboard)
        {
            state.connected = true;
            state.source = PadSource::Keyboard;
            padReadKeyboard(state);
        }
        if (IsGamepadAvailable(gamepad))
        {
            PadState fromGamepad;
            fromGamepad.connected = true;
            fromGamepad.source = PadSource::Gamepad;
            padReadGamepad(gamepad, fromGamepad);
            if (keyboard)
                padMerge(state, fromGamepad); // se queda con la fuente del mando
            else
                state = fromGamepad;
        }
        return state;
    }

    // GOW_PAD_DEBUG=1: cada vez que cambia la entrada de un puerto, lo que da raylib en crudo (botones
    // pulsados con su número de GamepadButton y todos los ejes) y lo que recibe el juego. Sirve para ver
    // si un mando concreto llega bien mapeado.
    void padTraceInput(uint32_t port, const PadState &state)
    {
        static std::array<std::string, kPadPorts> lastLine;
        std::string line = "botones=0x";
        char hex[16];
        std::snprintf(hex, sizeof(hex), "%04x", state.pressed);
        line += hex;
        line += " sticks(der.X,der.Y,izq.X,izq.Y)=";
        for (uint8_t value : state.sticks)
            line += std::to_string(value) + ",";
        const int gamepad = static_cast<int>(port);
        if (state.source == PadSource::Gamepad)
        {
            line += " | raylib: pulsados=[";
            for (int button = GAMEPAD_BUTTON_LEFT_FACE_UP; button <= GAMEPAD_BUTTON_RIGHT_THUMB; ++button)
            {
                if (IsGamepadButtonDown(gamepad, button))
                    line += std::to_string(button) + " ";
            }
            line += "] ejes=";
            const int axes = GetGamepadAxisCount(gamepad);
            for (int axis = 0; axis < axes && axis < 8; ++axis)
            {
                std::snprintf(hex, sizeof(hex), "%.2f,", GetGamepadAxisMovement(gamepad, axis));
                line += hex;
            }
        }
        if (line != lastLine[port])
        {
            lastLine[port] = line;
            std::cout << "[gow:pad] puerto " << (port + 1u) << ": " << line << std::endl;
        }
    }

    // ── Retención de pulsaciones ───────────────────────────────────────────────────────────────
    // El juego lee el mando una vez por frame de su lógica (FUN_0017C130), y hoy va a menos de 2 frames
    // por segundo: un toque de 100–200 ms cae casi siempre entre dos lecturas y el juego no lo ve (solo
    // respondía lo que se mantenía pulsado). raylib sí ve la entrada a 60 Hz (la sondea el hilo de la
    // ventana en EndDrawing), así que un hilo de aquí la muestrea cada 4 ms y acumula desde la última
    // lectura: los botones pulsados en algún momento, la presión máxima y la posición más alejada del
    // centro de cada stick. La lectura del juego se lleva lo acumulado y lo vacía; así un toque llega una
    // vez pulsado y en la lectura siguiente suelto, que es lo que el juego necesita para verlo.

    struct PadLatch
    {
        std::mutex mutex;
        std::array<PadState, kPadPorts> held{};
        std::array<bool, kPadPorts> pending{};
    };

    // No se destruye nunca: el hilo de muestreo puede seguir vivo mientras se cierran los estáticos
    PadLatch &padLatch()
    {
        static PadLatch *latch = new PadLatch();
        return *latch;
    }

    int padStickDeflection(uint8_t x, uint8_t y)
    {
        const int dx = static_cast<int>(x) - 0x80;
        const int dy = static_cast<int>(y) - 0x80;
        return dx * dx + dy * dy;
    }

    // Junta una muestra nueva con lo acumulado; la conexión y la fuente son las de la muestra
    void padMerge(PadState &into, const PadState &sample)
    {
        into.connected = sample.connected;
        into.source = sample.source;
        into.pressed |= sample.pressed;
        for (size_t stick = 0; stick < into.sticks.size(); stick += 2)
        {
            if (padStickDeflection(sample.sticks[stick], sample.sticks[stick + 1]) >
                padStickDeflection(into.sticks[stick], into.sticks[stick + 1]))
            {
                into.sticks[stick] = sample.sticks[stick];
                into.sticks[stick + 1] = sample.sticks[stick + 1];
            }
        }
        for (size_t i = 0; i < into.pressure.size(); ++i)
            into.pressure[i] = std::max(into.pressure[i], sample.pressure[i]);
    }

    void padSampleAll()
    {
        PadLatch &latch = padLatch();
        for (uint32_t port = 0; port < kPadPorts; ++port)
        {
            const PadState sample = padReadPort(port);
            std::lock_guard<std::mutex> lock(latch.mutex);
            if (!latch.pending[port])
            {
                latch.held[port] = sample;
                latch.pending[port] = true;
            }
            else
            {
                padMerge(latch.held[port], sample);
            }
        }
    }

    // Lo que el juego debe ver en esta lectura: lo acumulado desde la anterior más el estado actual
    PadState padTakeLatched(uint32_t port)
    {
        const PadState now = padReadPort(port);
        PadLatch &latch = padLatch();
        std::lock_guard<std::mutex> lock(latch.mutex);
        if (!latch.pending[port])
            return now;
        PadState state = latch.held[port];
        padMerge(state, now);
        latch.pending[port] = false;
        return state;
    }

    // ── Vibración con XInput (mandos de Xbox y compatibles) ────────────────────────────────────
    // Se carga en tiempo de ejecución, sin enlazar nada ni incluir <windows.h> (choca con raylib:
    // CloseWindow, ShowCursor...). Las dos declaraciones usan los mismos tipos que Windows, así que no
    // chocan si otro archivo del mismo bloque de la unity build sí lo incluye.
}

extern "C" __declspec(dllimport) struct HINSTANCE__ *__stdcall LoadLibraryA(const char *name);
extern "C" __declspec(dllimport) __int64(__stdcall *__stdcall GetProcAddress(struct HINSTANCE__ *module, const char *name))();

namespace
{
    struct PadXInputGamepad
    {
        uint16_t buttons;
        uint8_t leftTrigger, rightTrigger;
        int16_t thumbLX, thumbLY, thumbRX, thumbRY;
    };
    struct PadXInputState
    {
        uint32_t packet;
        PadXInputGamepad gamepad;
    };
    struct PadXInputVibration
    {
        uint16_t leftMotor;  // baja frecuencia (el motor grande del DualShock)
        uint16_t rightMotor; // alta frecuencia (el motor pequeño)
    };
    using PadXInputGetStateFn = uint32_t(__stdcall *)(uint32_t, PadXInputState *);
    using PadXInputSetStateFn = uint32_t(__stdcall *)(uint32_t, PadXInputVibration *);

    struct PadXInput
    {
        PadXInputGetStateFn getState = nullptr;
        PadXInputSetStateFn setState = nullptr;
    };

    const PadXInput &padXInput()
    {
        static const PadXInput api = []
        {
            PadXInput loaded;
            if (!padEnvFlag("GOW_PAD_RUMBLE", true))
                return loaded;
            for (const char *dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"})
            {
                if (HINSTANCE__ *module = LoadLibraryA(dll))
                {
                    loaded.getState = reinterpret_cast<PadXInputGetStateFn>(GetProcAddress(module, "XInputGetState"));
                    loaded.setState = reinterpret_cast<PadXInputSetStateFn>(GetProcAddress(module, "XInputSetState"));
                    if (loaded.getState && loaded.setState)
                    {
                        std::cout << "[gow:pad] vibración con " << dll << std::endl;
                        return loaded;
                    }
                }
            }
            std::cout << "[gow:pad] XInput no disponible: sin vibración" << std::endl;
            return PadXInput{};
        }();
        return api;
    }

    // Índice XInput del puerto: el primer mando XInput conectado es el puerto 1, el segundo el 2
    int padXInputIndex(uint32_t port)
    {
        const PadXInput &api = padXInput();
        if (!api.getState)
            return -1;
        uint32_t seen = 0;
        for (uint32_t index = 0; index < 4u; ++index)
        {
            PadXInputState state{};
            if (api.getState(index, &state) == 0u && seen++ == port)
                return static_cast<int>(index);
        }
        return -1;
    }

    std::atomic<bool> g_padRumbleActive{false};

    void padSetRumble(uint32_t port, uint8_t big, bool small)
    {
        const int index = padXInputIndex(port);
        if (index < 0)
            return;
        PadXInputVibration vibration{static_cast<uint16_t>(big * 257u), static_cast<uint16_t>(small ? 0xFFFFu : 0u)};
        padXInput().setState(static_cast<uint32_t>(index), &vibration);
        if (big != 0u || small)
            g_padRumbleActive = true;
    }

    // Al cerrar: parar el hilo de muestreo y que el mando no se quede vibrando
    std::atomic<bool> g_padSamplerStop{false};

    void padStopAllRumble()
    {
        g_padSamplerStop = true;
        if (!g_padRumbleActive.exchange(false))
            return;
        for (uint32_t port = 0; port < 2u; ++port)
            padSetRumble(port, 0, false);
    }

    // GOW_PAD_RUMBLE_TEST=1: vibra 1 s desde la primera lectura (en tiempo real: a 2 lecturas por
    // segundo, contar lecturas no sirve)
    std::atomic<int64_t> g_padRumbleTestOffMs{0};

    int64_t padNowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void padSamplerLoop()
    {
        while (!g_padSamplerStop.load())
        {
            padSampleAll();
            const int64_t offAt = g_padRumbleTestOffMs.load();
            if (offAt != 0 && padNowMs() >= offAt && g_padRumbleTestOffMs.exchange(0) != 0)
            {
                padSetRumble(0u, 0u, false);
                std::cout << "[gow:pad] prueba de vibración: apagada" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }

    // ── Sockets: qué socket es de qué puerto ───────────────────────────────────────────────────

    PS2Runtime::RecompiledFunction g_padOriginalCreateSocket = nullptr;
    PS2Runtime::RecompiledFunction g_padOriginalPortReset = nullptr;
    std::array<std::atomic<uint32_t>, kPadPorts> g_padSockets{kPadNoSocket, kPadNoSocket};
    std::array<std::atomic<PadSource>, kPadPorts> g_padLastSource{PadSource::None, PadSource::None};
    std::atomic<uint32_t> g_padReads{0};

    void padCreateSocket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // params = {2, puerto, ranura}
        const uint32_t params = getRegU32(ctx, 4);
        const uint32_t port = params != 0u ? Ps2FastRead32(rdram, params + 4u) : kPadNoSocket;
        g_padOriginalCreateSocket(rdram, ctx, runtime);
        const uint32_t socket = getRegU32(ctx, 2);
        if (port < kPadPorts)
            g_padSockets[port] = socket;
        std::cout << "[gow:pad] socket del puerto " << (port + 1u) << ": 0x" << std::hex << socket << std::dec << std::endl;
    }

    uint32_t padPortForSocket(uint32_t socket)
    {
        for (uint32_t port = 0; port < kPadPorts; ++port)
        {
            if (g_padSockets[port].load() == socket)
                return port;
        }
        return kPadNoSocket;
    }

    void padPortReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t port = getRegU32(ctx, 4);
        // El puerto 2 vacío se reinicia en cada frame: solo las primeras veces
        static std::atomic<uint32_t> traced{0};
        if (padDebugEnabled() && traced.fetch_add(1) < 4u)
        {
            std::cout << "[gow:pad] el juego reinicia un puerto como desconectado: estructura=0x" << std::hex << port
                      << " socket=0x" << Ps2FastRead32(rdram, port + 0x78u) << std::dec
                      << " (lecturas hasta ahora: " << g_padReads.load() << ")" << std::endl;
        }
        g_padOriginalPortReset(rdram, ctx, runtime);
    }

    // ── Pulsaciones programadas (pruebas sin nadie delante) ────────────────────────────────────
    //   $env:GOW_PAD_PRESS = "start@600,cross@1200"
    // pulsa START en la lectura 600 del puerto 1 y ✕ en la 1200 (una lectura por frame). Cada pulsación
    // dura kPadPressReads lecturas.

    struct PadScriptedPress
    {
        uint32_t read;
        uint16_t mask;
    };
    constexpr uint32_t kPadPressReads = 8u;
    std::vector<PadScriptedPress> g_padScript;

    void padLoadScript()
    {
        const char *env = std::getenv("GOW_PAD_PRESS");
        if (env == nullptr)
            return;
        static const std::pair<const char *, uint16_t> kNames[] = {
            {"select", kPadSelect}, {"l3", kPadL3}, {"r3", kPadR3}, {"start", kPadStart},
            {"up", kPadUp}, {"right", kPadRight}, {"down", kPadDown}, {"left", kPadLeft},
            {"l2", kPadL2}, {"r2", kPadR2}, {"l1", kPadL1}, {"r1", kPadR1},
            {"triangle", kPadTriangle}, {"circle", kPadCircle}, {"cross", kPadCross}, {"square", kPadSquare}};
        const std::string spec(env);
        size_t pos = 0;
        while (pos < spec.size())
        {
            const size_t comma = spec.find(',', pos);
            const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            const size_t at = item.find('@');
            if (at != std::string::npos)
            {
                const std::string name = item.substr(0, at);
                for (const auto &[label, mask] : kNames)
                {
                    if (name == label)
                    {
                        g_padScript.push_back({static_cast<uint32_t>(std::strtoul(item.c_str() + at + 1, nullptr, 10)), mask});
                        std::cout << "[gow:pad] pulsación programada: " << name << " en la lectura " << g_padScript.back().read << std::endl;
                    }
                }
            }
            if (comma == std::string::npos)
                break;
            pos = comma + 1;
        }
    }

    void padApplyScript(uint32_t read, PadState &state)
    {
        for (const PadScriptedPress &press : g_padScript)
        {
            if (read >= press.read && read < press.read + kPadPressReads)
            {
                state.connected = true;
                state.pressed |= press.mask;
                if (read == press.read)
                    std::cout << "[gow:pad] pulsando 0x" << std::hex << press.mask << std::dec << " (lectura " << read << ")" << std::endl;
            }
        }
    }

    // ── Funciones de la librería sustituidas ───────────────────────────────────────────────────

    void padGetStatus(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t port = padPortForSocket(getRegU32(ctx, 4));
        const PadState state = port < kPadPorts ? padReadPort(port) : PadState{};
        // Al quitar el mando del puerto 1 con el teclado activado, el puerto no se desconecta: se queda
        // con el teclado. Para que el juego vea la desconexión: GOW_PAD_KEYBOARD=0.
        if (port < kPadPorts && g_padLastSource[port].exchange(state.source) != state.source)
        {
            const int gamepad = static_cast<int>(port);
            std::cout << "[gow:pad] puerto " << (port + 1u) << ": ";
            if (state.source == PadSource::Gamepad)
                std::cout << "mando \"" << GetGamepadName(gamepad) << "\" conectado (" << GetGamepadAxisCount(gamepad) << " ejes)";
            else if (state.source == PadSource::Keyboard)
                std::cout << "solo teclado (no hay mando)";
            else
                std::cout << "desconectado";
            std::cout << std::endl;
        }
        setReturnS32(ctx, state.connected ? kPadStatusConnected : 0);
    }

    void padGetInfo(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t info = getRegU32(ctx, 5);
        if (info != 0u)
            rdram[info & PS2_RAM_MASK] = kPadInfoDualShock;
        setReturnS32(ctx, 1);
    }

    // El juego solo usa el resultado como indicador (+0x8C): con él distinto de 0 manda la vibración.
    void padGetActuators(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t port = padPortForSocket(getRegU32(ctx, 4));
        setReturnS32(ctx, (port < kPadPorts && padXInputIndex(port) >= 0) ? 1 : 0);
    }

    // 0x27C010(socket, 1, &cmd, 2, datos): FUN_0017C130 empaqueta las peticiones del frame
    // (+0x24 = motor pequeño, encendido/apagado; +0x25 = motor grande, 0..255, ya escaladas por la
    // intensidad de las opciones en +0x0C) como datos = (grande << 1) | pequeño, en 2 bytes, y solo
    // llama cuando cambian. Devuelve 1 = aceptado (el juego apunta el valor en +0x80).
    constexpr uint32_t kPadSetActuatorsAddr = 0x0027C010u;

    void padSetActuators(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t port = padPortForSocket(getRegU32(ctx, 4));
        const uint32_t data = getRegU32(ctx, 8); // $t0, quinto argumento
        if (port < kPadPorts && data != 0u)
        {
            const uint32_t packed = rdram[data & PS2_RAM_MASK] | (rdram[(data + 1u) & PS2_RAM_MASK] << 8);
            const bool small = (packed & 1u) != 0u;
            const uint8_t big = static_cast<uint8_t>((packed >> 1) & 0xFFu);
            padSetRumble(port, big, small);
            if (padDebugEnabled())
                std::cout << "[gow:pad] vibración puerto " << (port + 1u) << ": grande=" << static_cast<int>(big)
                          << " pequeño=" << (small ? 1 : 0) << std::endl;
        }
        setReturnS32(ctx, 1);
    }

    void padRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t port = padPortForSocket(getRegU32(ctx, 4));
        const uint32_t buffer = getRegU32(ctx, 5);
        if (buffer == 0u || port >= kPadPorts)
        {
            setReturnS32(ctx, -1);
            return;
        }

        PadState state = padTakeLatched(port);
        if (port == 0u)
        {
            const uint32_t read = g_padReads.fetch_add(1);
            padApplyScript(read, state);
            static const bool rumbleTest = padEnvFlag("GOW_PAD_RUMBLE_TEST", false);
            if (rumbleTest && read == 0u)
            {
                padSetRumble(0u, 200u, true);
                g_padRumbleTestOffMs = padNowMs() + 1000;
                std::cout << "[gow:pad] prueba de vibración: encendida" << std::endl;
            }
            // Ritmo de lectura: el juego lee el mando una vez por frame, así que esto son sus FPS
            if (padDebugEnabled() && read > 0u && (read % 50u) == 0u)
            {
                static double lastTime = GetTime();
                static uint32_t lastRead = 0u;
                const double now = GetTime();
                char rate[96];
                std::snprintf(rate, sizeof(rate), "[gow:pad] lectura %u: %.1f lecturas/s", read, (read - lastRead) / std::max(now - lastTime, 1e-3));
                std::cout << rate << std::endl;
                lastTime = now;
                lastRead = read;
            }
        }
        if (padDebugEnabled())
            padTraceInput(port, state);
        if (!state.connected)
        {
            setReturnS32(ctx, -1);
            return;
        }

        const uint16_t released = static_cast<uint16_t>(~state.pressed);
        std::array<uint8_t, 18> bytes{};
        bytes[0] = static_cast<uint8_t>(released & 0xFFu);
        bytes[1] = static_cast<uint8_t>(released >> 8);
        std::copy(state.sticks.begin(), state.sticks.end(), bytes.begin() + 2);
        std::copy(state.pressure.begin(), state.pressure.end(), bytes.begin() + 6);
        for (uint32_t i = 0; i < bytes.size(); ++i)
            rdram[(buffer + i) & PS2_RAM_MASK] = bytes[i];
        setReturnS32(ctx, kPadReadWithPressure);
    }

    void applyPadOverrides(PS2Runtime &runtime)
    {
        for (uint32_t address : {kPadCreateSocketAddr, kPadGetStatusAddr, kPadGetInfoAddr, kPadGetActuatorsAddr, kPadReadAddr})
        {
            if (!runtime.hasFunction(address))
            {
                std::cerr << "[gow:pad] falta la función 0x" << std::hex << address << std::dec
                          << " en el código generado; el mando no se conecta" << std::endl;
                return;
            }
        }
        padLoadScript();
        g_padOriginalCreateSocket = runtime.lookupFunction(kPadCreateSocketAddr);
        runtime.registerFunction(kPadCreateSocketAddr, &padCreateSocket);
        if (runtime.hasFunction(kPadPortResetAddr) && padDebugEnabled())
        {
            g_padOriginalPortReset = runtime.lookupFunction(kPadPortResetAddr);
            runtime.registerFunction(kPadPortResetAddr, &padPortReset);
        }
        runtime.registerFunction(kPadGetStatusAddr, &padGetStatus);
        runtime.registerFunction(kPadGetInfoAddr, &padGetInfo);
        runtime.registerFunction(kPadGetActuatorsAddr, &padGetActuators);
        runtime.registerFunction(kPadReadAddr, &padRead);
        if (runtime.hasFunction(kPadSetActuatorsAddr))
            runtime.registerFunction(kPadSetActuatorsAddr, &padSetActuators);
        std::atexit(padStopAllRumble);
        std::thread(padSamplerLoop).detach();
        std::cout << "[gow:pad] zona muerta " << padDeadzone() << (padKeyboardEnabled() ? ", teclado en el puerto 1" : ", sin teclado")
                  << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-pad",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyPadOverrides);
