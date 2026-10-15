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
4. **Exit crawl: done (2026-10-15).** Every exit of every playable level was taken once through the game's own transition: 317 first exits plus 406 others, plus the pilot.
   - **Method:** `JFG_TAKE_EXIT=<n>` (or survey job `LEVEL/n`), about 3 s into the level, sets the player's exit pointer to exit *n* (`pd+0xF4 = exit`, `pd+0x196 = −120`), as `exitControl` does for a player in range. The game's own transition code then changes level. Teleporting the player isn't enough, because objects outside the current region aren't updated.
   - **Result:** no crashes after the overlay-unload fix (runtime patch 0004). Every exit changed level except 306 Mizar Boy Fall's (a falling sequence with no player control), 105 (marked NOT USED), and exit-less cutscene and test levels.
   - **Level graph:** `python3 tools/diag/level-graph.py build/diag/level-names.txt 92 build/diag/survey/crawl-test build/diag/survey/crawl1 build/diag/survey/crawl2` gives 48 clusters, matching the planets (AlphaSewer, Canyon/CargoSewer, Goldwood with the CargoShip, Mizar's Palace, Swamp, Asteroid, Gem Quarry, race pits and others). 41 levels can be walked to from Goldwood; the rest are reached by ship travel or story events.
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

## Bosses and the soak test

- **Boss levels:**
  - Forest 85, AlphaSewer 162, SlugBoss 371/372 → 370, Swamp 193–195/377 → 376, Alien 352/351 → 393, ast_Boss3 336 → 373.
  - Mizar Boss 141; final Mizar Fight 387/386 → 388; Disco Credits 360.
  - Code modules: Mizar 68, SlugBoss 149, WaterBoss 152, boy/dog/girl bosses 50–52.
- **What's been run:** the survey ran every boss play level for 30 s without a crash.
- **What a warp misses:** a warp into a play level skips the sequence that starts the fight. In 372 Vela fires at an empty arena.
  - So defeat paths (phase changes, deaths, end sequences) need story-order arrival or the user's playthrough.
- **Soak test: done (2026-10-16), clean.** All 318 playable levels got about 2 minutes of moving, jumping, rolling, firing and weapon switching with `JFG_CHEATS=invuln,weapons` (`scripts/soak-batch.sh`, results in `build/diag/survey/soak1`). There was no game crash or hang.
  - **49:** failed once to open its window on the virtual display; passed on rerun.
  - **47 and 411:** merged into a bogus job "41147" (a missing newline in the job list) and crashed on the invalid warp; both passed when run separately.
  - **67 and 297:** the screenshots looked stuck on Midge's first dialogue page because the script's A presses were too sparse. With A every 2 s, his conversation runs through every page, hands over the item, and play resumes.

## Open issues

- **Multiplayer: players 2–4 added (2026-10-16).**
  - **Before:** the frontend reported only controller 1, so the MULTI modes (and a second player controlling Floyd) were unreachable.
  - **The setting:** General → Extra Players (off by default, so duplicate or virtual pads can't make a phantom player). When on, gamepad *n* in connection order is player *n+1* with the N64 bindings. Player 1 keeps the first gamepad, the keyboard, the mouse and the Modern scheme. Pad assignment is in `jfg::pad_for_player` (tested).
  - **Runtime bug fixed (patch 0005):** the controller-present bit pattern was assigned per controller instead of OR-ed. With two controllers only the last bit survived, and the game said "Controller 1 not present".
  - **Verified with `JFG_FAKE_PLAYERS=2` and `JFG_INPUT_SCRIPT_P2`:** player 2 joins the multiplayer character select, the setup menu works, and a two-player Target Range match (level 364) runs. Battle-mode split screen not yet checked.
- **Saves:** the existing game file loads ("Select Game" → "Play Game 1" lands in the character-select hub).

- **Menus cleaned of Zelda64Recomp leftovers (2026-10-15).**
  - **Removed (nothing in JFG reads them):** Targeting Mode, Gyro Sensitivity, Autosaving, Aiming Camera Mode, Analog Camera, Analog Camera Mode, Background Music Volume, Low Health Beeps, HUD Placement, and the hidden Debug tab (Zelda scene warp, Majora's Mask clock).
  - **Controls tab:** shows only what the active scheme reads, with N64 buttons named as buttons (JFG's own Normal/Expert mode changes what they do).
  - **Kept:** the Mods tab (the general recomp mod loader).
  - **Possible enhancement:** a real HUD Placement for JFG (move the HUD to the screen edges in widescreen). RT64's option only moves draws made with its extended commands, which JFG's display lists don't use.

- **Sound turned into loud static, intermittently, sometimes until the game closed: fixed in the frontend.**
  - **Cause:** when SDL already held too much sound, the frontend dropped samples by dividing the chunk's byte count by 2^skip without rounding to whole frames. JFG's converted chunks are an odd number of frames (1605 at 48 kHz), so it queued half-frames (6420 bytes) and, at skip 2, a quarter float (2406 bytes).
  - **Effect:** SDL and the sound server read the queue as one stream of frames, so everything after a misaligned chunk played as floats read a few bytes off: full-scale static. It recovered only when another odd chunk happened to shift the stream back.
  - **Load-dependent:** skipping only happens when the queue backs up, as on the user's machine while the exit crawl was running. The queue estimate also used the game's 22020 Hz instead of the 48 kHz output rate, so skipping started at about 46 ms instead of 100 ms.
  - **Fix:** `jfg::plan_queue` (`src/jfg/audio_queue.cpp`, tested) always queues whole frames, measures the queue at the output rate, and caps the ratio at 32.
  - **Found by:** the game's sound (`JFG_AUDIO_DUMP`) was clean while the output recorded by `tools/diag/capture-audio.sh` had the static, and `JFG_DEBUG_AUDIO_QUEUE` showed the odd byte counts.
  - **Evidence:** static appeared only in unfixed runs whose queue backed up (2 of 2, plus the user's session under the crawl's load). Unfixed runs that never skipped had none (0 of 6). Fixed runs skipping heavily (2,950 and 5,326 chunks) had none.
  - **Worker 0:** static appeared only while the stream was two bytes off a float (18–19% of those windows, 0% when aligned).
  - **Caveat:** one unfixed reproduction also had short bursts while aligned, unexplained, possibly an artifact of recording four games at once. Recheck if static is reported again.

- **Static burst at the character-select teleport (and when a Tribal beams away): the original does it too, not a bug.**
  - The game plays a loud noise sample on four voices, with ADPCM book `0x18CD18` (a near-zero predictor, scale 12).
  - ares, recorded over the same route with `tools/diag/ares-audio.sh`, has the same burst at the same moment and loudness: 250.9–252.5 s, RMS 0.40, against the port's 250.1–251.7 s, RMS 0.43.
  - The port's recompiled audio microcode decodes and resamples that voice bit-exactly against mupen64plus-rsp-hle's naudio_mp3, replaying a `JFG_ALIST_DUMP` task in isolation.
  - The reference's envelope mixer doesn't model JFG's per-voice volume state, so it can't check mixing.

- ~~**Exit 7/0 (AlphaSewer eastpit → 11) quit the game**~~: fixed, runtime patch 0004.
  - **Cause:** `unload_overlays` treated a loaded section that starts exactly where the unloaded module ends as overlapping (`>=`). It reported "Cannot partially unload section" and called `exit()`, and the exit then crashed in RmlUi's static destructors.
  - **Why it matters beyond the crawl:** this affects any level change that unloads a module sitting right before another one in RAM, so normal play hits it too.

- ~~**Black rectangle mid-screen** (120 Alien Cave Pyxis, 248/263/269 MilitaryBase, 275 Space Station landing, 258 Multi Race)~~: fixed.
  - **What the game does:** it renders an effect into a 64×64 8-bit intensity buffer (`0x346780`), then samples it as an IA8 texture.
  - **What went wrong:** RT64's GPU tile-copy path drew that texture as an opaque black rectangle.
  - **Fix:** with `copyWithGPU` off, the data goes through RDRAM as on the hardware. All six match ares, still at 30 fps.
  - **Debugging:** `JFG_DEBUG_FBTEX=1` logs framebuffer-as-texture reads.

- **Survey suspects, all compared with ares (warped to the same level):**
  - **Fixed** by the frame-copy change (`copyWithGPU = false`): 62 CargoSewer End, 68 AlphaSewer northwest, 120 Alien Cave Pyxis, 166 seq part4, 199 Forest Intro.
  - **74 AlphaSewer Stairs** matches ares: the "box" is a wall in the scene.
  - **197 Canyon Multi** is a multiplayer arena warped into a one-player game, an invalid state in both: ares white with no HUD, the port black with the HUD. Check it through the multiplayer menu.
  - **Black cutscenes after a warp (240, 253, 325, 345, 378, 380)** are black in ares too, after a confirmed warp. They need the state their normal arrival sets up; check them in story order.
    - **325 Ant Attack Seq verified:** on its normal arrival in the new-game intro (about 119 s) it renders correctly.
    - The rest are started by story events (Vela freed from the CargoShip brig, Floyd regenerating), not exits, so the exit crawl can't reach them.
  - Test and "NOT USED" levels (83, 105, 109, 111, 137) look empty, which is expected.
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

