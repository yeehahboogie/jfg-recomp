import struct

import pytest

from romtool.modules import (
    MAIN, RawReloc, RelocKind, RelocType, Target, read_modules, read_relocs, read_symbols,
    resolve, resolve_global,
)
from romtool.rom import RomError
from synth import JR_RA, NOP, build_module_rom, entry, jal, main_seg, words

SYMBOLS = [0x00085FE0, 0xFFD00010, 0xFFF00020, 0xFFC00300, 0x00200040]


def one_module(imports=(), locals_=(), text=None, data=b""):
    text = words(jal(0), NOP, JR_RA, NOP) if text is None else text
    return build_module_rom(SYMBOLS, [dict(text=text, data=data, bss=0x10, imports=list(imports), locals=list(locals_))])


def test_descriptor_fields():
    rom, tables = one_module(imports=[entry(0, 0, 4, 0)], data=words(0, 0))
    (m,) = read_modules(rom, tables)
    assert (m.index, m.rom, m.text_size, m.data_size, m.bss_size, m.import_size, m.local_size) == \
        (1, tables.image_rom, 16, 8, 0x10, 8, 0)
    assert read_symbols(rom, tables) == SYMBOLS


def test_gap_between_modules_rejected():
    mod = dict(text=words(JR_RA, NOP), data=b"", bss=0, imports=[], locals=[])
    rom, tables = build_module_rom(SYMBOLS, [mod, mod])
    rom = bytearray(rom)
    struct.pack_into(">I", rom, tables.desc_rom + 32 + 4, 8 + 4)  # module 2 claims to start 4 bytes late
    with pytest.raises(RomError, match="module 2 starts at"):
        read_modules(bytes(rom), tables)


def test_reloc_decoding():
    rom, tables = one_module(imports=[entry(0, 0, 4, 0)])
    (m,) = read_modules(rom, tables)
    assert read_relocs(rom, m) == [RawReloc(1, 0, RelocType.R_MIPS_26, RelocKind.GLOBAL, 0, "import")]


@pytest.mark.parametrize("bad, message", [
    (entry(0, 0, 7, 0), "unknown relocation type 7"),
    (entry(0, 0, 4, 3), "unknown relocation kind 3"),
    (entry(0, 2, 4, 0), "outside the module image"),
    (entry(0, 16, 4, 0), "instruction relocation outside text"),
    (entry(0, 0, 2, 1), "data relocation inside text"),
    (entry(0, 0, 5, 1), "HI16 not followed by LO16"),
])
def test_bad_entries_rejected(bad, message):
    rom, tables = one_module(locals_=[bad], data=words(0, 0))
    (m,) = read_modules(rom, tables)
    with pytest.raises(RomError, match=message):
        read_relocs(rom, m)


def test_duplicate_patch_site_rejected():
    rom, tables = one_module(imports=[entry(0, 0, 4, 0)], locals_=[entry(0, 0, 4, 2)])
    (m,) = read_modules(rom, tables)
    with pytest.raises(RomError, match="patched twice"):
        read_relocs(rom, m)


def test_resolve_global_variants():
    main = main_seg()
    assert resolve_global(SYMBOLS, 0, main, 2) == Target(MAIN, 0x85FE0)
    assert resolve_global(SYMBOLS, 1, main, 2) == Target(MAIN, 0x800A0660 - 0x80000450 + 0x10)
    assert resolve_global(SYMBOLS, 2, main, 2) == Target(MAIN, 0x800B0B50 - 0x80000450 + 0x20)
    assert resolve_global(SYMBOLS, 3, main, 2) == Target(None, 0x80000300)
    assert resolve_global(SYMBOLS, 4, main, 2) == Target(2, 0x40)
    with pytest.raises(RomError, match="only 1"):
        resolve_global(SYMBOLS, 4, main, 1)
    with pytest.raises(RomError, match="out of range"):
        resolve_global(SYMBOLS, 99, main, 2)


def test_resolve_local_kinds():
    main = main_seg()
    local = RawReloc(1, 0, RelocType.R_MIPS_26, RelocKind.LOCAL, 0x40, "local")
    call = RawReloc(1, 0, RelocType.R_MIPS_26, RelocKind.LOCAL_CALL, 0, "local")
    assert resolve(local, jal(0), SYMBOLS, main, 1) == Target(1, 0x40)
    assert resolve(call, jal(0x8), SYMBOLS, main, 1) == Target(1, 0x8)
