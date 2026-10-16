import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def romtool(*args):
    return subprocess.run([sys.executable, "-m", "romtool.cli", *args], capture_output=True, text=True)


def test_verify_rejects_garbage(tmp_path):
    bad = tmp_path / "bad.z64"
    bad.write_bytes(bytes.fromhex("80371240") + bytes(60))
    r = romtool("verify", str(bad))
    assert r.returncode == 1 and "does not match" in r.stderr


def test_verify_real(rom):
    r = romtool("verify", str(ROOT / "jfg.z64"))
    assert r.returncode == 0 and "OK" in r.stdout


def test_build_elf(rom, tmp_path):
    r = romtool("build-elf", str(ROOT / "jfg.z64"), "--out", str(tmp_path),
                "--overrides", str(ROOT / "recomp/function_overrides.toml"),
                "--template", str(ROOT / "recomp/jfg.toml.in"))
    assert r.returncode == 0, r.stderr
    for name in ("jfg.elf", "relocatable_sections.txt", "jfg.toml", "thunks.txt"):
        assert (tmp_path / name).exists()
    listed = (tmp_path / "relocatable_sections.txt").read_text().split()
    assert listed[0] == ".m001" and len(listed) == 157
    assert listed[20] == "*" and listed[55] == "*"  # modules 21 and 56 have no code: empty overlay slots
    toml = (tmp_path / "jfg.toml").read_text()
    template_hooks = (ROOT / "recomp/jfg.toml.in").read_text().count("[[patches.hook]]")
    assert "@GENERATED@" not in toml and toml.count("[[patches.hook]]") == template_hooks + 2  # + the 2 generated


def test_export_rejects_garbage(tmp_path):
    bad = tmp_path / "bad.z64"
    bad.write_bytes(bytes.fromhex("80371240") + bytes(60))
    r = romtool("export", str(bad), str(tmp_path / "out.z64"))
    assert r.returncode == 1 and "does not match" in r.stderr
    assert not (tmp_path / "out.z64").exists()


def test_export_writes_big_endian(rom, tmp_path):
    # A byte-swapped (.v64) dump comes out as the big-endian ROM the game stores and checks.
    data = (ROOT / "jfg.z64").read_bytes()
    swapped = bytearray(len(data))
    swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
    v64 = tmp_path / "jfg.v64"
    v64.write_bytes(bytes(swapped))
    r = romtool("export", str(v64), str(tmp_path / "out.z64"))
    assert r.returncode == 0, r.stderr
    assert (tmp_path / "out.z64").read_bytes() == data
