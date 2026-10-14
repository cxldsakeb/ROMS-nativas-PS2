"""Busca funciones que Ghidra no detecta y las imprime como entradas de extra_entry_points.

Dos tipos, los que causan "[guest-branch:missing-target]" en el runtime:
  vfn_  punteros de .data/.rodata/... a .text que caen fuera de toda función del CSV y empiezan
        donde acaba otra función ("jr ra" dos instrucciones antes) o tras relleno. Son sobre todo
        métodos virtuales de C++ (vtables) y callbacks, que nadie llama directamente.
  fn_   destinos de J/JAL que caen en huecos entre funciones (código que Ghidra no analizó).

    py -3 tools/scan_missing_functions.py              # usa config/gow_functions.csv + gow.manual.toml

Ya cuenta las entradas que haya en config/gow.manual.toml, así que tras volver a exportar desde
Ghidra solo saldrán las nuevas. El resultado se pega en extra_entry_points; revisa a mano las
dudosas (p. ej. 0x100000, el relleno antes del crt0, se descartó).
Requisitos: py -3 -m pip install pyelftools
"""
import argparse
import bisect
import csv
import os
import re
import struct

from elftools.elf.elffile import ELFFile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JR_RA = 0x03E00008


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--elf", default=os.path.join(REPO, "game", "disc", "SCUS_973.99"))
    parser.add_argument("--csv", default=os.path.join(REPO, "config", "gow_functions.csv"))
    parser.add_argument("--manual", default=os.path.join(REPO, "config", "gow.manual.toml"))
    args = parser.parse_args()

    elf = ELFFile(open(args.elf, "rb"))
    text = elf.get_section_by_name(".text")
    lo, data = text["sh_addr"], text.data()
    hi = lo + len(data)

    starts, ranges = set(), []
    for row in csv.DictReader(open(args.csv)):
        start, end = int(row["Start"], 16), int(row["End"], 16)
        starts.add(start)
        ranges.append((start, max(end, start + 4)))
    if os.path.exists(args.manual):
        for match in re.finditer(r'"[^"@]+@0x([0-9A-Fa-f]+)"', open(args.manual, encoding="utf-8").read()):
            address = int(match.group(1), 16)
            starts.add(address)
            ranges.append((address, address + 4))
    ranges.sort()
    range_starts = [r[0] for r in ranges]

    def covered(address):
        i = bisect.bisect_right(range_starts, address) - 1
        # las funciones no se solapan mucho: basta con mirar unas pocas hacia atrás
        for k in range(i, max(i - 64, -1), -1):
            if ranges[k][0] <= address < ranges[k][1]:
                return True
        return False

    def word(address):
        return struct.unpack_from("<I", data, address - lo)[0] if lo <= address < hi else 0

    vfn = set()
    for section in elf.iter_sections():
        if section.name == ".text" or section["sh_type"] != "SHT_PROGBITS" or not (section["sh_flags"] & 2):
            continue
        payload = section.data()
        for off in range(0, len(payload) - 3, 4):
            pointer, = struct.unpack_from("<I", payload, off)
            if lo < pointer < hi and pointer % 4 == 0 and pointer not in starts and not covered(pointer):
                if word(pointer - 8) == JR_RA or word(pointer - 4) == 0:
                    vfn.add(pointer)

    fn = set()
    for off in range(0, len(data), 4):
        ins, = struct.unpack_from("<I", data, off)
        if (ins >> 26) in (2, 3):  # J, JAL
            source = lo + off
            target = ((source + 4) & 0xF0000000) | ((ins & 0x03FFFFFF) << 2)
            if lo <= target < hi and target not in starts and target not in vfn and not covered(target):
                fn.add(target)

    print("# %d vfn_ + %d fn_ nuevas" % (len(vfn), len(fn)))
    for address in sorted(vfn):
        print('  "vfn_%08x@0x%08X",' % (address, address))
    for address in sorted(fn):
        print('  "fn_%08x@0x%08X",' % (address, address))


if __name__ == "__main__":
    main()
