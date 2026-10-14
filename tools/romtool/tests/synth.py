"""Instruction encoders and synthetic ROM builders for ROM-free tests. Contains no game data."""
import struct

NOP = 0x00000000
JR_RA = 0x03E00008


def words(*ws: int) -> bytes:
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in ws)


def z64_header() -> bytes:
    return bytes.fromhex("80371240") + bytes(0x3C)


def lui(rt: int, imm: int) -> int:
    return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)


def addiu(rt: int, rs: int, imm: int) -> int:
    return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def ori(rt: int, rs: int, imm: int) -> int:
    return (0x0D << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def place(buf: bytearray, vram: int, word: int) -> None:
    from romtool.layout import vram_to_rom
    struct.pack_into(">I", buf, vram_to_rom(vram), word & 0xFFFFFFFF)


T0, T1, T2, T4, T5, T9, A1 = 8, 9, 10, 12, 13, 25, 5


def make_layout_rom() -> bytearray:
    """A blank 32 MiB image containing only the instructions layout.py reads (same encodings as the game)."""
    b = bytearray(0x2000000)
    # entry stub: clear bss, jump to the game's main
    place(b, 0x80000400, lui(T0, 0x800B)); place(b, 0x80000408, addiu(T0, T0, 0x0B50))
    place(b, 0x80000404, lui(T1, 0x0005)); place(b, 0x8000040C, ori(T1, T1, 0x4E50))
    place(b, 0x80000424, lui(T2, 0x8004)); place(b, 0x8000042C, addiu(T2, T2, -0x0B00))
    # module-system init: ROM addresses of the module tables
    place(b, 0x800539E8, lui(T9, 0x01ED)); place(b, 0x80053A00, addiu(T9, T9, 0x3B20))
    place(b, 0x800539EC, lui(T0, 0x01ED)); place(b, 0x800539FC, addiu(T0, T0, 0x2780))
    place(b, 0x80053A50, lui(T2, 0x01ED)); place(b, 0x80053A54, addiu(T2, T2, 0x0270))
    # module-system init: main segment descriptor (module 0)
    place(b, 0x80053B6C, lui(A1, 0x8000)); place(b, 0x80053B70, addiu(A1, A1, 0x0450))
    place(b, 0x80053B7C, lui(T4, 0x800A)); place(b, 0x80053B88, addiu(T4, T4, 0x0660))
    place(b, 0x80053BAC, lui(T2, 0x800B)); place(b, 0x80053BC0, addiu(T2, T2, 0x0B50))
    place(b, 0x80053BB0, lui(T1, 0x8010)); place(b, 0x80053BBC, addiu(T1, T1, 0x59A0))
    # audio task setup: start of the audio microcode = end of CPU code
    place(b, 0x80002380, lui(T5, 0x800A)); place(b, 0x80002390, addiu(T5, T5, -0x25F0))
    return b


def jal(target: int) -> int:
    return (0x03 << 26) | ((target >> 2) & 0x3FFFFFF)


def lw(rt: int, imm: int, base: int) -> int:
    return (0x23 << 26) | (base << 21) | (rt << 16) | (imm & 0xFFFF)


def entry(word0: int, offset: int, rtype: int, kind: int) -> tuple[int, int]:
    """One relocation entry as stored in the ROM."""
    return word0, (offset << 8) | (rtype << 4) | kind


def build_module_rom(symbols: list[int], modules: list[dict]) -> tuple[bytes, "ModuleTables"]:
    """Lay out a symbol table, descriptors and module images the way the game's ROM does.

    Each module dict has: text (bytes), data (bytes), bss (int), imports and locals (lists of entry()).
    """
    from romtool.layout import ModuleTables
    symtab = 0x100
    desc = symtab + 4 * len(symbols)
    image = desc + 32 * len(modules)
    blobs, offset = [], 0
    buf = bytearray(image)
    for i, s in enumerate(symbols):
        struct.pack_into(">I", buf, symtab + 4 * i, s)
    for i, m in enumerate(modules):
        imp = b"".join(struct.pack(">II", *e) for e in m["imports"])
        loc = b"".join(struct.pack(">II", *e) for e in m["locals"])
        struct.pack_into(">IIIIIHHII", buf, desc + 32 * i, 0, offset, len(m["text"]), len(m["data"]),
                         m["bss"], len(imp), len(loc), 0, 0)
        blob = m["text"] + m["data"] + imp + loc
        blobs.append(blob)
        offset += len(blob)
    return bytes(buf) + b"".join(blobs), ModuleTables(symtab, desc, image)


def main_seg():
    from romtool.layout import MainSegment
    return MainSegment(0x1000, 0x80000400, 0x8003F500, 0x80000450, 0x8009DA10, 0x800A0660, 0x800B0B50, 0x801059A0)


SP, A0, RA = 29, 4, 31


def beq(rs: int, rt: int, offset_words: int) -> int:
    return (0x04 << 26) | (rs << 21) | (rt << 16) | (offset_words & 0xFFFF)


def b(offset_words: int) -> int:
    return beq(0, 0, offset_words)


def jr(rs: int) -> int:
    return (rs << 21) | 0x08


def addiu_sp(delta: int) -> int:
    return addiu(SP, SP, delta)
