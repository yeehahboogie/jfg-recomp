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
4. **Then a real playthrough order:** story progression, saves across planets, bosses, ending.

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
| 179 | Mine Cave Entrance | **bug** | Gameplay pillarboxed to 4:3: the level's full-screen 2D tint (a pink wedge, also in ares) trips the full-screen-2D detection |
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

1. **Pillarbox on gameplay (179 Mine Cave Entrance).** The full-screen-2D rule (any affine batch whose bounding box covers the scissor) fires on a diagonal tint wedge. Gather triangle count and covered area for the title vignette vs the wedge (`JFG_DEBUG_PILLARBOX=1`), then tighten the rule.
2. **ares on the desktop** shows its idle screen and never loads the ROM (since 2026-10-14 20:00). On the virtual display (`:99`, Mesa OpenGL) it loads and runs at 60 VPS, so the desktop failure is likely its NVIDIA video path.
   - `tools/diag/ares-shots.sh` runs ares there.
   - `ARES_WARP=<level>[:<entry>]` sends ares's new game to any level, like the port's `JFG_WARP`. It uses `tools/diag/ares-warp.py` over ares's GDB server, which is enabled only for the run and verified off afterwards.
   - **Every port finding can be compared with ares at the same level.**

