from romtool.thunks import DESC_TABLE_PTR, main_thunk, module_thunk


def run(code, regs, mem):
    """Tiny interpreter for the handful of instructions thunks use. Returns (regs, jump target)."""
    target = None
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
