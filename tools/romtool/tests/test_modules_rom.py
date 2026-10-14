from collections import Counter

from romtool.layout import read_main_segment, read_module_tables
from romtool.modules import RelocKind, RelocType, Target, read_modules, read_relocs, read_symbols, resolve


def _everything(rom):
    main, tables = read_main_segment(rom), read_module_tables(rom)
    mods, syms = read_modules(rom, tables), read_symbols(rom, tables)
    relocs = [r for m in mods for r in read_relocs(rom, m)]
    return main, mods, syms, relocs


def test_counts(rom):
    _, mods, syms, relocs = _everything(rom)
    assert (len(mods), len(syms)) == (157, 2372)
    assert Counter(r.table for r in relocs) == {"import": 11076, "local": 16439}
    assert Counter(int(r.type) for r in relocs) == {2: 2025, 4: 8467, 5: 8476, 6: 8547}
    assert Counter((r.table, int(r.kind)) for r in relocs) == {
        ("import", 0): 11076, ("local", 0): 514, ("local", 1): 14835, ("local", 2): 1090}


def test_every_jal_placeholder_is_relocated(rom):
    _, mods, _, relocs = _everything(rom)
    sites = {(r.module, r.offset) for r in relocs if r.type is RelocType.R_MIPS_26}
    placeholders = {(m.index, o) for m in mods for o in range(0, m.text_size, 4)
                    if rom[m.rom + o:m.rom + o + 4] == b"\x0c\x00\x00\x00"}
    assert len(placeholders) == 7414
    assert placeholders <= sites


def test_absolute_symbols(rom):
    main, mods, syms, relocs = _everything(rom)
    absolute = [r for r in relocs if r.kind is RelocKind.GLOBAL
                and resolve(r, 0, syms, main, len(mods)).module is None]
    assert len(absolute) == 2 and {r.module for r in absolute} == {99}
    assert {resolve(r, 0, syms, main, len(mods)) for r in absolute} == {Target(None, 0x80000300)}
