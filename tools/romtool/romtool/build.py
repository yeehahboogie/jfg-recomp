"""Turn the ROM into the sections, symbols, relocations, thunks and hooks N64Recomp needs."""
import struct
from dataclasses import dataclass

from .elf import FuncSym, Rel, Section
from .functions import Function, Overrides, find_all_functions
from .layout import MainSegment, read_main_imports_rom, read_main_segment, read_module_tables, read_word
from .loader_ref import STUB_DATA
from .mips import hi_lo, jal
from .modules import (MAIN, Module, RelocKind, RelocType, Target, read_modules, read_relocs, read_symbols,
                      resolve, resolve_global)
from .rom import RomError
from .thunks import main_thunk, module_thunk

MODULE_LINK_BASE = 0x82000000
THUNK_VRAM = 0x8E000000
THUNK_LMA = 0x2000000  # just past the end of the 32 MiB ROM; never DMA'd
HEADER_VRAM = 0x7FFFF000


@dataclass(frozen=True)
class Thunk:
    name: str
    caller_section: int
    site: int       # main: vram of the call; modules: text offset of the call
    vram: int
    target: Target | None  # what the game links the call to; None: the symbol is broken in the game too


@dataclass(frozen=True)
class Hook:
    function: str
    before_vram: int
    register: int
    hi_site: int
    lo_site: int
    static_lo: int


@dataclass
class Program:
    sections: list[Section]
    funcs: list[FuncSym]
    rels: list[Rel]
    thunks: list[Thunk]
    hooks: list[Hook]
    link_base: dict[int, int]
    entry: int


def section_name(index: int) -> str:
    return ".main" if index == MAIN else f".m{index:03d}"


def _link_bases(modules: list[Module]) -> dict[int, int]:
    bases, addr = {}, MODULE_LINK_BASE
    for m in modules:
        bases[m.index] = addr
        addr = (addr + m.image_size + m.bss_size + 0xFFF) & ~0xFFF
    return bases


def _address(t: Target, main: MainSegment, bases: dict[int, int]) -> int:
    if t.module is None:
        return t.offset
    if t.module == MAIN:
        return main.text_start + t.offset
    return bases[t.module] + t.offset


def _read_main_imports(rom: bytes, main: MainSegment) -> list[tuple[int, RelocType, int]]:
    """(site vram, type, global symbol index) for each entry of the main segment's import table."""
    table = read_main_imports_rom(rom)
    out = []
    for i in range(read_word(rom, table)):
        word0, info = struct.unpack_from(">II", rom, table + 4 + 8 * i)
        offset, rtype, where = info >> 8, RelocType((info >> 4) & 0xF), info & 0xF
        if where == 0:
            site = main.text_start + offset
        elif where == 3:
            site = main.data_start + offset
        else:
            raise RomError(f"main import #{i}: unknown patch area {where}")
        out.append((site, rtype, word0))
    return out


def build_program(rom: bytes, overrides: Overrides) -> Program:
    main, tables = read_main_segment(rom), read_module_tables(rom)
    modules, symbols = read_modules(rom, tables), read_symbols(rom, tables)
    functions = find_all_functions(rom, overrides)
    bases = _link_bases(modules)
    thunks: list[Thunk] = []
    thunk_code: list[int] = []
    rels: list[Rel] = []

    def new_thunk(name: str, section: int, site: int, code: list[int], target: Target) -> int:
        vram = THUNK_VRAM + 4 * len(thunk_code)
        thunks.append(Thunk(name, section, site, vram, target))
        thunk_code.extend(code)
        return vram

    # Modules: link every module at its own address, as a linker would.
    sections = [Section(".header", HEADER_VRAM, 0x1000, data=bytes(0x1000), lma=0)]
    module_sections, bss_sections = [], []
    for m in modules:
        image = bytearray(rom[m.rom:m.rom + m.image_size])
        relocs = read_relocs(rom, m)
        name, base = section_name(m.index), bases[m.index]

        def word(off: int) -> int:
            return struct.unpack_from(">I", image, off)[0]

        def put(off: int, value: int) -> None:
            struct.pack_into(">I", image, off, value & 0xFFFFFFFF)

        def put_imm(off: int, value: int) -> None:
            struct.pack_into(">H", image, off + 2, value & 0xFFFF)

        def rel_for(t: Target, off: int, rtype: RelocType) -> None:
            if t.module not in (None, MAIN):
                rels.append(Rel(name, base + off, section_name(t.module), int(rtype)))

        i = 0
        while i < len(relocs):
            r = relocs[i]
            insn = word(r.offset)
            t = resolve(r, insn, symbols, main, len(modules))
            addr = _address(t, main, bases)
            if r.type is RelocType.R_MIPS_26:
                if t.module not in (None, MAIN, m.index):
                    vram = new_thunk(f"thunk_m{m.index:03d}_{r.offset:05X}", m.index, r.offset,
                                     module_thunk(m.index, r.offset), t)
                    put(r.offset, (insn & 0xFC000000) | ((vram >> 2) & 0x3FFFFFF))
                else:
                    put(r.offset, (insn & 0xFC000000) | ((addr >> 2) & 0x3FFFFFF))
                    rel_for(t, r.offset, r.type)
            elif r.type is RelocType.R_MIPS_32:
                put(r.offset, addr + (insn if r.kind is RelocKind.LOCAL else 0))
                rel_for(t, r.offset, r.type)
            elif r.type is RelocType.R_MIPS_HI16:
                lo = relocs[i + 1]
                lo_insn = word(lo.offset)
                ahl = ((insn & 0xFFFF) << 16) + (((lo_insn & 0xFFFF) ^ 0x8000) - 0x8000)
                full = (addr + (ahl if ahl != STUB_DATA else 0)) & 0xFFFFFFFF
                hi, lo_imm = hi_lo(full)
                put_imm(r.offset, hi)
                put_imm(lo.offset, lo_imm)
                rel_for(t, r.offset, RelocType.R_MIPS_HI16)
                rel_for(t, lo.offset, RelocType.R_MIPS_LO16)
                i += 2
                continue
            else:  # lone LO16 continuing the previous HI16
                put_imm(r.offset, addr + (insn & 0xFFFF))
                rel_for(t, r.offset, r.type)
            i += 1
        module_sections.append(Section(name, base, m.image_size, data=bytes(image), lma=m.rom, code=True))
        if m.bss_size:
            bss_sections.append(Section(name + ".bss", base + m.image_size, m.bss_size))

    # Main segment: static, except for the calls and address pairs the game re-links at run time.
    main_image = bytearray(rom[main.rom_start:main.rom_start + (main.bss_start - main.vram_start)])
    hooks: list[Hook] = []
    imports = _read_main_imports(rom, main)
    main_funcs = [f for f in functions if f.section == MAIN]

    def containing(vram: int) -> Function:
        inner = [f for f in main_funcs if f.start <= vram < f.end]
        if not inner:
            raise RomError(f"main import site {vram:#x} is not inside any function")
        return max(inner, key=lambda f: f.start)  # innermost when functions nest

    k = 0
    while k < len(imports):
        site, rtype, word0 = imports[k]
        try:
            target = resolve_global(symbols, word0, main, len(modules))
        except RomError:
            if rtype is not RelocType.R_MIPS_26:
                raise
            # A call whose symbol is broken in the game itself (it names a module that doesn't exist).
            # The thunk follows the game's own run-time linking, so the port behaves exactly like the
            # original; only our bookkeeping lacks a target.
            target = None
        off = site - main.vram_start
        if rtype is RelocType.R_MIPS_26:
            vram = new_thunk(f"thunk_main_{site:08X}", MAIN, site, main_thunk(site), target)
            struct.pack_into(">I", main_image, off, jal(vram))
        elif rtype is RelocType.R_MIPS_HI16:
            lo_site, lo_type, _ = imports[k + 1]
            if lo_type is not RelocType.R_MIPS_LO16:
                raise RomError(f"main import HI16 at {site:#x} not followed by LO16")
            hi_insn = read_word(rom, main.rom_of(site))
            lo_insn = read_word(rom, main.rom_of(lo_site))
            reg = (hi_insn >> 16) & 31
            if (lo_insn >> 21) & 31 != reg:
                raise RomError(f"main import LO16 at {lo_site:#x} does not use the HI16 register")
            hooks.append(Hook(containing(lo_site).name, lo_site, reg, site, lo_site,
                              ((lo_insn & 0xFFFF) ^ 0x8000) - 0x8000))
            k += 2
            continue
        elif rtype is not RelocType.R_MIPS_32:  # R_32 lives in main data; the game patches it in RAM
            raise RomError(f"main import at {site:#x}: unsupported type {rtype}")
        k += 1

    sections.append(Section(".main", main.vram_start, len(main_image), data=bytes(main_image),
                            lma=main.rom_start, code=True))
    sections.extend(module_sections)
    thunk_blob = b"".join(w.to_bytes(4, "big") for w in thunk_code)
    sections.append(Section(".thunks", THUNK_VRAM, len(thunk_blob), data=thunk_blob, lma=THUNK_LMA, code=True))
    sections.append(Section(".main.bss", main.bss_start, main.bss_end - main.bss_start))
    sections.extend(bss_sections)

    funcs = [FuncSym(f.name, section_name(f.section),
                     f.start if f.section == MAIN else bases[f.section] + f.start, f.end - f.start)
             for f in functions]
    ends = [t.vram for t in thunks[1:]] + [THUNK_VRAM + len(thunk_blob)]
    funcs += [FuncSym(t.name, ".thunks", t.vram, end - t.vram) for t, end in zip(thunks, ends)]
    return Program(sections, funcs, rels, thunks, hooks, bases, main.vram_start)
