"""Minimal GDB remote-protocol client: dump memory from a GDB stub. Usage: gdbmem.py HOST:PORT ADDR LEN OUT"""
import socket, sys

def checksum(s: bytes) -> bytes:
    return b"%02x" % (sum(s) & 0xFF)

class Stub:
    def __init__(self, hostport):
        host, port = hostport.split(":")
        self.s = socket.create_connection((host, int(port)), timeout=10)
        self.buf = b""
    def send(self, payload: str) -> bytes:
        p = payload.encode()
        self.s.sendall(b"$" + p + b"#" + checksum(p))
        return self.recv()
    def recv(self) -> bytes:
        while True:
            while b"#" not in self.buf or len(self.buf) < self.buf.index(b"#") + 3:
                self.buf += self.s.recv(65536)
            start = self.buf.find(b"$")
            end = self.buf.index(b"#", start)
            data = self.buf[start + 1:end]
            self.buf = self.buf[end + 3:]
            self.s.sendall(b"+")
            return data

stub = Stub(sys.argv[1])
stub.s.sendall(b"+")
stub.send("qSupported")
stub.send("?")
stub.s.sendall(b"\x03")  # pause the target for a consistent snapshot
print("stop reply:", stub.recv()[:40])
addr, length, out = int(sys.argv[2], 0), int(sys.argv[3], 0), sys.argv[4]
data = bytearray()
import time
chunk = int(sys.argv[5], 0) if len(sys.argv) > 5 else 0x4000
t0 = time.time()
f = open(out, 'wb')
while len(data) < length:
    n = min(chunk, length - len(data))
    reply = stub.send(f"m{addr + len(data):x},{n:x}")
    if (len(reply) == 3 and reply.startswith(b"E")) or not reply:
        raise SystemExit(f"read failed at {addr + len(data):#x}: {reply[:20]}")
    piece = bytes.fromhex(reply.decode())
    data += piece
    f.write(piece); f.flush()
f.close()
stub.s.sendall(b"$c#63")  # resume
print(f'{time.time() - t0:.1f}s')
print(f"read {len(data):#x} bytes from {addr:#x}")
