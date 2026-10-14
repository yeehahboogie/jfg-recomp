from pathlib import Path

from romtool.functions import find_all_functions, load_overrides
from romtool.layout import read_module_tables
from romtool.modules import read_modules

OVERRIDES = Path(__file__).resolve().parents[3] / "recomp" / "function_overrides.toml"


def test_real_rom_functions(rom):
    funcs = find_all_functions(rom, load_overrides(OVERRIDES))  # raises RomError listing problems
    main = [f for f in funcs if f.section == 0]
    assert main[0].start == 0x80000400
    assert any(f.start == 0x8003F500 for f in main)
    assert len(main) >= 1343  # distinct jal targets in main code, measured in the spike
    mods = read_modules(rom, read_module_tables(rom))
    with_code = {m.index for m in mods if m.text_size}
    assert {f.section for f in funcs} - {0} == with_code  # every module with code has functions
    print(f"\nfunctions: main {len(main)}, modules {len(funcs) - len(main)}")


def test_duplicate_function_names_rejected(rom):
    import dataclasses

    import pytest

    from romtool.rom import RomError
    o = load_overrides(OVERRIDES)
    clash = dataclasses.replace(o, names=o.names | {(0, 0x8003F500): "entrypoint"})
    with pytest.raises(RomError, match="named more than once: entrypoint"):
        find_all_functions(rom, clash)
