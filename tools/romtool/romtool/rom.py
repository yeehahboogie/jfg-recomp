"""Load the user's Jet Force Gemini (USA) ROM and make sure it is the exact dump we support."""
import hashlib
import os
from pathlib import Path

EXPECTED_MD5 = "772cc6eab2620d2d3cdc17bbc26c4f68"
ROM_SIZE = 0x2000000

_Z64 = bytes.fromhex("80371240")  # big-endian (native)
_V64 = bytes.fromhex("37804012")  # 16-bit byte-swapped
_N64 = bytes.fromhex("40123780")  # 32-bit little-endian


class RomError(Exception):
    """The input is not the supported ROM, or breaks an assumption about its layout."""


def normalize_byte_order(data: bytes) -> bytes:
    magic = bytes(data[:4])
    if magic == _Z64:
        return bytes(data)
    if magic == _V64:
        out = bytearray(len(data))
        out[0::2], out[1::2] = data[1::2], data[0::2]
        return bytes(out)
    if magic == _N64:
        out = bytearray(len(data))
        out[0::4], out[1::4], out[2::4], out[3::4] = data[3::4], data[2::4], data[1::4], data[0::4]
        return bytes(out)
    raise RomError(f"not an N64 ROM: first word {magic.hex()} is not a known header magic")


def load_rom(path: str | os.PathLike) -> bytes:
    path = Path(path)
    if not path.is_file():
        raise RomError(f"ROM not found: {path}")
    data = path.read_bytes()
    if len(data) % 4:
        raise RomError(f"{path}: length {len(data)} is not a multiple of 4; the file is truncated or not a ROM")
    data = normalize_byte_order(data)
    md5 = hashlib.md5(data).hexdigest()
    if md5 != EXPECTED_MD5:
        raise RomError(
            f"{path}: MD5 {md5} does not match Jet Force Gemini (USA) [!] {EXPECTED_MD5}. "
            "Only that exact dump is supported (other regions, revisions, hacks and overdumps are not)."
        )
    return data
