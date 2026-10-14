import pytest
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

from romtool.elf import FuncSym, Rel, Section, write_elf


def _write(tmp_path):
    sections = [
        Section(".header", 0x7FFFF000, 0x1000, data=bytes(0x1000), lma=0),
        Section(".main", 0x80000400, 8, data=bytes.fromhex("03e0000800000000"), lma=0x1000, code=True),
        Section(".m001", 0x82000000, 8, data=bytes.fromhex("0c00000000000000"), lma=0x1ED3B20, code=True),
        Section(".m001.bss", 0x82000008, 0x20),
    ]
    funcs = [FuncSym("entrypoint", ".main", 0x80000400, 8), FuncSym("m001_00000", ".m001", 0x82000000, 8)]
    rels = [Rel(".m001", 0x82000000, ".m001", 4)]
    path = tmp_path / "t.elf"
    write_elf(path, sections, funcs, rels, entry=0x80000400)
    return path


def test_header_and_sections(tmp_path):
    with open(_write(tmp_path), "rb") as f:
        elf = ELFFile(f)
        assert (elf.elfclass, elf.little_endian, elf["e_machine"], elf["e_entry"]) == (32, False, "EM_MIPS", 0x80000400)
        secs = {s.name: s for s in elf.iter_sections()}
        assert secs[".main"]["sh_addr"] == 0x80000400 and secs[".main"].data() == bytes.fromhex("03e0000800000000")
        assert secs[".m001.bss"]["sh_type"] == "SHT_NOBITS" and secs[".m001.bss"]["sh_size"] == 0x20
        assert ".rel.m001" in secs


def test_segments_carry_rom_addresses(tmp_path):
    with open(_write(tmp_path), "rb") as f:
        elf = ELFFile(f)
        loads = sorted((s["p_vaddr"], s["p_paddr"], s["p_filesz"]) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD")
        assert loads == [(0x7FFFF000, 0, 0x1000), (0x80000400, 0x1000, 8), (0x82000000, 0x1ED3B20, 8)]
        for sec in (".main", ".m001"):
            s = elf.get_section_by_name(sec)
            assert any(seg.section_in_segment(s) for seg in elf.iter_segments())


def test_symbols_and_relocations(tmp_path):
    with open(_write(tmp_path), "rb") as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name(".symtab")
        funcs = {s.name: s for s in symtab.iter_symbols() if s["st_info"]["type"] == "STT_FUNC"}
        assert funcs["entrypoint"]["st_value"] == 0x80000400 and funcs["entrypoint"]["st_size"] == 8
        rel = elf.get_section_by_name(".rel.m001")
        assert isinstance(rel, RelocationSection)
        (r,) = list(rel.iter_relocations())
        sym = symtab.get_symbol(r["r_info_sym"])
        assert (r["r_offset"], r["r_info_type"], sym["st_info"]["type"]) == (0x82000000, 4, "STT_SECTION")
        assert elf.get_section(sym["st_shndx"]).name == ".m001"


def test_rejects_unknown_target(tmp_path):
    with pytest.raises(ValueError):
        write_elf(tmp_path / "x.elf", [Section(".a", 0, 4, data=bytes(4), lma=0)], [], [Rel(".a", 0, ".nope", 2)], 0)
