// Diagnóstico del heap de God of War (bloqueo ABIERTO de FUN_0013dc78, ver docs/NOTES.md).
//
// FUN_0013dc78 es el malloc del juego: recorre la lista circular de bloques libres de un heap
// buscando uno donde quepa la petición. El arranque se queda girando dentro de ese recorrido, así
// que la lista ya no vuelve al nodo cabecera: o está corrupta o el heap nunca se inicializó bien.
//
// Este archivo NO arregla nada. Envuelve CreateHeap y malloc para registrar sus argumentos y
// comprobar la lista antes de cada malloc; después llama a la función original del juego, así que
// el comportamiento no cambia. Solo se activa con la variable de entorno GOW_HEAP_DEBUG=1, para no
// ralentizar ni llenar el log a los demás:
//
//   $env:GOW_HEAP_DEBUG = "1"; .\scripts\run.ps1
//
// [DaniSBR1] DIAGNÓSTICO: quitar cuando se cierre el bloqueo.
// Las direcciones solo valen para el ELF cuyo SHA1 está en docs/NOTES.md.

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    // CreateHeap(mem, tamaño, nombre, n): escribe la cabecera (magic 0xC0DE1111, nombre en +0x28),
    // enlaza el heap a la lista global de 0x29BEB0 y lo apila en la pila de heaps de 0x2CB940.
    constexpr uint32_t kCreateHeapAddr = 0x0013D710u;
    // malloc(heap, bytes, alineación). operator new (0x13D630) lo llama con el heap actual y alineación 8.
    constexpr uint32_t kMallocAddr = 0x0013DC78u;

    constexpr uint32_t kHeapMagic = 0xC0DE1111u;
    // Nodo libre: +0 cabecera (bits 0-24 = tamaño en palabras de 4 bytes), +4 siguiente, +8 anterior.
    // El propio heap hace de nodo cabecera: su +4 apunta al primer nodo libre.
    constexpr uint32_t kSizeMask = 0x01FFFFFFu;
    constexpr uint32_t kMaxSteps = 1000000u;
    constexpr uint32_t kVerboseCalls = 32u; // los primeros mallocs se registran siempre
    constexpr int kDumpNodes = 12;

    PS2Runtime::RecompiledFunction g_originalCreateHeap = nullptr;
    PS2Runtime::RecompiledFunction g_originalMalloc = nullptr;
    // Atómicos porque el runtime puede ejecutar hilos del juego en paralelo
    std::atomic<uint32_t> g_mallocCalls{0};
    std::atomic<bool> g_reportedBroken{false};

    uint32_t read32(const uint8_t *rdram, uint32_t addr)
    {
        return Ps2FastRead32(rdram, addr);
    }

    std::string readCString(const uint8_t *rdram, uint32_t addr, size_t maxLen = 16)
    {
        if (addr == 0)
        {
            return "(null)";
        }
        std::string text;
        for (size_t i = 0; i < maxLen; ++i)
        {
            const char c = static_cast<char>(rdram[(addr + i) & PS2_RAM_MASK]);
            if (c == '\0')
            {
                break;
            }
            text.push_back(c);
        }
        return text;
    }

    std::string hex32(uint32_t value)
    {
        std::ostringstream out;
        out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
        return out.str();
    }

    // Recorre la lista de libres con el algoritmo de Floyd (un puntero avanza de uno en uno y otro
    // de dos en dos): si se encuentran sin pasar por la cabecera, hay un ciclo. Así se detecta en
    // tiempo lineal y sin memoria extra, que es justo el caso que cuelga al malloc original.
    bool checkFreeList(const uint8_t *rdram, uint32_t heap, uint32_t &steps, uint32_t &badNode, const char *&reason)
    {
        uint32_t slow = read32(rdram, heap + 4u);
        uint32_t fast = slow;
        steps = 0;
        while (true)
        {
            for (int i = 0; i < 2; ++i)
            {
                if (fast == heap)
                {
                    return true;
                }
                if (fast == 0 || (fast & 3u) != 0)
                {
                    badNode = fast;
                    reason = (fast == 0) ? "puntero nulo" : "puntero desalineado";
                    return false;
                }
                fast = read32(rdram, fast + 4u);
                ++steps;
            }
            slow = read32(rdram, slow + 4u);
            if (slow == fast && fast != heap)
            {
                badNode = fast;
                reason = "ciclo que no pasa por la cabecera";
                return false;
            }
            if (steps > kMaxSteps)
            {
                badNode = fast;
                reason = "demasiados nodos";
                return false;
            }
        }
    }

    void dumpFreeList(const uint8_t *rdram, uint32_t heap)
    {
        uint32_t node = read32(rdram, heap + 4u);
        for (int i = 0; i < kDumpNodes && node != heap && node != 0 && (node & 3u) == 0; ++i)
        {
            const uint32_t header = read32(rdram, node);
            std::cout << "[gow:heap]     nodo " << hex32(node)
                      << " cabecera=" << hex32(header)
                      << " (" << (header & kSizeMask) << " palabras)"
                      << " sig=" << hex32(read32(rdram, node + 4u))
                      << " ant=" << hex32(read32(rdram, node + 8u)) << std::endl;
            node = read32(rdram, node + 4u);
        }
    }

    void createHeapProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mem = getRegU32(ctx, 4);
        const uint32_t size = getRegU32(ctx, 5);
        const uint32_t name = getRegU32(ctx, 6);
        const uint32_t count = getRegU32(ctx, 7);
        const uint32_t ra = getRegU32(ctx, 31);

        g_originalCreateHeap(rdram, ctx, runtime);

        std::cout << "[gow:heap] CreateHeap mem=" << hex32(mem)
                  << " nombre=\"" << readCString(rdram, name) << "\""
                  << " bytes=" << hex32(size) << " n=" << count
                  << " primerLibre=" << hex32(read32(rdram, mem + 4u))
                  << " desde=" << hex32(ra) << std::endl;
    }

    void mallocProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t heap = getRegU32(ctx, 4);
        const uint32_t bytes = getRegU32(ctx, 5);
        const uint32_t align = getRegU32(ctx, 6);
        const uint32_t ra = getRegU32(ctx, 31);
        const uint32_t call = g_mallocCalls.fetch_add(1);

        uint32_t steps = 0;
        uint32_t badNode = 0;
        const char *reason = "sin heap actual (heap = 0)";
        const bool ok = heap != 0 && checkFreeList(rdram, heap, steps, badNode, reason);

        // Solo se informa del primer fallo: después el malloc original se cuelga igual que antes
        const bool firstBroken = !ok && !g_reportedBroken.exchange(true);
        if (call < kVerboseCalls || firstBroken)
        {
            std::cout << "[gow:heap] malloc #" << call
                      << " heap=" << hex32(heap)
                      << " (\"" << readCString(rdram, heap + 0x28u) << "\""
                      << (read32(rdram, heap) == kHeapMagic ? "" : ", SIN MAGIC")
                      << ") bytes=" << hex32(bytes) << " alin=" << align
                      << " desde=" << hex32(ra)
                      << " libres=" << steps << std::endl;
        }
        if (firstBroken)
        {
            std::cout << "[gow:heap] LISTA ROTA en " << hex32(badNode) << ": " << reason << std::endl;
            if (heap != 0)
            {
                dumpFreeList(rdram, heap);
            }
        }

        g_originalMalloc(rdram, ctx, runtime);

        if (call < kVerboseCalls)
        {
            std::cout << "[gow:heap]   -> " << hex32(getRegU32(ctx, 2)) << std::endl;
        }
    }

    // ── Árbol de 0x1769F8 (búsqueda/inserción por clave) ──────────────────────────────────────
    // Nodos de 16 bytes de un pool fijo (8000 bloques, creado en 0x175D30): +0 clave (u32),
    // +0xA hijo izquierdo y +0xC hijo derecho como índices u16 desde la base del pool (0x29C4BC).
    // El nodo "vacío" está en *0x29C4B4. La búsqueda baja hasta encontrar la clave o el nodo vacío.
    constexpr uint32_t kTreeFindAddr = 0x001769F8u;
    constexpr uint32_t kTreeNodeAllocAddr = 0x00175D88u;
    constexpr uint32_t kTreeNilPtr = 0x0029C4B4u;
    constexpr uint32_t kTreePoolBasePtr = 0x0029C4BCu;
    constexpr uint32_t kTreeMaxDepth = 100000u;

    PS2Runtime::RecompiledFunction g_originalTreeFind = nullptr;
    PS2Runtime::RecompiledFunction g_originalTreeNodeAlloc = nullptr;
    std::atomic<uint32_t> g_treeNodeAllocs{0};
    std::atomic<bool> g_treeReportedNull{false};
    std::atomic<bool> g_treeReportedCycle{false};

    // Backtrace heurístico: palabras de la pila que caen en .text justo después de un JAL/JALR
    // (es decir, direcciones de retorno guardadas). Puede incluir restos viejos de la pila, pero
    // las primeras suelen ser la cadena de llamadas real.
    void printGuestBacktrace(const uint8_t *rdram, uint32_t sp, uint32_t ra)
    {
        constexpr uint32_t kTextBegin = 0x00100008u;
        constexpr uint32_t kTextEnd = 0x0029BBA4u;
        std::cout << "[gow:heap]   backtrace: " << hex32(ra);
        int found = 0;
        for (uint32_t off = 0; off < 0x800u && found < 16; off += 4u)
        {
            const uint32_t value = read32(rdram, sp + off);
            if (value < kTextBegin + 8u || value >= kTextEnd || (value & 3u) != 0u)
                continue;
            const uint32_t call = read32(rdram, value - 8u);
            const bool isJal = (call >> 26) == 3u;
            const bool isJalr = (call >> 26) == 0u && (call & 0x3Fu) == 9u;
            if (isJal || isJalr)
            {
                std::cout << " <- " << hex32(value - 8u);
                ++found;
            }
        }
        std::cout << std::endl;
    }

    void treeNodeAllocProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_originalTreeNodeAlloc(rdram, ctx, runtime);
        const uint32_t count = g_treeNodeAllocs.fetch_add(1) + 1u;
        if (getRegU32(ctx, 2) == 0u && !g_treeReportedNull.exchange(true))
        {
            std::cout << "[gow:heap] POOL DEL ÁRBOL AGOTADO: la reserva de nodo #" << count << " devolvió 0" << std::endl;
        }
    }

    void treeFindProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t rootPtr = getRegU32(ctx, 4);
        const uint32_t key = getRegU32(ctx, 5);
        const uint32_t nil = read32(rdram, kTreeNilPtr);
        const uint32_t base = read32(rdram, kTreePoolBasePtr);
        uint32_t node = read32(rdram, rootPtr);

        uint32_t depth = 0;
        if (node != 0u && !g_treeReportedCycle.load())
        {
            while (node != nil && depth < kTreeMaxDepth)
            {
                const uint32_t nodeKey = read32(rdram, node);
                if (nodeKey == key)
                    break;
                const uint32_t index = (key < nodeKey) ? (read32(rdram, node + 8u) >> 16) : (read32(rdram, node + 0xCu) & 0xFFFFu);
                node = base + index * 16u;
                ++depth;
            }
        }
        if (depth >= kTreeMaxDepth && !g_treeReportedCycle.exchange(true))
        {
            std::cout << "[gow:heap] ÁRBOL CON CICLO: raíz=" << hex32(read32(rdram, rootPtr)) << " (en " << hex32(rootPtr) << ")"
                      << " clave=" << hex32(key) << " base=" << hex32(base) << " vacío=" << hex32(nil)
                      << " nodos reservados=" << g_treeNodeAllocs.load() << " desde=" << hex32(getRegU32(ctx, 31)) << std::endl;
            printGuestBacktrace(rdram, getRegU32(ctx, 29), getRegU32(ctx, 31));
            node = read32(rdram, rootPtr);
            for (int i = 0; i < 6 && node != nil; ++i)
            {
                const uint32_t nodeKey = read32(rdram, node);
                const uint32_t lr = read32(rdram, node + 8u);
                const uint32_t right = read32(rdram, node + 0xCu) & 0xFFFFu;
                std::cout << "[gow:heap]     nodo " << hex32(node) << " (#" << ((node - base) / 16u) << ") clave=" << hex32(nodeKey)
                          << " izq=#" << (lr >> 16) << " der=#" << right
                          << " +4=" << hex32(read32(rdram, node + 4u)) << " +8=" << hex32(lr) << std::endl;
                const uint32_t index = (key < nodeKey) ? (lr >> 16) : right;
                node = base + index * 16u;
            }
        }

        g_originalTreeFind(rdram, ctx, runtime);
    }

    // ── Máquina de estados de 0x180D08 (cambio de estado) ─────────────────────────────────────
    // FUN_00180D08(máquina, estado): guarda el estado en +4 y llama a su "entrar" (vtable +0x34). Con
    // estado = NULL lee la vtable de 0xC y salta a 0 (bloqueo de la pantalla de título tras conectar
    // el mando). Aquí se avisa con backtrace antes de que pase.
    constexpr uint32_t kStateSetAddr = 0x00180D08u;
    PS2Runtime::RecompiledFunction g_originalStateSet = nullptr;
    std::atomic<uint32_t> g_stateNullReports{0};

    void stateSetProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t machine = getRegU32(ctx, 4);
        const uint32_t state = getRegU32(ctx, 5);
        if (state == 0u && g_stateNullReports.fetch_add(1) < 4u)
        {
            std::cout << "[gow:heap] ESTADO NULO: máquina=" << hex32(machine)
                      << " estado actual=" << hex32(read32(rdram, machine + 4u))
                      << " +8=" << hex32(read32(rdram, machine + 8u)) << std::endl;
            printGuestBacktrace(rdram, getRegU32(ctx, 29), getRegU32(ctx, 31));
        }
        g_originalStateSet(rdram, ctx, runtime);
    }

    // FUN_00180E90(máquina, ruta, ?): cambia de estado siguiendo una ruta con '/' (con '/' inicial
    // parte de la raíz, en máquina+8). FUN_0017FD10(estado, nombre) busca el hijo con ese nombre.
    constexpr uint32_t kStatePathAddr = 0x00180E90u;
    constexpr uint32_t kStateFindChildAddr = 0x0017FD10u;
    PS2Runtime::RecompiledFunction g_originalStatePath = nullptr;
    PS2Runtime::RecompiledFunction g_originalStateFindChild = nullptr;
    std::atomic<uint32_t> g_statePathLogs{0};

    void statePathProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const std::string path = readCString(rdram, getRegU32(ctx, 5), 96);
        const bool interesting = path.find("Anim") != std::string::npos || path.find("Navig") != std::string::npos;
        if (g_statePathLogs.fetch_add(1) < 40u || interesting)
        {
            std::cout << "[gow:heap] cambio de estado: \"" << path << "\" en " << hex32(getRegU32(ctx, 5))
                      << " (desde " << hex32(getRegU32(ctx, 31)) << ")" << std::endl;
        }
        g_originalStatePath(rdram, ctx, runtime);
    }

    void stateFindChildProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t parent = getRegU32(ctx, 4);
        const uint32_t name = getRegU32(ctx, 5);
        const std::string wanted = readCString(rdram, name, 64);
        g_originalStateFindChild(rdram, ctx, runtime);
        if (getRegU32(ctx, 2) == 0u)
        {
            std::cout << "[gow:heap] ESTADO NO ENCONTRADO: \"" << wanted << "\" en " << hex32(name) << " bajo " << hex32(parent) << std::endl;
            if (wanted.rfind("Anim", 0) == 0 && wanted != "Animation")
            {
                // Volcar la zona del nombre corrupto y la pila de quien lo pidió
                std::cout << "[gow:heap]   bytes en " << hex32(name & ~0xFu) << ":";
                for (uint32_t i = 0; i < 48u; ++i)
                {
                    const uint8_t b = rdram[((name & ~0xFu) + i) & PS2_RAM_MASK];
                    std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
                }
                std::cout << std::dec << std::setfill(' ') << std::endl;
                printGuestBacktrace(rdram, getRegU32(ctx, 29), getRegU32(ctx, 31));
            }
        }
    }

    void applyHeapDebug(PS2Runtime &runtime)
    {
        const char *flag = std::getenv("GOW_HEAP_DEBUG");
        if (flag == nullptr || flag[0] != '1')
        {
            return;
        }
        if (!runtime.hasFunction(kCreateHeapAddr) || !runtime.hasFunction(kMallocAddr))
        {
            std::cerr << "[gow:heap] faltan CreateHeap o malloc en el código generado; diagnóstico desactivado" << std::endl;
            return;
        }

        // Guardar las funciones recompiladas originales antes de sustituirlas, para llamarlas desde los envoltorios
        g_originalCreateHeap = runtime.lookupFunction(kCreateHeapAddr);
        g_originalMalloc = runtime.lookupFunction(kMallocAddr);
        runtime.registerFunction(kCreateHeapAddr, &createHeapProbe);
        runtime.registerFunction(kMallocAddr, &mallocProbe);
        if (runtime.hasFunction(kTreeFindAddr) && runtime.hasFunction(kTreeNodeAllocAddr))
        {
            g_originalTreeFind = runtime.lookupFunction(kTreeFindAddr);
            g_originalTreeNodeAlloc = runtime.lookupFunction(kTreeNodeAllocAddr);
            runtime.registerFunction(kTreeFindAddr, &treeFindProbe);
            runtime.registerFunction(kTreeNodeAllocAddr, &treeNodeAllocProbe);
        }
        if (runtime.hasFunction(kStateSetAddr))
        {
            g_originalStateSet = runtime.lookupFunction(kStateSetAddr);
            runtime.registerFunction(kStateSetAddr, &stateSetProbe);
        }
        if (runtime.hasFunction(kStatePathAddr) && runtime.hasFunction(kStateFindChildAddr))
        {
            g_originalStatePath = runtime.lookupFunction(kStatePathAddr);
            g_originalStateFindChild = runtime.lookupFunction(kStateFindChildAddr);
            runtime.registerFunction(kStatePathAddr, &statePathProbe);
            runtime.registerFunction(kStateFindChildAddr, &stateFindChildProbe);
        }
        std::cout << "[gow:heap] diagnóstico del heap activo (GOW_HEAP_DEBUG=1)" << std::endl;
    }
}

PS2_REGISTER_GAME_OVERRIDE(
    "god-of-war-scus97399-heap-debug",
    "SCUS_973.99",
    0x00100008u,
    0x822956F8u,
    applyHeapDebug);
