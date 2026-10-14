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


BREAK7 = 0x0007000D
ADDIU_A0 = 0x24844000  # addiu $a0, $a0, 0x4000 (a one-instruction entry point that falls through)


def test_trap_after_return_is_padding():
    code = words(addiu_sp(-8), JR_RA, NOP, BREAK7, addiu_sp(-8), JR_RA, NOP)
    assert split_functions(code, BASE, {BASE, BASE + 16}) == [(BASE, BASE + 12), (BASE + 16, BASE + 28)]


def test_fallthrough_function_extends_into_next():
    from romtool.functions import extend_fallthroughs
    code = words(ADDIU_A0, addiu_sp(-8), JR_RA, NOP)
    spans = split_functions(code, BASE, {BASE, BASE + 4})
    assert spans == [(BASE, BASE + 4), (BASE + 4, BASE + 16)]
    extended = extend_fallthroughs(code, BASE, spans)
    assert extended == [(BASE, BASE + 16), (BASE + 4, BASE + 16)]
    assert check_invariants(code, BASE, extended, call_targets={BASE, BASE + 4}) == []


def test_partial_overlap_still_rejected():
    code = words(addiu_sp(-8), JR_RA, NOP, addiu_sp(-8), JR_RA, NOP)
    problems = check_invariants(code, BASE, [(BASE, BASE + 16), (BASE + 12, BASE + 24)], call_targets=set())
    assert any("overlap" in p for p in problems)


def test_exempt_functions_skip_body_checks():
    code = words(beq(A0, 0, 3), NOP, JR_RA, NOP, addiu_sp(8), JR_RA, NOP)
    bad = [(BASE, BASE + 16), (BASE + 16, BASE + 28)]
    assert check_invariants(code, BASE, bad, call_targets=set(), exempt={BASE}) == []


def test_exempt_override_parsed(tmp_path):
    path = tmp_path / "o.toml"
    path.write_text('[[exempt]]\nsection = 0\naddress = 0x80075020\nreason = "libultra exception handler"\n')
    assert load_overrides(path).exempt == {(0, 0x80075020)}


def test_hardware_override_parsed_and_validated(tmp_path):
    import pytest
    from romtool.rom import RomError
    good = tmp_path / "g.toml"
    good.write_text('[[hardware]]\nsection = 0\naddress = 0x80001000\ndecision = "patch"\nreason = "flash"\n')
    assert load_overrides(good).hardware == {(0, 0x80001000): "patch"}
    bad = tmp_path / "b.toml"
    bad.write_text('[[hardware]]\nsection = 0\naddress = 0x80001000\ndecision = "maybe"\nreason = "x"\n')
    with pytest.raises(RomError, match="decision"):
        load_overrides(bad)


def test_extend_override_contains_later_functions():
    from romtool.functions import apply_extensions
    code = words(beq(A0, 0, 6), NOP, JR_RA, NOP,      # f0: branches to BASE+28, past f1's start
                 addiu_sp(-8), JR_RA, NOP,             # f1 at BASE+16 (a call target)
                 addiu_sp(8), JR_RA, NOP)              # label targeted by f0
    spans = split_functions(code, BASE, {BASE, BASE + 16})
    assert any("branch" in p for p in check_invariants(code, BASE, spans, {BASE, BASE + 16}))
    extended = apply_extensions(spans, {BASE: BASE + 40})
    assert extended[0] == (BASE, BASE + 40)
    assert check_invariants(code, BASE, extended, {BASE, BASE + 16}) == []


def test_extend_override_parsed(tmp_path):
    path = tmp_path / "o.toml"
    path.write_text('[[extend]]\nsection = 0\naddress = 0x80073E80\nend = 0x80074E50\nreason = "asm routine"\n')
    assert load_overrides(path).extend == {(0, 0x80073E80): 0x80074E50}


def test_elf_word_override_parsed(tmp_path):
    path = tmp_path / "o.toml"
    path.write_text('[[elf_word]]\nsection = 0\naddress = 0x800A7478\nvalue = 0x80074E34\nreason = "jtbl entry 0"\n')
    assert load_overrides(path).elf_words == {(0, 0x800A7478): 0x80074E34}
