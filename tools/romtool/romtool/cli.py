"""Command line: verify / info / build-elf / disasm."""
import argparse
import dataclasses
import struct
import sys
from pathlib import Path

from .build import Program, build_program, section_name
from .elf import write_elf
from .functions import Overrides, load_overrides
from .layout import read_main_segment, read_module_tables
from .modules import read_modules
from .rom import RomError, load_rom


def _with_libultra(rom: bytes, overrides: Overrides, n64sym: str | None) -> Overrides:
    if not n64sym:
        return overrides
    from .libultra import run_n64sym
    names = {(0, a): n for a, n in run_n64sym(rom, read_main_segment(rom), n64sym).items()}
    return dataclasses.replace(overrides, names=names | overrides.names)


def _hook_toml(program: Program) -> str:
    out = []
    for h in program.hooks:
        out.append(
            "[[patches.hook]]\n"
            f'func = "{h.function}"\n'
            f"before_vram = {h.before_vram:#010x}\n"
            f'text = "ctx->r{h.register} = (uint64_t)(int64_t)(int32_t)(jfg_patched_hi_lo(rdram, '
            f'{h.hi_site:#010x}, {h.lo_site:#010x}) - ({h.static_lo}));"\n'
        )
    return "\n".join(out)


def cmd_verify(args) -> int:
    load_rom(args.rom)
    print("OK: Jet Force Gemini (USA) [!]")
    return 0


def cmd_info(args) -> int:
    rom = load_rom(args.rom)
    main, tables = read_main_segment(rom), read_module_tables(rom)
    print(f"main: {main}")
    print(f"modules: {tables.module_count}, symbols: {tables.symbol_count}")
    import xxhash
    print(f"rom_hash (XXH3-64): {xxhash.xxh3_64_intdigest(rom):#018x}")
    return 0


def cmd_build_elf(args) -> int:
    rom = load_rom(args.rom)
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    overrides = _with_libultra(rom, load_overrides(args.overrides), args.n64sym)
    program = build_program(rom, overrides)
    write_elf(out / "jfg.elf", program.sections, program.funcs, program.rels, program.entry)
    modules = [section_name(i) for i in sorted(program.link_base)]
    (out / "relocatable_sections.txt").write_text("\n".join(modules) + "\n")
    template = Path(args.template).read_text().replace("@GENERATED@", str(out))
    (out / "jfg.toml").write_text(template + "\n" + _hook_toml(program))
    (out / "thunks.txt").write_text("".join(
        f"{t.vram:08X} {t.name} -> "
        + (f"section {t.target.module} + {t.target.offset:#x}\n" if t.target else "unresolvable (as in the game)\n")
        for t in program.thunks))
    print(f"wrote {out}: {len(program.funcs)} functions, {len(program.rels)} relocations, "
          f"{len(program.thunks)} thunks, {len(program.hooks)} hooks")
    return 0


def cmd_disasm(args) -> int:
    import rabbitizer
    rom = load_rom(args.rom)
    if args.section == 0:
        start, vram = read_main_segment(rom).rom_of(args.address), args.address
    else:
        m = read_modules(rom, read_module_tables(rom))[args.section - 1]
        start, vram = m.rom + args.address, args.address
    for i in range(args.count):
        w = struct.unpack_from(">I", rom, start + 4 * i)[0]
        print(f"{vram + 4 * i:08X} {w:08X}  {rabbitizer.Instruction(w, vram + 4 * i).disassemble()}")
    return 0


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[3]
    p = argparse.ArgumentParser(prog="romtool")
    sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("verify", "info"):
        s = sub.add_parser(name)
        s.add_argument("rom")
    b = sub.add_parser("build-elf")
    b.add_argument("rom")
    b.add_argument("--out", required=True)
    b.add_argument("--overrides", default=str(root / "recomp/function_overrides.toml"))
    b.add_argument("--template", default=str(root / "recomp/jfg.toml.in"))
    b.add_argument("--n64sym", default=None)
    d = sub.add_parser("disasm")
    d.add_argument("rom")
    d.add_argument("--section", type=int, default=0)
    d.add_argument("--address", type=lambda s: int(s, 0), required=True)
    d.add_argument("--count", type=int, default=32)
    args = p.parse_args(argv)
    try:
        return {"verify": cmd_verify, "info": cmd_info, "build-elf": cmd_build_elf, "disasm": cmd_disasm}[args.cmd](args)
    except RomError as e:
        print(f"romtool: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
