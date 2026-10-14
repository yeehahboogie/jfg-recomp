# Jet Force Gemini — Native Base Port: Design

**Date:** 2026-10-14
**Status:** Approved in conversation (sections 1–4); user asked to proceed straight to plan and execution.
**Sub-project:** 3 of 4 (1 = ROM survey spike ✅, 2 = relocation spike ✅, 3 = base port ← this, 4 = controls mod)

## 1. Goal and context

The user owns a Jet Force Gemini cartridge and has dumped it (`jfg.z64`). The end goal is a native
x86-64 Linux build of the game whose controls can be reworked to twin-stick gamepad **and**
mouse + keyboard (move while aiming). This spec covers only the **base port**: a faithful native
build of the original game, which the controls mod (sub-project 4) is then built on.

**Route:** static recompilation with N64Recomp → C, run on N64ModernRuntime with the RT64 renderer,
using a frontend forked from Zelda64Recomp.

### Stated by the user
- Wants extras beyond emulation; primary extra is new controls (twin-stick + M&KB).
- Base port is "done" when the first planet (Goldwood) is solid (see §7).
- Linux only.
- Approach A: generate the ELF directly from the ROM with our own tool.

### Assumptions (uncorrected by user)
- Personal use; no public release planned. Regardless, the repo never contains ROM data or
  anything derived from it — all generated material is rebuilt locally from the user's ROM.
- Widescreen / 60fps are welcome later but out of scope here.

## 2. What the spikes established (ROM facts)

ROM: Jet Force Gemini (USA) [!], MD5 `772cc6eab2620d2d3cdc17bbc26c4f68`, 32 MiB, `.z64` order,
CIC-6105, Flash RAM save, Rumble Pak.

| Item | Value |
|---|---|
| Main segment | ROM `0x1000` → vram `0x80000400`; entry stub jumps to `0x8003F500` |
| Main sections | text `0x80000450–0x800A0660`, data `–0x800B0B50`, bss `–0x801059A0` |
| Asset file table | ROM `0xB1750`: u32 count (71) + u32 offsets, data base `0xB1880`. Assets only. |
| Asset compression | u32 LE uncompressed size, byte `0x09`, raw deflate (zlib `wbits=-15`) |
| Global symbol table | ROM `0x1ED0270`, 2372 × u32 = `module<<20 \| offset` |
| Module descriptors | ROM `0x1ED2780`, 157 × 32 B (see below) |
| Module images | contiguous from ROM `0x1ED3B20`: text \| data \| import relocs \| local relocs |
| Unknown tables | ROM `0x1ECF220` (count + 520 id/value pairs); ROM `0x1FEB040` (2372 entries, size only referenced) |

**Module descriptor (32 B):** `+0` runtime vram (0 = not loaded), `+4` ROM offset relative to
`0x1ED3B20`, `+8` text size, `+C` data size, `+10` bss size, `+14` u16 import-reloc bytes,
`+16` u16 local-reloc bytes, `+18`/`+1C` unknown. Module 0 is the main segment (filled in at runtime).

**Relocation entry (8 B):** `u32 word0; u32 (patch_offset << 8 | type << 4 | kind)`.
- `type`: ELF MIPS numbering — 2 `R_MIPS_32`, 4 `R_MIPS_26`, 5 `R_MIPS_HI16` (paired with the
  following entry, which is the `LO16`), 6 `R_MIPS_LO16`.
- `kind`: 0 = global symbol index into the symbol table; 1 = module-local offset (`word0`);
  2 = module-local call (target already in the instruction's 26-bit field).
- Symbol module ids `0xFFD`/`0xFFE` → main data, `0xFFF` → main bss, `0xFFC` = special
  (seen once: module 99, symbol `0xFFC00300`).
- Some HI16/LO16 pairs carry an addend of `−0x80000000` (they produce physical addresses).
- R_MIPS_26 *replaces* the 26-bit target field; HI/LO add the instruction's existing addend.

**Loader functions (main segment):** init `0x800539E0`, load module `0x80053D2C`,
apply relocation `0x80052AB8`, resolve symbol `0x80052874`, write patch `0x80052A00`.
References to an unloaded module resolve to stub function `0x80054530` (calls/pointers) or dummy
`0x800FEAD4` (data) and are re-linked when the module loads.

**Spike validation:** all 7,414 `jal 0` placeholders are covered by R_26 relocations;
8,467/8,467 R_26, 8,474/8,475 HI/LO pairs and 2,025/2,025 R_32 resolve into sensible targets;
three entries need manual handling.

## 3. Architecture

### Repository
The repository is a fork of Zelda64Recomp (history imported, `upstream` remote kept so runtime and
renderer fixes can be merged), with Zelda-specific code removed and ours added:

```
jfg/
├─ jfg.z64              user's ROM (git-ignored, never committed)
├─ tools/romtool/       our Python tool: ROM → ELF (+ tests)
├─ recomp/              N64Recomp config for JFG, function overrides, stubs
├─ patches/             C replacements for game functions, compiled for MIPS
├─ src/                 frontend: JFG-specific startup, config, menus (from the fork)
├─ lib/                 submodules: N64Recomp, N64ModernRuntime, RT64, …
├─ docs/                specs and plans
└─ build/, generated/   build outputs (git-ignored, regenerated from the ROM)
```

### Build pipeline
```
jfg.z64 ─ romtool verify (MD5 must equal USA [!])
        ─ romtool build-elf ─► generated/jfg.elf  (main + 157 module sections + relocs + symbols)
                                └─ N64Recomp ─► generated/funcs/*.c
patches/*.c ─ clang (mips target) ─► patches.elf ─ N64Recomp ─► patch code
all ─ CMake + clang ─► build/jfg-recomp  (Linux x86-64)
```
At runtime the binary reads `jfg.z64` itself for assets via the game's own DMA code.

### Licensing
The fork makes the repository GPL-3.0. `tools/romtool` is our own code.

## 4. Component: `romtool` (Python ≥ 3.12)

| Module | Responsibility |
|---|---|
| `rom.py` | Load ROM; normalise `.v64`/`.n64` byte order to `.z64` in memory; require the USA [!] MD5. |
| `layout.py` | Derive main-segment bounds from the entry stub; locate module tables; every hard-coded address is asserted against ROM contents. |
| `modules.py` | Decode 157 descriptors, module images, import + local reloc tables into typed records `Reloc(section, offset, type, target)` where target is `(section, offset)` or `Absolute(value)`. Handles `0xFFC–0xFFF`, physical-address pairs, kind-2 local calls. |
| `functions.py` | Function boundary discovery. Seeds: entry point, main-segment `jal` targets, R_26 targets, code-targeting R_32/HI-LO, prologue patterns. Ends: `jr $ra` + delay slot + alignment padding. Manual corrections and names in `recomp/function_overrides.toml`. |
| `libultra.py` | Identify libultra functions by signature — primary: shygoo's `n64sym`; fallback for anything it misses: manual identification recorded in `function_overrides.toml` — so N64Recomp maps them to N64ModernRuntime's reimplementations; fail if any runtime-required function is unidentified. |
| `elf.py` | Minimal big-endian MIPS ELF32 writer: main as fixed `.text/.data/.bss`; each module as relocatable `.text.mNNN/.data.mNNN/.bss.mNNN` with `.rel.*` sections; REL-style implicit addends encoded into instruction fields. |
| `loader_ref.py` | Faithful Python port of the game's loader (`0x80052AB8` and callees) — the independent reference for verification. |
| `cli.py` | `romtool verify`, `romtool info`, `romtool build-elf -o generated/jfg.elf`. |

**Error policy — fail loudly, never guess:** wrong MD5 (print found vs expected); any broken layout
invariant (e.g. "module 37 doesn't end where module 38 begins"); symbol index out of range;
unknown reloc type/kind — all are hard errors unless the exact entry is allow-listed in
`recomp/function_overrides.toml`.

**Core correctness property:** for every module and several random load addresses, (a) applying our
ELF's relocations with a generic ELF relocator and (b) running `loader_ref.py` on the raw ROM
tables produce byte-identical module images; and stripping relocated fields from the ELF section
data reproduces the original ROM bytes exactly.

## 5. Component: recompilation config and runtime glue

### N64Recomp config (`recomp/jfg.toml`)
- Entry point `0x80000400`; main segment static; 157 modules relocatable.
- libultra functions named so the runtime's reimplementations are used.
- Stubs / skips / single-instruction patches as needed, each with a comment explaining why.
- N64Recomp warnings are build errors except for explicitly allow-listed entries.

### Runtime glue (`patches/`, `src/`)
- **Module registration:** replace the game's module load (`0x80053D2C`) and unload functions with
  wrappers that run the original logic and notify the runtime (`module N loaded at X` /
  `module N unloaded`), so relocatable recompiled code resolves its sections correctly.
- **Cross-module calls:** behaviour must match the game — a call into an unloaded module goes to the
  stub (`0x80054530`) and never into code whose data is absent. Verify how N64Recomp emits R_26
  calls between relocatable sections; if it calls directly, `romtool` routes those (~460) references
  through a lookup trampoline that checks the target module is loaded.
- **Game registration:** ROM hash, Flash RAM save type, Rumble Pak, CIC-6105 boot state.
- **Audio:** identify JFG's audio RSP microcode and statically recompile it with RSPRecomp.
- **Graphics:** RT64 HLE of JFG's graphics microcode.
- **Frontend:** JFG naming, ROM selection with hash check, Zelda-specific menus and mods removed;
  the runtime's input remapping is kept.

### Risks to retire first (each may change the design)
1. Graphics microcode identity and RT64 support.
2. Audio microcode identity and RSPRecomp support.
3. N64Recomp's handling of R_26 between relocatable sections.
4. Rare anti-tamper checks in JFG (find; disable if present).

## 6. Testing

- **romtool, ROM-free (pytest):** synthetic module images/reloc tables (no game data in repo):
  every reloc type × kind, special symbol modules, physical-address pairs, local calls; ELF output
  round-trips through a standard reader (pyelftools); error paths (bad MD5, broken invariant,
  unknown reloc).
- **romtool, ROM-backed (skipped if `jfg.z64` absent):** the core correctness property at several
  random load addresses for all 157 modules; spike counts reproduced; function-finder invariants
  (no overlaps; every `jal` target is a function start; every function ends in `jr $ra` or a tail
  jump).
- **Recompile:** zero non-allow-listed N64Recomp warnings.
- **Runtime:** debug-flag logging of every module load/unload and every call that hits the
  unloaded-module stub; a smoke script boots the binary with the ROM for a fixed time and checks it
  is alive, presenting frames, and logged no runtime errors; ares used as visual/audio reference.

## 7. Definition of done

1. Boot: logos → intro → title → file select render correctly.
2. Saves: create a file, save, quit, relaunch, the save is still there (Flash RAM).
3. Goldwood start to finish: enemies, weapons, pickups, Tribal rescues, cutscenes, leaving for the
   next area.
4. Graphics: no missing/corrupt geometry, textures or HUD versus ares.
5. Audio: music and SFX correct, no crackle or drift.
6. Input: gamepad and keyboard via runtime remapping; rumble on a supported pad.
7. Stability: ≥ 30 minutes play with no crash and no unexpected unloaded-module stub hits.

## 8. Out of scope

Widescreen, 60fps/frame-rate changes, any control-scheme change (sub-project 4), Windows/macOS
builds, a decompilation, public distribution, verifying planets beyond Goldwood.

## 9. Environment

Ubuntu 24.04, NVIDIA RTX 3080 (Vulkan ICD present), gcc 13, cmake 3.28, ninja 1.11, Python 3.12.
Missing and required: clang (host + MIPS target), SDL2 development package; others per the
fork's build docs.
