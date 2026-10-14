import struct

from romtool.layout import read_main_segment, read_module_tables
from romtool.loader_ref import STUB_FUNC, link_module
from romtool.modules import read_modules, read_symbols


def _setup(rom):
    main, tables = read_main_segment(rom), read_module_tables(rom)
    return main, read_modules(rom, tables), read_symbols(rom, tables)


def _bases(mods):
    bases, addr = {}, 0x80400000
    for m in mods:
        bases[m.index] = addr
        addr += (m.image_size + m.bss_size + 0xFFFF) & ~0xFFFF
    return bases


def test_all_loaded_leaves_no_placeholders_and_calls_land_in_code(rom):
    main, mods, syms = _setup(rom)
    bases = _bases(mods)
    code_ranges = [(main.vram_start, main.code_end)] + [(bases[m.index], bases[m.index] + m.text_size) for m in mods]
    for m in mods:
        image = link_module(rom, m, syms, main, len(mods), bases)
        for off in range(0, m.text_size, 4):
            w = struct.unpack_from(">I", image, off)[0]
            assert w != 0x0C000000, f"module {m.index}+{off:#x} still jal 0"
            if w >> 26 == 3:
                target = 0x80000000 | ((w & 0x3FFFFFF) << 2)
                assert any(lo <= target < hi for lo, hi in code_ranges), f"module {m.index}+{off:#x} -> {target:#x}"


def test_alone_cross_module_calls_hit_stub(rom):
    main, mods, syms = _setup(rom)
    hits = 0
    for m in mods:
        image = link_module(rom, m, syms, main, len(mods), {m.index: 0x80400000})
        hits += sum(1 for off in range(0, m.text_size, 4)
                    if struct.unpack_from(">I", image, off)[0] == (0x0C000000 | ((STUB_FUNC >> 2) & 0x3FFFFFF)))
    assert hits > 0
