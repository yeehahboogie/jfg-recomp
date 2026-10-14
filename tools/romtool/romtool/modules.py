"""Decode the game's relocatable code modules: descriptors, global symbols and relocation entries."""
import struct
from collections import Counter
from dataclasses import dataclass
from enum import IntEnum

from .layout import MainSegment, ModuleTables
from .rom import RomError

MAIN = 0
_MAIN_DATA_IDS = (0xFFD, 0xFFE)
_MAIN_BSS_ID = 0xFFF
_ABSOLUTE_ID = 0xFFC  # absolute KSEG0 address 0x80000000 + offset (libultra boot globals, e.g. osTvType)


class RelocType(IntEnum):
    R_MIPS_32 = 2
    R_MIPS_26 = 4
    R_MIPS_HI16 = 5
    R_MIPS_LO16 = 6


class RelocKind(IntEnum):
    GLOBAL = 0      # word0 indexes the global symbol table
    LOCAL = 1       # word0 is an offset from this module's base
    LOCAL_CALL = 2  # the target is the instruction's own 26-bit field, relative to this module's base


_INSTRUCTION_TYPES = (RelocType.R_MIPS_26, RelocType.R_MIPS_HI16, RelocType.R_MIPS_LO16)


@dataclass(frozen=True)
class Module:
    index: int
    rom: int
    text_size: int
    data_size: int
    bss_size: int
    import_size: int
    local_size: int

    @property
    def image_size(self) -> int:
        return self.text_size + self.data_size

    @property
    def import_rom(self) -> int:
        return self.rom + self.image_size

    @property
    def local_rom(self) -> int:
        return self.import_rom + self.import_size

    @property
    def end_rom(self) -> int:
        return self.local_rom + self.local_size


@dataclass(frozen=True)
class RawReloc:
    module: int
    offset: int  # patch location, relative to the module's text start
    type: RelocType
    kind: RelocKind
    word0: int
    table: str   # "import" or "local"


@dataclass(frozen=True)
class Target:
    module: int | None  # None: `offset` is an absolute address
    offset: int         # module 0: relative to MainSegment.text_start; others: to the module's text start


def read_symbols(rom: bytes, tables: ModuleTables) -> list[int]:
    return list(struct.unpack_from(f">{tables.symbol_count}I", rom, tables.symtab_rom))


def read_modules(rom: bytes, tables: ModuleTables) -> list[Module]:
    modules, expected = [], tables.image_rom
    for i in range(tables.module_count):
        _, rel, text, data, bss, imp, loc, _, _ = struct.unpack_from(">IIIIIHHII", rom, tables.desc_rom + 32 * i)
        module = Module(i + 1, tables.image_rom + rel, text, data, bss, imp, loc)
        if module.rom != expected:
            raise RomError(f"module {module.index} starts at ROM {module.rom:#x}, expected {expected:#x} "
                           f"(where module {module.index - 1} ends)")
        if text % 4 or data % 4 or imp % 8 or loc % 8:
            raise RomError(f"module {module.index}: misaligned sizes text {text:#x} data {data:#x} "
                           f"relocs {imp:#x}/{loc:#x}")
        if module.end_rom > len(rom):
            raise RomError(f"module {module.index} runs past the end of the ROM")
        modules.append(module)
        expected = module.end_rom
    return modules


def _read_table(rom: bytes, module: Module, table: str, start: int, size: int) -> list[RawReloc]:
    relocs = []
    for k in range(size // 8):
        word0, info = struct.unpack_from(">II", rom, start + 8 * k)
        offset, raw_type, raw_kind = info >> 8, (info >> 4) & 0xF, info & 0xF
        where = f"module {module.index} {table} reloc #{k} (offset {offset:#x})"
        try:
            rtype = RelocType(raw_type)
        except ValueError:
            raise RomError(f"{where}: unknown relocation type {raw_type}") from None
        try:
            kind = RelocKind(raw_kind)
        except ValueError:
            raise RomError(f"{where}: unknown relocation kind {raw_kind}") from None
        if offset % 4 or offset + 4 > module.image_size:
            raise RomError(f"{where}: patch location outside the module image")
        if rtype in _INSTRUCTION_TYPES and offset >= module.text_size:
            raise RomError(f"{where}: instruction relocation outside text")
        if rtype is RelocType.R_MIPS_32 and offset < module.text_size:
            raise RomError(f"{where}: data relocation inside text")
        relocs.append(RawReloc(module.index, offset, rtype, kind, word0, table))
    for k, r in enumerate(relocs):
        if r.type is RelocType.R_MIPS_HI16 and (k + 1 == len(relocs) or relocs[k + 1].type is not RelocType.R_MIPS_LO16):
            raise RomError(f"module {module.index} {table} reloc #{k}: HI16 not followed by LO16")
    return relocs


def read_relocs(rom: bytes, module: Module) -> list[RawReloc]:
    relocs = (_read_table(rom, module, "import", module.import_rom, module.import_size)
              + _read_table(rom, module, "local", module.local_rom, module.local_size))
    twice = [off for off, n in Counter(r.offset for r in relocs).items() if n > 1]
    if twice:
        raise RomError(f"module {module.index}: offset {twice[0]:#x} is patched twice")
    return relocs


def resolve_global(symbols: list[int], index: int, main: MainSegment, module_count: int) -> Target:
    if not 0 <= index < len(symbols):
        raise RomError(f"global symbol index {index} out of range (table has {len(symbols)})")
    value = symbols[index]
    module, offset = value >> 20, value & 0xFFFFF
    if module in _MAIN_DATA_IDS:
        return Target(MAIN, main.data_start - main.text_start + offset)
    if module == _MAIN_BSS_ID:
        return Target(MAIN, main.bss_start - main.text_start + offset)
    if module == _ABSOLUTE_ID:
        return Target(None, 0x80000000 + offset)
    if module > module_count:
        raise RomError(f"global symbol {index} names module {module}, but there are only {module_count}")
    return Target(module, offset)


def resolve(reloc: RawReloc, insn: int, symbols: list[int], main: MainSegment, module_count: int) -> Target:
    if reloc.kind is RelocKind.GLOBAL:
        return resolve_global(symbols, reloc.word0, main, module_count)
    if reloc.kind is RelocKind.LOCAL:
        return Target(reloc.module, reloc.word0)
    return Target(reloc.module, (insn & 0x3FFFFFF) << 2)
