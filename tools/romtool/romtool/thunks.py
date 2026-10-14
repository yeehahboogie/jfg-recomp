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
