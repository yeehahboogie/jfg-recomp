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


def is_trap(w: int) -> bool:
    return w & 0xFC00003F == 0x0000000D  # break


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
