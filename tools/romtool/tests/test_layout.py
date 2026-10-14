import pytest

from romtool.layout import (
    MainSegment, ModuleTables, read_main_segment, read_module_tables, vram_to_rom,
)
from romtool.rom import RomError
from synth import T4, addiu, make_layout_rom, place

EXPECTED_MAIN = MainSegment(
    rom_start=0x1000, vram_start=0x80000400, entry_target=0x8003F500, text_start=0x80000450,
    code_end=0x8009DA10, data_start=0x800A0660, bss_start=0x800B0B50, bss_end=0x801059A0,
)
EXPECTED_TABLES = ModuleTables(symtab_rom=0x1ED0270, desc_rom=0x1ED2780, image_rom=0x1ED3B20)


def test_vram_to_rom():
    assert vram_to_rom(0x80000400) == 0x1000
    assert vram_to_rom(0x800B0B50) == 0xB1750


def test_synthetic_layout():
    rom = bytes(make_layout_rom())
    assert read_main_segment(rom) == EXPECTED_MAIN
    tables = read_module_tables(rom)
    assert tables == EXPECTED_TABLES
    assert (tables.symbol_count, tables.module_count) == (2372, 157)


def test_wrong_instruction_names_address():
    rom = make_layout_rom()
    place(rom, 0x80053B88, addiu(T4 + 1, T4 + 1, 0x0660))  # different register
    with pytest.raises(RomError, match="0x80053b88"):
        read_main_segment(bytes(rom))


def test_bss_disagreement_rejected():
    rom = make_layout_rom()
    place(rom, 0x80053BC0, addiu(10, 10, 0x0B60))  # linker's bss start ≠ entry stub's
    with pytest.raises(RomError, match="disagree"):
        read_main_segment(bytes(rom))


def test_rom_of_bounds():
    assert EXPECTED_MAIN.rom_of(0x800B0B4C) == 0xB174C
    with pytest.raises(ValueError):
        EXPECTED_MAIN.rom_of(0x800B0B50)  # bss has no ROM backing


def test_real_rom_layout(rom):
    assert read_main_segment(rom) == EXPECTED_MAIN
    assert read_module_tables(rom) == EXPECTED_TABLES


def test_real_main_imports_rom(rom):
    from romtool.layout import read_main_imports_rom
    assert read_main_imports_rom(rom) == 0x1ECF220
