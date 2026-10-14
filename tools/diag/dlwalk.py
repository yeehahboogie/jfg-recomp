"""Walk a JFG (DKR-family) display list in a big-endian RDRAM image with GLideN64's semantics and write the
clip-space triangles (x y z w rgba per corner) plus a command log. Usage: dlwalk.py RDRAM DL_ADDR TRIS LOG"""
import struct, sys
import numpy as np

ram = open(sys.argv[1], "rb").read()
MASK = len(ram) - 1
def u32(a): return struct.unpack_from(">I", ram, a & MASK)[0]
def s16(a): return struct.unpack_from(">h", ram, a & MASK)[0]
def u16(a): return struct.unpack_from(">H", ram, a & MASK)[0]
def u8(a): return ram[a & MASK]

seg = [0] * 16
def phys(a): return (seg[(a >> 24) & 0xF] + (a & 0xFFFFFF)) & MASK

def matrix(a):
    m = np.zeros((4, 4))
    for i in range(4):
        for j in range(4):
            k = i * 4 + j
            m[i, j] = s16(a + 2 * k) + u16(a + 32 + 2 * k) / 65536.0
    return m

slots = [np.eye(4) for _ in range(4)]
active, billboard, vi = 0, False, 0
mtx_off = vtx_off = 0
clip = [np.zeros(4)] * 64
col = [0] * 64
tris, log, seq = [], [], []
stack = []            # return addresses
counted = []          # remaining-command counters parallel to stack (None for plain calls)
pc = int(sys.argv[2], 16) & MASK
steps = 0
while steps < 200000:
    steps += 1
    w0, w1 = u32(pc), u32(pc + 4)
    op = w0 >> 24
    seq.append(f"{w0:08X} {w1:08X}")
    nxt = pc + 8
    if op == 0x01:
        idx = (w0 >> 16) & 0xF
        if idx == 0:
            idx, mul = (w0 >> 22) & 3, False
        else:
            mul = bool((w0 >> 23) & 1)
        a = mtx_off + phys(w1)
        m = matrix(a)
        slots[idx & 3] = m @ slots[0] if mul else m
        active = idx & 3
        log.append(f"MTX slot {idx} mul {int(mul)} @{a:06X} row3 {np.round(m[3], 3).tolist()}")
    elif op == 0x04:
        if w0 & 0x10000:
            if billboard: vi = 1
        else:
            vi = 0
        n, dst = (w0 >> 19) & 0x1F, vi + ((w0 >> 9) & 0x1F)
        a = vtx_off + phys(w1)
        for i in range(n):
            v = a + 10 * i
            c = np.array([s16(v), s16(v + 2), s16(v + 4), 1.0]) @ slots[active]
            if billboard: c = c + clip[0]
            clip[(dst + i) & 63], col[(dst + i) & 63] = c, u32(v + 6) if False else (u8(v+6) << 24) | (u8(v+7) << 16) | (u8(v+8) << 8) | u8(v+9)
        vi += n
        log.append(f"VTX n {n} dst {dst} @{a:06X} slot {active}")
    elif op == 0x05:
        n = ((w0 >> 20) & 0xF) + 1
        a = phys(w1)
        for i in range(n):
            t = a + 16 * i
            vs = [u8(t + 1) & 63, u8(t + 2) & 63, u8(t + 3) & 63]
            tris.append(" ".join(f"{clip[v][0]} {clip[v][1]} {clip[v][2]} {clip[v][3]} {col[v]:08X}" for v in vs))
        vi = 0
        log.append(f"TRI n {n} @{a:06X}")
    elif op == 0x06:
        if ((w0 >> 16) & 0xFF) == 0:
            stack.append(nxt); counted.append(None)
        nxt = phys(w1)
    elif op == 0x07:
        stack.append(nxt); counted.append((w0 >> 16) & 0xFF)
        nxt = phys(w1)
        log.append(f"DMA_DL count {(w0 >> 16) & 0xFF} -> {nxt:06X}")
        pc = nxt
        continue
    elif op == 0xB8:
        if not stack: break
        nxt = stack.pop(); counted.pop()
    elif op == 0xBF:
        mtx_off, vtx_off = w0 & 0xFFFFFF, w1 & 0xFFFFFF
        log.append(f"OFFSETS mtx {mtx_off:06X} vtx {vtx_off:06X}")
    elif op == 0xBC:
        idx = w0 & 0xFF
        if idx == 0x02: billboard = bool(w1 & 1); log.append(f"BILLBOARD {int(billboard)}")
        elif idx == 0x0A: active = (w1 >> 6) & 3
        elif idx == 0x06: seg[(w0 >> 10) & 0xF] = w1 & 0xFFFFFF; log.append(f"SEGMENT {(w0 >> 10) & 0xF} = {w1 & 0xFFFFFF:06X}")
    # counted display lists return after their command count
    pc = nxt
    if counted and counted[-1] is not None:
        counted[-1] -= 1
        if counted[-1] == 0:
            counted.pop(); pc = stack.pop()
open(sys.argv[3], "w").write("\n".join(tris) + "\n")
open(sys.argv[4], "w").write("\n".join(log) + "\n")
open(sys.argv[4] + ".seq", "w").write("\n".join(seq) + "\n")
print(f"{steps} commands, {len(tris)} triangles")
