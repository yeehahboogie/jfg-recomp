# Jet Force Gemini — Native Base Port: Design

**Date:** 2026-10-14
**Status:** Approved in conversation (sections 1–4); user asked to proceed straight to plan and execution.
Revised 2026-10-14 after planning research (see §10) — changes are marked *(rev)*.
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
├─ lib-patches/rt64/    our patch series for RT64 (JFG microcode support) *(rev)*
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
unknown reloc type/kind — all are hard errors. *(rev)* No allow-list: the MD5 pins the input and the
real ROM contains no unknown entries, so an allow-list could never be exercised.

**Core correctness property:** for every module and several random load addresses, (a) applying our
ELF's relocations with a generic ELF relocator and (b) running `loader_ref.py` on the raw ROM
tables produce byte-identical module images (call sites redirected to thunks are checked against the
thunk's recorded target instead); and every word that is not a relocation site is identical to the ROM.

## 5. Component: recompilation config and runtime glue

### N64Recomp config (`recomp/jfg.toml`)
- Entry point `0x80000400`; main segment static; 157 modules relocatable.
- libultra functions named so the runtime's reimplementations are used.
- Stubs / skips / single-instruction patches as needed, each with a comment explaining why.
- N64Recomp warnings are build errors except for explicitly allow-listed entries.

### Runtime glue (`patches/`, `src/`)
- **Module registration** *(rev)*: N64Recomp `[[patches.hook]]` entries (no MIPS patch pipeline in
  the base port): a hook at the start of the game's DMA routine (`0x8004D640`) registers a module with
  the runtime whenever its code is copied in (covers every load path, and re-registers a module that
  moves); a hook in the unload function (`0x8005372C`, before `0x800537E0`) unregisters it.
- **Dynamic linking** *(rev)*: `0x80054530` is the game's *lazy linker*, not an error stub: a call into a
  module is first compiled as `jal 0x80054530`; on first use it finds the call site via `$ra`, loads the
  module, patches that call instruction in RAM and jumps to the target; unloading re-points calls at
  it. The main segment has its own import table (ROM `0x1ECF220`: 513 calls, 2 HI/LO pairs, 3 data
  words). Recompiled code never re-reads instructions, so every re-linkable call — 513 in main, 515
  module→other-module — is redirected in the ELF to a generated MIPS thunk that sets `$ra` to the real
  return address, reads the call instruction from RAM and tail-jumps to wherever it points (lazy linker
  or real target). The 2 main HI/LO pairs get hooks that load the RAM-patched address. Module→own and
  module→main calls stay direct. N64Recomp's own HI16/LO16 relocation macros handle module data.
- **Game registration:** ROM hash, Flash RAM save type, Rumble Pak, CIC-6105 boot state.
- **Audio:** JFG's audio microcode (vram `0x8009DA10`; n_audio + MP3 family, as Banjo-Tooie / Perfect
  Dark) is statically recompiled with RSPRecomp, including any IMEM overlay.
- **Graphics** *(rev)*: JFG uses Rare's Diddy Kong Racing–derived microcode ("F3DJFG" in GLideN64),
  which RT64 does not support (it would crash). We add a `F3DDKR`/`F3DJFG` GBI family to RT64,
  modelled on its Perfect Dark family, kept as a patch series in `lib-patches/rt64/`.
- **Frontend:** JFG naming, ROM selection with hash check, Zelda-specific menus and mods removed;
  the runtime's input remapping is kept.

- **Hardware access audit** *(rev)*: N64ModernRuntime replaces libultra by name and cannot run kseg1 /
  COP0 / cache code. romtool names libultra with n64sym (whose signatures lack libultra 2.0D–2.0G —
  gaps are named by hand) and flags every other function touching hardware; each must be reviewed
  (`patch` via a hook that does the work natively and returns, or `harmless`). This is where a custom
  Flash RAM driver or CIC-6105 anti-tamper check would surface.
- **Versions** *(rev)*: keep Zelda64Recomp's pinned N64ModernRuntime and RT64; build N64Recomp at the
  commit Zelda64Recomp's CI uses (bump only if `[[patches.hook]]` is missing).

### Risks — status after planning research *(rev)*
1. Graphics microcode: identified; **unsupported by RT64** → implement (largest single work item).
2. Audio microcode: identified; RSPRecomp-able; overlay layout to confirm.
3. R_26 between relocatable sections: N64Recomp emits direct calls → solved with thunks.
4. Anti-tamper / custom hardware drivers: covered by the hardware audit.

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
- **Runtime:** debug-flag logging (`JFG_DEBUG_MODULES=1`) of every module load/unload and every
  lazy-link event; a C++ unit test of the registration hooks against a fake runtime; a smoke script boots the binary with the ROM for a fixed time and checks it
  is alive, presenting frames, and logged no runtime errors; ares used as visual/audio reference.

## 7. Definition of done

1. Boot: logos → intro → title → file select render correctly.
2. Saves: create a file, save, quit, relaunch, the save is still there (Flash RAM).
3. Goldwood start to finish: enemies, weapons, pickups, Tribal rescues, cutscenes, leaving for the
   next area.
4. Graphics: no missing/corrupt geometry, textures or HUD versus ares.
5. Audio: music and SFX correct, no crackle or drift.
6. Input: gamepad and keyboard via runtime remapping; rumble on a supported pad.
7. Stability: ≥ 30 minutes play with no crash and no `Failed to find function` / unregistered-module
   errors. *(rev)* Lazy-link events are normal game behaviour, not failures.

## 8. Out of scope

Widescreen, 60fps/frame-rate changes, any control-scheme change (sub-project 4), Windows/macOS
builds, a decompilation, public distribution, verifying planets beyond Goldwood.

## 9. Environment

Ubuntu 24.04, NVIDIA RTX 3080 (Vulkan ICD present), gcc 13, cmake 3.28, ninja 1.11, Python 3.12.
Missing and required: clang (host + MIPS target), SDL2 development package; others per the
fork's build docs.

## 10. Revision notes *(rev, 2026-10-14)*

Planning research (reading N64Recomp, N64ModernRuntime, Zelda64Recomp, RT64, n64sym, GLideN64 and
mupen64plus-rsp-hle sources, plus further ROM analysis) changed these points; the approach (A: ROM →
ELF → N64Recomp on a Zelda64Recomp fork) is unchanged:
- N64Recomp's symbol-file mode cannot express cross-section relocations → ELF mode (as designed).
- The ELF must be a *linked* image: instruction immediates hold link-time addresses, `r_offset` is a
  virtual address, ROM addresses come from `PT_LOAD` `p_paddr`, relocations match by `.rel<name>`.
- RT64 lacks JFG's microcode → RT64 patch series (§5 Graphics).
- `0x80054530` is a lazy linker; dynamic-call thunks + main HI/LO hooks reproduce it (§5).
- Module registration via DMA + unload hooks instead of wrapping the load function; no MIPS C patch
  pipeline in the base port (sub-project 4 reintroduces it for the controls).
- Microcode occupies `0x8009DA10–0x800A0660` of main "text"; CPU code ends at `0x8009DA10`.
