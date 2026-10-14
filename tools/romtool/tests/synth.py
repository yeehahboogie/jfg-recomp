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
