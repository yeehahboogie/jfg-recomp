import struct

from romtool.loader_ref import STUB_DATA, STUB_FUNC, link_module
from romtool.modules import read_modules, read_symbols
from synth import JR_RA, NOP, build_module_rom, entry, jal, lui, lw, main_seg, words

AT, T0 = 1, 8
SYMBOLS = [0x00085FE0, 0xFFD00010, 0xFFF00020, 0xFFC00300, 0x00200040]
BASE1, BASE2 = 0x80400000, 0x80500000


def build(text, data=b"", imports=(), locals_=(), second=True):
    mods = [dict(text=text, data=data, bss=0, imports=list(imports), locals=list(locals_))]
    if second:
        mods.append(dict(text=words(JR_RA, NOP) + bytes(0x40), data=b"", bss=0, imports=[], locals=[]))
    rom, tables = build_module_rom(SYMBOLS, mods)
    return rom, read_modules(rom, tables), read_symbols(rom, tables)


def word_at(image, off):
    return struct.unpack_from(">I", image, off)[0]


def link(rom, mods, syms, bases):
    return link_module(rom, mods[0], syms, main_seg(), len(mods), bases)


def test_global_call_into_main():
    rom, mods, syms = build(words(jal(0), NOP), imports=[entry(0, 0, 4, 0)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == jal(0x80000450 + 0x85FE0)


def test_global_hi_lo_into_main_data():
    rom, mods, syms = build(words(lui(AT, 0), lw(T0, 0, AT)),
                            imports=[entry(1, 0, 5, 0), entry(1, 4, 6, 0)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == lui(AT, 0x800A) and word_at(image, 4) == lw(T0, 0x0670, AT)


def test_hi_lo_carry():
    # a local target whose low half has bit 15 set forces the carry into the high half
    rom, mods, syms = build(words(lui(AT, 0), lw(T0, 0, AT)),
                            locals_=[entry(0x8000, 0, 5, 1), entry(0x8000, 4, 6, 1)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == lui(AT, 0x8041) and word_at(image, 4) == lw(T0, 0x8000, AT)


def test_physical_address_addend():
    rom, mods, syms = build(words(lui(AT, 0x8000), lw(T0, 0, AT)),
                            locals_=[entry(0x100, 0, 5, 1), entry(0x100, 4, 6, 1)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == lui(AT, 0x0040) and word_at(image, 4) == lw(T0, 0x0100, AT)


def test_local_data_pointer_keeps_addend():
    rom, mods, syms = build(words(JR_RA, NOP), data=words(0x10), locals_=[entry(0x4, 8, 2, 1)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 8) == BASE1 + 0x4 + 0x10


def test_local_call_uses_instruction_field():
    rom, mods, syms = build(words(jal(0x8), NOP, JR_RA, NOP), locals_=[entry(0, 0, 4, 2)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == jal(BASE1 + 0x8)


def test_absolute_symbol():
    rom, mods, syms = build(words(lui(AT, 0), lw(T0, 0, AT)), imports=[entry(3, 0, 5, 0), entry(3, 4, 6, 0)])
    image = link(rom, mods, syms, {1: BASE1})
    assert word_at(image, 0) == lui(AT, 0x8000) and word_at(image, 4) == lw(T0, 0x0300, AT)


def test_unloaded_targets_use_stand_ins():
    rom, mods, syms = build(words(jal(0), NOP, lui(AT, 0), lw(T0, 4, AT)), data=words(0),
                            imports=[entry(4, 0, 4, 0), entry(4, 8, 5, 0), entry(4, 12, 6, 0), entry(4, 16, 2, 0)])
    unloaded = link(rom, mods, syms, {1: BASE1})
    assert word_at(unloaded, 0) == jal(STUB_FUNC)
    assert word_at(unloaded, 16) == STUB_FUNC
    data_ref = STUB_DATA + 4
    assert word_at(unloaded, 8) == lui(AT, (data_ref >> 16) + ((data_ref >> 15) & 1))
    loaded = link(rom, mods, syms, {1: BASE1, 2: BASE2})
    assert word_at(loaded, 0) == jal(BASE2 + 0x40)
    assert word_at(loaded, 16) == BASE2 + 0x40
