"""Where code and tables live in the ROM.

Every address is read out of the instructions that use it, so a wrong assumption fails loudly
instead of quietly producing a broken ELF.
"""
import struct
from dataclasses import dataclass

from .rom import RomError

MAIN_ROM = 0x1000
MAIN_VRAM = 0x80000400

_T0, _T1, _T2, _T4, _T5, _T9, _A1 = 8, 9, 10, 12, 13, 25, 5
_LUI, _ADDIU, _ORI = 0x0F, 0x09, 0x0D


def vram_to_rom(vram: int) -> int:
    return vram - MAIN_VRAM + MAIN_ROM


def read_word(rom: bytes, rom_offset: int) -> int:
    return struct.unpack_from(">I", rom, rom_offset)[0]


def _imm(rom: bytes, vram: int, opcode: int, reg: int) -> int:
    word = read_word(rom, vram_to_rom(vram))
    rs, rt = (word >> 21) & 31, (word >> 16) & 31
    if word >> 26 != opcode or rt != reg or (opcode != _LUI and rs != reg):
        raise RomError(
            f"layout check failed at {vram:#010x}: expected opcode {opcode:#04x} on register {reg}, "
            f"found {word:#010x}"
        )
    return word & 0xFFFF


def _hi_lo(rom: bytes, lui_at: int, lo_at: int, reg: int, lo_opcode: int = _ADDIU) -> int:
    hi = _imm(rom, lui_at, _LUI, reg)
    lo = _imm(rom, lo_at, lo_opcode, reg)
    if lo_opcode == _ADDIU:
        lo = (lo ^ 0x8000) - 0x8000
    return ((hi << 16) + lo) & 0xFFFFFFFF


@dataclass(frozen=True)
class MainSegment:
    rom_start: int     # ROM offset of the main segment (0x1000)
    vram_start: int    # where the entry stub runs (0x80000400)
    entry_target: int  # the game's main function, jumped to by the stub
    text_start: int    # base address the module linker uses for module 0
    code_end: int      # first byte of RSP microcode; CPU code is [vram_start, code_end)
    data_start: int
    bss_start: int
    bss_end: int

    def rom_of(self, vram: int) -> int:
        if not self.vram_start <= vram < self.bss_start:
            raise ValueError(f"{vram:#010x} is not in the main segment's ROM-backed range")
        return vram - self.vram_start + self.rom_start


@dataclass(frozen=True)
class ModuleTables:
    symtab_rom: int  # global symbol table: u32 (module << 20 | offset) per symbol
    desc_rom: int    # 32-byte module descriptors
    image_rom: int   # module images start here, back to back

    @property
    def symbol_count(self) -> int:
        return (self.desc_rom - self.symtab_rom) // 4

    @property
    def module_count(self) -> int:
        return (self.image_rom - self.desc_rom) // 32


def read_main_segment(rom: bytes) -> MainSegment:
    bss_start = _hi_lo(rom, 0x80000400, 0x80000408, _T0)
    bss_size = _hi_lo(rom, 0x80000404, 0x8000040C, _T1, lo_opcode=_ORI)
    entry_target = _hi_lo(rom, 0x80000424, 0x8000042C, _T2)
    text_start = _hi_lo(rom, 0x80053B6C, 0x80053B70, _A1)
    data_start = _hi_lo(rom, 0x80053B7C, 0x80053B88, _T4)
    linker_bss_start = _hi_lo(rom, 0x80053BAC, 0x80053BC0, _T2)
    linker_bss_end = _hi_lo(rom, 0x80053BB0, 0x80053BBC, _T1)
    code_end = _hi_lo(rom, 0x80002380, 0x80002390, _T5)
    if (linker_bss_start, linker_bss_end) != (bss_start, bss_start + bss_size):
        raise RomError(
            f"bss bounds disagree: entry stub clears {bss_start:#x}+{bss_size:#x}, "
            f"module linker uses {linker_bss_start:#x}..{linker_bss_end:#x}"
        )
    if not MAIN_VRAM < text_start < code_end < data_start < bss_start:
        raise RomError(
            f"main segment bounds out of order: text {text_start:#x}, microcode {code_end:#x}, "
            f"data {data_start:#x}, bss {bss_start:#x}"
        )
    if not text_start <= entry_target < code_end:
        raise RomError(f"entry target {entry_target:#x} is outside main code")
    return MainSegment(MAIN_ROM, MAIN_VRAM, entry_target, text_start, code_end,
                       data_start, bss_start, linker_bss_end)


def read_module_tables(rom: bytes) -> ModuleTables:
    image = _hi_lo(rom, 0x800539E8, 0x80053A00, _T9)
    desc = _hi_lo(rom, 0x800539EC, 0x800539FC, _T0)
    symtab = _hi_lo(rom, 0x80053A50, 0x80053A54, _T2)
    if not symtab < desc < image <= len(rom):
        raise RomError(f"module tables out of order: symbols {symtab:#x}, descriptors {desc:#x}, images {image:#x}")
    if (desc - symtab) % 4 or (image - desc) % 32:
        raise RomError("module table sizes are not whole entries")
    return ModuleTables(symtab, desc, image)
