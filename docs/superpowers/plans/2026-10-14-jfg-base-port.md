# Jet Force Gemini Base Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A native x86-64 Linux build of Jet Force Gemini (USA), recompiled from the user's own ROM, that plays the first planet (Goldwood) correctly.

**Architecture:** Our Python tool `romtool` reads the ROM, decodes the game's 157 relocatable code modules and their relocations, finds function boundaries, and writes a synthetic MIPS ELF. N64Recomp turns that ELF into C; the C is built with N64ModernRuntime + RT64 inside a frontend forked from Zelda64Recomp. Small C patches (compiled for MIPS) hook the game's module loader so the runtime knows where each module lives.

**Tech Stack:** Python 3.12 (stdlib + pytest, pyelftools, rabbitizer for tests/debugging), N64Recomp, RSPRecomp, N64ModernRuntime, RT64, Zelda64Recomp frontend (C++20, CMake, clang, SDL2), n64sym.

**Spec:** `docs/superpowers/specs/2026-10-14-jfg-base-port-design.md`

## Global Constraints

- Only ROM accepted: Jet Force Gemini (USA) [!], MD5 `772cc6eab2620d2d3cdc17bbc26c4f68` (`.v64`/`.n64` byte orders are normalised first).
- Never commit the ROM or anything derived from it. `*.z64 *.n64 *.v64 *.rom /build/ /generated/` stay git-ignored; generated C, ELFs, symbol dumps live only in `generated/` or `build/`.
- Linux x86-64 only (Ubuntu 24.04). No Windows/macOS work.
- Python ≥ 3.12; `romtool` has no runtime dependencies outside the stdlib (rabbitizer only for the optional `disasm` command).
- Fail loudly, never guess: layout invariant violations, unknown relocation types/kinds, out-of-range symbols, and N64Recomp warnings are errors.
- Out of scope: widescreen, frame-rate changes, any control-scheme change, decompilation, planets beyond Goldwood.
- All work happens on branch `base-port`. Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Run romtool tests with `tools/romtool/.venv/bin/pytest -q tools/romtool` from the repo root. ROM-backed tests read `./jfg.z64` (or `$JFG_ROM`) and are skipped if it is absent.

## Review Focus

1. **Byte-swapped or wrong dumps.** A `.v64`/`.n64` copy of the right game must work transparently; a PAL/overdumped/hacked ROM must be rejected with a message naming the expected dump, never a stack trace. Pinned by Task 1 tests `test_v64_and_n64_normalise` and `test_wrong_dump_rejected`.
2. **References into a module that isn't loaded.** The original game redirects them to stand-ins (`0x80054530` for calls/pointers, `0x800FEAD4` for data); the port must do the same and never execute code whose data is absent. Pinned by Task 4 `test_unloaded_targets_use_stand_ins` and Task 9 `test_module_thunk_follows_patched_call` / `test_main_thunk_follows_patched_call`.
3. **Mis-split functions** (early returns, switch tables, entry stub ending in `jr $t2`). They must surface as build failures, not miscompiled code. Pinned by Task 5 `test_early_return_not_split`, `test_switch_dispatch_not_split`, `test_invariants_catch_bad_split`, and N64Recomp warnings-as-errors.
4. **A module unloaded and reloaded at a different address** (every level change). The runtime must forget the old address and use the new one. Pinned by Task 12 `scripts/test-hooks.sh` (`reload_at_new_address`).
5. **Launch with the ROM missing/moved, or a save from a previous run.** The frontend must ask for the ROM rather than crash, and the Flash RAM save must survive relaunch. The launcher's existing ROM selection covers the first; Task 16 checklist items 1–2 pin both by hand (no automated test: needs a display and a human).

---

## File Structure

```
tools/romtool/
  pyproject.toml              package metadata, test extras, `romtool` entry point
  romtool/__init__.py
  romtool/rom.py              load, byte-order normalisation, MD5 check (RomError)
  romtool/layout.py           MainSegment / ModuleTables derived from instructions, vram↔rom
  romtool/mips.py             tiny MIPS decoding helpers (branches, jumps, calls)
  romtool/modules.py          Module, RawReloc, Target; read_modules/read_relocs/resolve
  romtool/loader_ref.py       faithful port of the game's linker (reference for verification)
  romtool/functions.py        function discovery + invariants + overrides
  romtool/libultra.py         n64sym integration → libultra names
  romtool/elf.py              big-endian MIPS ELF32 writer
  romtool/elf_relocate.py     generic ELF relocation pass (for the round-trip check)
  romtool/build.py            ties everything together: ROM → ELF + symbol report
  romtool/cli.py              verify / info / build-elf / disasm
  tests/conftest.py           `rom` fixture (skips when ROM absent)
  tests/synth.py              instruction encoders + synthetic ROM builders (no game data)
  tests/test_*.py
recomp/
  function_overrides.toml     manual function-finder corrections and names
  jfg.toml                    N64Recomp config
  aspMain.toml                RSPRecomp config for the audio microcode (if needed)
patches/                      MIPS C patches (module loader hooks)
src/, lib/, CMakeLists.txt    frontend fork
scripts/smoke.sh              boot smoke test
```

---

## Phase 1 — `romtool`

### Task 1: romtool scaffold and ROM loading

**Files:**
- Create: `tools/romtool/pyproject.toml`, `tools/romtool/romtool/__init__.py`, `tools/romtool/romtool/rom.py`
- Create: `tools/romtool/tests/conftest.py`, `tools/romtool/tests/synth.py`, `tools/romtool/tests/test_rom.py`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `romtool.rom.RomError(Exception)`, `romtool.rom.EXPECTED_MD5: str`, `romtool.rom.ROM_SIZE = 0x2000000`, `normalize_byte_order(data: bytes) -> bytes`, `load_rom(path: str | os.PathLike) -> bytes` (normalised + verified). Test fixture `rom` (session-scoped `bytes`, skips if absent). `tests/synth.py` encoders (extended by later tasks).

- [x] **Step 1: Create the package skeleton and dev venv**

`tools/romtool/pyproject.toml`:
```toml
[build-system]
requires = ["setuptools>=68"]
build-backend = "setuptools.build_meta"

[project]
name = "romtool"
version = "0.1.0"
description = "Turns the Jet Force Gemini (USA) ROM into a MIPS ELF for N64Recomp"
requires-python = ">=3.12"
dependencies = []

[project.optional-dependencies]
test = ["pytest>=8", "pyelftools>=0.31"]
disasm = ["rabbitizer>=1.12"]

[project.scripts]
romtool = "romtool.cli:main"

[tool.setuptools]
packages = ["romtool"]

[tool.pytest.ini_options]
testpaths = ["tests"]
pythonpath = ["tests"]
```

`tools/romtool/romtool/__init__.py`:
```python
"""ROM → ELF tooling for the Jet Force Gemini native port."""
```

Append to `.gitignore`:
```
tools/romtool/.venv/
__pycache__/
.pytest_cache/
*.egg-info/
```

Run:
```bash
python3 -m venv tools/romtool/.venv
tools/romtool/.venv/bin/pip install -q -e 'tools/romtool[test,disasm]'
```
Expected: installs without errors.

- [x] **Step 2: Write the failing tests**

`tools/romtool/tests/conftest.py`:
```python
import os
from pathlib import Path

import pytest

from romtool.rom import load_rom

REPO_ROOT = Path(__file__).resolve().parents[3]


def rom_path() -> Path:
    return Path(os.environ.get("JFG_ROM", REPO_ROOT / "jfg.z64"))


@pytest.fixture(scope="session")
def rom() -> bytes:
    path = rom_path()
    if not path.is_file():
        pytest.skip(f"ROM-backed test: {path} not present")
    return load_rom(path)
```

`tools/romtool/tests/synth.py`:
```python
"""Instruction encoders and synthetic ROM builders for ROM-free tests. Contains no game data."""
import struct

NOP = 0x00000000
JR_RA = 0x03E00008


def words(*ws: int) -> bytes:
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in ws)


def z64_header() -> bytes:
    return bytes.fromhex("80371240") + bytes(0x3C)
```

`tools/romtool/tests/test_rom.py`:
```python
import pytest

from romtool.rom import EXPECTED_MD5, ROM_SIZE, RomError, load_rom, normalize_byte_order
from synth import z64_header


def _v64(z64: bytes) -> bytes:
    out = bytearray(len(z64))
    out[0::2], out[1::2] = z64[1::2], z64[0::2]
    return bytes(out)


def _n64(z64: bytes) -> bytes:
    out = bytearray(len(z64))
    out[0::4], out[1::4], out[2::4], out[3::4] = z64[3::4], z64[2::4], z64[1::4], z64[0::4]
    return bytes(out)


def test_z64_passes_through():
    data = z64_header() + bytes(range(64))
    assert normalize_byte_order(data) == data


def test_v64_and_n64_normalise():
    data = z64_header() + bytes(range(64))
    assert normalize_byte_order(_v64(data)) == data
    assert normalize_byte_order(_n64(data)) == data


def test_unknown_magic_rejected():
    with pytest.raises(RomError, match="not an N64 ROM"):
        normalize_byte_order(b"PK\x03\x04" + bytes(60))


def test_wrong_dump_rejected(tmp_path):
    path = tmp_path / "other.z64"
    path.write_bytes(z64_header() + bytes(0x1000))
    with pytest.raises(RomError, match=f"does not match Jet Force Gemini \\(USA\\) \\[!\\] {EXPECTED_MD5}"):
        load_rom(path)


def test_missing_file_rejected(tmp_path):
    with pytest.raises(RomError, match="ROM not found"):
        load_rom(tmp_path / "nope.z64")


def test_odd_length_rejected(tmp_path):
    path = tmp_path / "odd.v64"
    path.write_bytes(_v64(z64_header()) + b"\x00")
    with pytest.raises(RomError, match="length"):
        load_rom(path)


def test_real_rom(rom):
    assert len(rom) == ROM_SIZE
    assert rom[0x20:0x34].decode("ascii").strip() == "JET FORCE GEMINI"
```

- [x] **Step 3: Run tests to verify they fail**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_rom.py`
Expected: FAIL / collection error — `ModuleNotFoundError: No module named 'romtool.rom'`.

- [x] **Step 4: Implement `rom.py`**

`tools/romtool/romtool/rom.py`:
```python
"""Load the user's Jet Force Gemini (USA) ROM and make sure it is the exact dump we support."""
import hashlib
import os
from pathlib import Path

EXPECTED_MD5 = "772cc6eab2620d2d3cdc17bbc26c4f68"
ROM_SIZE = 0x2000000

_Z64 = bytes.fromhex("80371240")  # big-endian (native)
_V64 = bytes.fromhex("37804012")  # 16-bit byte-swapped
_N64 = bytes.fromhex("40123780")  # 32-bit little-endian


class RomError(Exception):
    """The input is not the supported ROM, or breaks an assumption about its layout."""


def normalize_byte_order(data: bytes) -> bytes:
    magic = bytes(data[:4])
    if magic == _Z64:
        return bytes(data)
    if magic == _V64:
        out = bytearray(len(data))
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out)
    if magic == _N64:
        out = bytearray(len(data))
        out[0::4], out[1::4], out[2::4], out[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(out)
    raise RomError(f"not an N64 ROM: first word {magic.hex()} is not a known header magic")


def load_rom(path: str | os.PathLike) -> bytes:
    path = Path(path)
    if not path.is_file():
        raise RomError(f"ROM not found: {path}")
    data = path.read_bytes()
    if len(data) % 4:
        raise RomError(f"{path}: length {len(data)} is not a multiple of 4; the file is truncated or not a ROM")
    data = normalize_byte_order(data)
    md5 = hashlib.md5(data).hexdigest()
    if md5 != EXPECTED_MD5:
        raise RomError(
            f"{path}: MD5 {md5} does not match Jet Force Gemini (USA) [!] {EXPECTED_MD5}. "
            "Only that exact dump is supported (other regions, revisions, hacks and overdumps are not)."
        )
    return data
```

- [x] **Step 5: Run tests to verify they pass**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_rom.py`
Expected: 7 passed (`test_real_rom` passes when `jfg.z64` is present).

- [x] **Step 6: Commit**

```bash
git add .gitignore tools/romtool
git commit -m "romtool: scaffold and verified ROM loading

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Main-segment and module-table layout

**Files:**
- Create: `tools/romtool/romtool/layout.py`, `tools/romtool/tests/test_layout.py`
- Modify: `tools/romtool/tests/synth.py` (add encoders + `make_layout_rom`)

**Interfaces:**
- Consumes: `RomError` (Task 1).
- Produces:
  - `MAIN_ROM = 0x1000`, `MAIN_VRAM = 0x80000400`, `vram_to_rom(vram: int) -> int`, `read_word(rom: bytes, rom_offset: int) -> int`
  - `@dataclass(frozen=True) MainSegment(rom_start, vram_start, entry_target, text_start, code_end, data_start, bss_start, bss_end)` with `rom_of(vram) -> int`
  - `@dataclass(frozen=True) ModuleTables(symtab_rom, desc_rom, image_rom)` with properties `symbol_count`, `module_count`
  - `read_main_segment(rom) -> MainSegment`, `read_module_tables(rom) -> ModuleTables`

Known values (from the spikes; the code must *derive* them, the tests assert them):
`text_start 0x80000450`, `code_end 0x8009DA10` (first microcode byte), `data_start 0x800A0660`, `bss_start 0x800B0B50`, `bss_end 0x801059A0`, `entry_target 0x8003F500`; `symtab_rom 0x1ED0270`, `desc_rom 0x1ED2780`, `image_rom 0x1ED3B20` → 2372 symbols, 157 modules.

- [x] **Step 1: Add encoders and the synthetic layout ROM to `synth.py`**

Append to `tools/romtool/tests/synth.py`:
```python
def lui(rt: int, imm: int) -> int:
    return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)


def addiu(rt: int, rs: int, imm: int) -> int:
    return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def ori(rt: int, rs: int, imm: int) -> int:
    return (0x0D << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def place(buf: bytearray, vram: int, word: int) -> None:
    from romtool.layout import vram_to_rom
    struct.pack_into(">I", buf, vram_to_rom(vram), word & 0xFFFFFFFF)


T0, T1, T2, T4, T5, T9, A1 = 8, 9, 10, 12, 13, 25, 5


def make_layout_rom() -> bytearray:
    """A blank 32 MiB image containing only the instructions layout.py reads (same encodings as the game)."""
    b = bytearray(0x2000000)
    # entry stub: clear bss, jump to the game's main
    place(b, 0x80000400, lui(T0, 0x800B)); place(b, 0x80000408, addiu(T0, T0, 0x0B50))
    place(b, 0x80000404, lui(T1, 0x0005)); place(b, 0x8000040C, ori(T1, T1, 0x4E50))
    place(b, 0x80000424, lui(T2, 0x8004)); place(b, 0x8000042C, addiu(T2, T2, -0x0B00))
    # module-system init: ROM addresses of the module tables
    place(b, 0x800539E8, lui(T9, 0x01ED)); place(b, 0x80053A00, addiu(T9, T9, 0x3B20))
    place(b, 0x800539EC, lui(T0, 0x01ED)); place(b, 0x800539FC, addiu(T0, T0, 0x2780))
    place(b, 0x80053A50, lui(T2, 0x01ED)); place(b, 0x80053A54, addiu(T2, T2, 0x0270))
    # module-system init: main segment descriptor (module 0)
    place(b, 0x80053B6C, lui(A1, 0x8000)); place(b, 0x80053B70, addiu(A1, A1, 0x0450))
    place(b, 0x80053B7C, lui(T4, 0x800A)); place(b, 0x80053B88, addiu(T4, T4, 0x0660))
    place(b, 0x80053BAC, lui(T2, 0x800B)); place(b, 0x80053BC0, addiu(T2, T2, 0x0B50))
    place(b, 0x80053BB0, lui(T1, 0x8010)); place(b, 0x80053BBC, addiu(T1, T1, 0x59A0))
    # audio task setup: start of the audio microcode = end of CPU code
    place(b, 0x80002380, lui(T5, 0x800A)); place(b, 0x80002390, addiu(T5, T5, -0x25F0))
    return b
```

- [x] **Step 2: Write the failing tests**

`tools/romtool/tests/test_layout.py`:
```python
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
```

- [x] **Step 3: Run tests to verify they fail**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_layout.py`
Expected: FAIL — `No module named 'romtool.layout'`.

- [x] **Step 4: Implement `layout.py`**

`tools/romtool/romtool/layout.py`:
```python
"""Where code and tables live in the ROM.

Every address is read out of the instructions that use it, so a wrong assumption fails loudly
instead of quietly producing a broken ELF.
"""
import struct
from dataclasses import dataclass

from .rom import RomError

MAIN_ROM = 0x1000
MAIN_VRAM = 0x80000400

_T0, _T1, _T2, _T4, _T5, _T9, _A1 = 8, 9, 10, 12, 13, 25, 5
_LUI, _ADDIU, _ORI = 0x0F, 0x09, 0x0D


def vram_to_rom(vram: int) -> int:
    return vram - MAIN_VRAM + MAIN_ROM


def read_word(rom: bytes, rom_offset: int) -> int:
    return struct.unpack_from(">I", rom, rom_offset)[0]


def _imm(rom: bytes, vram: int, opcode: int, reg: int) -> int:
    word = read_word(rom, vram_to_rom(vram))
    rs, rt = (word >> 21) & 31, (word >> 16) & 31
    if word >> 26 != opcode or rt != reg or (opcode != _LUI and rs != reg):
        raise RomError(
            f"layout check failed at {vram:#010x}: expected opcode {opcode:#04x} on register {reg}, "
            f"found {word:#010x}"
        )
    return word & 0xFFFF


def _hi_lo(rom: bytes, lui_at: int, lo_at: int, reg: int, lo_opcode: int = _ADDIU) -> int:
    hi = _imm(rom, lui_at, _LUI, reg)
    lo = _imm(rom, lo_at, lo_opcode, reg)
    if lo_opcode == _ADDIU:
        lo = (lo ^ 0x8000) - 0x8000
    return ((hi << 16) + lo) & 0xFFFFFFFF


@dataclass(frozen=True)
class MainSegment:
    rom_start: int     # ROM offset of the main segment (0x1000)
    vram_start: int    # where the entry stub runs (0x80000400)
    entry_target: int  # the game's main function, jumped to by the stub
    text_start: int    # base address the module linker uses for module 0
    code_end: int      # first byte of RSP microcode; CPU code is [vram_start, code_end)
    data_start: int
    bss_start: int
    bss_end: int

    def rom_of(self, vram: int) -> int:
        if not self.vram_start <= vram < self.bss_start:
            raise ValueError(f"{vram:#010x} is not in the main segment's ROM-backed range")
        return vram - self.vram_start + self.rom_start


@dataclass(frozen=True)
class ModuleTables:
    symtab_rom: int  # global symbol table: u32 (module << 20 | offset) per symbol
    desc_rom: int    # 32-byte module descriptors
    image_rom: int   # module images start here, back to back

    @property
    def symbol_count(self) -> int:
        return (self.desc_rom - self.symtab_rom) // 4

    @property
    def module_count(self) -> int:
        return (self.image_rom - self.desc_rom) // 32


def read_main_segment(rom: bytes) -> MainSegment:
    bss_start = _hi_lo(rom, 0x80000400, 0x80000408, _T0)
    bss_size = _hi_lo(rom, 0x80000404, 0x8000040C, _T1, lo_opcode=_ORI)
    entry_target = _hi_lo(rom, 0x80000424, 0x8000042C, _T2)
    text_start = _hi_lo(rom, 0x80053B6C, 0x80053B70, _A1)
    data_start = _hi_lo(rom, 0x80053B7C, 0x80053B88, _T4)
    linker_bss_start = _hi_lo(rom, 0x80053BAC, 0x80053BC0, _T2)
    linker_bss_end = _hi_lo(rom, 0x80053BB0, 0x80053BBC, _T1)
    code_end = _hi_lo(rom, 0x80002380, 0x80002390, _T5)
    if (linker_bss_start, linker_bss_end) != (bss_start, bss_start + bss_size):
        raise RomError(
            f"bss bounds disagree: entry stub clears {bss_start:#x}+{bss_size:#x}, "
            f"module linker uses {linker_bss_start:#x}..{linker_bss_end:#x}"
        )
    if not MAIN_VRAM < text_start < code_end < data_start < bss_start:
        raise RomError(
            f"main segment bounds out of order: text {text_start:#x}, microcode {code_end:#x}, "
            f"data {data_start:#x}, bss {bss_start:#x}"
        )
    if not text_start <= entry_target < code_end:
        raise RomError(f"entry target {entry_target:#x} is outside main code")
    return MainSegment(MAIN_ROM, MAIN_VRAM, entry_target, text_start, code_end,
                       data_start, bss_start, linker_bss_end)


def read_module_tables(rom: bytes) -> ModuleTables:
    image = _hi_lo(rom, 0x800539E8, 0x80053A00, _T9)
    desc = _hi_lo(rom, 0x800539EC, 0x800539FC, _T0)
    symtab = _hi_lo(rom, 0x80053A50, 0x80053A54, _T2)
    if not symtab < desc < image <= len(rom):
        raise RomError(f"module tables out of order: symbols {symtab:#x}, descriptors {desc:#x}, images {image:#x}")
    if (desc - symtab) % 4 or (image - desc) % 32:
        raise RomError("module table sizes are not whole entries")
    return ModuleTables(symtab, desc, image)
```

- [x] **Step 5: Run tests to verify they pass**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_layout.py`
Expected: 6 passed.

- [x] **Step 6: Commit**

```bash
git add tools/romtool
git commit -m "romtool: derive main segment and module table layout from code

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Module descriptors and relocation decoding

**Files:**
- Create: `tools/romtool/romtool/modules.py`, `tools/romtool/tests/test_modules.py`, `tools/romtool/tests/test_modules_rom.py`
- Modify: `tools/romtool/tests/synth.py` (add `jal`, `lw`, `entry`, `build_module_rom`, `MAIN_SEG`)

**Interfaces:**
- Consumes: `RomError`; `MainSegment`, `ModuleTables`, `read_main_segment`, `read_module_tables` (Task 2).
- Produces (all in `romtool.modules`):
  - `MAIN = 0`
  - `class RelocType(IntEnum)`: `R_MIPS_32=2, R_MIPS_26=4, R_MIPS_HI16=5, R_MIPS_LO16=6`
  - `class RelocKind(IntEnum)`: `GLOBAL=0, LOCAL=1, LOCAL_CALL=2`
  - `@dataclass(frozen=True) Module(index, rom, text_size, data_size, bss_size, import_size, local_size)`; properties `image_size, import_rom, local_rom, end_rom`
  - `@dataclass(frozen=True) RawReloc(module, offset, type: RelocType, kind: RelocKind, word0, table: str)` — `offset` is relative to the module's text start; `table` is `"import"` or `"local"`
  - `@dataclass(frozen=True) Target(module: int | None, offset: int)` — `module None` means `offset` is an absolute address; module 0 offsets are relative to `MainSegment.text_start`
  - `read_symbols(rom, tables) -> list[int]`, `read_modules(rom, tables) -> list[Module]` (index 1..N, list position = index-1)
  - `read_relocs(rom, module) -> list[RawReloc]` (import table first, then local; HI16 always immediately followed by its LO16)
  - `resolve_global(symbols, index, main, module_count) -> Target`
  - `resolve(reloc, insn, symbols, main, module_count) -> Target`

Format reminder (spec §2): descriptor = `u32 runtime_vram, u32 rom_offset_from_image_rom, u32 text, u32 data, u32 bss, u16 import_bytes, u16 local_bytes, u32, u32`. Image = text | data | import relocs | local relocs. Reloc = `u32 word0, u32 (offset << 8 | type << 4 | kind)`. Global symbol = `module << 20 | offset`; module `0xFFD`/`0xFFE` → main data, `0xFFF` → main bss, `0xFFC` → absolute `0x80000000 + offset`.

Deviation from spec §4 noted deliberately: the spec allows allow-listing unknown relocation entries. The ROM is pinned by MD5 and Step 7 proves it contains no unknown entries, so that path can never run; it is not implemented (YAGNI). Unknown entries stay hard errors.

- [ ] **Step 1: Extend `synth.py`**

Append to `tools/romtool/tests/synth.py`:
```python
def jal(target: int) -> int:
    return (0x03 << 26) | ((target >> 2) & 0x3FFFFFF)


def lw(rt: int, imm: int, base: int) -> int:
    return (0x23 << 26) | (base << 21) | (rt << 16) | (imm & 0xFFFF)


def entry(word0: int, offset: int, rtype: int, kind: int) -> tuple[int, int]:
    """One relocation entry as stored in the ROM."""
    return word0, (offset << 8) | (rtype << 4) | kind


def build_module_rom(symbols: list[int], modules: list[dict]) -> tuple[bytes, "ModuleTables"]:
    """Lay out a symbol table, descriptors and module images the way the game's ROM does.

    Each module dict has: text (bytes), data (bytes), bss (int), imports and locals (lists of entry()).
    """
    from romtool.layout import ModuleTables
    symtab = 0x100
    desc = symtab + 4 * len(symbols)
    image = desc + 32 * len(modules)
    blobs, offset = [], 0
    buf = bytearray(image)
    for i, s in enumerate(symbols):
        struct.pack_into(">I", buf, symtab + 4 * i, s)
    for i, m in enumerate(modules):
        imp = b"".join(struct.pack(">II", *e) for e in m["imports"])
        loc = b"".join(struct.pack(">II", *e) for e in m["locals"])
        struct.pack_into(">IIIIIHHII", buf, desc + 32 * i, 0, offset, len(m["text"]), len(m["data"]),
                         m["bss"], len(imp), len(loc), 0, 0)
        blob = m["text"] + m["data"] + imp + loc
        blobs.append(blob)
        offset += len(blob)
    return bytes(buf) + b"".join(blobs), ModuleTables(symtab, desc, image)


def main_seg():
    from romtool.layout import MainSegment
    return MainSegment(0x1000, 0x80000400, 0x8003F500, 0x80000450, 0x8009DA10, 0x800A0660, 0x800B0B50, 0x801059A0)
```

- [ ] **Step 2: Write the failing ROM-free tests**

`tools/romtool/tests/test_modules.py`:
```python
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
```

- [ ] **Step 3: Write the failing ROM-backed tests**

`tools/romtool/tests/test_modules_rom.py`:
```python
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
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_modules.py tools/romtool/tests/test_modules_rom.py`
Expected: FAIL — `No module named 'romtool.modules'`.

- [ ] **Step 5: Implement `modules.py`**

`tools/romtool/romtool/modules.py`:
```python
"""Decode the game's relocatable code modules: descriptors, global symbols and relocation entries."""
import struct
from collections import Counter
from dataclasses import dataclass
from enum import IntEnum

from .layout import MainSegment, ModuleTables
from .rom import RomError

MAIN = 0
_MAIN_DATA_IDS = (0xFFD, 0xFFE)
_MAIN_BSS_ID = 0xFFF
_ABSOLUTE_ID = 0xFFC  # absolute KSEG0 address 0x80000000 + offset (libultra boot globals, e.g. osTvType)


class RelocType(IntEnum):
    R_MIPS_32 = 2
    R_MIPS_26 = 4
    R_MIPS_HI16 = 5
    R_MIPS_LO16 = 6


class RelocKind(IntEnum):
    GLOBAL = 0      # word0 indexes the global symbol table
    LOCAL = 1       # word0 is an offset from this module's base
    LOCAL_CALL = 2  # the target is the instruction's own 26-bit field, relative to this module's base


_INSTRUCTION_TYPES = (RelocType.R_MIPS_26, RelocType.R_MIPS_HI16, RelocType.R_MIPS_LO16)


@dataclass(frozen=True)
class Module:
    index: int
    rom: int
    text_size: int
    data_size: int
    bss_size: int
    import_size: int
    local_size: int

    @property
    def image_size(self) -> int:
        return self.text_size + self.data_size

    @property
    def import_rom(self) -> int:
        return self.rom + self.image_size

    @property
    def local_rom(self) -> int:
        return self.import_rom + self.import_size

    @property
    def end_rom(self) -> int:
        return self.local_rom + self.local_size


@dataclass(frozen=True)
class RawReloc:
    module: int
    offset: int  # patch location, relative to the module's text start
    type: RelocType
    kind: RelocKind
    word0: int
    table: str   # "import" or "local"


@dataclass(frozen=True)
class Target:
    module: int | None  # None: `offset` is an absolute address
    offset: int         # module 0: relative to MainSegment.text_start; others: to the module's text start


def read_symbols(rom: bytes, tables: ModuleTables) -> list[int]:
    return list(struct.unpack_from(f">{tables.symbol_count}I", rom, tables.symtab_rom))


def read_modules(rom: bytes, tables: ModuleTables) -> list[Module]:
    modules, expected = [], tables.image_rom
    for i in range(tables.module_count):
        _, rel, text, data, bss, imp, loc, _, _ = struct.unpack_from(">IIIIIHHII", rom, tables.desc_rom + 32 * i)
        module = Module(i + 1, tables.image_rom + rel, text, data, bss, imp, loc)
        if module.rom != expected:
            raise RomError(f"module {module.index} starts at ROM {module.rom:#x}, expected {expected:#x} "
                           f"(where module {module.index - 1} ends)")
        if text % 4 or data % 4 or imp % 8 or loc % 8:
            raise RomError(f"module {module.index}: misaligned sizes text {text:#x} data {data:#x} "
                           f"relocs {imp:#x}/{loc:#x}")
        if module.end_rom > len(rom):
            raise RomError(f"module {module.index} runs past the end of the ROM")
        modules.append(module)
        expected = module.end_rom
    return modules


def _read_table(rom: bytes, module: Module, table: str, start: int, size: int) -> list[RawReloc]:
    relocs = []
    for k in range(size // 8):
        word0, info = struct.unpack_from(">II", rom, start + 8 * k)
        offset, raw_type, raw_kind = info >> 8, (info >> 4) & 0xF, info & 0xF
        where = f"module {module.index} {table} reloc #{k} (offset {offset:#x})"
        try:
            rtype = RelocType(raw_type)
        except ValueError:
            raise RomError(f"{where}: unknown relocation type {raw_type}") from None
        try:
            kind = RelocKind(raw_kind)
        except ValueError:
            raise RomError(f"{where}: unknown relocation kind {raw_kind}") from None
        if offset % 4 or offset + 4 > module.image_size:
            raise RomError(f"{where}: patch location outside the module image")
        if rtype in _INSTRUCTION_TYPES and offset >= module.text_size:
            raise RomError(f"{where}: instruction relocation outside text")
        if rtype is RelocType.R_MIPS_32 and offset < module.text_size:
            raise RomError(f"{where}: data relocation inside text")
        relocs.append(RawReloc(module.index, offset, rtype, kind, word0, table))
    for k, r in enumerate(relocs):
        if r.type is RelocType.R_MIPS_HI16 and (k + 1 == len(relocs) or relocs[k + 1].type is not RelocType.R_MIPS_LO16):
            raise RomError(f"module {module.index} {table} reloc #{k}: HI16 not followed by LO16")
    return relocs


def read_relocs(rom: bytes, module: Module) -> list[RawReloc]:
    relocs = (_read_table(rom, module, "import", module.import_rom, module.import_size)
              + _read_table(rom, module, "local", module.local_rom, module.local_size))
    twice = [off for off, n in Counter(r.offset for r in relocs).items() if n > 1]
    if twice:
        raise RomError(f"module {module.index}: offset {twice[0]:#x} is patched twice")
    return relocs


def resolve_global(symbols: list[int], index: int, main: MainSegment, module_count: int) -> Target:
    if not 0 <= index < len(symbols):
        raise RomError(f"global symbol index {index} out of range (table has {len(symbols)})")
    value = symbols[index]
    module, offset = value >> 20, value & 0xFFFFF
    if module in _MAIN_DATA_IDS:
        return Target(MAIN, main.data_start - main.text_start + offset)
    if module == _MAIN_BSS_ID:
        return Target(MAIN, main.bss_start - main.text_start + offset)
    if module == _ABSOLUTE_ID:
        return Target(None, 0x80000000 + offset)
    if module > module_count:
        raise RomError(f"global symbol {index} names module {module}, but there are only {module_count}")
    return Target(module, offset)


def resolve(reloc: RawReloc, insn: int, symbols: list[int], main: MainSegment, module_count: int) -> Target:
    if reloc.kind is RelocKind.GLOBAL:
        return resolve_global(symbols, reloc.word0, main, module_count)
    if reloc.kind is RelocKind.LOCAL:
        return Target(reloc.module, reloc.word0)
    return Target(reloc.module, (insn & 0x3FFFFFF) << 2)
```

- [ ] **Step 6: Run ROM-free tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_modules.py`
Expected: all pass (13 tests including parametrized cases).

- [ ] **Step 7: Run ROM-backed tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_modules_rom.py`
Expected: 3 passed. If a count differs, do not edit the expected numbers — the spike measured them independently; find the decoding bug.

- [ ] **Step 8: Commit**

```bash
git add tools/romtool
git commit -m "romtool: decode module descriptors and relocations

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Reference port of the game's linker

**Files:**
- Create: `tools/romtool/romtool/loader_ref.py`, `tools/romtool/tests/test_loader_ref.py`, `tools/romtool/tests/test_loader_ref_rom.py`
- Modify: `tools/romtool/tests/synth.py` (add `LUI_AT`, `lw` already present)

**Interfaces:**
- Consumes: `Module`, `RawReloc`, `RelocKind`, `RelocType`, `Target`, `MAIN`, `read_relocs`, `resolve` (Task 3); `MainSegment` (Task 2).
- Produces: `STUB_FUNC = 0x80054530`, `STUB_DATA = 0x800FEAD4`,
  `link_module(rom: bytes, module: Module, symbols: list[int], main: MainSegment, module_count: int, bases: dict[int, int]) -> bytes`
  returning the module's text+data exactly as the game would leave them with every module in `bases` loaded at the given address (module 0 is always loaded at `main.text_start`).

Game behaviour being ported (main-segment functions `0x80052874` resolve, `0x80052A00` write, `0x80052AB8` apply):
- Target address: `GLOBAL` → symbol table; if the target module isn't loaded → `STUB_FUNC` for `R_MIPS_26`/`R_MIPS_32`, else `STUB_DATA`. `LOCAL` → `base + word0` (and for `R_MIPS_32`, plus the word already stored there). `LOCAL_CALL` → `base + (insn & 0x3FFFFFF) << 2`.
- `R_MIPS_32`: store address. `R_MIPS_26`: replace the 26-bit field with `address >> 2`.
- `R_MIPS_HI16` + following `R_MIPS_LO16`: `ahl = (hi_imm << 16) + sign_extend(lo_imm)`; if `ahl != STUB_DATA`, add it (the game's own odd comparison at `0x80052BDC`); write `hi = (addr >> 16) + bit15`, `lo = addr & 0xFFFF`.
- Lone `R_MIPS_LO16`: add the *unsigned* existing immediate, write `addr & 0xFFFF`.

- [ ] **Step 1: Write the failing ROM-free tests**

`tools/romtool/tests/test_loader_ref.py`:
```python
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
    # main bss + 0x20 = 0x800B0B70; force a carry with a local target whose low half has bit 15 set
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
```

- [ ] **Step 2: Write the failing ROM-backed tests**

`tools/romtool/tests/test_loader_ref_rom.py`:
```python
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
                    if struct.unpack_from(">I", image, off)[0] == (0x0C000000 | (STUB_FUNC >> 2)))
    assert hits > 0
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_loader_ref.py tools/romtool/tests/test_loader_ref_rom.py`
Expected: FAIL — `No module named 'romtool.loader_ref'`.

- [ ] **Step 4: Implement `loader_ref.py`**

`tools/romtool/romtool/loader_ref.py`:
```python
"""Python port of the game's module linker (main-segment functions 0x80052874, 0x80052A00, 0x80052AB8).

This is the independent reference the generated ELF is checked against, so it follows the game's
code — including its quirks — rather than ELF conventions.
"""
import struct

from .layout import MainSegment
from .modules import MAIN, Module, RelocKind, RelocType, Target, read_relocs, resolve

STUB_FUNC = 0x80054530  # stands in for calls and pointers into a module that isn't loaded
STUB_DATA = 0x800FEAD4  # stands in for data references into a module that isn't loaded


def _address(target: Target, main: MainSegment, bases: dict[int, int]) -> int | None:
    if target.module is None:
        return target.offset
    if target.module == MAIN:
        return main.text_start + target.offset
    base = bases.get(target.module)
    return None if base is None else base + target.offset


def link_module(rom: bytes, module: Module, symbols: list[int], main: MainSegment,
                module_count: int, bases: dict[int, int]) -> bytes:
    if module.index not in bases:
        raise ValueError(f"module {module.index} has no load address")
    image = bytearray(rom[module.rom:module.rom + module.image_size])
    relocs = read_relocs(rom, module)

    def word(off: int) -> int:
        return struct.unpack_from(">I", image, off)[0]

    def put_word(off: int, value: int) -> None:
        struct.pack_into(">I", image, off, value & 0xFFFFFFFF)

    def put_imm(off: int, value: int) -> None:
        struct.pack_into(">H", image, off + 2, value & 0xFFFF)

    i = 0
    while i < len(relocs):
        r = relocs[i]
        insn = word(r.offset)
        addr = _address(resolve(r, insn, symbols, main, module_count), main, bases)
        if addr is None:
            addr = STUB_FUNC if r.type in (RelocType.R_MIPS_26, RelocType.R_MIPS_32) else STUB_DATA
        elif r.kind is RelocKind.LOCAL and r.type is RelocType.R_MIPS_32:
            addr += insn
        if r.type is RelocType.R_MIPS_32:
            put_word(r.offset, addr)
        elif r.type is RelocType.R_MIPS_26:
            put_word(r.offset, (insn & 0xFC000000) | ((addr >> 2) & 0x3FFFFFF))
        elif r.type is RelocType.R_MIPS_HI16:
            lo = relocs[i + 1]
            lo_insn = word(lo.offset)
            ahl = ((insn & 0xFFFF) << 16) + (((lo_insn & 0xFFFF) ^ 0x8000) - 0x8000)
            if ahl != STUB_DATA:
                addr += ahl
            addr &= 0xFFFFFFFF
            put_imm(r.offset, (addr >> 16) + ((addr >> 15) & 1))
            put_imm(lo.offset, addr)
            i += 2
            continue
        else:
            put_imm(r.offset, addr + (insn & 0xFFFF))
        i += 1
    return bytes(image)
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_loader_ref.py tools/romtool/tests/test_loader_ref_rom.py`
Expected: 10 passed.

- [ ] **Step 6: Commit**

```bash
git add tools/romtool
git commit -m "romtool: reference port of the game's module linker

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Function discovery

**Files:**
- Create: `tools/romtool/romtool/mips.py`, `tools/romtool/romtool/functions.py`, `tools/romtool/tests/test_functions.py`, `tools/romtool/tests/test_functions_rom.py`
- Create: `recomp/function_overrides.toml`
- Modify: `tools/romtool/tests/synth.py` (add `beq`, `b`, `jr`, `addiu_sp`)

**Interfaces:**
- Consumes: `MainSegment`, `read_main_segment`, `read_module_tables` (Task 2); `Module`, `RelocType`, `MAIN`, `read_modules`, `read_relocs`, `read_symbols`, `resolve` (Task 3).
- Produces:
  - `romtool.mips`: `JR_RA`, `is_jal(w)`, `jal_target(w, pc)`, `branch_target(w, pc) -> int | None`, `is_jump(w)` (j / jr any / unconditional b), `is_call(w)` (jal / jalr)
  - `romtool.functions`:
    - `@dataclass(frozen=True) Function(section: int, start: int, end: int, name: str)` — `section` 0 = main (addresses are vram), N = module N (addresses are text offsets); `end` exclusive
    - `@dataclass(frozen=True) Overrides(add_start: frozenset[tuple[int,int]], merge: frozenset[tuple[int,int]], names: dict[tuple[int,int], str])`, `load_overrides(path) -> Overrides`
    - `split_functions(code: bytes, base: int, seeds: set[int], merge: set[int] = frozenset()) -> list[tuple[int, int]]`
    - `check_invariants(code: bytes, base: int, spans: list[tuple[int, int]], call_targets: set[int]) -> list[str]`
    - `find_all_functions(rom: bytes, overrides: Overrides) -> list[Function]` (main first, then modules in index order; raises `RomError` listing every invariant problem)

Rules:
- Main code range is `[main.vram_start, main.code_end)` (includes the entry stub; excludes microcode).
- Seeds: main entry stub and entry target; every `jal` target in main code; every `R_MIPS_26` target resolved into main or into a module (from all modules' relocations); overrides `add_start`.
- Scan: start a function at the first non-zero word; track the furthest forward branch target. End after the delay slot when no branch reaches past it and either the instruction is `jr $ra`, or it is another unconditional jump (`j`, `jr $reg`, `b`) followed by zero padding or a known start. A known start always ends the previous function. Addresses in `merge` never begin a function.
- Invariants: spans sorted and non-overlapping; every call target is a span start; every branch inside a span targets the same span; each span's last non-zero instruction (or the one before it, when the delay slot is a nop) is a jump or call.

- [ ] **Step 1: Extend `synth.py`**

Append to `tools/romtool/tests/synth.py`:
```python
SP, A0, RA = 29, 4, 31


def beq(rs: int, rt: int, offset_words: int) -> int:
    return (0x04 << 26) | (rs << 21) | (rt << 16) | (offset_words & 0xFFFF)


def b(offset_words: int) -> int:
    return beq(0, 0, offset_words)


def jr(rs: int) -> int:
    return (rs << 21) | 0x08


def addiu_sp(delta: int) -> int:
    return addiu(SP, SP, delta)
```

- [ ] **Step 2: Write the failing ROM-free tests**

`tools/romtool/tests/test_functions.py`:
```python
from romtool.functions import check_invariants, load_overrides, split_functions
from synth import A0, JR_RA, NOP, T2, addiu_sp, b, beq, jal, jr, words

BASE = 0x80000400


def test_padding_separates_functions():
    code = words(addiu_sp(-8), JR_RA, addiu_sp(8), 0, 0, addiu_sp(-8), JR_RA, NOP)
    assert split_functions(code, BASE, {BASE}) == [(BASE, BASE + 12), (BASE + 20, BASE + 32)]


def test_early_return_not_split():
    # beq jumps past the first jr ra, so the function continues after it
    code = words(beq(A0, 0, 3), NOP, JR_RA, NOP, addiu_sp(8), JR_RA, NOP)
    assert split_functions(code, BASE, {BASE}) == [(BASE, BASE + 28)]


def test_call_target_splits_adjacent_functions():
    code = words(jal(BASE + 16), NOP, JR_RA, NOP, addiu_sp(-8), JR_RA, addiu_sp(8))
    assert split_functions(code, BASE, {BASE, BASE + 16}) == [(BASE, BASE + 16), (BASE + 16, BASE + 28)]


def test_entry_stub_jr_reg_then_padding_ends():
    code = words(jr(T2), NOP, 0, 0, addiu_sp(-8), JR_RA, NOP)
    assert split_functions(code, BASE, {BASE}) == [(BASE, BASE + 8), (BASE + 16, BASE + 28)]


def test_switch_dispatch_not_split():
    # jr $t0 followed directly by case code is a switch, not a function end
    code = words(jr(8), NOP, addiu_sp(1), JR_RA, NOP, addiu_sp(2), JR_RA, NOP)
    spans = split_functions(code, BASE, {BASE})
    assert spans[0] == (BASE, BASE + 20)


def test_merge_override_keeps_function_whole():
    code = words(jr(8), NOP, 0, addiu_sp(1), JR_RA, NOP)
    assert split_functions(code, BASE, {BASE}) == [(BASE, BASE + 8), (BASE + 12, BASE + 24)]
    assert split_functions(code, BASE, {BASE}, merge={BASE + 12}) == [(BASE, BASE + 24)]


def test_invariants_catch_bad_split():
    code = words(beq(A0, 0, 3), NOP, JR_RA, NOP, addiu_sp(8), JR_RA, NOP)
    bad = [(BASE, BASE + 16), (BASE + 16, BASE + 28)]
    problems = check_invariants(code, BASE, bad, call_targets=set())
    assert any("branch" in p for p in problems)


def test_invariants_catch_call_into_middle():
    code = words(addiu_sp(-8), JR_RA, addiu_sp(8))
    problems = check_invariants(code, BASE, [(BASE, BASE + 12)], call_targets={BASE + 4})
    assert any("call target" in p for p in problems)


def test_invariants_accept_infinite_loop_end():
    code = words(addiu_sp(-8), b(-1), NOP)
    assert check_invariants(code, BASE, [(BASE, BASE + 12)], call_targets={BASE}) == []


def test_overrides_file(tmp_path):
    path = tmp_path / "o.toml"
    path.write_text(
        '[[add_start]]\nsection = 0\naddress = 0x80001000\nreason = "x"\n'
        '[[merge]]\nsection = 3\naddress = 0x40\nreason = "y"\n'
        '[[name]]\nsection = 0\naddress = 0x8003F500\nname = "game_main"\n'
    )
    o = load_overrides(path)
    assert o.add_start == {(0, 0x80001000)} and o.merge == {(3, 0x40)}
    assert o.names == {(0, 0x8003F500): "game_main"}
```

- [ ] **Step 3: Write the failing ROM-backed test**

`tools/romtool/tests/test_functions_rom.py`:
```python
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
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_functions.py tools/romtool/tests/test_functions_rom.py`
Expected: FAIL — `No module named 'romtool.functions'`.

- [ ] **Step 5: Implement `mips.py`**

`tools/romtool/romtool/mips.py`:
```python
"""Just enough MIPS decoding to find function boundaries."""

JR_RA = 0x03E00008

_BRANCH_OPS = {0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17}  # beq bne blez bgtz (+ likely forms)
_REGIMM_BRANCHES = {0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13}  # bltz bgez (+likely, +al)


def _offset(w: int, pc: int) -> int:
    return pc + 4 + ((((w & 0xFFFF) ^ 0x8000) - 0x8000) << 2)


def is_jal(w: int) -> bool:
    return w >> 26 == 0x03


def jal_target(w: int, pc: int) -> int:
    return (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)


def branch_target(w: int, pc: int) -> int | None:
    op = w >> 26
    if op in _BRANCH_OPS:
        return _offset(w, pc)
    if op == 0x01 and (w >> 16) & 31 in _REGIMM_BRANCHES:
        return _offset(w, pc)
    if op == 0x11 and (w >> 21) & 31 == 0x08:  # bc1f / bc1t
        return _offset(w, pc)
    return None


def is_jr(w: int) -> bool:
    return w & 0xFC1FFFFF == 0x00000008


def is_unconditional_branch(w: int) -> bool:
    return w >> 16 == 0x1000  # beq $zero, $zero


def is_jump(w: int) -> bool:
    return w >> 26 == 0x02 or is_jr(w) or is_unconditional_branch(w)


def is_call(w: int) -> bool:
    return is_jal(w) or w & 0xFC1F07FF == 0x00000009  # jal / jalr
```

- [ ] **Step 6: Implement `functions.py`**

`tools/romtool/romtool/functions.py`:
```python
"""Find function boundaries in main code and in every module's text."""
import struct
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .layout import read_main_segment, read_module_tables
from .mips import JR_RA, branch_target, is_call, is_jal, is_jump, jal_target
from .modules import MAIN, RelocType, read_modules, read_relocs, read_symbols, resolve
from .rom import RomError


@dataclass(frozen=True)
class Function:
    section: int  # 0 = main (vram addresses), N = module N (text offsets)
    start: int
    end: int      # exclusive
    name: str


@dataclass(frozen=True)
class Overrides:
    add_start: frozenset = frozenset()
    merge: frozenset = frozenset()
    names: dict = field(default_factory=dict)


def load_overrides(path: str | Path) -> Overrides:
    data = tomllib.loads(Path(path).read_text())
    key = lambda e: (int(e["section"]), int(e["address"]))  # noqa: E731
    return Overrides(
        add_start=frozenset(key(e) for e in data.get("add_start", [])),
        merge=frozenset(key(e) for e in data.get("merge", [])),
        names={key(e): e["name"] for e in data.get("name", [])},
    )


def _words(code: bytes) -> list[int]:
    return list(struct.unpack(f">{len(code) // 4}I", code))


def split_functions(code: bytes, base: int, seeds: set[int], merge: set[int] = frozenset()) -> list[tuple[int, int]]:
    ws = _words(code)
    end_addr = base + 4 * len(ws)
    starts = {s for s in seeds if base <= s < end_addr} - set(merge)
    spans, current, furthest, pc = [], None, 0, base
    while pc < end_addr:
        w = ws[(pc - base) // 4]
        if current is None:
            if w == 0 and pc not in starts:
                pc += 4
                continue
            current, furthest = pc, pc
        elif pc in starts:
            spans.append((current, pc))
            current, furthest = pc, pc
        target = branch_target(w, pc)
        if target is not None and target > furthest:
            furthest = target
        after = pc + 8
        if furthest <= pc + 4 and after <= end_addr and after not in merge:
            next_word = ws[(after - base) // 4] if after < end_addr else 0
            if w == JR_RA or (is_jump(w) and (next_word == 0 or after in starts)):
                spans.append((current, after))
                current = None
                pc = after
                continue
        pc += 4
    if current is not None:
        spans.append((current, end_addr))
    return spans


def check_invariants(code: bytes, base: int, spans: list[tuple[int, int]], call_targets: set[int]) -> list[str]:
    ws = _words(code)
    word = lambda a: ws[(a - base) // 4]  # noqa: E731
    problems = []
    for (s0, e0), (s1, _) in zip(spans, spans[1:]):
        if e0 > s1:
            problems.append(f"functions {s0:#x} and {s1:#x} overlap")
    starts = {s for s, _ in spans}
    for t in sorted(call_targets):
        if t not in starts:
            problems.append(f"call target {t:#x} is not a function start")
    for s, e in spans:
        for pc in range(s, e, 4):
            target = branch_target(word(pc), pc)
            if target is not None and not s <= target < e:
                problems.append(f"branch at {pc:#x} leaves function {s:#x} (to {target:#x})")
        last = max((pc for pc in range(s, e, 4) if word(pc) != 0), default=None)
        if last is None:
            continue
        ends_ok = any(is_jump(word(pc)) or is_call(word(pc)) for pc in (last, last - 4) if pc >= s)
        if not ends_ok:
            problems.append(f"function {s:#x} does not end in a jump or call")
    return problems


def find_all_functions(rom: bytes, overrides: Overrides) -> list[Function]:
    main, tables = read_main_segment(rom), read_module_tables(rom)
    modules, symbols = read_modules(rom, tables), read_symbols(rom, tables)

    call_targets: dict[int, set[int]] = {MAIN: set()}
    for m in modules:
        call_targets[m.index] = {0} if m.text_size else set()
    main_code = rom[main.rom_of(main.vram_start):main.rom_of(main.code_end)]
    for i, w in enumerate(_words(main_code)):
        pc = main.vram_start + 4 * i
        if is_jal(w):
            call_targets[MAIN].add(jal_target(w, pc))
    for m in modules:
        for r in read_relocs(rom, m):
            if r.type is not RelocType.R_MIPS_26:
                continue
            insn = struct.unpack_from(">I", rom, m.rom + r.offset)[0]
            t = resolve(r, insn, symbols, main, len(modules))
            if t.module == MAIN:
                call_targets[MAIN].add(main.text_start + t.offset)
            elif t.module is not None:
                call_targets[t.module].add(t.offset)
    call_targets[MAIN] |= {main.vram_start, main.entry_target}
    for section, addr in overrides.add_start:
        call_targets.setdefault(section, set()).add(addr)

    functions, problems = [], []
    sections = [(MAIN, main_code, main.vram_start)] + [
        (m.index, rom[m.rom:m.rom + m.text_size], 0) for m in modules if m.text_size]
    for section, code, base in sections:
        merge = {a for s, a in overrides.merge if s == section}
        seeds = call_targets.get(section, set())
        outside = {t for t in seeds if not base <= t < base + len(code)}
        if section == MAIN:
            seeds = seeds - outside  # main jal targets outside main code would be bugs; report them
            problems += [f"main: call target {t:#x} outside main code" for t in sorted(outside)]
        spans = split_functions(code, base, seeds, merge)
        problems += [f"section {section}: {p}" for p in check_invariants(code, base, spans, seeds - outside)]
        for s, e in spans:
            default = f"func_{s:08X}" if section == MAIN else f"m{section:03d}_{s:05X}"
            functions.append(Function(section, s, e, overrides.names.get((section, s), default)))
    if problems:
        raise RomError("function discovery found problems:\n  " + "\n  ".join(problems))
    return functions
```

Create `recomp/function_overrides.toml`:
```toml
# Manual corrections to romtool's function finder, plus human-readable names.
# section: 0 = main segment (address = vram); N = module N (address = offset into its text).
# Every add_start / merge entry must say why it exists.
#
# [[add_start]]  section = 0  address = 0x80001234  reason = "only reached through a function pointer"
# [[merge]]      section = 12 address = 0x340       reason = "switch case after jr $t6, not a new function"
# [[name]]       section = 0  address = 0x8003F500  name = "game_main"

[[name]]
section = 0
address = 0x80000400
name = "entrypoint"

[[name]]
section = 0
address = 0x80052AB8
name = "module_apply_reloc"

[[name]]
section = 0
address = 0x80053D2C
name = "module_load"
```

- [ ] **Step 7: Run ROM-free tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_functions.py`
Expected: 10 passed.

- [ ] **Step 8: Run the ROM-backed test and resolve every reported problem**

Run: `tools/romtool/.venv/bin/pytest -q -s tools/romtool/tests/test_functions_rom.py`
Expected first time: either PASS, or `RomError` listing problems. For each problem, inspect the code around it:
```bash
tools/romtool/.venv/bin/python -I -c "
import sys, struct, rabbitizer
rom = open('jfg.z64','rb').read(); v = int(sys.argv[1], 16)
for a in range(v - 0x20, v + 0x40, 4):
    w = struct.unpack_from('>I', rom, a - 0x80000400 + 0x1000)[0]
    print(f'{a:08X} {w:08X} {rabbitizer.Instruction(w, a).disassemble()}')" 0x800XXXXX
```
(for module sections use `rom[m.rom + offset]` from `read_modules`). Fix the scanner if a rule is wrong for a whole class of cases; add a `merge`/`add_start` entry **with a reason** for one-offs. Re-run until it passes. Record the final main/module function counts printed by the test in the commit message.

- [ ] **Step 9: Commit**

```bash
git add tools/romtool recomp/function_overrides.toml
git commit -m "romtool: function discovery with invariant checks

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Phase 2 — Toolchain, fork, external tools

### Task 6: Install toolchain, import the Zelda64Recomp fork, build N64Recomp / RSPRecomp / n64sym

**Files:**
- Modify: repository history (merge of upstream Zelda64Recomp), `.gitmodules`, `.gitignore`
- Remove: `lib/mm-decomp`, `Zelda64RecompSyms` submodules
- Add submodules: `lib/N64Recomp`, `lib/n64sym`

**Interfaces:**
- Produces: `build/tools/N64Recomp/N64Recomp`, `build/tools/N64Recomp/RSPRecomp`, `lib/n64sym/<binary>` (path recorded in `tools/paths.env` as `N64SYM=...`), the Zelda64Recomp tree at the repo root with its pinned `lib/N64ModernRuntime` and `lib/rt64`.

Decisions fixed here: keep Zelda64Recomp's pinned runtime/RT64 submodule commits (a known-working pair — do **not** bump them); build N64Recomp from the commit Zelda64Recomp's CI uses, bumping only if a feature this plan needs (`[[patches.hook]]`) is missing.

- [ ] **Step 1: Install system packages**

Run: `sudo -n true 2>/dev/null && echo SUDO_OK || echo SUDO_NEEDS_PASSWORD`
If `SUDO_OK`: `sudo apt-get install -y clang lld llvm cmake ninja-build make g++ libsdl2-dev libgtk-3-dev libfreetype-dev`.
If `SUDO_NEEDS_PASSWORD`: stop and ask the user to run `! sudo apt-get install -y clang lld llvm cmake ninja-build make g++ libsdl2-dev libgtk-3-dev libfreetype-dev`, then continue.
Verify: `clang --version && ld.lld --version && pkg-config --modversion sdl2 freetype2` — all print versions.

- [ ] **Step 2: Merge Zelda64Recomp's history into `base-port`**

```bash
git remote add upstream https://github.com/Zelda64Recomp/Zelda64Recomp.git
git fetch upstream
UPSTREAM_BRANCH=$(git remote show upstream | sed -n 's/.*HEAD branch: //p')
git merge --allow-unrelated-histories "upstream/$UPSTREAM_BRANCH" -m "Import Zelda64Recomp as the frontend base"
```
Expected conflict: `.gitignore` only. Resolve by keeping **both** sets of lines (ours first: the ROM/derived-data rules must remain), `git add .gitignore`, `git commit --no-edit`.
Record the upstream commit: `git rev-parse "upstream/$UPSTREAM_BRANCH"` → put it in the commit message of Step 4.

- [ ] **Step 3: Drop Majora's Mask submodules, add ours, init the rest**

```bash
git rm -q lib/mm-decomp Zelda64RecompSyms
N64RECOMP_COMMIT=$(grep -rhoE 'N64Recomp[^\n]*[0-9a-f]{40}' .github/workflows | grep -oE '[0-9a-f]{40}' | head -1)
echo "N64Recomp CI pin: $N64RECOMP_COMMIT"
git submodule add https://github.com/N64Recomp/N64Recomp.git lib/N64Recomp
git -C lib/N64Recomp checkout -q "$N64RECOMP_COMMIT"
git submodule add https://github.com/shygoo/n64sym.git lib/n64sym
git submodule update --init --recursive
```
If `N64RECOMP_COMMIT` is empty, open `.github/workflows/*.yml`, find how CI obtains N64Recomp, and use that commit; record it.

- [ ] **Step 4: Build the tools**

```bash
cmake -S lib/N64Recomp -B build/tools/N64Recomp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build/tools/N64Recomp
make -C lib/n64sym
ls lib/n64sym lib/n64sym/bin 2>/dev/null
```
Expected: `build/tools/N64Recomp/N64Recomp` and `build/tools/N64Recomp/RSPRecomp` exist; n64sym produced a binary (note its path). Write `tools/paths.env`:
```bash
N64RECOMP=build/tools/N64Recomp/N64Recomp
RSPRECOMP=build/tools/N64Recomp/RSPRecomp
N64SYM=lib/n64sym/<path printed above>
```

- [ ] **Step 5: Check the N64Recomp features this plan relies on**

Run: `grep -n '"hook"\|patches.hook\|before_vram' lib/N64Recomp/src/config.cpp | head`
Expected: matches (hooks supported). If none: `git -C lib/N64Recomp checkout -q main && git -C lib/N64Recomp log -1 --format=%H`, rebuild (Step 4), re-check, and note "bumped N64Recomp for patches.hook" in the commit.
Also run: `grep -n 'relocatable_sections_path\|elf_path' lib/N64Recomp/src/config.cpp | head -4` — expected: both keys present.

- [ ] **Step 6: Commit**

```bash
git add -A .gitmodules lib/N64Recomp lib/n64sym tools/paths.env .gitignore
git commit -m "Fork Zelda64Recomp; add N64Recomp and n64sym tools

Upstream Zelda64Recomp: <commit from Step 2>
N64Recomp: <commit from Steps 3/5>

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Phase 3 — `romtool` back half

### Task 7: libultra names and hardware-access audit

**Files:**
- Create: `tools/romtool/romtool/libultra.py`, `tools/romtool/romtool/audit.py`, `tools/romtool/tests/test_audit.py`, `tools/romtool/tests/test_libultra_rom.py`
- Modify: `tools/romtool/romtool/functions.py` (Overrides gains `hardware`), `recomp/function_overrides.toml`

**Interfaces:**
- Consumes: `Function`, `Overrides`, `load_overrides`, `find_all_functions` (Task 5); `read_main_segment` (Task 2); `read_modules` etc. (Task 3).
- Produces:
  - `libultra.run_n64sym(rom: bytes, main: MainSegment, n64sym: str) -> dict[int, str]` (vram → name, main segment only)
  - `libultra.runtime_handled_names(symbol_lists_cpp: str) -> set[str]` (every quoted identifier in N64Recomp's `src/symbol_lists.cpp`)
  - `audit.hardware_accesses(code: bytes, base: int, spans: list[tuple[int,int]]) -> dict[int, list[str]]` (function start → reasons)
  - `audit.unreviewed(functions, accesses, names_handled, overrides) -> list[str]`
  - `Overrides.hardware: dict[tuple[int,int], str]` (section,address → decision text) loaded from `[[hardware]]` entries with keys `section, address, decision, reason`; `decision` ∈ `{"patch", "harmless"}`

Why: N64ModernRuntime replaces libultra functions **by name**, and cannot run code that touches hardware registers directly (no kseg1, COP0, cache or TLB). Every function that does so must be either a libultra function the runtime handles, or reviewed and given a decision (`patch` → handled in Task 12 via a `[[patches.hook]]` that `return`s early, or `harmless`, e.g. dead debug code).

- [ ] **Step 1: Write the failing ROM-free audit tests**

`tools/romtool/tests/test_audit.py`:
```python
from romtool.audit import hardware_accesses
from synth import JR_RA, NOP, addiu_sp, lui, lw, words

BASE = 0x80000400
T0, T1 = 8, 9
MTC0_STATUS = (0x10 << 26) | (0x04 << 21) | (T0 << 16) | (12 << 11)
CACHE = (0x2F << 26) | (T0 << 21) | (0x19 << 16)
MTC1 = (0x11 << 26) | (0x04 << 21) | (T0 << 16) | (0 << 11)


def test_cop0_cache_and_register_access_flagged():
    code = words(MTC0_STATUS, JR_RA, NOP,
                 CACHE, JR_RA, NOP,
                 lui(T0, 0xA460), lw(T1, 0x10, T0), JR_RA, NOP)
    spans = [(BASE, BASE + 12), (BASE + 12, BASE + 24), (BASE + 24, BASE + 40)]
    found = hardware_accesses(code, BASE, spans)
    assert set(found) == {BASE, BASE + 12, BASE + 24}
    assert "cop0" in found[BASE][0] and "cache" in found[BASE + 12][0] and "0xa460" in found[BASE + 24][0]


def test_float_constant_not_flagged():
    code = words(lui(T0, 0xBF80), MTC1, JR_RA, NOP)  # -1.0f
    assert hardware_accesses(code, BASE, [(BASE, BASE + 16)]) == {}
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_audit.py`
Expected: FAIL — `No module named 'romtool.audit'`.

- [ ] **Step 3: Implement `audit.py`**

`tools/romtool/romtool/audit.py`:
```python
"""Find functions that touch hardware directly (COP0, cache ops, kseg1 / RCP / cartridge / PIF addresses)."""
import struct

_LOAD_STORE_OPS = {0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x28, 0x29, 0x2B, 0x31, 0x35, 0x37, 0x39, 0x3D, 0x3F}


def _reasons(ws: list[int], i: int) -> list[str]:
    w = ws[i]
    op = w >> 26
    if op == 0x10:
        return [f"cop0 instruction {w:#010x}"]
    if op == 0x2F:
        return [f"cache instruction {w:#010x}"]
    if op == 0x0F and 0xA000 <= (w & 0xFFFF) <= 0xBFFF:
        reg = (w >> 16) & 31
        for nxt in ws[i + 1:i + 9]:
            if nxt >> 26 in _LOAD_STORE_OPS and (nxt >> 21) & 31 == reg:
                return [f"kseg1 access via lui {w & 0xFFFF:#06x}"]
    return []


def hardware_accesses(code: bytes, base: int, spans: list[tuple[int, int]]) -> dict[int, list[str]]:
    ws = list(struct.unpack(f">{len(code) // 4}I", code))
    found: dict[int, list[str]] = {}
    for start, end in spans:
        for pc in range(start, end, 4):
            reasons = _reasons(ws, (pc - base) // 4)
            if reasons:
                found.setdefault(start, []).extend(reasons)
    return found


def unreviewed(functions, accesses_by_section, names_handled, overrides) -> list[str]:
    """Functions that touch hardware but are neither runtime-handled libultra nor reviewed in the overrides."""
    out = []
    by_key = {(f.section, f.start): f for f in functions}
    for section, accesses in accesses_by_section.items():
        for start, reasons in accesses.items():
            f = by_key[(section, start)]
            if f.name in names_handled or (section, start) in overrides.hardware:
                continue
            out.append(f"section {section} {f.name} @ {start:#x}: {reasons[0]}")
    return out
```

- [ ] **Step 4: Run the ROM-free tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_audit.py`
Expected: 2 passed.

- [ ] **Step 5: Implement `libultra.py` and extend overrides**

`tools/romtool/romtool/libultra.py`:
```python
"""Name libultra functions with n64sym so N64Recomp maps them to N64ModernRuntime's versions."""
import re
import subprocess
import tempfile
from pathlib import Path

from .layout import MainSegment


def run_n64sym(rom: bytes, main: MainSegment, n64sym: str) -> dict[int, str]:
    code = rom[main.rom_of(main.vram_start):main.rom_of(main.code_end)]
    with tempfile.TemporaryDirectory() as tmp:
        blob, out = Path(tmp) / "main.bin", Path(tmp) / "syms.txt"
        blob.write_bytes(code)  # not .z64: n64sym treats it as a raw blob
        subprocess.run([n64sym, str(blob), "-s", "-t", "-h", f"{main.vram_start:#x}", "-o", str(out)],
                       check=True, capture_output=True)
        names = {}
        for line in out.read_text().splitlines():
            m = re.fullmatch(r"([0-9A-Fa-f]{8}) (\S+)", line.strip())
            if m:
                names[int(m.group(1), 16)] = m.group(2)
    return names


def runtime_handled_names(symbol_lists_cpp: str) -> set[str]:
    return set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', Path(symbol_lists_cpp).read_text()))
```

In `functions.py`, extend `Overrides` and `load_overrides`:
```python
@dataclass(frozen=True)
class Overrides:
    add_start: frozenset = frozenset()
    merge: frozenset = frozenset()
    names: dict = field(default_factory=dict)
    hardware: dict = field(default_factory=dict)  # (section, address) -> decision


def load_overrides(path: str | Path) -> Overrides:
    data = tomllib.loads(Path(path).read_text())
    key = lambda e: (int(e["section"]), int(e["address"]))  # noqa: E731
    for e in data.get("hardware", []):
        if e.get("decision") not in ("patch", "harmless") or not e.get("reason"):
            raise RomError(f"hardware override {e} needs decision 'patch' or 'harmless' and a reason")
    return Overrides(
        add_start=frozenset(key(e) for e in data.get("add_start", [])),
        merge=frozenset(key(e) for e in data.get("merge", [])),
        names={key(e): e["name"] for e in data.get("name", [])},
        hardware={key(e): e["decision"] for e in data.get("hardware", [])},
    )
```

- [ ] **Step 6: Write the ROM-backed test**

`tools/romtool/tests/test_libultra_rom.py`:
```python
import os
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
    merged = type(overrides)(overrides.add_start, overrides.merge,
                             {(0, a): n for a, n in names.items()} | overrides.names, overrides.hardware)
    funcs = find_all_functions(rom, merged)
    handled = runtime_handled_names(str(ROOT / "lib/N64Recomp/src/symbol_lists.cpp"))
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
```

- [ ] **Step 7: Run it and review every unreviewed function**

Run: `tools/romtool/.venv/bin/pytest -q -s tools/romtool/tests/test_libultra_rom.py`
For each listed function, disassemble it (`romtool disasm` arrives in Task 10; until then use the rabbitizer one-liner from Task 5 Step 8) and decide:
- It is a libultra function n64sym missed (compare against libultra source behaviour: `__osPiRawStartDma`, `osFlashReadArray`, `__osSiRawReadIo`, …) → add a `[[name]]` entry with the libultra name. Re-run: it disappears if that name is runtime-handled.
- It touches hardware in a way the game needs (custom Flash RAM driver, CIC-6105 check, direct VI/PI/AI poke) → `[[hardware]] decision = "patch"` with a reason saying what it does and what the port must do instead.
- Unreachable/debug-only → `decision = "harmless"` with the evidence (e.g. "only caller is the crash screen at 0x...").
Repeat until the test passes. The `patch` list is the input to Task 12 Step 6.

- [ ] **Step 8: Commit**

```bash
git add tools/romtool recomp/function_overrides.toml
git commit -m "romtool: libultra naming via n64sym and hardware-access audit

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: ELF writer

**Files:**
- Create: `tools/romtool/romtool/elf.py`, `tools/romtool/tests/test_elf.py`

**Interfaces:**
- Produces:
  - `@dataclass(frozen=True) Section(name, vram, size, data: bytes | None = None, lma: int | None = None, code: bool = False)` (data None → NOBITS)
  - `@dataclass(frozen=True) FuncSym(name, section, vram, size)`
  - `@dataclass(frozen=True) Rel(section, vram, target, type)` — `target` names the section whose load address the patch depends on; emitted against that section's STT_SECTION symbol
  - `write_elf(path, sections, funcs, rels, entry) -> None`

Format rules N64Recomp depends on (verified in its `src/elf.cpp`): every PROGBITS section must lie inside a `PT_LOAD` segment whose `p_paddr` is its ROM address (LMA); ROM addresses are LMA − min LMA; REL entries' `r_offset` is the patch site's **virtual address**; relocations are matched to sections by the name `.rel<name>`; HI16/LO16 pairing is by table order (keep input order); only `STT_FUNC` symbols with a size become functions.

- [ ] **Step 1: Write the failing tests**

`tools/romtool/tests/test_elf.py`:
```python
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
    import pytest
    with pytest.raises(ValueError):
        write_elf(tmp_path / "x.elf", [Section(".a", 0, 4, data=bytes(4), lma=0)], [], [Rel(".a", 0, ".nope", 2)], 0)
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_elf.py`
Expected: FAIL — `No module named 'romtool.elf'`.

- [ ] **Step 3: Implement `elf.py`**

`tools/romtool/romtool/elf.py`:
```python
"""Minimal big-endian MIPS ELF32 writer: an executable with PT_LOAD segments, symbols and REL sections."""
import struct
from dataclasses import dataclass
from pathlib import Path

EM_MIPS = 8
EF_MIPS_ARCH_3 = 0x20000000
PT_LOAD = 1
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS, SHT_REL = 1, 2, 3, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4
STB_LOCAL, STB_GLOBAL = 0, 1
STT_FUNC, STT_SECTION = 2, 3
_SHDR = ">IIIIIIIIII"
_PHDR = ">IIIIIIII"
_SYM = ">IIIBBH"


@dataclass(frozen=True)
class Section:
    name: str
    vram: int
    size: int
    data: bytes | None = None  # None: NOBITS
    lma: int | None = None     # ROM address; required for PROGBITS
    code: bool = False


@dataclass(frozen=True)
class FuncSym:
    name: str
    section: str
    vram: int
    size: int


@dataclass(frozen=True)
class Rel:
    section: str  # section being patched
    vram: int     # virtual address of the patched word
    target: str   # section whose load address the patch depends on
    type: int     # ELF MIPS relocation type


class _Strtab:
    def __init__(self) -> None:
        self.data = bytearray(b"\0")
        self._index = {"": 0}

    def add(self, s: str) -> int:
        if s not in self._index:
            self._index[s] = len(self.data)
            self.data += s.encode() + b"\0"
        return self._index[s]


def write_elf(path: str | Path, sections: list[Section], funcs: list[FuncSym], rels: list[Rel], entry: int) -> None:
    names = [s.name for s in sections]
    if len(set(names)) != len(names):
        raise ValueError("duplicate section names")
    index = {name: i + 1 for i, name in enumerate(names)}
    groups: dict[str, list[Rel]] = {}
    for r in rels:
        if r.section not in index or r.target not in index:
            raise ValueError(f"relocation refers to an unknown section: {r}")
        groups.setdefault(r.section, []).append(r)
    rel_names = [n for n in names if n in groups]
    symtab_index = len(sections) + 1 + len(rel_names)
    strtab_index, shstrtab_index = symtab_index + 1, symtab_index + 2

    shstr = _Strtab()
    for n in names:
        shstr.add(n)
    for n in rel_names:
        shstr.add(".rel" + n)
    for n in (".symtab", ".strtab", ".shstrtab"):
        shstr.add(n)

    strtab = _Strtab()
    syms = [struct.pack(_SYM, 0, 0, 0, 0, 0, 0)]
    section_symbol = {}
    for s in sections:
        section_symbol[s.name] = len(syms)
        syms.append(struct.pack(_SYM, 0, s.vram, 0, (STB_LOCAL << 4) | STT_SECTION, 0, index[s.name]))
    first_global = len(syms)
    for f in funcs:
        if f.section not in index:
            raise ValueError(f"function {f.name} is in unknown section {f.section}")
        syms.append(struct.pack(_SYM, strtab.add(f.name), f.vram, f.size, (STB_GLOBAL << 4) | STT_FUNC, 0, index[f.section]))

    progbits = [s for s in sections if s.data is not None]
    out = bytearray(52 + 32 * len(progbits))

    def put(blob: bytes, align: int) -> int:
        out.extend(bytes(-len(out) % align))
        at = len(out)
        out.extend(blob)
        return at

    shdrs, phdrs = [bytes(40)], []
    for s in sections:
        if s.data is None:
            shdrs.append(struct.pack(_SHDR, shstr.add(s.name), SHT_NOBITS, SHF_ALLOC | SHF_WRITE, s.vram, len(out), s.size, 0, 0, 16, 0))
            continue
        if len(s.data) != s.size or s.lma is None:
            raise ValueError(f"section {s.name}: data length must equal size and lma is required")
        at = put(s.data, 16)
        flags = SHF_ALLOC | (SHF_EXECINSTR if s.code else SHF_WRITE)
        shdrs.append(struct.pack(_SHDR, shstr.add(s.name), SHT_PROGBITS, flags, s.vram, at, s.size, 0, 0, 16, 0))
        phdrs.append(struct.pack(_PHDR, PT_LOAD, at, s.vram, s.lma, s.size, s.size, 5 if s.code else 6, 16))
    for n in rel_names:
        blob = b"".join(struct.pack(">II", r.vram, (section_symbol[r.target] << 8) | r.type) for r in groups[n])
        at = put(blob, 4)
        shdrs.append(struct.pack(_SHDR, shstr.add(".rel" + n), SHT_REL, 0, 0, at, len(blob), symtab_index, index[n], 4, 8))
    blob = b"".join(syms)
    at = put(blob, 4)
    shdrs.append(struct.pack(_SHDR, shstr.add(".symtab"), SHT_SYMTAB, 0, 0, at, len(blob), strtab_index, first_global, 4, 16))
    at = put(bytes(strtab.data), 1)
    shdrs.append(struct.pack(_SHDR, shstr.add(".strtab"), SHT_STRTAB, 0, 0, at, len(strtab.data), 0, 0, 1, 0))
    at = put(bytes(shstr.data), 1)
    shdrs.append(struct.pack(_SHDR, shstr.add(".shstrtab"), SHT_STRTAB, 0, 0, at, len(shstr.data), 0, 0, 1, 0))
    shoff = put(b"".join(shdrs), 4)

    ident = b"\x7fELF" + bytes([1, 2, 1]) + bytes(9)
    out[0:52] = ident + struct.pack(">HHIIIIIHHHHHH", 2, EM_MIPS, 1, entry, 52, shoff, EF_MIPS_ARCH_3,
                                     52, 32, len(phdrs), 40, len(shdrs), shstrtab_index)
    out[52:52 + 32 * len(phdrs)] = b"".join(phdrs)
    Path(path).write_bytes(out)
```

- [ ] **Step 4: Run tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_elf.py`
Expected: 4 passed.

- [ ] **Step 5: Commit**

```bash
git add tools/romtool
git commit -m "romtool: big-endian MIPS ELF32 writer

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Program builder — link layout, dynamic-call thunks, hooks, round-trip verification

**Files:**
- Create: `tools/romtool/romtool/thunks.py`, `tools/romtool/romtool/build.py`, `tools/romtool/romtool/elf_relocate.py`
- Create: `tools/romtool/tests/test_thunks.py`, `tools/romtool/tests/test_build_rom.py`
- Modify: `tools/romtool/romtool/layout.py` (add `read_main_imports_rom`), `tools/romtool/romtool/mips.py` (encoders)

**Interfaces:**
- Consumes: everything from Tasks 2–5, 7 (`Overrides` with names), 8 (`Section`, `FuncSym`, `Rel`).
- Produces:
  - `layout.read_main_imports_rom(rom) -> int` (= `0x1ECF220`, derived from `lui/addiu $t4` at `0x80053A94/0x80053A98`)
  - `mips` encoders: `lui(rt, imm)`, `addiu(rt, rs, imm)`, `addu(rd, rs, rt)`, `lw(rt, imm, base)`, `sll(rd, rt, sa)`, `srl(rd, rt, sa)`, `or_(rd, rs, rt)`, `jr(rs)`, `jal(target)`, `hi_lo(value) -> (hi, lo)` (hi includes the carry)
  - `thunks.DESC_TABLE_PTR = 0x800FEAA0`, `thunks.module_thunk(module: int, site_offset: int) -> list[int]`, `thunks.main_thunk(site_vram: int) -> list[int]`
  - `build.MODULE_LINK_BASE = 0x82000000`, `build.THUNK_VRAM = 0x8E000000`, `build.THUNK_LMA = 0x2000000`
  - `@dataclass(frozen=True) build.Thunk(name, caller_section, site, vram, target: Target)`
  - `@dataclass(frozen=True) build.Hook(function, before_vram, register, hi_site, lo_site, static_lo)`
  - `@dataclass build.Program(sections, funcs, rels, thunks, hooks, link_base: dict[int, int], entry)`
  - `build.section_name(index: int) -> str` (`".main"`, `".m001"`…)
  - `build.build_program(rom: bytes, overrides: Overrides) -> Program`
  - `elf_relocate.relocate_module(program, module_index, bases: dict[int, int]) -> bytes`

**Why thunks.** The game links lazily. A call into a module is first compiled as `jal 0x80054530` (the game's lazy linker); on the first call the linker uses `$ra` to find which import it came from, loads the module, patches *that call instruction in RAM* to the real target and jumps there. Unloading a module patches the calls back. Recompiled code never re-reads instructions from RAM, so a direct call would bypass all of this. Instead every call that the game re-links at run time — the 513 call imports of the main segment (table at `0x1ECF220`) and the 515 module calls into *other* modules — is redirected in our ELF to a small generated MIPS thunk that:
1. sets `$ra` to the call site's real return address (N64Recomp does not set `$ra` on `jal`; the lazy linker needs it),
2. reads the call instruction from RAM, where the game keeps it patched,
3. tail-jumps (`jr $t9`, which N64Recomp turns into a function lookup) to whatever it currently points at — the lazy linker or the real target.
Thunks only clobber `$t6 $t7 $t9`, which a caller may not keep live across a call.

**Why hooks.** The main segment also has 2 HI16/LO16 import pairs (the game patches the immediates in RAM). Main code is static, so we inject a `[[patches.hook]]` before the LO16 instruction that sets the base register so the static LO16 immediate yields the address the game patched into RAM.

Link layout: each module is linked at its own address from `0x82000000` (4 KiB aligned, text+data+bss contiguous, same as the game allocates it). Relocations are emitted **only** for targets inside module sections (own or another module); a reference to a module's bss targets that module's PROGBITS section with an offset past its data (module memory is one contiguous block). Main-segment and absolute targets need no relocation: their final address is already in the instruction.

- [ ] **Step 1: Write the failing thunk tests**

`tools/romtool/tests/test_thunks.py`:
```python
from romtool.thunks import DESC_TABLE_PTR, main_thunk, module_thunk


def run(code, regs, mem):
    """Tiny interpreter for the handful of instructions thunks use. Returns (regs, jump target)."""
    s32 = lambda v: v - (1 << 32) if v & 0x80000000 else v  # noqa: E731
    for w in code:
        op, rs, rt, rd, sa, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F: regs[rt] = (imm << 16) & 0xFFFFFFFF
        elif op == 0x09: regs[rt] = (regs[rs] + simm) & 0xFFFFFFFF
        elif op == 0x23: regs[rt] = mem[(regs[rs] + simm) & 0xFFFFFFFF]
        elif op == 0 and w & 0x3F == 0x21: regs[rd] = (regs[rs] + regs[rt]) & 0xFFFFFFFF
        elif op == 0 and w & 0x3F == 0x00 and w: regs[rd] = (regs[rt] << sa) & 0xFFFFFFFF
        elif op == 0 and w & 0x3F == 0x02: regs[rd] = regs[rt] >> sa
        elif op == 0 and w & 0x3F == 0x25: regs[rd] = regs[rs] | regs[rt]
        elif op == 0 and w & 0x3F == 0x08: target = regs[rs]
        elif w == 0: pass
        else: raise AssertionError(f"unexpected instruction {w:#010x}")
    _ = s32
    return regs, target


def test_module_thunk_follows_patched_call():
    module, site = 37, 0x1A2C4  # site offset beyond 0x7FFF exercises the carry
    table, base = 0x80230000, 0x80345000
    patched = 0x0C000000 | ((0x80456780 >> 2) & 0x3FFFFFF)
    mem = {DESC_TABLE_PTR: table, table + module * 32: base, base + site: patched}
    regs, target = run(module_thunk(module, site), [0] * 32, mem)
    assert target == 0x80456780
    assert regs[31] == base + site + 8


def test_main_thunk_follows_patched_call():
    site = 0x80012340
    mem = {site: 0x0C000000 | ((0x80054530 >> 2) & 0x3FFFFFF)}
    regs, target = run(main_thunk(site), [0] * 32, mem)
    assert target == 0x80054530 and regs[31] == site + 8


def test_thunks_only_clobber_temporaries():
    used = set()
    for w in module_thunk(5, 0x40) + main_thunk(0x80001000):
        if w >> 26 in (0x0F, 0x09, 0x23):
            used.add((w >> 16) & 31)
        elif w >> 26 == 0 and w & 0x3F in (0x21, 0x00, 0x02, 0x25) and w:
            used.add((w >> 11) & 31)
    assert used <= {14, 15, 25, 31}
```

- [ ] **Step 2: Run to verify failure**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_thunks.py`
Expected: FAIL — `No module named 'romtool.thunks'`.

- [ ] **Step 3: Add encoders to `mips.py` and implement `thunks.py`**

Append to `tools/romtool/romtool/mips.py`:
```python
def lui(rt: int, imm: int) -> int: return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)
def addiu(rt: int, rs: int, imm: int) -> int: return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def lw(rt: int, imm: int, base: int) -> int: return (0x23 << 26) | (base << 21) | (rt << 16) | (imm & 0xFFFF)
def addu(rd: int, rs: int, rt: int) -> int: return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def sll(rd: int, rt: int, sa: int) -> int: return (rt << 16) | (rd << 11) | (sa << 6)
def srl(rd: int, rt: int, sa: int) -> int: return (rt << 16) | (rd << 11) | (sa << 6) | 0x02
def or_(rd: int, rs: int, rt: int) -> int: return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def jr(rs: int) -> int: return (rs << 21) | 0x08
def jal(target: int) -> int: return (0x03 << 26) | ((target >> 2) & 0x3FFFFFF)


def hi_lo(value: int) -> tuple[int, int]:
    value &= 0xFFFFFFFF
    return ((value >> 16) + ((value >> 15) & 1)) & 0xFFFF, value & 0xFFFF
```

`tools/romtool/romtool/thunks.py`:
```python
"""MIPS thunks that make recompiled code follow the game's run-time call patching.

The game keeps each re-linkable call instruction in RAM pointed at either its lazy linker (0x80054530)
or the real target. A thunk reads that instruction and jumps where it points, with $ra set to the call
site's return address exactly as a real jal would have.
"""
from .mips import addiu, addu, hi_lo, jr, lui, lw, or_, sll, srl

DESC_TABLE_PTR = 0x800FEAA0  # variable holding the address of the module descriptor table
_T6, _T7, _T9, _RA = 14, 15, 25, 31


def _follow_call_at_ra_minus_8() -> list[int]:
    return [
        lw(_T9, -8, _RA),           # the call instruction as the game last patched it
        sll(_T9, _T9, 6),
        srl(_T9, _T9, 4),           # target & 0x0FFFFFFC
        lui(_T7, 0x8000),
        or_(_T9, _T9, _T7),
        jr(_T9),
        0,
    ]


def module_thunk(module: int, site_offset: int) -> list[int]:
    table_hi, table_lo = hi_lo(DESC_TABLE_PTR)
    ret_hi, ret_lo = hi_lo(site_offset + 8)
    return [
        lui(_T6, table_hi), lw(_T6, table_lo, _T6),  # descriptor table
        lw(_T6, module * 32, _T6),                   # where the calling module is loaded
        lui(_T7, ret_hi), addiu(_T7, _T7, ret_lo),
        addu(_RA, _T6, _T7),                         # $ra = call site + 8, in RAM
    ] + _follow_call_at_ra_minus_8()


def main_thunk(site_vram: int) -> list[int]:
    ret_hi, ret_lo = hi_lo(site_vram + 8)
    return [lui(_RA, ret_hi), addiu(_RA, _RA, ret_lo)] + _follow_call_at_ra_minus_8()
```

- [ ] **Step 4: Run thunk tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_thunks.py`
Expected: 3 passed.

- [ ] **Step 5: Add `read_main_imports_rom` to `layout.py`**

Append to `tools/romtool/romtool/layout.py`:
```python
def read_main_imports_rom(rom: bytes) -> int:
    """ROM address of the main segment's import table (u32 count, then 8-byte relocation entries)."""
    return _hi_lo(rom, 0x80053A94, 0x80053A98, _T4)
```
and to `tools/romtool/tests/test_layout.py`:
```python
def test_real_main_imports_rom(rom):
    from romtool.layout import read_main_imports_rom
    assert read_main_imports_rom(rom) == 0x1ECF220
```

- [ ] **Step 6: Write the failing ROM-backed build tests**

`tools/romtool/tests/test_build_rom.py`:
```python
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


def _program(rom):
    return build_program(rom, load_overrides(ROOT / "recomp/function_overrides.toml"))


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
    original = rom[main.rom_of(main.vram_start):main.rom_of(main.bss_start)]
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
```

- [ ] **Step 7: Run to verify failure**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_build_rom.py tools/romtool/tests/test_layout.py`
Expected: FAIL — `No module named 'romtool.build'`; `test_real_main_imports_rom` passes.

- [ ] **Step 8: Implement `build.py`**

`tools/romtool/romtool/build.py`:
```python
"""Turn the ROM into the sections, symbols, relocations, thunks and hooks N64Recomp needs."""
import struct
from dataclasses import dataclass

from .elf import FuncSym, Rel, Section
from .functions import Function, Overrides, find_all_functions
from .layout import MainSegment, read_main_imports_rom, read_main_segment, read_module_tables, read_word
from .loader_ref import STUB_DATA
from .mips import hi_lo, jal
from .modules import (MAIN, Module, RawReloc, RelocKind, RelocType, Target, read_modules, read_relocs,
                      read_symbols, resolve, resolve_global)
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
    target: Target  # what the game links the call to once everything is loaded


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
        word = lambda off: struct.unpack_from(">I", image, off)[0]  # noqa: E731

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
    main_image = bytearray(rom[main.rom_of(main.vram_start):main.rom_of(main.bss_start)])
    hooks: list[Hook] = []
    imports = _read_main_imports(rom, main)
    main_funcs = [f for f in functions if f.section == MAIN]

    def containing(vram: int) -> Function:
        for f in main_funcs:
            if f.start <= vram < f.end:
                return f
        raise RomError(f"main import site {vram:#x} is not inside any function")

    k = 0
    while k < len(imports):
        site, rtype, word0 = imports[k]
        target = resolve_global(symbols, word0, main, len(modules))
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
    thunk_sizes = [b.vram - a.vram for a, b in zip(thunks, thunks[1:])] + [THUNK_VRAM + len(thunk_blob) - thunks[-1].vram]
    funcs += [FuncSym(t.name, ".thunks", t.vram, size) for t, size in zip(thunks, thunk_sizes)]
    return Program(sections, funcs, rels, thunks, hooks, bases, main.vram_start)
```

- [ ] **Step 9: Implement `elf_relocate.py`**

`tools/romtool/romtool/elf_relocate.py`:
```python
"""Generic relocation of a linked module image to new load addresses, the way N64Recomp's runtime
macros (RELOC_HI16/LO16, relocated jal) and the game's own data relocation behave."""
import struct

from .build import Program, section_name


def relocate_module(program: Program, module_index: int, bases: dict[int, int]) -> bytes:
    name = section_name(module_index)
    section = next(s for s in program.sections if s.name == name)
    image = bytearray(section.data)
    link = {section_name(i): b for i, b in program.link_base.items()}
    delta = {sec: bases[int(sec[2:])] - b for sec, b in link.items()}
    rels = [r for r in program.rels if r.section == name]
    word = lambda off: struct.unpack_from(">I", image, off)[0]  # noqa: E731
    i = 0
    while i < len(rels):
        r = rels[i]
        off, d = r.vram - section.vram, delta[r.target]
        w = word(off)
        if r.type == 2:
            struct.pack_into(">I", image, off, (w + d) & 0xFFFFFFFF)
        elif r.type == 4:
            struct.pack_into(">I", image, off, (w & 0xFC000000) | (((w & 0x3FFFFFF) + (d >> 2)) & 0x3FFFFFF))
        elif r.type == 5:
            lo_off = rels[i + 1].vram - section.vram
            lo = word(lo_off)
            full = ((w & 0xFFFF) << 16) + (((lo & 0xFFFF) ^ 0x8000) - 0x8000) + d
            full &= 0xFFFFFFFF
            struct.pack_into(">H", image, off + 2, ((full >> 16) + ((full >> 15) & 1)) & 0xFFFF)
            struct.pack_into(">H", image, lo_off + 2, full & 0xFFFF)
            i += 2
            continue
        elif r.type == 6:
            struct.pack_into(">H", image, off + 2, ((w & 0xFFFF) + d) & 0xFFFF)
        i += 1
    return bytes(image)
```

- [ ] **Step 10: Run all romtool tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool`
Expected: all pass. If `test_roundtrip_matches_game_linker` fails, the message names seed, module and offset; compare the two words, find which side mis-implements the game's rule (the game's code in Task 4's header is the authority), fix, re-run.

- [ ] **Step 11: Commit**

```bash
git add tools/romtool
git commit -m "romtool: link layout, dynamic-call thunks, main import hooks, round-trip check

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: CLI and generated recompiler inputs

**Files:**
- Create: `tools/romtool/romtool/cli.py`, `tools/romtool/tests/test_cli.py`, `recomp/jfg.toml.in`
- Modify: `tools/romtool/pyproject.toml` (add `xxhash>=3` to the `test` extra and a new `hash` extra)

**Interfaces:**
- Consumes: `load_rom`, `build_program`, `write_elf`, `load_overrides`, `run_n64sym`, `Program`, `Hook`.
- Produces: console script `romtool` with subcommands:
  - `romtool verify ROM` → prints `OK: Jet Force Gemini (USA) [!]`, exit 0; on `RomError` prints the message, exit 1
  - `romtool info ROM` → layout summary and `rom_hash (XXH3-64): 0x…` (requires xxhash)
  - `romtool build-elf ROM --out DIR [--overrides FILE] [--template FILE] [--n64sym PATH]` → writes `DIR/jfg.elf`, `DIR/relocatable_sections.txt`, `DIR/jfg.toml`, `DIR/thunks.txt`, prints counts
  - `romtool disasm ROM --section N --address A [--count C]`
- `generated/jfg.toml` = `recomp/jfg.toml.in` with `@GENERATED@` replaced by the absolute output dir, plus one `[[patches.hook]]` per `Hook`.

- [ ] **Step 1: Create the N64Recomp config template**

`recomp/jfg.toml.in`:
```toml
# N64Recomp configuration for Jet Force Gemini (USA).
# romtool build-elf replaces @GENERATED@ and appends generated [[patches.hook]] entries.

[input]
entrypoint = 0x80000400
elf_path = "@GENERATED@/jfg.elf"
output_func_path = "@GENERATED@/funcs"
relocatable_sections_path = "@GENERATED@/relocatable_sections.txt"
recomp_include = "#include \"recomp.h\"\n#include \"jfg_hooks.h\""
functions_per_output_file = 50

[patches]
stubs = []
ignored = []

# Register a module with the runtime whenever the game DMAs its code into RAM (all load paths use this).
[[patches.hook]]
func = "dma_copy"
text = "jfg_on_dma(rdram, (uint32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6);"

# Unregister a module just before the game marks it unloaded ($s3 holds the module index).
[[patches.hook]]
func = "module_unload"
before_vram = 0x800537E0
text = "jfg_on_module_unload(rdram, (uint32_t)ctx->r19);"

# Debug visibility into the game's lazy linker.
[[patches.hook]]
func = "lazy_link"
text = "jfg_on_lazy_link(rdram, (uint32_t)ctx->r31);"
```
Add to `recomp/function_overrides.toml` (named here so n64sym can never rename a hooked function):
```toml
[[name]]
section = 0
address = 0x8004D640
name = "dma_copy"

[[name]]
section = 0
address = 0x8005372C
name = "module_unload"

[[name]]
section = 0
address = 0x80054530
name = "lazy_link"
```

- [ ] **Step 2: Write the failing CLI tests**

`tools/romtool/tests/test_cli.py`:
```python
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def romtool(*args):
    return subprocess.run([sys.executable, "-m", "romtool.cli", *args], capture_output=True, text=True)


def test_verify_rejects_garbage(tmp_path):
    bad = tmp_path / "bad.z64"
    bad.write_bytes(bytes.fromhex("80371240") + bytes(60))
    r = romtool("verify", str(bad))
    assert r.returncode == 1 and "does not match" in r.stderr


def test_verify_real(rom):
    r = romtool("verify", str(ROOT / "jfg.z64"))
    assert r.returncode == 0 and "OK" in r.stdout


def test_build_elf(rom, tmp_path):
    r = romtool("build-elf", str(ROOT / "jfg.z64"), "--out", str(tmp_path),
                "--overrides", str(ROOT / "recomp/function_overrides.toml"),
                "--template", str(ROOT / "recomp/jfg.toml.in"))
    assert r.returncode == 0, r.stderr
    for name in ("jfg.elf", "relocatable_sections.txt", "jfg.toml", "thunks.txt"):
        assert (tmp_path / name).exists()
    listed = (tmp_path / "relocatable_sections.txt").read_text().split()
    assert listed[0] == ".m001" and len(listed) == 157
    toml = (tmp_path / "jfg.toml").read_text()
    assert "@GENERATED@" not in toml and toml.count("[[patches.hook]]") == 3 + 2
```

- [ ] **Step 3: Run to verify failure**

Run: `tools/romtool/.venv/bin/pip install -q -e 'tools/romtool[test,disasm]' && tools/romtool/.venv/bin/pytest -q tools/romtool/tests/test_cli.py`
Expected: FAIL — `No module named romtool.cli`.

- [ ] **Step 4: Implement `cli.py`**

`tools/romtool/romtool/cli.py`:
```python
"""Command line: verify / info / build-elf / disasm."""
import argparse
import struct
import sys
from pathlib import Path

from .build import Program, build_program, section_name
from .elf import write_elf
from .functions import Overrides, load_overrides
from .layout import read_main_segment, read_module_tables
from .modules import read_modules
from .rom import RomError, load_rom


def _with_libultra(rom: bytes, overrides: Overrides, n64sym: str | None) -> Overrides:
    if not n64sym:
        return overrides
    from .libultra import run_n64sym
    names = {(0, a): n for a, n in run_n64sym(rom, read_main_segment(rom), n64sym).items()}
    return Overrides(overrides.add_start, overrides.merge, names | overrides.names, overrides.hardware)


def _hook_toml(program: Program) -> str:
    out = []
    for h in program.hooks:
        out.append(
            "[[patches.hook]]\n"
            f'func = "{h.function}"\n'
            f"before_vram = {h.before_vram:#010x}\n"
            f'text = "ctx->r{h.register} = (uint64_t)(int64_t)(int32_t)(jfg_patched_hi_lo(rdram, '
            f'{h.hi_site:#010x}, {h.lo_site:#010x}) - ({h.static_lo}));"\n'
        )
    return "\n".join(out)


def cmd_verify(args) -> int:
    load_rom(args.rom)
    print("OK: Jet Force Gemini (USA) [!]")
    return 0


def cmd_info(args) -> int:
    rom = load_rom(args.rom)
    main, tables = read_main_segment(rom), read_module_tables(rom)
    print(f"main: {main}")
    print(f"modules: {tables.module_count}, symbols: {tables.symbol_count}")
    import xxhash
    print(f"rom_hash (XXH3-64): {xxhash.xxh3_64_intdigest(rom):#018x}")
    return 0


def cmd_build_elf(args) -> int:
    rom = load_rom(args.rom)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    overrides = _with_libultra(rom, load_overrides(args.overrides), args.n64sym)
    program = build_program(rom, overrides)
    write_elf(out / "jfg.elf", program.sections, program.funcs, program.rels, program.entry)
    modules = [section_name(i) for i in sorted(program.link_base)]
    (out / "relocatable_sections.txt").write_text("\n".join(modules) + "\n")
    template = Path(args.template).read_text().replace("@GENERATED@", str(out))
    (out / "jfg.toml").write_text(template + "\n" + _hook_toml(program))
    (out / "thunks.txt").write_text("".join(
        f"{t.vram:08X} {t.name} -> section {t.target.module} + {t.target.offset:#x}\n" for t in program.thunks))
    print(f"wrote {out}: {len(program.funcs)} functions, {len(program.rels)} relocations, "
          f"{len(program.thunks)} thunks, {len(program.hooks)} hooks")
    return 0


def cmd_disasm(args) -> int:
    import rabbitizer
    rom = load_rom(args.rom)
    if args.section == 0:
        start, vram = read_main_segment(rom).rom_of(args.address), args.address
    else:
        m = read_modules(rom, read_module_tables(rom))[args.section - 1]
        start, vram = m.rom + args.address, args.address
    for i in range(args.count):
        w = struct.unpack_from(">I", rom, start + 4 * i)[0]
        print(f"{vram + 4 * i:08X} {w:08X}  {rabbitizer.Instruction(w, vram + 4 * i).disassemble()}")
    return 0


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[3]
    p = argparse.ArgumentParser(prog="romtool")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("verify", "info"):
        s = sub.add_parser(name)
        s.add_argument("rom")
    b = sub.add_parser("build-elf")
    b.add_argument("rom")
    b.add_argument("--out", required=True)
    b.add_argument("--overrides", default=str(root / "recomp/function_overrides.toml"))
    b.add_argument("--template", default=str(root / "recomp/jfg.toml.in"))
    b.add_argument("--n64sym", default=None)
    d = sub.add_parser("disasm")
    d.add_argument("rom")
    d.add_argument("--section", type=int, default=0)
    d.add_argument("--address", type=lambda s: int(s, 0), required=True)
    d.add_argument("--count", type=int, default=32)
    args = p.parse_args(argv)
    try:
        return {"verify": cmd_verify, "info": cmd_info, "build-elf": cmd_build_elf, "disasm": cmd_disasm}[args.cmd](args)
    except RomError as e:
        print(f"romtool: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
```
Add to `pyproject.toml`: in `[project.optional-dependencies]` set `test = ["pytest>=8", "pyelftools>=0.31", "xxhash>=3"]` and add `hash = ["xxhash>=3"]`. Reinstall: `tools/romtool/.venv/bin/pip install -q -e 'tools/romtool[test,disasm]'`.

- [ ] **Step 5: Run tests**

Run: `tools/romtool/.venv/bin/pytest -q tools/romtool`
Expected: all pass.

- [ ] **Step 6: Generate the real inputs**

```bash
source tools/paths.env
tools/romtool/.venv/bin/romtool build-elf jfg.z64 --out generated --n64sym "$N64SYM"
tools/romtool/.venv/bin/romtool info jfg.z64
```
Expected: `wrote …/generated: N functions, M relocations, 1028 thunks, 2 hooks`, and the XXH3 hash (save it for Task 12).

- [ ] **Step 7: Commit**

```bash
git add tools/romtool recomp
git commit -m "romtool: CLI and generated N64Recomp config

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Phase 4 — Recompile and run

### Task 11: Recompile with N64Recomp (warnings are errors)

**Files:**
- Create: `scripts/recompile.sh`, `recomp/recompile_allowlist.txt`
- Modify: `recomp/function_overrides.toml`, `recomp/jfg.toml.in` (as problems are fixed)

**Interfaces:**
- Consumes: `generated/jfg.toml`, `build/tools/N64Recomp/N64Recomp`.
- Produces: `generated/funcs/*.c`, `generated/funcs/funcs.h`, `generated/funcs/recomp_overlays.inl`, `generated/funcs/lookup.cpp`; `scripts/recompile.sh` (exit 0 only when output is clean).

- [ ] **Step 1: Write the wrapper script**

`scripts/recompile.sh`:
```bash
#!/usr/bin/env bash
# Regenerate everything from the ROM and recompile. Any N64Recomp warning or error that isn't
# allow-listed fails the run.
set -euo pipefail
cd "$(dirname "$0")/.."
source tools/paths.env
ROM="${JFG_ROM:-jfg.z64}"
tools/romtool/.venv/bin/romtool verify "$ROM"
tools/romtool/.venv/bin/romtool build-elf "$ROM" --out generated --n64sym "$N64SYM"
rm -rf generated/funcs && mkdir -p generated/funcs
"$N64RECOMP" generated/jfg.toml > generated/recomp.log 2>&1 || { tail -50 generated/recomp.log; exit 1; }
problems=$(grep -E '\[WARN\]|[Ee]rror|Unhandled|Could not|No function found' generated/recomp.log \
           | grep -vxF -f <(grep -v '^#' recomp/recompile_allowlist.txt | sed '/^$/d') || true)
if [[ -n "$problems" ]]; then
  echo "N64Recomp reported problems:"; echo "$problems"; exit 1
fi
echo "recompiled: $(ls generated/funcs/*.c | wc -l) C files"
```
`recomp/recompile_allowlist.txt`:
```
# Exact N64Recomp output lines that are accepted. Each needs a comment above it explaining why.
```
Run: `chmod +x scripts/recompile.sh`

- [ ] **Step 2: Run it**

Run: `scripts/recompile.sh`
Expected first time: a list of problems, or success.

- [ ] **Step 3: Resolve every problem, one class at a time**

For each reported line:
- *Unhandled branch / jump table out of function / No function found for jal target* → a function-boundary error. Inspect with `tools/romtool/.venv/bin/romtool disasm jfg.z64 --section S --address A`; fix with `[[merge]]`/`[[add_start]]` (with reason) in `recomp/function_overrides.toml`, or fix the scanner if it's a systematic rule; re-run `tools/romtool/.venv/bin/pytest -q tools/romtool` then `scripts/recompile.sh`.
- *Unpaired LO16 / HI16 follows HI16 with different symbol* → check the module's original entry order in `read_relocs`; if the game's table genuinely has that shape and `test_roundtrip_matches_game_linker` passes, allow-list the exact line with a comment.
- *Undefined symbol / unknown section* → bug in `build.py`/`elf.py`: write a failing romtool test reproducing it first, then fix.
Loop until `scripts/recompile.sh` prints `recompiled: N C files` and exits 0.

- [ ] **Step 4: Check section indices match modules**

Run: `grep -n "overlay_sections_by_index" -A 3 generated/funcs/recomp_overlays.inl | head`; `grep -c "rom_addr" generated/funcs/recomp_overlays.inl`
Expected: 157 overlay sections listed in module order; section_table rom_addr values ascending and equal to module ROM addresses (`0x1ED3B20`, …).

- [ ] **Step 5: Commit**

```bash
git add scripts/recompile.sh recomp
git commit -m "Recompile JFG with N64Recomp; warnings are errors

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Frontend — JFG game entry, module registration, hardware patches

**Files:**
- Create: `include/jfg_hooks.h`, `src/jfg/jfg_hooks.cpp`
- Modify: `CMakeLists.txt`, `src/main/main.cpp`, `src/main/register_overlays.cpp`, `src/ui/ui_launcher.cpp` (title/strings), `recomp/jfg.toml.in` (hardware patches)
- Remove (MM-specific, from Task 6 research): `patches/` MM sources and its CMake targets, `us.rev1.toml`, `overlays.us.rev1.txt`, `rev1_overlays.txt`, `aspMain.us.rev1.toml`, `njpgdspMain.us.rev1.toml`, `src/game/{rom_decompression,scene_table,quicksaving,debug,recomp_actor_api,recomp_data_api}.cpp`, `include/zelda_*.h`, `mods/mm_recomp_dpad_builtin.nrm`, `assets/mm-clipped.svg`, `config_example.cheats*.json`, MM flatpak ids

**Interfaces:**
- Consumes: generated `funcs/` (Task 11); XXH3 ROM hash (Task 10 Step 6); runtime API `extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size)`, `extern "C" void unload_overlays(int32_t ram_addr, uint32_t size)` (librecomp `overlays.hpp`).
- Produces: build target `JFGRecompiled` (binary `build/JFGRecompiled`); C-callable `jfg_on_dma`, `jfg_on_module_unload`, `jfg_on_lazy_link`, `jfg_patched_hi_lo` (declared in `include/jfg_hooks.h`, used by generated code via `recomp_include`).

Game memory used by the hooks (from the spikes): `0x800FEAA0` = pointer to the descriptor table (32 bytes per entry, entry 0 = main), `0x800FEAAC` = number of entries; entry `+0` load address, `+4` absolute ROM address, `+8` text size, `+0xC` data size.

- [ ] **Step 1: Write the hook header and implementation**

`include/jfg_hooks.h`:
```c
#ifndef JFG_HOOKS_H
#define JFG_HOOKS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called at the start of the game's DMA routine: registers a module when its code is copied in. */
void jfg_on_dma(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size);
/* Called just before the game marks module `index` unloaded. */
void jfg_on_module_unload(uint8_t* rdram, uint32_t index);
/* Called on entry to the game's lazy linker; `ra` is the return address of the call being linked. */
void jfg_on_lazy_link(uint8_t* rdram, uint32_t ra);
/* The address the game has patched into a lui/LO16 pair in RAM. */
uint32_t jfg_patched_hi_lo(uint8_t* rdram, uint32_t hi_site, uint32_t lo_site);
#ifdef __cplusplus
}
#endif
#endif
```

`src/jfg/jfg_hooks.cpp`:
```cpp
// Keeps N64ModernRuntime's idea of where each JFG code module is loaded in sync with the game's own
// module system, which loads, moves and unloads modules at will.
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include "jfg_hooks.h"
#include "librecomp/overlays.hpp"

namespace {
constexpr uint32_t desc_table_ptr = 0x800FEAA0;
constexpr uint32_t desc_count = 0x800FEAAC;

uint32_t read_word(uint8_t* rdram, uint32_t vaddr) {
    // RDRAM is stored as native-endian 32-bit words, so aligned words read directly.
    return *reinterpret_cast<uint32_t*>(rdram + (vaddr - 0x80000000u));
}

struct Loaded { uint32_t ram; uint32_t size; };
std::unordered_map<uint32_t, Loaded> loaded_modules;
const bool debug = std::getenv("JFG_DEBUG_MODULES") != nullptr;
}

extern "C" void jfg_on_dma(uint8_t* rdram, uint32_t rom, uint32_t ram, uint32_t size) {
    uint32_t table = read_word(rdram, desc_table_ptr);
    if (table == 0) {
        return; // module system not initialised yet
    }
    uint32_t count = read_word(rdram, desc_count);
    for (uint32_t i = 1; i < count; i++) {
        uint32_t entry = table + i * 32;
        if (read_word(rdram, entry + 4) != rom || read_word(rdram, entry + 8) != size) {
            continue;
        }
        uint32_t image = read_word(rdram, entry + 8) + read_word(rdram, entry + 12);
        if (auto it = loaded_modules.find(i); it != loaded_modules.end()) {
            unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
            loaded_modules.erase(it);
        }
        load_overlays(rom, static_cast<int32_t>(ram), image);
        loaded_modules[i] = {ram, image};
        if (debug) std::fprintf(stderr, "[jfg] module %u loaded at %08X (%u bytes)\n", i, ram, image);
        return;
    }
}

extern "C" void jfg_on_module_unload(uint8_t*, uint32_t index) {
    auto it = loaded_modules.find(index);
    if (it == loaded_modules.end()) {
        return;
    }
    unload_overlays(static_cast<int32_t>(it->second.ram), it->second.size);
    if (debug) std::fprintf(stderr, "[jfg] module %u unloaded from %08X\n", index, it->second.ram);
    loaded_modules.erase(it);
}

extern "C" void jfg_on_lazy_link(uint8_t*, uint32_t ra) {
    if (debug) std::fprintf(stderr, "[jfg] lazy link for call at %08X\n", ra - 8);
}

extern "C" uint32_t jfg_patched_hi_lo(uint8_t* rdram, uint32_t hi_site, uint32_t lo_site) {
    uint32_t hi = read_word(rdram, hi_site) & 0xFFFF;
    uint32_t lo = read_word(rdram, lo_site) & 0xFFFF;
    return (hi << 16) + static_cast<int16_t>(lo);
}
```
If `librecomp/overlays.hpp` at the pinned runtime does not declare `load_overlays`/`unload_overlays` with `extern "C"`, declare them locally with the signatures above inside `extern "C" { }`.

- [ ] **Step 1b: Unit-test the hooks against a fake runtime**

`tests/fake/librecomp/overlays.hpp`:
```cpp
#pragma once
#include <cstdint>
extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);
```
`tests/jfg_hooks_test.cpp`:
```cpp
// Drives jfg_hooks.cpp with a fake RDRAM and records what it tells the runtime.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "jfg_hooks.h"

static std::vector<std::string> calls;
extern "C" void load_overlays(uint32_t rom, int32_t ram, uint32_t size) {
    char b[64]; std::snprintf(b, sizeof b, "load %X %X %X", rom, (uint32_t)ram, size); calls.push_back(b);
}
extern "C" void unload_overlays(int32_t ram, uint32_t size) {
    char b[64]; std::snprintf(b, sizeof b, "unload %X %X", (uint32_t)ram, size); calls.push_back(b);
}

static std::vector<uint8_t> rdram(0x400000);
static void w(uint32_t vaddr, uint32_t v) { std::memcpy(&rdram[vaddr - 0x80000000u], &v, 4); }

int main() {
    const uint32_t table = 0x80200000;
    w(0x800FEAA0, table); w(0x800FEAAC, 3);
    w(table + 32 * 2 + 4, 0x1F00000); w(table + 32 * 2 + 8, 0x800); w(table + 32 * 2 + 12, 0x100);
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80300000, 0x800);          // load module 2
    jfg_on_dma(rdram.data(), 0x1F00000 + 0x900, 0x80310000, 0x40);   // its relocation table: ignored
    jfg_on_dma(rdram.data(), 0x1F00000, 0x80340000, 0x800);          // reload_at_new_address
    jfg_on_module_unload(rdram.data(), 2);
    jfg_on_module_unload(rdram.data(), 2);                            // second unload: no-op
    std::vector<std::string> want = {"load 1F00000 80300000 900", "unload 80300000 900",
                                     "load 1F00000 80340000 900", "unload 80340000 900"};
    for (auto& c : calls) std::puts(c.c_str());
    assert(calls == want);
    w(0x80001000, 0x3C018041); w(0x80001004, 0x8C238000);             // lui at,0x8041 ; lw v1,-0x8000(at)
    assert(jfg_patched_hi_lo(rdram.data(), 0x80001000, 0x80001004) == 0x80408000);
    std::puts("jfg_hooks_test: OK");
}
```
`scripts/test-hooks.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
clang++ -std=c++20 -Wall -Iinclude -Itests/fake src/jfg/jfg_hooks.cpp tests/jfg_hooks_test.cpp -o build/jfg_hooks_test
build/jfg_hooks_test
```
Run: `chmod +x scripts/test-hooks.sh && scripts/test-hooks.sh`
Expected: `jfg_hooks_test: OK`. (Write the test first and run it before Step 1's implementation exists to see it fail to link.)

- [ ] **Step 2: Remove Majora's Mask specifics**

Delete the files listed under **Remove** with `git rm`. Then `grep -rn "zelda\|Zelda\|majora\|mm_\|MM_" CMakeLists.txt src include | grep -v '^Binary'` and fix every remaining reference: rename the CMake target to `JFGRecompiled`, drop MM sources and the patches build (`PatchesLib`, `patches.toml` steps — the base port needs no MIPS patches; sub-project 4 will reintroduce the pipeline), point `RecompiledFuncs` at `generated/funcs/*.c`, add `src/jfg/jfg_hooks.cpp` and `include/` to the sources/includes, and generate RSP code from `generated/rsp/` (Task 13).

- [ ] **Step 3: Game entry**

In `src/main/main.cpp`, replace the MM `GameEntry` with (field names per the pinned runtime's `librecomp/include/librecomp/game.hpp`; omit fields that don't exist there):
```cpp
recomp::GameEntry jfg_game {
    .rom_hash = 0x<XXH3 from Task 10 Step 6>ULL,
    .internal_name = "JET FORCE GEMINI",
    .game_id = u8"jfg.n64.us",
    .save_type = recomp::SaveType::Flashram,
    .is_enabled = true,
    .entrypoint_address = 0x80000400,
    .entrypoint = recomp_entrypoint,
};
```
`recomp_entrypoint` is the function N64Recomp generated for the STT_FUNC at ROM 0x1000 (check `generated/funcs/funcs.h` for its exact name, e.g. `entrypoint`, and reference that). Update window title / launcher strings to "Jet Force Gemini" in `src/ui/ui_launcher.cpp` and assets text. Keep `register_overlays.cpp` including `generated/funcs/recomp_overlays.inl`.

- [ ] **Step 4: Microcode mapping**

In `get_rsp_microcode` (`src/main/main.cpp`), map `M_AUDTASK` to the recompiled JFG audio function from Task 13 (`aspMain`), keep graphics tasks going to RT64, and remove the MM JPEG mapping.

- [ ] **Step 5: Hardware patches from the audit**

For every `[[hardware]] decision = "patch"` entry from Task 7, add the replacement to `recomp/jfg.toml.in` as a hook that performs the operation natively and returns:
```toml
[[patches.hook]]
func = "<function name>"
text = "jfg_<what_it_does>(rdram, ctx); return;"
```
with `jfg_<what_it_does>` implemented in `src/jfg/jfg_hooks.cpp` (declared in `include/jfg_hooks.h`) using the runtime's equivalent (e.g. Flash RAM → the functions in `librecomp/src/flash.cpp`; a CIC-6105 check → set the result register the way real hardware would answer). Write each one's expected behaviour in a comment citing the disassembly address. Functions that only need to do nothing go into `[patches] stubs` instead.

- [ ] **Step 6: Build**

```bash
scripts/recompile.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target JFGRecompiled
```
Expected: links `build/JFGRecompiled`. Fix compile errors in our code; for errors inside generated code, trace back to the romtool/N64Recomp input that caused them (never hand-edit `generated/`).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Frontend: JFG game entry, module registration hooks, hardware patches

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Audio microcode via RSPRecomp

**Files:**
- Create: `recomp/aspMain.jfg.toml`
- Modify: `scripts/recompile.sh` (also run RSPRecomp), `CMakeLists.txt` (compile `generated/rsp/aspMain.cpp`)

**Interfaces:**
- Consumes: audio microcode location (spike): text at vram `0x8009DA10` (ROM `0x9E610`), initial IMEM load `0x1000` bytes, remaining `0x840` bytes up to the boot microcode at `0x8009F250` (an IMEM overlay, likely the MP3 decoder); data at `0x800AFC10`, `0x800` bytes. Emulator HLE identifies this microcode as the n_audio + MP3 family shared with Banjo-Tooie / Perfect Dark.
- Produces: `generated/rsp/aspMain.cpp` defining `aspMain`, referenced by Task 12 Step 4.

- [ ] **Step 1: Find the IMEM load address and overlay DMAs**

Disassemble the boot microcode (vram `0x8009F250`, `0x180` bytes) and the audio text as RSP code:
```bash
tools/romtool/.venv/bin/python -I -c "
import struct, rabbitizer, sys
rom = open('jfg.z64','rb').read(); start, n, base = int(sys.argv[1],16), int(sys.argv[2],16), int(sys.argv[3],16)
for i in range(0, n, 4):
    w = struct.unpack_from('>I', rom, start + i)[0]
    print(f'{base+i:08X} {w:08X} {rabbitizer.InstructionRsp(w, base+i).disassemble()}')" 0x9FE50 0x180 0x04001000
```
In the boot code, find the DMA that copies the task's ucode into IMEM (`mtc0` to `SP_MEM_ADDR` = `$c0`) — its IMEM destination is `text_address` (standard is `0x04001080`). Then disassemble the audio text (`0x9E610`, `0x1000`, that address) and find any `mtc0 … $c0` whose value has bit 12 set (IMEM destination): each is an overlay load; note its IMEM slot address and the DRAM offset it copies from (relative to the ucode text start).

- [ ] **Step 2: Write the RSPRecomp config**

`recomp/aspMain.jfg.toml`:
```toml
text_offset = 0x9E610
text_size = 0x1000
text_address = 0x04001080           # replace with the value found in Step 1 if different
rom_file_path = "../jfg.z64"
output_file_path = "../generated/rsp/aspMain.cpp"
output_function_name = "aspMain"
# If Step 1 found IMEM overlay loads, describe them:
# [[overlay_slots]]
# text_address = 0x<slot IMEM address>
# overlays = [ { offset = 0x<offset into text>, size = 0x<size> } ]
```
Uncomment and fill `[[overlay_slots]]` only with the values found in Step 1.

- [ ] **Step 3: Run RSPRecomp**

Add to `scripts/recompile.sh` before the final `echo`:
```bash
mkdir -p generated/rsp
(cd recomp && "../$RSPRECOMP" aspMain.jfg.toml) > generated/rsp.log 2>&1 || { cat generated/rsp.log; exit 1; }
```
Run: `scripts/recompile.sh`
Expected: `generated/rsp/aspMain.cpp` exists. If RSPRecomp reports unhandled indirect branch targets, add them to `extra_indirect_branch_targets = [...]` (values printed in its message) and re-run; if it reports unsupported instructions, list them in `unsupported_instructions` only after confirming they're unreachable.

- [ ] **Step 4: Build and commit**

Add `generated/rsp/aspMain.cpp` to the build in `CMakeLists.txt`, rebuild (`cmake --build build --target JFGRecompiled`), then:
```bash
git add recomp scripts CMakeLists.txt
git commit -m "Recompile JFG's audio microcode with RSPRecomp

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: RT64 support for JFG's graphics microcode

**Files:**
- Create: `lib-patches/rt64/0001-jfg-gbi.patch` (exported from work done inside `lib/rt64`), `scripts/apply-lib-patches.sh`
- Work inside `lib/rt64`: `src/gbi/rt64_gbi.h` (enum), `src/gbi/rt64_gbi.cpp` (database entries), new `src/gbi/rt64_gbi_f3ddkr.{h,cpp}`, `src/hle/rt64_rsp.{h,cpp}` (DKR vertex/matrix state), `CMakeLists.txt` (new sources)

**Interfaces:**
- Consumes: graphics microcode location — boot `0x8009F250`, gfx text from `0x8009F3D0` (task `ucode` field; confirm from the graphics task setup near `0x8004DBE0`), gfx data `0x800B0350`.
- Produces: RT64 recognises JFG's microcode as new family `GBIUCode::F3DJFG` and renders it.

Facts to build on (Task 6 research):
- RT64 picks a GBI family by XXH3-64 of RDRAM at the task's ucode text and ucode data over sizes from fixed tables; an unknown pair prints `Unable to find a matching GBI` and then dereferences null. A database entry whose hash is `0xFFFFFFFFFFFFFFFF` makes RT64 print `HASH: 0x… SIZE: 0x…` for that size — use this to obtain JFG's hashes.
- Families are small setup functions layered on `GBI_F3D::setup` that override `gbi->map[opcode]` entries; `rt64_gbi_f3dpd.cpp` (Perfect Dark, another Rare microcode) is the model, using `state->rsp->setVertexPD(...)`-style helpers.
- JFG's microcode is Diddy Kong Racing's F3D variant with one change (GLideN64 `src/uCodes/F3DDKR.cpp`, `F3DJFG_Init` = `F3DDKR_Init` + a `G_DMA_VTX` that uses the vertex count without `+1`). DKR additions over F3D: `G_DMA_MTX 0x01`, `G_DMA_TEX_OFFSET 0x02`, `G_DMA_VTX 0x04`, `G_DMA_TRI 0x05`, `G_DMA_DL 0x07`, `G_DMA_OFFSETS 0xBF`, and `G_MOVEWORD` cases for matrix index and billboarding. Read GLideN64's implementation for exact bit layouts and semantics; write the RT64 version independently against RT64's APIs (different code base and licence).

- [ ] **Step 1: Confirm the failure**

Run: `JFG_DEBUG_MODULES=1 timeout 30 build/JFGRecompiled 2>&1 | tee build/run1.log | grep -m3 -E "GBI|HASH|Detected name"`
Expected: `Unable to find a matching GBI …`. (If the ROM is not yet stored by the launcher, pick `jfg.z64` in the launcher UI once; ask the user to do this if no display is available to this shell.)

- [ ] **Step 2: Get JFG's hashes**

In `lib/rt64/src/gbi/rt64_gbi.cpp`, add a temporary database instance for JFG whose text and data hashes are `0xFFFFFFFFFFFFFFFF` at sizes covering JFG's ucode (text `0x1000`, data `0x800`, following the existing entries' structure), rebuild, run as in Step 1, and record the printed `HASH`/`SIZE` pairs. Replace the temporary values with the real ones.

- [ ] **Step 3: Add the family**

1. `rt64_gbi.h`: add `F3DDKR` and `F3DJFG` to `enum class GBIUCode`.
2. `rt64_gbi_f3ddkr.{h,cpp}`: namespace `GBI_F3DDKR` with `setup(GBI*)` = `GBI_F3D::setup` + handlers for the DKR commands listed above; `GBI_F3DJFG::setup` = `GBI_F3DDKR::setup` with the JFG `G_DMA_VTX` variant.
3. `rt64_rsp.{h,cpp}`: the per-display-list state DKR needs (indexed matrix slots selected by `G_DMA_MTX`/`G_MOVEWORD`, the vertex/matrix address offsets set by `G_DMA_OFFSETS`, billboard mode, DKR's packed vertex format for `G_DMA_VTX`, and `G_DMA_TRI`'s triangle format with per-triangle texture coordinates).
4. Map the JFG hash entry to `GBIUCode::F3DJFG` and dispatch it to `GBI_F3DJFG::setup` wherever other families are dispatched.

- [ ] **Step 4: Iterate against a reference**

Run the port and ares side by side (`ares jfg.z64`) and compare: Rare logo, intro, title, file select, first Goldwood room. Fix one visual difference at a time (missing geometry → vertex/triangle decoding; wrong placement → matrix slots/offsets; sprites facing wrong way → billboarding; wrong textures → texture offset/coords). Screenshots go in `build/compare/` (not committed).

- [ ] **Step 5: Export the patch and make it reproducible**

```bash
git -C lib/rt64 add -A && git -C lib/rt64 -c user.name=jfg -c user.email=jfg@localhost commit -qm "Add JFG (DKR-family) GBI"
mkdir -p lib-patches/rt64 && git -C lib/rt64 format-patch -1 --stdout > lib-patches/rt64/0001-jfg-gbi.patch
```
`scripts/apply-lib-patches.sh`:
```bash
#!/usr/bin/env bash
# Apply our patches to submodules after `git submodule update`.
set -euo pipefail
cd "$(dirname "$0")/.."
for p in lib-patches/rt64/*.patch; do
  git -C lib/rt64 apply --check "../../$p" 2>/dev/null && git -C lib/rt64 apply "../../$p" || echo "already applied: $p"
done
```
Commit:
```bash
chmod +x scripts/apply-lib-patches.sh
git add lib-patches scripts/apply-lib-patches.sh
git commit -m "RT64: support JFG's DKR-family graphics microcode

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
(The submodule pointer for `lib/rt64` stays at upstream's commit; the patch file is the source of truth.)

---

### Task 15: Boot to gameplay, smoke test, debug logging

**Files:**
- Create: `scripts/smoke.sh`
- Modify: whatever the failures point to (romtool overrides, hooks, frontend)

**Interfaces:**
- Consumes: `build/JFGRecompiled`.
- Produces: `scripts/smoke.sh` — exit 0 when the port runs for 90 s without dying, loads modules, and logs no fatal runtime messages.

- [ ] **Step 1: Write the smoke script**

`scripts/smoke.sh`:
```bash
#!/usr/bin/env bash
# Boot the port for a fixed time and check it stayed alive and healthy.
set -uo pipefail
cd "$(dirname "$0")/.."
SECONDS_TO_RUN="${1:-90}"
LOG=build/smoke.log
JFG_DEBUG_MODULES=1 timeout "$SECONDS_TO_RUN" build/JFGRecompiled > "$LOG" 2>&1
status=$?
fail=0
[[ $status -eq 124 ]] || { echo "FAIL: exited early with status $status"; fail=1; }
grep -q '\[jfg\] module .* loaded' "$LOG" || { echo "FAIL: no modules loaded"; fail=1; }
if grep -E 'Failed to find function|Unable to find a matching GBI|Cannot partially unload|Unhandled|assert' "$LOG"; then
  echo "FAIL: fatal runtime message above"; fail=1
fi
echo "modules loaded: $(grep -c 'module .* loaded' "$LOG"), unloaded: $(grep -c 'unloaded' "$LOG"), lazy links: $(grep -c 'lazy link' "$LOG")"
exit $fail
```
`chmod +x scripts/smoke.sh`. This needs a display; if `$DISPLAY`/`$WAYLAND_DISPLAY` is unset in this shell, ask the user to run `! scripts/smoke.sh` and paste nothing — the log lands in `build/smoke.log` for inspection.

- [ ] **Step 2: Iterate until the smoke test passes**

Run `scripts/smoke.sh`. For each failure, read `build/smoke.log` and fix the root cause (use superpowers:systematic-debugging):
- `Failed to find function at 0x8xxxxxxx` → an address that should be a function isn't registered: if it's in a module's range, a load path bypassed `jfg_on_dma` (check the size/rom match and that `dma_copy` is the routine the loader calls) or a function boundary is missing (add start); if it's a main address, a missing function start.
- `Cannot partially unload` → our recorded size disagrees with the section size: compare `loaded_modules` vs `recomp_overlays.inl`.
- Crash with no message → run under `gdb -batch -ex run -ex bt build/JFGRecompiled`, map the generated function name back with `romtool disasm`.
Commit each fix separately with a message naming the symptom and cause.

- [ ] **Step 3: Get to Goldwood**

Play (or ask the user to play) through intro → file select → new game → Goldwood start with `JFG_DEBUG_MODULES=1` and fix whatever breaks the same way. Commit fixes.

---

### Task 16: Acceptance (user)

**Files:**
- Create: `docs/acceptance/2026-base-port.md` (checklist results)

- [ ] **Step 1: Hand the checklist to the user**

Give the user the spec §7 checklist (boot; saves persist across relaunch; Goldwood start to finish; graphics vs ares; audio; gamepad + keyboard + rumble; ≥30 min stability with no `Failed to find function`/crash) and how to run: `build/JFGRecompiled` (with `JFG_DEBUG_MODULES=1` when reporting problems; logs in `build/smoke.log` style).

- [ ] **Step 2: Fix reported issues**

Each issue: reproduce from the user's description/log, systematic debugging, fix, `scripts/recompile.sh && cmake --build build --target JFGRecompiled && scripts/smoke.sh`, commit.

- [ ] **Step 3: Record results and finish**

Write `docs/acceptance/2026-base-port.md` with each checklist item, pass/fail, date and notes; commit; then use superpowers:finishing-a-development-branch.
