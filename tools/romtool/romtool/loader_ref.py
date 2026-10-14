"""Python port of the game's module linker (main-segment functions 0x80052874, 0x80052A00, 0x80052AB8).

This is the independent reference the generated ELF is checked against, so it follows the game's
code — including its quirks — rather than ELF conventions.
"""
import struct

from .layout import MainSegment
from .modules import MAIN, Module, RelocKind, RelocType, Target, read_relocs, resolve

STUB_FUNC = 0x80054530  # stands in for calls and pointers into a module that isn't loaded
STUB_DATA = 0x800FEAD4  # stands in for data references into a module that isn't loaded


def _address(target: Target, main: MainSegment, bases: dict[int, int]) -> int | None:
    if target.module is None:
        return target.offset
    if target.module == MAIN:
        return main.text_start + target.offset
    base = bases.get(target.module)
    return None if base is None else base + target.offset


def link_module(rom: bytes, module: Module, symbols: list[int], main: MainSegment,
                module_count: int, bases: dict[int, int]) -> bytes:
    if module.index not in bases:
        raise ValueError(f"module {module.index} has no load address")
    image = bytearray(rom[module.rom:module.rom + module.image_size])
    relocs = read_relocs(rom, module)

    def word(off: int) -> int:
        return struct.unpack_from(">I", image, off)[0]

    def put_word(off: int, value: int) -> None:
        struct.pack_into(">I", image, off, value & 0xFFFFFFFF)

    def put_imm(off: int, value: int) -> None:
        struct.pack_into(">H", image, off + 2, value & 0xFFFF)

    i = 0
    while i < len(relocs):
        r = relocs[i]
        insn = word(r.offset)
        addr = _address(resolve(r, insn, symbols, main, module_count), main, bases)
        if addr is None:
            addr = STUB_FUNC if r.type in (RelocType.R_MIPS_26, RelocType.R_MIPS_32) else STUB_DATA
        elif r.kind is RelocKind.LOCAL and r.type is RelocType.R_MIPS_32:
            addr += insn
        if r.type is RelocType.R_MIPS_32:
            put_word(r.offset, addr)
        elif r.type is RelocType.R_MIPS_26:
            put_word(r.offset, (insn & 0xFC000000) | ((addr >> 2) & 0x3FFFFFF))
        elif r.type is RelocType.R_MIPS_HI16:
            lo = relocs[i + 1]
            lo_insn = word(lo.offset)
            ahl = ((insn & 0xFFFF) << 16) + (((lo_insn & 0xFFFF) ^ 0x8000) - 0x8000)
            if ahl != STUB_DATA:
                addr += ahl
            addr &= 0xFFFFFFFF
            put_imm(r.offset, (addr >> 16) + ((addr >> 15) & 1))
            put_imm(lo.offset, addr)
            i += 2
            continue
        else:
            put_imm(r.offset, addr + (insn & 0xFFFF))
        i += 1
    return bytes(image)
