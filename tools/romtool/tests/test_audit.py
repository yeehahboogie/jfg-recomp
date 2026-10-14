from romtool.audit import hardware_accesses
from synth import JR_RA, NOP, lui, lw, words

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


def test_register_reused_after_float_constant_not_flagged():
    ADDIU_T0 = (0x09 << 26) | (T0 << 21) | (T0 << 16) | 0x0010  # addiu t0, t0, 0x10 (t0 now a new value)
    LUI_T0_DATA = lui(T0, 0x8010)
    code = words(lui(T0, 0xBF80), MTC1, LUI_T0_DATA, ADDIU_T0, lw(T1, 0, T0), JR_RA, NOP)
    assert hardware_accesses(code, BASE, [(BASE, BASE + 28)]) == {}


def test_ambiguous_n64sym_names_dropped():
    from romtool.libultra import unique_names
    names = {0x80048B0C: "__osDisableInt", 0x80099D40: "__osDisableInt", 0x80096910: "osRecvMesg"}
    assert unique_names(names) == {0x80096910: "osRecvMesg"}


def test_scan_stops_at_unconditional_branch():
    B_FWD = (0x04 << 26) | 2           # b +2 (beq $zero, $zero)
    ADD_S = 0x46000000                  # add.s (delay slot; not a GPR write)
    code = words(lui(T0, 0xBF80), MTC1, B_FWD, ADD_S, lw(T1, 0x12C, T0), JR_RA, NOP)
    assert hardware_accesses(code, BASE, [(BASE, BASE + 28)]) == {}
