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
    return unique_names(names)


def unique_names(names: dict[int, str]) -> dict[int, str]:
    """Drop names n64sym gave to more than one address: duplicate symbols would break N64Recomp and the
    match is ambiguous anyway. An override can still name the right one."""
    counts: dict[str, int] = {}
    for n in names.values():
        counts[n] = counts.get(n, 0) + 1
    return {a: n for a, n in names.items() if counts[n] == 1}


def runtime_handled_names(symbol_lists_cpp: str) -> set[str]:
    return set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', Path(symbol_lists_cpp).read_text()))
