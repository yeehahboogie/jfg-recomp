# Whole game: survey and fixes (living tracker)

Goal (user, 2026-10-15): the definition of done is the **entire game**, start to finish, not just Goldwood
(base-port spec §7 stopped at Goldwood). This file is the loop's state: what's known, what's checked, what's broken.

## Method

1. **Reach any level.** Use dev switches:
   - `JFG_DEBUG_LEVELS=1` logs every `mainChangeLevel` call, timestamped, and the level being played. `JFG_DEBUG_LEVELS=names` also dumps all 412 level names once gameplay starts.
   - `JFG_WARP=<level>[:<entry>]` sends a new game straight to that level. It replaces the story intro (level 322, requested about 54 s in with the standard menu script). The character is the default, Vela, because character select is skipped.
   - `scripts/survey-level.sh LEVEL OUT` does one level: menus, warp, four screenshots and a health line. `scripts/survey-batch.sh ROOT LEVEL…` runs many.
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

## Open issues

