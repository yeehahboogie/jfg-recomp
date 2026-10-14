"""Find function boundaries in main code and in every module's text."""
import struct
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .layout import read_main_segment, read_module_tables
from .mips import JR_RA, branch_target, is_call, is_jal, is_jump, is_trap, jal_target
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
    exempt: frozenset = frozenset()  # (section, start): runtime-replaced assembly; body checks skipped
    hardware: dict = field(default_factory=dict)  # (section, address) -> decision ("patch" | "harmless")
    extend: dict = field(default_factory=dict)  # (section, start) -> end: function contains later ones


def load_overrides(path: str | Path) -> Overrides:
    data = tomllib.loads(Path(path).read_text())
    key = lambda e: (int(e["section"]), int(e["address"]))  # noqa: E731
    for e in data.get("extend", []):
        if not e.get("reason") or int(e["end"]) <= int(e["address"]):
            raise RomError(f"extend override {e} needs end > address and a reason")
    for e in data.get("hardware", []):
        if e.get("decision") not in ("patch", "harmless") or not e.get("reason"):
            raise RomError(f"hardware override {e} needs decision 'patch' or 'harmless' and a reason")
    return Overrides(
        add_start=frozenset(key(e) for e in data.get("add_start", [])),
        merge=frozenset(key(e) for e in data.get("merge", [])),
        names={key(e): e["name"] for e in data.get("name", [])},
        exempt=frozenset(key(e) for e in data.get("exempt", [])),
        hardware={key(e): e["decision"] for e in data.get("hardware", [])},
        extend={key(e): int(e["end"]) for e in data.get("extend", [])},
    )


def _words(code: bytes) -> list[int]:
    return list(struct.unpack(f">{len(code) // 4}I", code))


def split_functions(code: bytes, base: int, seeds: set[int], merge: set[int] = frozenset()) -> list[tuple[int, int]]:
    ws = _words(code)
    end_addr = base + 4 * len(ws)
    starts = {s for s in seeds if base <= s < end_addr} - set(merge)
    word = lambda a: ws[(a - base) // 4] if a < end_addr else 0  # noqa: E731
    padding = lambda w: w == 0 or is_trap(w)  # noqa: E731  (unreachable traps follow some returns)
    spans, current, furthest, pc = [], None, 0, base
    while pc < end_addr:
        w = word(pc)
        if current is None:
            if padding(w) and pc not in starts:
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
        if furthest <= pc + 4 and after <= end_addr:
            following = after  # where the next function would begin: past any zero padding
            while following < end_addr and padding(word(following)) and following not in starts:
                following += 4
            ends = w == JR_RA or (is_jump(w) and (padding(word(after)) or after in starts))
            if ends and following not in merge:
                spans.append((current, after))
                current = None
                pc = after
                continue
        pc += 4
    if current is not None:
        spans.append((current, end_addr))
    return spans


def _ends_in_jump(word, s: int, e: int) -> bool:
    last = max((pc for pc in range(s, e, 4) if word(pc) != 0), default=None)
    if last is None:
        return True
    return any(is_jump(word(pc)) or is_call(word(pc)) or is_trap(word(pc)) for pc in (last, last - 4) if pc >= s)


def extend_fallthroughs(code: bytes, base: int, spans: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """A function that runs straight into the next one (e.g. libultra coss → sins) is extended through it."""
    ws = _words(code)
    word = lambda a: ws[(a - base) // 4]  # noqa: E731
    out = list(spans)
    for i in range(len(out) - 2, -1, -1):
        s, e = out[i]
        if e == out[i + 1][0] and not _ends_in_jump(word, s, e):
            out[i] = (s, out[i + 1][1])
    return out


def apply_extensions(spans: list[tuple[int, int]], extend: dict[int, int]) -> list[tuple[int, int]]:
    """Hand-written routines that branch into helpers placed after them become one function containing them."""
    return [(s, max(e, extend.get(s, e))) for s, e in spans]


def check_invariants(code: bytes, base: int, spans: list[tuple[int, int]], call_targets: set[int],
                     exempt: set[int] = frozenset()) -> list[str]:
    ws = _words(code)
    word = lambda a: ws[(a - base) // 4]  # noqa: E731
    problems = []
    for i, (s0, e0) in enumerate(spans):
        for s1, e1 in spans[i + 1:]:
            if s1 >= e0:
                break
            if e1 > e0:  # a function may fully contain later ones (fall-through, [[extend]]), never half
                problems.append(f"functions {s0:#x} and {s1:#x} overlap")
    starts = {s for s, _ in spans}
    for t in sorted(call_targets):
        if t not in starts:
            problems.append(f"call target {t:#x} is not a function start")
    for s, e in spans:
        if s in exempt:
            continue
        for pc in range(s, e, 4):
            target = branch_target(word(pc), pc)
            if target is not None and not s <= target < e:
                problems.append(f"branch at {pc:#x} leaves function {s:#x} (to {target:#x})")
        if not _ends_in_jump(word, s, e):
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
            problems += [f"main: call target {t:#x} outside main code" for t in sorted(outside)]
        seeds = seeds - outside
        spans = extend_fallthroughs(code, base, split_functions(code, base, seeds, merge))
        spans = apply_extensions(spans, {a: e for (s, a), e in overrides.extend.items() if s == section})
        exempt = {a for s, a in overrides.exempt if s == section}
        problems += [f"section {section}: {p}" for p in check_invariants(code, base, spans, seeds, exempt)]
        for s, e in spans:
            default = f"func_{s:08X}" if section == MAIN else f"m{section:03d}_{s:05X}"
            functions.append(Function(section, s, e, overrides.names.get((section, s), default)))
    seen: dict[str, int] = {}
    for f in functions:
        seen[f.name] = seen.get(f.name, 0) + 1
    problems += [f"named more than once: {n}" for n, k in sorted(seen.items()) if k > 1]
    if problems:
        raise RomError("function discovery found problems:\n  " + "\n  ".join(problems))
    return functions
