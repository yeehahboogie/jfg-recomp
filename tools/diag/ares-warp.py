"""Warp ares to a level, like the port's JFG_WARP: break on mainChangeLevel (0x8004665C) and, when a new game asks for
the story intro (level 322), rewrite its arguments to (LEVEL, ENTRY, 0, 16). Needs ares's GDB debug server enabled.
Usage: ares-warp.py HOST:PORT LEVEL [ENTRY]"""
import socket, sys, time

MAIN_CHANGE_LEVEL = 0x8004665C
NEW_GAME_INTRO = 322

def checksum(s: bytes) -> bytes:
    return b"%02x" % (sum(s) & 0xFF)

class Stub:
    def __init__(self, hostport, timeout=120):
        host, port = hostport.split(":")
        deadline = time.time() + timeout
        while True:  # ares opens the port a little after launch
            try:
                self.s = socket.create_connection((host, int(port)), timeout=600)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.5)
        self.buf = b""
        self.s.sendall(b"+")
    def send(self, payload: str) -> bytes:
        p = payload.encode()
        self.s.sendall(b"$" + p + b"#" + checksum(p))
        return self.recv()
    def recv(self) -> bytes:
        while True:
            while b"$" not in self.buf or b"#" not in self.buf[self.buf.find(b"$"):] or \
                    len(self.buf) < self.buf.index(b"#", self.buf.find(b"$")) + 3:
                chunk = self.s.recv(65536)
                if not chunk:
                    raise SystemExit("ares-warp: connection closed")
                self.buf += chunk
            start = self.buf.find(b"$")
            end = self.buf.index(b"#", start)
            data = self.buf[start + 1:end]
            self.buf = self.buf[end + 3:]
            self.s.sendall(b"+")
            return data

def main():
    level = int(sys.argv[2], 0)
    entry = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
    stub = Stub(sys.argv[1])
    stub.send("qSupported")
    stub.s.sendall(b"\x03")  # pause so the breakpoint can be inserted
    print("paused:", stub.recv()[:40], flush=True)
    for addr in (f"{MAIN_CHANGE_LEVEL:x}", f"ffffffff{MAIN_CHANGE_LEVEL:08x}"):
        reply = stub.send(f"Z0,{addr},4")
        print(f"Z0 {addr}: {reply}", flush=True)
        if reply == b"OK":
            break
    else:
        raise SystemExit("ares-warp: could not set a breakpoint")
    hits = 0
    while True:
        stop = stub.send("c")
        regs = stub.send("g").decode()
        width = 16 if len(regs) >= 38 * 16 else 8
        reg = lambda n: int(regs[n * width:(n + 1) * width], 16) & 0xFFFFFFFF
        pc, a0 = reg(37), reg(4)  # GDB MIPS layout: r0-r31, sr, lo, hi, bad, cause, pc
        hits += 1
        if hits <= 20 or a0 == NEW_GAME_INTRO:
            print(f"stop {stop[:12]} pc={pc:08x} a0={a0}", flush=True)
        if pc == MAIN_CHANGE_LEVEL and a0 == NEW_GAME_INTRO:
            break
        # Step past the breakpoint before continuing, or it fires again at once.
        stub.send(f"z0,{addr},4")
        stub.send("s")
        stub.send(f"Z0,{addr},4")
    values = {4: level, 5: entry, 6: 0, 7: 16}
    ok = all(stub.send(f"P{r:x}={v:0{width}x}") == b"OK" for r, v in values.items())
    if not ok:  # no P support: write the whole register file
        new = list(regs)
        for r, v in values.items():
            new[r * width:(r + 1) * width] = f"{v:0{width}x}"
        print("G:", stub.send("G" + "".join(new)), flush=True)
    stub.send(f"z0,{addr},4")
    stub.s.sendall(b"$c#63")
    print(f"ares-warp: new game sent to level {level} entry {entry}", flush=True)
    time.sleep(1)
    stub.s.close()

main()
