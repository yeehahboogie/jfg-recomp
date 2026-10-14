import os
from pathlib import Path

import pytest

from romtool.rom import load_rom

REPO_ROOT = Path(__file__).resolve().parents[3]


def rom_path() -> Path:
    return Path(os.environ.get("JFG_ROM", REPO_ROOT / "jfg.z64"))


@pytest.fixture(scope="session")
def rom() -> bytes:
    path = rom_path()
    if not path.is_file():
        pytest.skip(f"ROM-backed test: {path} not present")
    return load_rom(path)
