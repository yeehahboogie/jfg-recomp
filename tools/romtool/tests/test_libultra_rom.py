import dataclasses
from pathlib import Path

import pytest

from romtool.audit import hardware_accesses, unreviewed
from romtool.functions import find_all_functions, load_overrides
from romtool.layout import read_main_segment, read_module_tables
from romtool.libultra import run_n64sym, runtime_handled_names
from romtool.modules import read_modules

ROOT = Path(__file__).resolve().parents[3]


def _env():
    paths = dict(line.split("=", 1) for line in (ROOT / "tools/paths.env").read_text().split() if "=" in line)
    n64sym = ROOT / paths["N64SYM"]
    if not n64sym.exists():
        pytest.skip("n64sym not built")
    return str(n64sym)


def test_libultra_and_hardware_audit(rom):
    main = read_main_segment(rom)
    names = run_n64sym(rom, main, _env())
    assert names, "n64sym found nothing"
    overrides = load_overrides(ROOT / "recomp/function_overrides.toml")
    merged = dataclasses.replace(overrides, names={(0, a): n for a, n in names.items()} | overrides.names)
    funcs = find_all_functions(rom, merged)
    handled = runtime_handled_names(str(ROOT / "lib/N64Recomp/src/symbol_lists.cpp"))
    by_start = {(f.section, f.start): f.name for f in funcs}
    unhandled_exempt = [by_start[k] for k in sorted(merged.exempt) if by_start[k] not in handled]
    assert unhandled_exempt == [], f"[[exempt]] functions must be runtime-handled libultra: {unhandled_exempt}"
    mods = {m.index: m for m in read_modules(rom, read_module_tables(rom))}
    accesses = {}
    for section in {f.section for f in funcs}:
        spans = [(f.start, f.end) for f in funcs if f.section == section]
        if section == 0:
            code, base = rom[main.rom_of(main.vram_start):main.rom_of(main.code_end)], main.vram_start
        else:
            m = mods[section]
            code, base = rom[m.rom:m.rom + m.text_size], 0
        accesses[section] = hardware_accesses(code, base, spans)
    problems = unreviewed(funcs, accesses, handled, merged)
    print(f"\nlibultra names: {len(names)}; hardware-touching functions: "
          f"{sum(len(a) for a in accesses.values())}; unreviewed: {len(problems)}")
    assert problems == [], "review these and add [[hardware]] entries:\n" + "\n".join(problems)
