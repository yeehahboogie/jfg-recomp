# Whole game: survey and fixes (living tracker)

Goal (user, 2026-10-15): the definition of done is the **entire game**, start to finish, not just Goldwood
(base-port spec §7 stopped at Goldwood). This file is the loop's state: what's known, what's checked, what's broken.

## Method

1. **Reach any level.** Use dev switches:
   - `JFG_DEBUG_LEVELS=1` logs every `mainChangeLevel` call, timestamped, and the level being played. `JFG_DEBUG_LEVELS=names` also dumps all 412 level names once gameplay starts.
   - `JFG_WARP=<level>[:<entry>]` sends a new game straight to that level. It replaces the story intro (level 322, requested about 54 s in with the standard menu script). The character is the default, Vela, because character select is skipped.
   - `scripts/survey-level.sh LEVEL OUT` does one level: menus, warp, four screenshots and a health line. `scripts/survey-batch.sh ROOT LEVEL…` runs many.
   - All runs use a private virtual display (`Xvfb :99`, GPU-rendered); nothing appears on the desktop.
   - **Reference:** `ARES_WARP=<level> tools/diag/ares-shots.sh OUT "<menu script>" 62 70 78 86` shows the same level in ares.
   - The game's own cheat flags (`allLevelCheat`, `allweaponsCheat`, `invulnerabilityCheat`) can open the ship's map.
2. **Survey each level.** Warp there, let it run, screenshot, and look for crashes, `Failed to find function`, unregistered modules and rendering faults.
3. **Fix** what the survey finds, one root cause at a time (systematic debugging), committing and pushing each fix to `base-port`.
4. **Exit crawl (tool ready).** `JFG_TAKE_EXIT=<n>` (or survey job `LEVEL/n`), about 3 s into the level, sets the player's exit pointer to exit *n* (`pd+0xF4 = exit`, `pd+0x196 = −120`), as `exitControl` does for a player in range. The game's own transition code then changes level. Teleporting the player isn't enough, because objects outside the current region aren't updated. Result lines read `L/n ok exits=N last='playing level D'`. Example: 92/0 → 47 Forest Village (6 exits), 47/0 → 48.
5. **Progression without gameplay skill.** In a warped level, put the player on each exit object (type 8) in turn. The game's own exit logic then changes level, and the log shows where each exit goes. Over all levels this builds the level graph and checks every transition. Exits that won't fire show which story flag they need.
6. **Then story order:** saves across planets, bosses and the ending. The user plays with input recording for anything skill-bound.

## Addresses (main = `0x80000450 + offset`; data module 4093 = `0x800A0660 + offset`; bss 4095 = `0x800B0B50 + offset`)

| What | Address |
|---|---|
| `mainChangeLevel(level, a1, a2, a3, [sp+0x10], [sp+0x14])` | `0x8004665C` |
| next-level globals (level, a2, a1, a3); countdown | `0x800A3250`, `0x800A3258`, `0x800A3260`, `0x800A3264`; `0x800A3294` |
| `levelGetNumber` → current level `*0x800FB114`; level count | `0x800446BC`; `*0x800FB124` |
| `levelGetName(n)` (loads asset 0x1E) | `0x8004471C` |
| `mainGetNextLevel` / `mainSyncNextLevel` / `mainGetCurrentLevel` | `0x800468DC` / `0x80047570` / `0x800483B8` |
| `allLevelCheat` / `invulnerabilityCheat` / `allweaponsCheat` | `0x800A5068` / `0x800A5070` / `0x800A5078` |
| Map screen (calls `mainChangeLevel(level, entry, 0, 0x10, …)`) | `frontMap` `m009_00690` |
| Symbol dump | `python3 build/diag/syms.py` (reads the ROM's own table) |
| Object list: `ObjList` (pointer array), count | `*0x800F2CA4`, `*0x800F2CA8` (`objGetObject(i)` `0x80005E58`) |
| Object fields | `+0x0C/+0x10/+0x14` position, `+0x40` objdef, `+0x48` control type (s16), `+0x68` per-type data |
| Control dispatcher | `0x8000FBF0`: jump table `0x800AB24C[type − 2]` |
| **Exits** | Control type **8**, `exitControl` `0x8002561C`; data `+0x10` radius, `+0x14` exit kind (flag gate via table `0x800A1120`, checked by `0x80047ED0`). A player in range gets `pd+0xF4 = exit`, `pd+0x196 = −120`, and then changes level itself |
| Doors | `doorControl` `0x80025A08`, `doorUnlock` `0x8002662C`, `exitInit` module 136 |
| Game flags | `mainGetGameFlag` `0x80047ED0`, `mainSetGameFlag` `0x80047EFC` |

## Levels

412 levels (`*0x800FB124`). Names from `JFG_DEBUG_LEVELS=names` are kept in `build/diag/level-names.txt` (git-ignored; ROM-derived), and only surveyed ones are listed here.
- **The new-game path:** 0 front end → 349 ship scroll → 0 → 322–327 story intro → 127 character select → 365 boy select sequence → 93 Forest Day Sequence → 92 Forest Day Landing (Goldwood).
- **Internal names:** "Forest" is Goldwood.

| Level | Name | Status | Notes |
|---|---|---|---|
| 92 | Forest Day Landing | ok | Warp works (Vela); renders as before |
| 53 | Dune | ok | |
| 35 | CargoShip Dock | ok | |
| 67 | AlphaSewer Lobby START | ok | Midge dialogue shows |
| 16 | Cave 1 | ok | |
| 21 | Forest First | ok | |
| 43 | MilitaryBase Approach | ok | |
| 23 | Alien Cave | ok | |
| 28 | ast_Cave | ok | |
| 29 | Canyon Lobby1 | ok | |
| 61 | CargoSewer Lobby | ok | |
| 113 | Termite level | ok | |
| 115 | Swamp level | ok | |
| 141 | Mizar Boss | ok (warp artefact) | ares shows the same: white quad, void, T-pose. Warping into the scripted boss fight leaves it unset, so check in story order later |
| 155 | MizarPalace | ok | |
| 179 | Mine Cave Entrance | fixed | Was pillarboxed: the level's full-screen 2D tint (also in ares) and its lamps' huge glow sprites tripped the full-screen-2D detection. The rule now also considers texture and size (see `JFG_DEBUG_PILLARBOX`) |
| 225 | Ice level | ok | |
| 267 | Gem level | ok | |
| 271 | Space Station | ok (no player) | Cutscene-like |
| 308 | Pyramid Land | ok (no player) | Empty room, no HUD |
| 311 | Dog Landing Pad | ok | |
| 385 | EarthCityDark Scroll | ok (cutscene) | |
| 170 | Jet Pack | ok | |
| 175 | Tribal family | ok | |
| 178 | Galaxians | ok | Minigame |
| 375 | Arcade | ok | |
| 161 | Multi Player Character Select | ok (no player) | |

## Open issues

- ~~**Black rectangle mid-screen** (120 Alien Cave Pyxis, 248/263/269 MilitaryBase, 275 Space Station landing, 258 Multi Race)~~: fixed.
  - **What the game does:** it renders an effect into a 64×64 8-bit intensity buffer (`0x346780`), then samples it as an IA8 texture.
  - **What went wrong:** RT64's GPU tile-copy path drew that texture as an opaque black rectangle.
  - **Fix:** with `copyWithGPU` off, the data goes through RDRAM as on the hardware. All six match ares, still at 30 fps.
  - **Debugging:** `JFG_DEBUG_FBTEX=1` logs framebuffer-as-texture reads.

- **Full survey (383 levels), first 210 reviewed.** Suspects still to compare with ares when the machine is idle (the ares runs under survey load missed their menu timing): 62 CargoSewer End (near black, orange fragments), 68 AlphaSewer northhwest (white frame), 74 AlphaSewer Stairs (partly boxed), 120 Alien Cave Pyxis (black rectangle mid-screen), 166 seq part4 / 197 Canyon Multi / 199 Forest Intro (black). Test and "NOT USED" levels (83, 105, 109, 111, 137) look empty, which is expected.
- **Widescreen polish: black screen corners in the ast levels (220, 224).** ares (4:3) shows the same scene with the corners covered by the level's backdrop; the wider view shows past it. Content limit, not a renderer fault; low priority.

0. ~~Scripted menu presses use the wall clock~~: fixed. `JFG_INPUT_CLOCK=frames:30` (the survey default) counts controller reads. All seven `no-warp` levels (89 133 243 245 246 254 270) passed on rerun under five-worker load.
1b. ~~45 BigShip Sequence and 273 Others crash the port after a warp~~: warp artefacts, not port bugs. The new crash handler names the routines:
   - **45:** `levelInit` → `animseqInitPath` → `objSetupObject` → `severedLimbInit` reads through a NULL table pointer (N64 address `0x108`).
   - **273:** `SquaddieInit` → `TryToSetAnim` reads N64 address `0x1`.
   - On hardware both are unmapped reads that raise an exception (ares shows a black screen). The scenes need the state their normal arrival sets up, so check them in story order.
   - The port segfaults instead of hanging, which only matters in such invalid states.

1. ~~Pillarbox on gameplay (179)~~: fixed. Full-screen 2D layers now count as a 4:3 frame only if they are screen-sized and textured or one colour (images, the vignette, fades), or oversized and untextured (iris wipes). Gradient tints and glow sprites no longer count.
2. **ares on the desktop** shows its idle screen and never loads the ROM (since 2026-10-14 20:00). On the virtual display (`:99`, Mesa OpenGL) it loads and runs at 60 VPS, so the desktop failure is likely its NVIDIA video path.
   - `tools/diag/ares-shots.sh` runs ares there.
   - `ARES_WARP=<level>[:<entry>]` sends ares's new game to any level, like the port's `JFG_WARP`. It uses `tools/diag/ares-warp.py` over ares's GDB server, which is enabled only for the run and verified off afterwards.
   - **Every port finding can be compared with ares at the same level.**

