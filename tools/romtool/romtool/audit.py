"""Find functions that touch hardware directly (COP0, cache ops, kseg1 / RCP / cartridge / PIF addresses)."""
import struct

from .mips import is_jump

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
        window = ws[i + 1:i + 9]
        for k, nxt in enumerate(window):
            if nxt >> 26 in _LOAD_STORE_OPS and (nxt >> 21) & 31 == reg:
                return [f"kseg1 access via lui {w & 0xFFFF:#06x}"]
            if _writes(nxt) == reg:
                break  # the register holds something else now (e.g. the lui was a float constant)
            if is_jump(nxt):
                slot = window[k + 1] if k + 1 < len(window) else 0
                if slot >> 26 in _LOAD_STORE_OPS and (slot >> 21) & 31 == reg:
                    return [f"kseg1 access via lui {w & 0xFFFF:#06x}"]
                break  # code after an unconditional jump is on another path
    return []


_WRITES_RT = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37}


def _writes(w: int) -> int | None:
    """The general-purpose register an instruction writes, if any (approximate; enough for the audit)."""
    op = w >> 26
    if op in _WRITES_RT:
        return (w >> 16) & 31
    if op == 0 and w:
        return (w >> 11) & 31
    if op == 0x11 and (w >> 21) & 31 in (0x00, 0x01, 0x02):  # mfc1 / dmfc1 / cfc1
        return (w >> 16) & 31
    return None


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
