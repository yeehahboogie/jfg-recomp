import pytest

from romtool.rom import EXPECTED_MD5, ROM_SIZE, RomError, load_rom, normalize_byte_order
from synth import z64_header


def _v64(z64: bytes) -> bytes:
    out = bytearray(len(z64))
    out[0::2], out[1::2] = z64[1::2], z64[0::2]
    return bytes(out)


def _n64(z64: bytes) -> bytes:
    out = bytearray(len(z64))
    out[0::4], out[1::4], out[2::4], out[3::4] = z64[3::4], z64[2::4], z64[1::4], z64[0::4]
    return bytes(out)


def test_z64_passes_through():
    data = z64_header() + bytes(range(64))
    assert normalize_byte_order(data) == data


def test_v64_and_n64_normalise():
    data = z64_header() + bytes(range(64))
    assert normalize_byte_order(_v64(data)) == data
    assert normalize_byte_order(_n64(data)) == data


def test_unknown_magic_rejected():
    with pytest.raises(RomError, match="not an N64 ROM"):
        normalize_byte_order(b"PK\x03\x04" + bytes(60))


def test_wrong_dump_rejected(tmp_path):
    path = tmp_path / "other.z64"
    path.write_bytes(z64_header() + bytes(0x1000))
    with pytest.raises(RomError, match=f"does not match Jet Force Gemini \\(USA\\) \\[!\\] {EXPECTED_MD5}"):
        load_rom(path)


def test_missing_file_rejected(tmp_path):
    with pytest.raises(RomError, match="ROM not found"):
        load_rom(tmp_path / "nope.z64")


def test_odd_length_rejected(tmp_path):
    path = tmp_path / "odd.v64"
    path.write_bytes(_v64(z64_header()) + b"\x00")
    with pytest.raises(RomError, match="length"):
        load_rom(path)


def test_real_rom(rom):
    assert len(rom) == ROM_SIZE
    assert rom[0x20:0x34].decode("ascii").strip() == "JET FORCE GEMINI"
