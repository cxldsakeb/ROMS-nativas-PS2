"""Desensamblador mínimo de microcódigo de VU: la parte lower entera (saltos, aritmética entera,
cargas/escrituras, XGKICK...) y los bits I/E/M/D/T de la upper (la operación upper se muestra en hexadecimal).

    py -3 tools/vu_dis.py volcado.bin 0xINICIO 0xFIN

El volcado empieza con los 16 KB de código de la VU1 (como out/logs/vu1_atasco.bin de las trazas locales).
Sin dependencias."""
import struct, sys

LOWER7 = {0x00: "LQ", 0x01: "SQ", 0x04: "ILW", 0x05: "ISW", 0x08: "IADDIU", 0x09: "ISUBIU",
          0x10: "FCEQ", 0x11: "FCSET", 0x12: "FCAND", 0x13: "FCOR", 0x14: "FSEQ", 0x15: "FSSET",
          0x16: "FSAND", 0x17: "FSOR", 0x18: "FMEQ", 0x1A: "FMAND", 0x1B: "FMOR", 0x1C: "FCGET",
          0x20: "B", 0x21: "BAL", 0x24: "JR", 0x25: "JALR", 0x28: "IBEQ", 0x29: "IBNE",
          0x2C: "IBLTZ", 0x2D: "IBGTZ", 0x2E: "IBLEZ", 0x2F: "IBGEZ"}
LOWEROP = {0x30: "IADD", 0x31: "ISUB", 0x32: "IADDI", 0x34: "IAND", 0x35: "IOR"}
SPECIAL2 = {0x30: "MOVE", 0x31: "MR32", 0x34: "LQI", 0x35: "SQI", 0x36: "LQD", 0x37: "SQD",
            0x38: "DIV", 0x39: "SQRT", 0x3A: "RSQRT", 0x3B: "WAITQ", 0x3C: "MTIR", 0x3D: "MFIR",
            0x3E: "ILWR", 0x3F: "ISWR", 0x40: "RNEXT", 0x41: "RGET", 0x42: "RINIT", 0x43: "RXOR",
            0x64: "MFP", 0x68: "XTOP", 0x69: "XITOP", 0x6C: "XGKICK", 0x70: "ESADD", 0x71: "ERSADD",
            0x72: "ELENG", 0x73: "ERLENG", 0x74: "EATANxy", 0x75: "EATANxz", 0x76: "ESUM", 0x78: "ESQRT",
            0x79: "ERSQRT", 0x7A: "ERCPR", 0x7B: "WAITP", 0x7C: "ESIN", 0x7D: "EATAN", 0x7E: "EEXP"}

def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

def lower(ins, pc):
    op = ins >> 25
    it, is_, id_ = (ins >> 16) & 31, (ins >> 11) & 31, (ins >> 6) & 31
    dest = "".join(c for c, b in zip("xyzw", (24, 23, 22, 21)) if ins >> b & 1)
    if op == 0x40:
        f = ins & 0x3F
        if f in LOWEROP:
            n = LOWEROP[f]
            if n == "IADDI":
                return "IADDI vi%d, vi%d, %d" % (it, is_, sext(id_, 5))
            return "%s vi%d, vi%d, vi%d" % (n, id_, is_, it)
        if f >= 0x3C:
            code = ((ins >> 4) & 0x7C) | (ins & 3)
            n = SPECIAL2.get(code, "?%02X" % code)
            if n in ("MTIR",):
                return "MTIR vi%d, vf%d" % (it, is_)
            if n in ("MFIR",):
                return "MFIR.%s vf%d, vi%d" % (dest, it, is_)
            if n in ("ILWR", "ISWR"):
                return "%s.%s vi%d, (vi%d)" % (n, dest, it, is_)
            if n in ("LQI", "LQD"):
                return "%s.%s vf%d, (vi%d%s)" % (n, dest, it, is_, "++" if n == "LQI" else "--")
            if n in ("SQI", "SQD"):
                return "%s.%s vf%d, (vi%d%s)" % (n, dest, is_, it, "++" if n == "SQI" else "--")
            if n in ("XGKICK", "XTOP", "XITOP"):
                return "%s vi%d" % (n, is_ if n == "XGKICK" else it)
            return "%s.%s (it=%d is=%d)" % (n, dest, it, is_)
        return "LOWEROP?%02X" % f
    n = LOWER7.get(op, "?op%02X" % op)
    imm11 = sext(ins & 0x7FF, 11)
    if n in ("B", "BAL", "IBEQ", "IBNE", "IBLTZ", "IBGTZ", "IBLEZ", "IBGEZ"):
        target = pc + 8 + imm11 * 8
        if n in ("B",):
            return "B 0x%04X" % target
        if n == "BAL":
            return "BAL vi%d, 0x%04X" % (it, target)
        if n in ("IBEQ", "IBNE"):
            return "%s vi%d, vi%d, 0x%04X" % (n, it, is_, target)
        return "%s vi%d, 0x%04X" % (n, is_, target)
    if n in ("JR", "JALR"):
        return "%s vi%d" % (n, is_) if n == "JR" else "JALR vi%d, vi%d" % (it, is_)
    if n in ("IADDIU", "ISUBIU"):
        imm15 = ((ins >> 10) & 0x7800) | (ins & 0x7FF)
        return "%s vi%d, vi%d, %d" % (n, it, is_, imm15)
    if n in ("LQ", "SQ"):
        return "%s.%s vf%d, %d(vi%d)" % (n, dest, it if n == "LQ" else is_, imm11, is_ if n == "LQ" else it)
    if n in ("ILW", "ISW"):
        return "%s.%s vi%d, %d(vi%d)" % (n, dest, it, imm11, is_)
    if n.startswith("F"):
        return "%s vi1, 0x%X" % (n, ins & 0xFFFFFF)
    return n

def main():
    data = open(sys.argv[1], "rb").read()
    start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
    for pc in range(start, end, 8):
        lo, up = struct.unpack_from("<II", data, pc)
        flags = "".join(f for f, b in (("I", 31), ("E", 30), ("M", 29), ("D", 28), ("T", 27)) if up >> b & 1)
        lw = ("LOI 0x%08X" % lo) if (up >> 31) & 1 else lower(lo, pc)
        print("%04X: [%-2s] upper=%08X | %s" % (pc, flags, up & 0x07FFFFFF, lw))

main()
