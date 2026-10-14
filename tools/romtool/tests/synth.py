"""Instruction encoders and synthetic ROM builders for ROM-free tests. Contains no game data."""
import struct

NOP = 0x00000000
JR_RA = 0x03E00008


def words(*ws: int) -> bytes:
    return b"".join(struct.pack(">I", w & 0xFFFFFFFF) for w in ws)


def z64_header() -> bytes:
    return bytes.fromhex("80371240") + bytes(0x3C)
