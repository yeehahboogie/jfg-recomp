import random
import struct
from pathlib import Path

from romtool.build import THUNK_VRAM, build_program, section_name
from romtool.elf_relocate import relocate_module
from romtool.functions import load_overrides
from romtool.layout import read_main_segment, read_module_tables
from romtool.loader_ref import link_module
from romtool.mips import jal
from romtool.modules import MAIN, read_modules, read_relocs, read_symbols

ROOT = Path(__file__).resolve().parents[3]
_cache = {}


def _program(rom):
    if "p" not in _cache:
        _cache["p"] = build_program(rom, load_overrides(ROOT / "recomp/function_overrides.toml"))
    return _cache["p"]


def test_counts(rom):
    p = _program(rom)
    assert sum(1 for t in p.thunks if t.caller_section == MAIN) == 513
    assert sum(1 for t in p.thunks if t.caller_section != MAIN) == 515
    assert len(p.hooks) == 2
    names = [s.name for s in p.sections]
    assert names[:2] == [".header", ".main"] and names[2] == ".m001"
    lmas = [s.lma for s in p.sections if s.data is not None]
    assert lmas == sorted(lmas)  # N64Recomp needs ELF order == ROM order


def test_main_image_only_changes_thunk_sites(rom):
    p, main = _program(rom), read_main_segment(rom)
    image = next(s for s in p.sections if s.name == ".main").data
    original = rom[main.rom_start:main.rom_start + (main.bss_start - main.vram_start)]
    sites = {t.site for t in p.thunks if t.caller_section == MAIN}
    for off in range(0, len(original), 4):
        vram = main.vram_start + off
        if vram in sites:
            assert image[off:off + 4] != original[off:off + 4]
            assert struct.unpack_from(">I", image, off)[0] >> 26 == 3
        else:
            assert image[off:off + 4] == original[off:off + 4], f"{vram:#x} changed"


def test_module_words_outside_patch_sites_match_rom(rom):
    p = _program(rom)
    mods = read_modules(rom, read_module_tables(rom))
    by_name = {s.name: s for s in p.sections}
    for m in mods:
        sites = {r.offset for r in read_relocs(rom, m)}
        image = by_name[section_name(m.index)].data
        for off in range(0, m.image_size, 4):
            if off not in sites:
                assert image[off:off + 4] == rom[m.rom + off:m.rom + off + 4]


def test_roundtrip_matches_game_linker(rom):
    p, main = _program(rom), read_main_segment(rom)
    tables = read_module_tables(rom)
    mods, syms = read_modules(rom, tables), read_symbols(rom, tables)
    for seed in (1, 2, 3):
        rng, addr, bases = random.Random(seed), 0x80200000, {}
        for m in rng.sample(mods, len(mods)):
            addr += rng.randrange(0, 16) * 0x10
            bases[m.index] = addr
            addr = (addr + m.image_size + m.bss_size + 0xF) & ~0xF
        thunk_at = {(t.caller_section, t.site): t for t in p.thunks}
        for m in mods:
            ref = link_module(rom, m, syms, main, len(mods), bases)
            ours = relocate_module(p, m.index, bases)
            for off in range(0, m.image_size, 4):
                t = thunk_at.get((m.index, off))
                if t is not None:
                    expect = jal(bases[t.target.module] + t.target.offset)
                    assert struct.unpack_from(">I", ref, off)[0] == expect, f"thunk target m{m.index}+{off:#x}"
                    assert ours[off:off + 4] == jal(t.vram).to_bytes(4, "big")
                else:
                    assert ours[off:off + 4] == ref[off:off + 4], f"seed {seed} module {m.index}+{off:#x}"


def test_thunks_live_in_thunk_section(rom):
    p = _program(rom)
    sec = next(s for s in p.sections if s.name == ".thunks")
    assert sec.vram == THUNK_VRAM and all(sec.vram <= t.vram < sec.vram + sec.size for t in p.thunks)


def test_broken_main_call_import_kept_as_thunk(rom):
    # main import #473 names "module 0xD66", which does not exist; the call still goes through a thunk
    # so the port does exactly what the game does (the game's own linker resolves it at run time)
    p = _program(rom)
    broken = [t for t in p.thunks if t.target is None]
    assert [(t.caller_section, t.site) for t in broken] == [(MAIN, 0x80068404)]
