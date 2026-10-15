// Perfilador de muestreo: dónde pasa el tiempo cada hilo del proceso (lentitud de la fase 5).
//
// La lógica del juego va a ~1,8 frames por segundo (se mide con GOW_PAD_DEBUG=1). Para saber por qué
// sin un perfilador externo, este archivo hace lo mismo que uno de muestreo: cada pocos ms congela un
// instante cada hilo, apunta en qué instrucción está y recorre su pila (con la información de
// desenrollado del propio .exe); al acabar cada intervalo traduce las direcciones a nombres de
// función con el PDB (dbghelp) e imprime, por hilo:
//   - ocupado / en espera: qué parte del tiempo trabaja y qué parte duerme en el sistema;
//   - propio: en qué función estaba exactamente (dónde se gasta la CPU);
//   - inclusivo: qué funciones estaban en la pila (quién provoca ese gasto). Las funciones
//     recompiladas se llaman FUN_<dirección del EE>, así que se ve la cadena de llamadas del juego.
//
// Solo se activa con GOW_PROFILE=1, y necesita el PDB (RelWithDebInfo o Debug):
//   $env:GOW_PROFILE = "1"; .\scripts\run.ps1 -Config RelWithDebInfo -NoBuild
//   GOW_PROFILE_DELAY=20     segundos antes de empezar (durante el arranque se cargan DLL)
//   GOW_PROFILE_SECONDS=15   duración de cada informe
//   GOW_PROFILE_TOP=12       funciones por lista
//   GOW_PROFILE_DEPTH=7      niveles de cada "pila más frecuente"
//   GOW_PROFILE_FILTER=texto solo las pilas que pasan por una función cuyo nombre contiene el texto
//
// Las funciones de Windows se cargan con GetProcAddress: <windows.h> choca con raylib, y en la unity
// build ese choque llegaría a otros overrides.
//
// [DaniSBR1] DIAGNÓSTICO. No cambia el comportamiento del juego (solo lo frena un poco al muestrear).

#include "game_overrides.h"
#include "ps2_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Las mismas declaraciones que gow_pad.cpp (pueden caer en el mismo bloque de la unity build)
extern "C" __declspec(dllimport) struct HINSTANCE__ *__stdcall LoadLibraryA(const char *name);
extern "C" __declspec(dllimport) __int64(__stdcall *__stdcall GetProcAddress(struct HINSTANCE__ *module, const char *name))();

namespace
{
    // ── API de Windows, cargada a mano ─────────────────────────────────────────────────────────

    using ProfHandle = void *;

    struct ProfThreadEntry // THREADENTRY32
    {
        uint32_t size;
        uint32_t usage;
        uint32_t threadId;
        uint32_t ownerProcessId;
        int32_t basePriority;
        int32_t deltaPriority;
        uint32_t flags;
    };

    struct ProfSymbolInfo // SYMBOL_INFO con espacio para el nombre
    {
        uint32_t sizeOfStruct;
        uint32_t typeIndex;
        uint64_t reserved[2];
        uint32_t index;
        uint32_t size;
        uint64_t modBase;
        uint32_t flags;
        uint64_t value;
        uint64_t address;
        uint32_t reg;
        uint32_t scope;
        uint32_t tag;
        uint32_t nameLen;
        uint32_t maxNameLen;
        char name[512];
    };
    static_assert(offsetof(ProfSymbolInfo, name) == 84, "SYMBOL_INFO");

    // CONTEXT de x64: solo se tocan ContextFlags, Rsp y Rip; el resto lo usa RtlVirtualUnwind
    constexpr size_t kProfContextSize = 0x4D0;
    constexpr size_t kProfContextFlags = 0x30;
    constexpr size_t kProfContextRsp = 0x98;
    constexpr size_t kProfContextRip = 0xF8;
    constexpr uint32_t kProfContextFull = 0x0010000Bu; // CONTROL | INTEGER | FLOATING_POINT
    struct alignas(16) ProfContext
    {
        unsigned char bytes[kProfContextSize];
        uint64_t &at(size_t offset) { return *reinterpret_cast<uint64_t *>(bytes + offset); }
    };

    struct ProfApi
    {
        ProfHandle(*createSnapshot)(uint32_t, uint32_t) = nullptr;
        int (*thread32First)(ProfHandle, ProfThreadEntry *) = nullptr;
        int (*thread32Next)(ProfHandle, ProfThreadEntry *) = nullptr;
        ProfHandle (*openThread)(uint32_t, int, uint32_t) = nullptr;
        uint32_t (*suspendThread)(ProfHandle) = nullptr;
        uint32_t (*resumeThread)(ProfHandle) = nullptr;
        int (*getThreadContext)(ProfHandle, void *) = nullptr;
        int (*closeHandle)(ProfHandle) = nullptr;
        uint32_t (*currentThreadId)() = nullptr;
        uint32_t (*currentProcessId)() = nullptr;
        long (*getThreadDescription)(ProfHandle, wchar_t **) = nullptr;
        void *(*localFree)(void *) = nullptr;
        void *(*lookupFunctionEntry)(uint64_t, uint64_t *, void *) = nullptr;
        void *(*virtualUnwind)(uint32_t, uint64_t, uint64_t, void *, void *, void **, uint64_t *, void *) = nullptr;
        uint32_t (*symSetOptions)(uint32_t) = nullptr;
        int (*symInitialize)(ProfHandle, const char *, int) = nullptr;
        int (*symFromAddr)(ProfHandle, uint64_t, uint64_t *, ProfSymbolInfo *) = nullptr;
        bool ok = false;
    };

    template <typename T>
    void profLoad(T &target, HINSTANCE__ *module, const char *name)
    {
        target = module != nullptr ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
    }

    ProfApi profLoadApi()
    {
        ProfApi api;
        HINSTANCE__ *kernel = LoadLibraryA("kernel32.dll");
        HINSTANCE__ *ntdll = LoadLibraryA("ntdll.dll");
        HINSTANCE__ *dbghelp = LoadLibraryA("dbghelp.dll");
        profLoad(api.createSnapshot, kernel, "CreateToolhelp32Snapshot");
        profLoad(api.thread32First, kernel, "Thread32First");
        profLoad(api.thread32Next, kernel, "Thread32Next");
        profLoad(api.openThread, kernel, "OpenThread");
        profLoad(api.suspendThread, kernel, "SuspendThread");
        profLoad(api.resumeThread, kernel, "ResumeThread");
        profLoad(api.getThreadContext, kernel, "GetThreadContext");
        profLoad(api.closeHandle, kernel, "CloseHandle");
        profLoad(api.currentThreadId, kernel, "GetCurrentThreadId");
        profLoad(api.currentProcessId, kernel, "GetCurrentProcessId");
        profLoad(api.getThreadDescription, kernel, "GetThreadDescription");
        profLoad(api.localFree, kernel, "LocalFree");
        profLoad(api.lookupFunctionEntry, ntdll, "RtlLookupFunctionEntry");
        profLoad(api.virtualUnwind, ntdll, "RtlVirtualUnwind");
        profLoad(api.symSetOptions, dbghelp, "SymSetOptions");
        profLoad(api.symInitialize, dbghelp, "SymInitialize");
        profLoad(api.symFromAddr, dbghelp, "SymFromAddr");
        api.ok = api.createSnapshot && api.thread32First && api.thread32Next && api.openThread && api.suspendThread &&
                 api.resumeThread && api.getThreadContext && api.closeHandle && api.currentThreadId &&
                 api.currentProcessId && api.lookupFunctionEntry && api.virtualUnwind && api.symSetOptions &&
                 api.symInitialize && api.symFromAddr;
        return api;
    }

    ProfApi g_prof;
    ProfHandle const kProfCurrentProcess = reinterpret_cast<ProfHandle>(static_cast<intptr_t>(-1));

    // ── Muestreo ───────────────────────────────────────────────────────────────────────────────

    constexpr int kProfMaxFrames = 48;

    // Recorre la pila de un hilo congelado. Sin objetos de C++ para poder usar __try: si la pila está
    // a medias (el hilo estaba en mitad de un prólogo raro), se corta sin tumbar el proceso. Mientras
    // el hilo está congelado no se reserva memoria: si tenía tomado el lock del heap, nos bloquearíamos.
    int profUnwind(ProfContext *context, uint64_t *frames)
    {
        int depth = 0;
        __try
        {
            while (depth < kProfMaxFrames)
            {
                const uint64_t rip = context->at(kProfContextRip);
                if (rip == 0)
                    break;
                frames[depth++] = rip;
                uint64_t imageBase = 0;
                void *entry = g_prof.lookupFunctionEntry(rip, &imageBase, nullptr);
                if (entry == nullptr)
                {
                    // Función hoja sin datos de desenrollado: la dirección de retorno está en la cima
                    uint64_t &rsp = context->at(kProfContextRsp);
                    context->at(kProfContextRip) = *reinterpret_cast<const volatile uint64_t *>(rsp);
                    rsp += 8;
                }
                else
                {
                    void *handlerData = nullptr;
                    uint64_t establisher = 0;
                    g_prof.virtualUnwind(0, imageBase, rip, entry, context, &handlerData, &establisher, nullptr);
                }
            }
        }
        __except (1)
        {
        }
        return depth;
    }

    struct ProfFunction
    {
        uint64_t start = 0; // 0 = sin símbolo
        std::string name;
        bool waits = false; // función del sistema en la que un hilo duerme
        bool yields = false;
    };

    struct ProfThreadStats
    {
        std::string description;
        uint64_t samples = 0;
        uint64_t waiting = 0;
        uint64_t yielding = 0;
        std::unordered_map<uint64_t, uint64_t> self;      // función -> muestras en la cima
        std::unordered_map<uint64_t, uint64_t> inclusive; // función -> muestras en la pila
        std::map<std::vector<uint64_t>, uint64_t> stacks;  // pila (funciones, desde la cima) -> muestras
    };

    // Profundidad de las pilas que se agrupan en "pilas más frecuentes" (GOW_PROFILE_DEPTH)
    int g_profStackDepth = 7;

    struct ProfThread
    {
        uint32_t id = 0;
        ProfHandle handle = nullptr;
        std::string description;
    };

    std::unordered_map<uint64_t, uint64_t> g_profFunctionOf; // dirección -> inicio de función
    std::unordered_map<uint64_t, ProfFunction> g_profFunctions;

    bool profContains(const std::string &text, const char *needle)
    {
        return text.find(needle) != std::string::npos;
    }

    const ProfFunction &profFunctionFor(uint64_t address)
    {
        auto found = g_profFunctionOf.find(address);
        if (found != g_profFunctionOf.end())
            return g_profFunctions[found->second];

        ProfSymbolInfo symbol{};
        symbol.sizeOfStruct = 88; // sizeof(SYMBOL_INFO)
        symbol.maxNameLen = sizeof(symbol.name) - 1;
        uint64_t displacement = 0;
        uint64_t start = address;
        std::string name;
        if (g_prof.symFromAddr(kProfCurrentProcess, address, &displacement, &symbol))
        {
            start = symbol.address;
            name.assign(symbol.name, std::min<size_t>(symbol.nameLen, sizeof(symbol.name) - 1));
        }
        else
        {
            char hex[32];
            std::snprintf(hex, sizeof(hex), "0x%llx", static_cast<unsigned long long>(address));
            name = hex;
        }
        g_profFunctionOf[address] = start;
        ProfFunction &function = g_profFunctions[start];
        if (function.name.empty())
        {
            function.start = start;
            function.name = name;
            function.waits = profContains(name, "WaitFor") || profContains(name, "Delay") || profContains(name, "Sleep") ||
                             profContains(name, "RemoveIoCompletion") || profContains(name, "WaitOnAddress") ||
                             profContains(name, "AlertByThreadId") || profContains(name, "NtUserMsgWait") ||
                             profContains(name, "NtUserGetMessage") || profContains(name, "NtGdiDdDDIWait");
            function.yields = profContains(name, "YieldExecution") || profContains(name, "SwitchToThread");
        }
        return function;
    }

    std::string profDescribeThread(ProfHandle handle)
    {
        if (g_prof.getThreadDescription == nullptr || g_prof.localFree == nullptr)
            return {};
        wchar_t *wide = nullptr;
        if (g_prof.getThreadDescription(handle, &wide) < 0 || wide == nullptr)
            return {};
        std::string text;
        for (const wchar_t *c = wide; *c != 0; ++c)
            text.push_back(*c < 128 ? static_cast<char>(*c) : '?');
        g_prof.localFree(wide);
        return text;
    }

    void profRefreshThreads(std::vector<ProfThread> &threads)
    {
        const uint32_t self = g_prof.currentThreadId();
        const uint32_t process = g_prof.currentProcessId();
        std::vector<uint32_t> alive;
        ProfHandle snapshot = g_prof.createSnapshot(0x4u /* TH32CS_SNAPTHREAD */, 0);
        if (snapshot == reinterpret_cast<ProfHandle>(static_cast<intptr_t>(-1)))
            return;
        ProfThreadEntry entry{};
        entry.size = sizeof(entry);
        for (int ok = g_prof.thread32First(snapshot, &entry); ok; ok = g_prof.thread32Next(snapshot, &entry))
        {
            if (entry.ownerProcessId == process && entry.threadId != self)
                alive.push_back(entry.threadId);
            entry.size = sizeof(entry);
        }
        g_prof.closeHandle(snapshot);

        // Fuera los que han terminado, dentro los nuevos
        threads.erase(std::remove_if(threads.begin(), threads.end(), [&](ProfThread &thread)
                                     {
                                         const bool gone = std::find(alive.begin(), alive.end(), thread.id) == alive.end();
                                         if (gone)
                                             g_prof.closeHandle(thread.handle);
                                         return gone; }),
                      threads.end());
        for (uint32_t id : alive)
        {
            if (std::any_of(threads.begin(), threads.end(), [&](const ProfThread &thread)
                            { return thread.id == id; }))
                continue;
            // THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION
            ProfHandle handle = g_prof.openThread(0x0002u | 0x0008u | 0x0800u, 0, id);
            if (handle != nullptr)
                threads.push_back({id, handle, profDescribeThread(handle)});
        }
    }

    int profEnvInt(const char *name, int fallback)
    {
        const char *value = std::getenv(name);
        return value != nullptr && value[0] != '\0' ? std::atoi(value) : fallback;
    }

    std::string profPercent(uint64_t part, uint64_t total)
    {
        char text[16];
        std::snprintf(text, sizeof(text), "%5.1f%%", total != 0 ? 100.0 * static_cast<double>(part) / static_cast<double>(total) : 0.0);
        return text;
    }

    void profPrintTop(const char *title, const std::unordered_map<uint64_t, uint64_t> &counts, uint64_t total, int top)
    {
        std::vector<std::pair<uint64_t, uint64_t>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
                  { return a.second > b.second; });
        std::cout << "[gow:prof]     " << title << ":" << std::endl;
        for (int i = 0; i < top && i < static_cast<int>(sorted.size()); ++i)
        {
            std::string name = g_profFunctions[sorted[i].first].name;
            if (name.size() > 110)
                name = name.substr(0, 107) + "...";
            std::cout << "[gow:prof]       " << profPercent(sorted[i].second, total) << "  " << name << std::endl;
        }
    }

    // Nombre corto para las pilas: sin espacios de nombres ni plantillas
    std::string profShortName(const std::string &name)
    {
        std::string text = name;
        const size_t paren = text.find('(');
        if (paren != std::string::npos)
            text.resize(paren);
        const size_t colons = text.rfind("::");
        if (colons != std::string::npos && text.find('<') == std::string::npos)
            text = text.substr(colons + 2);
        return text.size() > 40 ? text.substr(0, 37) + "..." : text;
    }

    void profPrintStacks(const std::map<std::vector<uint64_t>, uint64_t> &stacks, uint64_t total, int top)
    {
        std::vector<std::pair<const std::vector<uint64_t> *, uint64_t>> sorted;
        static const std::string filter = std::getenv("GOW_PROFILE_FILTER") ? std::getenv("GOW_PROFILE_FILTER") : "";
        for (const auto &[stack, count] : stacks)
        {
            const bool matches = filter.empty() || std::any_of(stack.begin(), stack.end(), [&](uint64_t function)
                                                               { return profContains(g_profFunctions[function].name, filter.c_str()); });
            if (matches)
                sorted.push_back({&stack, count});
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b)
                  { return a.second > b.second; });
        std::cout << "[gow:prof]     pilas más frecuentes (cima <- quien la llama)"
                  << (filter.empty() ? "" : ", solo las que pasan por \"" + filter + "\"") << ":" << std::endl;
        for (int i = 0; i < top && i < static_cast<int>(sorted.size()); ++i)
        {
            std::string line;
            for (uint64_t function : *sorted[i].first)
                line += (line.empty() ? "" : " <- ") + profShortName(g_profFunctions[function].name);
            std::cout << "[gow:prof]       " << profPercent(sorted[i].second, total) << "  " << line << std::endl;
        }
    }

    void profReport(std::unordered_map<uint32_t, ProfThreadStats> &stats, double seconds, uint64_t rounds, int top)
    {
        std::vector<std::pair<uint32_t, ProfThreadStats *>> busy;
        for (auto &[id, thread] : stats)
        {
            const uint64_t working = thread.samples - thread.waiting;
            if (working * 100u >= std::max<uint64_t>(rounds, 1u)) // al menos un 1 % ocupado
                busy.push_back({id, &thread});
        }
        std::sort(busy.begin(), busy.end(), [](const auto &a, const auto &b)
                  { return (a.second->samples - a.second->waiting) > (b.second->samples - b.second->waiting); });

        char header[160];
        std::snprintf(header, sizeof(header), "[gow:prof] ── informe: %.1f s, %llu rondas de muestreo, %zu hilos, %zu ocupados ──",
                      seconds, static_cast<unsigned long long>(rounds), stats.size(), busy.size());
        std::cout << header << std::endl;
        int shown = 0;
        for (auto &[id, thread] : busy)
        {
            std::cout << "[gow:prof]   hilo " << id << (thread->description.empty() ? "" : " \"" + thread->description + "\"")
                      << ": ocupado " << profPercent(thread->samples - thread->waiting - thread->yielding, thread->samples)
                      << ", cediendo (yield) " << profPercent(thread->yielding, thread->samples)
                      << ", en espera " << profPercent(thread->waiting, thread->samples) << std::endl;
            const int limit = shown < 4 ? top : 3;
            profPrintTop("propio", thread->self, thread->samples, limit);
            if (shown < 4)
                profPrintTop("inclusivo", thread->inclusive, thread->samples, top + 8);
            if (shown == 0)
                profPrintStacks(thread->stacks, thread->samples, top);
            ++shown;
        }
        std::cout << std::flush;
        stats.clear();
    }

    void profLoop()
    {
        const int delay = profEnvInt("GOW_PROFILE_DELAY", 20);
        const int seconds = std::max(1, profEnvInt("GOW_PROFILE_SECONDS", 15));
        const int top = std::clamp(profEnvInt("GOW_PROFILE_TOP", 12), 1, 60);
        g_profStackDepth = std::clamp(profEnvInt("GOW_PROFILE_DEPTH", 7), 1, kProfMaxFrames);
        std::this_thread::sleep_for(std::chrono::seconds(delay));

        g_prof.symSetOptions(0x2u | 0x4u); // SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
        if (!g_prof.symInitialize(kProfCurrentProcess, nullptr, 1))
            std::cout << "[gow:prof] SymInitialize falló: saldrán direcciones sin nombre" << std::endl;
        std::cout << "[gow:prof] muestreando (informes cada " << seconds << " s)" << std::endl;

        std::vector<ProfThread> threads;
        std::unordered_map<uint32_t, ProfThreadStats> stats;
        auto intervalStart = std::chrono::steady_clock::now();
        auto lastRefresh = intervalStart - std::chrono::seconds(10);
        uint64_t rounds = 0;
        uint64_t frames[kProfMaxFrames];
        ProfContext context;

        for (;;)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRefresh >= std::chrono::seconds(1))
            {
                profRefreshThreads(threads);
                lastRefresh = now;
            }
            for (const ProfThread &thread : threads)
            {
                if (g_prof.suspendThread(thread.handle) == 0xFFFFFFFFu)
                    continue;
                std::memset(&context, 0, sizeof(context));
                *reinterpret_cast<uint32_t *>(context.bytes + kProfContextFlags) = kProfContextFull;
                int depth = 0;
                if (g_prof.getThreadContext(thread.handle, context.bytes))
                    depth = profUnwind(&context, frames);
                g_prof.resumeThread(thread.handle);
                if (depth == 0)
                    continue;

                // Ya con el hilo en marcha: símbolos y recuentos
                ProfThreadStats &stat = stats[thread.id];
                if (stat.description.empty())
                    stat.description = thread.description;
                ++stat.samples;
                const ProfFunction &leaf = profFunctionFor(frames[0]);
                if (leaf.waits)
                {
                    ++stat.waiting;
                    continue;
                }
                if (leaf.yields)
                    ++stat.yielding;
                ++stat.self[leaf.start];
                uint64_t seen[kProfMaxFrames];
                int seenCount = 0;
                std::vector<uint64_t> stack;
                for (int i = 0; i < depth; ++i)
                {
                    const uint64_t start = profFunctionFor(frames[i]).start;
                    if (i < g_profStackDepth)
                        stack.push_back(start);
                    if (std::find(seen, seen + seenCount, start) != seen + seenCount)
                        continue; // recursión: una vez por muestra
                    seen[seenCount++] = start;
                    ++stat.inclusive[start];
                }
                ++stat.stacks[stack];
            }
            ++rounds;

            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - intervalStart).count();
            if (elapsed >= seconds)
            {
                profReport(stats, elapsed, rounds, top);
                rounds = 0;
                intervalStart = std::chrono::steady_clock::now();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void applyProfile(PS2Runtime &runtime)
    {
        (void)runtime;
        const char *enabled = std::getenv("GOW_PROFILE");
        if (enabled == nullptr || enabled[0] != '1')
            return;
        g_prof = profLoadApi();
        if (!g_prof.ok)
        {
            std::cerr << "[gow:prof] no se pudo cargar la API de Windows/dbghelp: perfilador desactivado" << std::endl;
            return;
        }
        std::thread(profLoop).detach();
        std::cout << "[gow:prof] perfilador activo (GOW_PROFILE=1)" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-profile",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyProfile);
