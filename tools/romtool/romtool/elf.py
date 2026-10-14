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
