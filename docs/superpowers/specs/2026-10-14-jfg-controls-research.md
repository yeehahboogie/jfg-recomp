# Jet Force Gemini modern controls: research and design

**Date:** 2026-10-14
**Status:** Research complete (static analysis only). The camera behaviour is **open for the user to decide** (§6, §9).
**Branch:** `controls-research`
**Sub-project:** 4 of 4 (controls mod), building on the base port (`base-port`).

## Summary

What the controls mod has to change, and where:

- **Input path.** The game reads one N64 pad per player through Rare's `joy*` layer (`joyRead` at `0x80043130`). Once per frame and per player, `controlReadJoypad` (`0x80037250`) copies the buttons and stick into a small set of globals (`controlKeys`, `controlDkeys`, `controlXjoy`, `controlYjoy`, `0x800F6DA0`–`0x800F6DBC`). All player code reads those globals. Two places bypass them:
  - `hitPlayerAimCheck` reads R directly, for lock-on.
  - The front end reads the pad directly.
- **Existing option.** The game has a per-controller **"Control Mode: Normal Control / Expert Control"** option (`frontGetTargetControl`, `0x8005AAB8`). It swaps the action buttons through a key-map table (`controlModeKeys`, `0x800A18B0`) and changes some behaviour:
  - In Expert mode, C-up and C-down **walk forward and back while aiming**.
  - Back-pedalling and the half-turn are Normal-only.
- **Two different "aim" systems.**
  - *Hip fire* (no R): the game auto-aims at the nearest enemy in a cone in front of the character (`controlUpdatePlayerAim`, `0x80039FCC`).
  - *R held:* the player state changes to "aim" (state 11) and the camera goes over the shoulder.
    - The stick position maps directly to a crosshair offset of ±20° (`controlGetManualAim`, `0x8003AABC`).
    - Pushing the stick to the rim turns the whole body.
    - In Normal mode the character can only strafe while aiming (C-left / C-right). Expert mode adds forward/back walking on C-up / C-down.
- **One facing, separate forward and strafe speeds.** The character has a single facing angle (`pd+0x11C`). Velocity is built from a forward speed along that facing (`pd+0x04`) plus a separate strafe speed (`pd+0x10`). So the engine already supports *moving in one direction while facing another*. The twin-stick mod can use this instead of fighting it.
- **Camera.** The camera runs after the character each frame (static function `0x8002B418`):
  - The *follow camera* (`0x8002CF6C`) orbits behind the character's facing whenever the character moves (or an enemy is near, or R is held). It stays where it is when the character stands still. C-left / C-right pan it by up to ±45°.
  - The *aim camera* (`0x8002C118`) sits over the shoulder and follows the body's yaw and the aim pitch, not the crosshair.
- **Proposed architecture.** Use native C++ hooks (N64Recomp `[[patches.hook]]`) on a few shared main-segment functions:
  - `controlReadJoypad`.
  - `controlGetManualAim`, which is shared by all characters.
  - `controlPlayer`.
  - The camera dispatcher.

  Add per-character module hooks only where unavoidable: movement while aiming lives in each character's module (Vela `m015`, Juno `m016`, Lupus `m017`). Extra input (right stick, mouse deltas, mouse buttons) comes straight from the frontend's SDL state through native calls, so no reserved RDRAM is needed. Every hook does nothing when the scheme is "Original", which keeps the original controls byte-for-byte.
- **New tooling find.** The ROM holds the **names of all 2,372 global symbols** (name table at ROM `0x1FEB040`, parallel to the value table at `0x1ED0270`, previously "unknown"). That is where the function names in this document come from. It is also a large win for the base port; see §7.

## 1. Method, sources and conventions

**Static analysis only.** I did not run the game. I read the recompiled C in `generated/funcs/` (each instruction is kept as a comment) and the ROM's data. Anything that can only be confirmed by running the game is listed in §7.2.

**Symbol names.**
- The relocatable-module linker resolves imports through a global symbol table: value table at ROM `0x1ED0270`, 2,372 × u32 `module<<20 | offset`.
- ROM `0x1FEB040` holds 2,372 u32 offsets into a NUL-terminated name table that starts right after them (`0x1FEB040 + 4·2372`).
- For main-segment code, `vram = 0x80000450 + offset`. For main `.bss` (module id `0xFFF`), `vram = 0x800B0B50 + offset`. For main `.data` (`0xFFD`), `vram = 0x800A0660 + offset`.
- These anchor names were checked against code: `osContGetReadData` → `0x80097DD4`, `joyRead` → `0x80043130`, `controlXjoy` → `0x800F6DAC`.
- Names in this document come from that table. *File-static* functions are not in it. They are described by address, e.g. "camera dispatcher `0x8002B418` (static)".

**Addresses.**
- Main segment: N64 virtual addresses.
- Relocatable modules: the generated code's names `mNNN_OOOOO` (module NNN, text offset OOOOO) and the ELF link addresses those functions show in `generated/funcs` (module 15 is linked at `0x82044000`, module 16 at `0x8204D000`, module 17 at `0x82056000`). At run time modules load wherever the game puts them. Hooks still use the link addresses.

**Angles.** Signed 16-bit binary angles: `0x10000` = 360°, `0x4000` = 90°, `0x2000` = 45°, `0xE38` ≈ 20°, `0x2AAA` = 60°.

**Structures.**
- `obj` is a game object.
- `pd` is the player data block that `obj+0x68` points to.
- `cam` is a camera (`controlcam`, `0x800F6DC0`).

**"Inferred"** marks a meaning I deduced from how a value is used rather than read directly.

### The player-character modules

| Character | Module | Control entry (exported) | Ground state | Aim state | Crouch-aim state | Animation select | Facing → velocity |
|---|---|---|---|---|---|---|---|
| Vela ("girl") | 15 | `girlControl` `m015_00000` | `m015_02B2C` | `m015_04658` | `m015_0418C` | `m015_05328` | `m015_05ED4` |
| Juno ("boy") | 16 | `boyControl` `m016_00000` | `m016_02708` | `m016_04440` | `m016_03F30` | `m016_05120` | `m016_05BB8` |
| Lupus ("dog") | 17 | `dogControl` `m017_00000` | `m017_02A54` | `m017_04614` | `m017_0430C` | `m017_04C50` | `m017_050BC` |

The three modules are near-copies with different offsets. Other modules also drive "player" objects and call the same main-segment helpers:
- `boyBossControl` (m050), `dogBossControl` (m051) and `girlBossControl` (m052): boss-fight variants (inferred).
- `hovershipControl` (m053, with its own `hovershipCamera`).
- `shipControl` (m054).
- The co-op drone Floyd is `sidekickControl` (`0x8002F728`, main segment), driven by controller 2.

Most analysis below was done on Vela's module (m015). The Juno and Lupus equivalents are in the table above.

## 2. How the game reads input

### 2.1 Frontend → runtime → game buffer

| Step | Where | What happens |
|---|---|---|
| SDL → one N64 pad | `recomp::get_n64_input` (`src/game/controls.cpp:82`) | Returns `false` for every controller except 0. Buttons are the OR of the keyboard and gamepad bindings. The stick is the left stick (per-axis deadzone, default 5%) plus WASD, clamped to ±1. |
| Pad → `OSContPad` | `osContGetReadData` (`lib/N64ModernRuntime/ultramodern/src/input.cpp:130`) | `stick_x = (int8_t)(127 * x)`. Full tilt reads **±127**; a real N64 pad reaches roughly ±80. |
| Poll | `osContStartReadData` → `poll_input` → `recomp::poll_inputs` | Latches keyboard, controller and **mouse deltas** once per game read (inferred: once per game frame, see §2.5). |
| Already available, unused by JFG | `recomp::get_right_analog`, `recomp::get_mouse_deltas` (`src/game/input.cpp:721`, `:679`) | The right stick as analog, and the mouse motion accumulated since the last poll. Relative mouse mode is on when mouse sensitivity ≠ 0. |

The frontend binds the **right stick to the C buttons** by default. In JFG that means right-stick X strafes and right-stick Y changes weapon (Normal mode).

**Mouse buttons cannot be bound yet.** `InputType::Mouse` is a "TODO mouse support" stub (`src/game/input.cpp:626`, `:657`).

### 2.2 Rare's `joy` layer (main segment)

| Function | Address | Behaviour |
|---|---|---|
| `joyInit` | `0x80042FEC` | `osContInit`, then starts the first read. It also fills the connected list and resets the player→port map. |
| `joyRead` | `0x80043130` | Runs once per game frame, called from the main loop (static `0x800468EC`). It also has other callers: `mainThread` `0x80044B50`, static `0x80044FAC` and `0x80066EB0`. Steps:<br>1. If the previous read has finished (`osRecvMesg`, no-block), copy the current pads to "previous" and call `osContGetReadData` into the current pads.<br>2. Handle Controller Pak and rumble requests.<br>3. Start the next read (`osContStartReadData`).<br>4. Per port, compute `pressed = cur & ~prev & mask` and `released = prev & ~cur & mask`. Ports that are disabled have their pad zeroed first. |
| `joyGetButtons(p)` | `0x800434E4` | Held buttons of the port that player `p` is mapped to. |
| `joyGetPressed(p)` | `0x80043510` | Newly pressed buttons (64 call sites, mostly menus). |
| `joyGetReleased(p)` | `0x80043534` | Newly released buttons. |
| `joyGetStickX/Y(p)` | `0x80043558` / `0x800435C8` | Clamped stick via static `0x80043638`. Values in −4…4 become 0; otherwise the value is moved 5 towards zero and clamped to **±65**. |
| `joyGetAbsX/Y(p)` | `0x8004359C` / `0x8004360C` | Raw signed stick byte (no deadzone, no clamp). |
| `joyResetMap`, `joyCreateMap`, `joyGetController` | `0x800433E0`, `0x80043430`, `0x800434D0` | Player→port map. |
| `joyEnable`, `joyDisable` | `0x80043418`, `0x80043404` | Per-port enable flags. |
| `joySetSecurity` | `0x800436B4` | Sets the button mask to 0. The mask is `0x800A3194`, initial value `0xFFFF`. No callers. |

**`joy` buffers (main `.bss`, static unless named):**
- `OSContStatus[4]` at `0x800FB0B0`.
- Current `OSContPad[4]` at `0x800FB0C0` (6-byte stride), previous at `0x800FB0D8`.
- Pressed `u16[4]` at `0x800FB0F0`, released `u16[4]` at `0x800FB0F8`.
- Player→port `u8[4]` at `0x800FB100`.
- Enabled `u8[4]` at `0x800FB104`.
- `connected` `u8[4]` at `0x800FB108`.
- `numberOfJoypads` at `0x800FB10C`.

### 2.3 The `control` layer: per-player input globals

Each character's control function calls `controlReadJoypad(player)` (`0x80037250`) once per frame. The call is at `m015_00000`+`0x2F0` for Vela, with equivalents in `m016`, `m017`, `m050`–`m054`. It fills:

| Global | Address | Content |
|---|---|---|
| `controlKeys` | `0x800F6DA0` | Held buttons (`joyGetButtons`) |
| `controlDkeys` | `0x800F6DA4` | Newly pressed buttons (`joyGetPressed`) |
| (static) | `0x800F6DA8` | Released buttons |
| `controlXjoy`, `controlYjoy` | `0x800F6DAC`, `0x800F6DB0` | Clamped stick (±65), used for **movement** |
| (static) | `0x800F6DB4`, `0x800F6DB8` | Raw stick, used for **manual aim** and by Floyd |
| `disablejoy` | `0x800F6DBC` | When non-zero, `controlReadJoypad` writes zeros instead. Set by `controlDisableJoypad` (`0x80037244`). The character code sets it during input-lock timers (`pd+0x56B`, `pd+0x577`) and while a cutscene or interaction flag is active. |
| `controlcam` | `0x800F6DC0` | Pointer to this player's camera (set in `controlPlayer`) |

Rumble goes through `controlSetRumble` (`0x8003733C`).

### 2.4 Readers that bypass the control layer

- **`hitPlayerAimCheck`** (`0x8007BCD0`) calls `joyGetButtons` and tests **R** (`0x0010`) to keep a lock-on target for lock-on weapons. If the mod fakes "R held" only in `controlKeys`, lock-on will not see it. Faking R at the pad level (frontend) or hooking `joyGetButtons` avoids that.
- Pause, menus and the front end (`joyGetPressed` in modules 7, 9, 18, 22, 31, 32, 40, 42 and others) read the pad directly. They are unaffected by gameplay hooks.
- **Floyd** (`sidekickControl` `0x8002F728`) is driven by player index 1. The frontend currently provides only controller 0.

### 2.5 Frame timing and per-frame order

The game runs at a variable frame rate. Update functions receive an integer tick count: the `a1` of `controlPlayer` and of the character control functions. Many of them also use it as a float for frame-rate-independent smoothing via `Powerf` (`0x80049BEC`) (inferred: ticks of 1/60 s).

Order within one game frame:

1. Main loop (static `0x800468EC`) → `joyRead`, once.
2. `objObjectsTick` (`0x80008FF0`) → `controlPlayer(obj, ticks)` (`0x80032A48`) for each player object. In `controlPlayer`:
   - It sets `controlModeKeys` from the Control Mode option (`0x80032B10`–`0x80032B3C`).
   - It points `controlcam` at this player's camera.
   - It calls the character's control function through a module thunk (`0x80033090` girl, `0x800330A8` boy, `0x800330C0` dog, `0x8003301C`/`34`/`4C` boss variants, `0x800330D8` hovership).
   - It calls the **camera update** (static `0x8002B418`, call at `0x80033350`).

So the character moves using the camera yaw from the **previous** frame, and then the camera reacts.

Mouse deltas must be consumed **once per game frame** (not per VI) and must not be scaled by the tick count. Stick-driven turn rates must be scaled by it.

## 3. JFG's existing control option: "Control Mode"

- **Storage.** `frontGetTargetControl(p)` (`0x8005AAB8`) and `frontSetTargetControl(p, v)` (`0x8005AAD0`) store one byte per controller at `0x800FF38D + p`, inside the front-end settings block that also holds stereo, volumes and screen mode. Inferred: it is saved with the options.
- **Menu.** The setting is edited on the Options page (`frontOptionsPage`, module 37), one row per connected controller ("1 - …"). The value strings in the ROM text are **"Normal Control"** and **"Expert Control"**, under the heading "Control Mode" (inferred order: 0 = Normal, 1 = Expert).
- **How it takes effect.** Each frame, `controlPlayer` sets `controlModeKeys` (`0x800A18B0`) to one of two 9-entry button-mask tables. The player code tests every remappable action as `controlKeys & controlModeKeys[slot]`.

| Slot (byte offset) | Action (from the code that uses it) | Normal (`0x800A18B4`) | Expert (`0x800A18D8`) |
|---|---|---|---|
| 0 (`+0x00`) | Fire (`controlUpdateWeapon`; also raises the gun, `pd+0x1F4`) | Z | Z |
| 1 (`+0x04`) | Next weapon (`controlSelectNextWeapon` `0x80038324`) | C-up | B |
| 2 (`+0x08`) | Previous weapon (`controlSelectPreviousWeapon` `0x80038528`) | C-down | A |
| 3 (`+0x0C`) | Jump (ground state → air state; also cancels the half-turn) | A | C-up |
| 4 (`+0x10`) | Crouch / roll (ground → state 1 or 2) | B | C-down |
| 5 (`+0x14`) | Strafe left (`pd+0x10` −), side-roll when crouched | C-left | C-left |
| 6 (`+0x18`) | Strafe right | C-right | C-right |
| 7 (`+0x1C`) | Aim state: Expert = walk forward; zoom weapon = zoom in | A | C-up |
| 8 (`+0x20`) | Aim state: Expert = walk back; zoom weapon = zoom out | B | C-down |

**Hard-wired buttons (not remappable):**
- **R** (`0x0010`): aim / crouch-aim, camera recentre, lock-on.
- **D-pad**: four quick-weapon slots, table `quickWeaponKey` `0x800A197C` = up, right, down, left, selecting `pd+0x534[i]`.
- **C-left / C-right**: also pan the follow camera (§4.7).
- START: pause.
- I found no gameplay use of L.

**What else Expert changes** (code paths that call `frontGetTargetControl` directly):
- Aim state: slots 7 and 8 walk forward (up to full run speed) and back (up to 3.0) while aiming (`m015_04658`, `0x820486D4`–`0x820487DC`). In Normal mode the forward speed just decays while aiming.
- `controlWalkingBack` (`0x80033D74`), back-pedalling, is Normal-only.
- `controlHalfTurn` (`0x800346C4`) runs while fire is held only in Expert (inferred from the branch).
- Follow camera: the "enemy nearby" flag forces camera follow only in Normal.
- Smaller tuning differences in the ground state (`0x82047010`) and in `controlFadePlayer` (`0x80033BB8`).

**Takeaway.** Expert Control is Rare's GoldenEye-style scheme: aim with the stick, move with the C buttons. So "move while aiming" already exists in the game, digitally and only on C-up/down/left/right.

## 4. The player-control pipeline

### 4.1 Data

**Object (`obj`).** Layout matches Rare's DKR-era object.

| Offset | Field |
|---|---|
| `+0x00` | s16 yaw (copied from `pd+0x11C` every frame) |
| `+0x02` | s16 pitch |
| `+0x04` | s16 roll |
| `+0x0C`/`+0x10`/`+0x14` | f32 position x/y/z |
| `+0x1C`/`+0x20`/`+0x24` | f32 velocity x/y/z (clamped to ±20, y to [−20, 15]) |
| `+0x68` | pointer to `pd` |

**Player data (`pd`).** Only the fields that matter here:

| Offset | Type | Meaning |
|---|---|---|
| `+0x000` | s8 | player index (`controlReadJoypad` argument) |
| `+0x001` | s8 | character (low 2 bits; 3 = hovership camera case; 2 = dog special case in auto-aim) (inferred) |
| `+0x004` | f32 | **forward speed** along the facing (**negative = forward**) |
| `+0x010` | f32 | **strafe speed**, ±2.5 from C-left/right |
| `+0x104` | s16 | follow-camera orbit heading |
| `+0x10A` | s16 | follow-camera C-pan offset (±`0x2000`) |
| `+0x10C` | u8 | follow-camera "recentre / follow" flag |
| `+0x11C` | s16 | **facing** (body yaw) |
| `+0x120` | s16 | lean from turning (inferred) |
| `+0x128` | s16 | heading used while airborne in some states |
| `+0x13C` / `+0x13E` | s16 / u8 | half-turn locked heading / active |
| `+0x1A4` | ptr | auto-aim target object |
| `+0x1C6` / `+0x1C8` | s16 | **aim yaw / pitch, absolute** |
| `+0x1CA` / `+0x1CC` | s16 | **fire direction** (aim + random spread). The crosshair is drawn from this. |
| `+0x1CE` / `+0x1D0` | s16 | **crosshair offset** from the body, yaw/pitch, smoothed |
| `+0x1DC` / `+0x1DE` | s16 | torso aim pose targets (inferred) |
| `+0x1E0` | s16 | smoothed pitch helper |
| `+0x1E2` | s16 | **aim pitch base**, accumulated by pushing the stick to the rim, clamped ±`0x2AAA` |
| `+0x1E4` / `+0x1E8` | f32 | rim-push turn rates, yaw/pitch |
| `+0x1EC` / `+0x1F0` | f32 | zoom (1…5) and zoom rate, for the zoom weapon (weapon type 7) |
| `+0x1F4` | s8 | fire-held / gun-raised timer (set to 15 while fire is held) (inferred) |
| `+0x1F6` / `+0x1F7` | s8 | recoil kick (inferred) |
| `+0x541` low nibble | u4 | "enemy nearby" countdown: set to 10 when a target is in auto-aim range. Drives back-pedalling and camera follow (inferred). |
| `+0x568` | u8 | **state**. 0 ground, 1 crouch, 2 roll (inferred), 3 airborne, 5 crouch-aim, 6 ledge hang (inferred), **11 aim**; 4, 7, 8, 10, 12 not identified. |
| `+0x569` | u8 | walking-back (back-pedal) active |
| `+0x56B` / `+0x577` | s8 | input-lock timers (force `disablejoy`) |
| `+0x584` | s8 | side-roll sub-state |

**Camera (`cam`).** Array returned by `camGetListPtr` (`0x80041E0C`), one 0x4C-byte entry per viewport.
- `+0x00` yaw, `+0x02` pitch, `+0x04` roll (s16).
- `+0x0C`/`+0x10`/`+0x14` position (f32).

Per-player camera tuning lives at `0x800F6E58 + 0x2C·player`, fields `+0x14`, `+0x18`, `+0x1C`, `+0x24` (inferred: distance and blend factors).

### 4.2 One frame of a character (Vela, `girlControl` `m015_00000`)

1. **Reset and lock.** Reset per-frame flags. If an input-lock timer or interaction flag is set, `controlDisableJoypad(1)`.
2. **Read.** `controlReadJoypad(pd->index)` (`0x820442F0`). Then `controlMakeGravity`, velocity clamps, `controlFSUvels`.
3. **Stick → desired motion** (`0x82044494`–`0x82044550`):
   - `mag = min(sqrt(X² + Y²), 65) × scale`. `scale` is 1/16 normally, other values in multiplayer or for large characters (inferred). So run speed tops out at about 4.06.
   - If `mag ≠ 0`: `heading = Arctanf(X, −Y) − cam->yaw`, i.e. **movement is camera-relative** (`Arctanf` is `0x80049C84`). Otherwise `heading = pd->facing`.
4. **Hip-fire auto-aim.** `controlUpdatePlayerAim` (`0x80039FCC`) unless the state is 5, 10 or 11 (§4.5).
5. **State handler** through a 13-entry jump table (`0x82044AD4`; table in module data at `0x8204C700`). Each handler takes `(obj, pd, mag, heading, ticks)`.
6. **Weapon selection.** Quick slots (D-pad), next / previous (key-map slots 1 / 2).
7. **Animation select.** `m015_05328`, then **facing → velocity** `m015_05ED4`:
   - `obj->yaw = pd->facing`.
   - `vel.x = fwd·sin(facing) + strafe·cos(facing)`, `vel.z = fwd·cos(facing) − strafe·sin(facing)`. (`sin` and `cos` here are the game's `Sinf` `0x80049C34` and `Cosf` `0x80049C30`.)
8. **Physics checks.** `controlSquashCheckPost`. Ledge checks `controlHangOK` (states 0, 5, 11) and `controlGrabOK` (state 3).
9. **Firing.** `controlUpdateWeapon` (`0x80037550`, called at `0x820458D0`).
10. **Camera.** After returning to `controlPlayer`, the camera updates (§4.7).

### 4.3 Movement (ground state 0: `m015_02B2C`, Juno `m016_02708`)

- **R held** → enter aim (§4.4).
- Otherwise:
  - `controlWalkingBack(obj, pd, &speed, heading, 0x5000, 0x6800, 5.0, 3.0)` caps the speed at 5.0.
  - In Normal mode, with an enemy nearby, pulling the stick more than ~146° (`0x6800`) away from the facing starts back-pedalling (`pd+0x569`). The character keeps facing forward and walks backwards (speed ≤ 3.0) until the stick comes back within ~112°.
  - Strafe helper `m015_04B4C(obj, pd, 0, ticks)`: C-left / C-right ramp `pd+0x10` to ±2.5 and pick a strafe animation when |strafe| > |forward|.
  - Jump (slot 3) and crouch/roll (slot 4) change state.
  - Fire (slot 0) held sets `pd+0x1F4 = 15`.
  - `controlHalfTurn`: reversing the stick at speed starts a skid / half-turn with a locked heading.
  - **Facing turns towards the heading:** `pd->facing = dAngle(facing, heading, rate)`. `dAngle` (`0x80033FA4`) is the generic "approach angle" helper with 145 call sites. The forward speed `pd+0x04` approaches the target speed (negated; forward is negative).

So in the original scheme **the character always faces where it runs**. The exceptions are strafing on C-left/right and back-pedalling.

### 4.4 The R aim mode (state 11: `m015_04658`, Juno `m016_04440`)

**Entry** (ground state, `0x82046BD8`):
- `facing −= pd->camPan`, then `camPan = 0`, so the character turns to where the camera was panned.
- State is set to 11 and the aim fields are cleared.
- `pd+0x1C6/0x1C8` (aim yaw/pitch) are set to the object's yaw/pitch.
- Crouch + R goes to state 5 (crouch-aim), which works the same way.

**Exit** (`0x8204869C`): R released and no aim-lock timer → state 0, and the follow-camera heading is reset behind the character (`pd+0x104 = 0x8000 − obj->yaw`).

**Each frame in state 11:**

1. **Movement.**
   - Expert mode (and not the zoom weapon): slot 7/8 held walks forward/back.
   - Otherwise the forward speed decays to 0.
   - The strafe helper runs (C-left/right, ±2.5).
   - The aim state has **no jump or crouch handling**.
2. **`controlGetManualAim(obj, pd, 0xE38, 0xE38, −0x2AAA, 0x2AAA, &yawOff, &pitchOff, ticks)`** (`0x8003AABC`, shared by every character and the boss and hovership variants):
   - It reads the **raw** stick (`0x800F6DB4`/`B8`), negated.
   - **Zoom weapon** (weapon type 7): slot 7/8 change `pd+0x1EC` by ±0.05/tick within [1, 5]. The function returns 1, and the caller then calls `camSetZoom` (`0x80042438`).
   - Sensitivity `s = 1 / ((z − 1)·z/2 + 1)` for zoom `z`.
   - **Position mapping:** `yawOff = 0xE38·s·clamp(rawX, ±40)/40`, and the same for pitch. **The stick position *is* the crosshair offset**, ±20° at full tilt.
   - **Rim turning:** when |raw| > 45 and the crosshair is already near its limit, a turn rate comes from the table at `0x800A2E60` (0, 2, 6, 12, …, 380, i.e. n(n+1)) indexed by `min(|raw| − 45, 19)` and scaled by `s`. It is smoothed into `pd+0x1E4`/`0x1E8`. Then `facing += rate·ticks`, and `pd+0x1E2` (pitch base) `+= rate·ticks`, clamped to ±60°. The maximum is 380 units per tick ≈ 2.1°/tick, so ≈ 125°/s at 60 ticks/s (inferred tick length).
   - Because the runtime reports up to ±127, a PC stick reaches the maximum rim speed at about half tilt (|raw| ≥ 64). An N64 pad needs about ±64 of its ±80 range.
3. **Smoothing.** `dAngle` with `1 − Powerf(k, ticks)` moves the offsets towards their targets:
   - crosshair offset `pd+0x1CE/0x1D0`;
   - torso pose `pd+0x1DC/0x1DE`;
   - pitch `pd+0x1E0`, with `pitch = pitchOff + pd+0x1E2`.
4. **Absolute aim.** `pd+0x1C6 = obj->yaw + pd+0x1CE`, `pd+0x1C8 = obj->pitch + pd+0x1D0`, copied to the fire direction `pd+0x1CA/0x1CC`. Recoil (`pd+0x1F6/0x1F7`) is added to the pitch.
5. **Pose.** `m015_04088` sets the torso/head pose from the smoothed values (inferred).

### 4.5 Firing and its direction

- **Trigger.** `controlUpdateWeapon` (`0x80037550`) reads slot 0 (Z) from `controlKeys` / `controlDkeys`. It handles charge weapons (`controlGetMaxCharge`) and calls the projectile spawner (static `0x800386B0`, four call sites).
- **Direction.**
  - The spawner adds random spread to the fire direction: `pd+0x1CA += mathRnd(±def+0x08)`, `pd+0x1CC += mathRnd(±def+0x0A)` (`mathRnd` is `0x80048E2C`).
  - It fires along `pd+0x1CA` (yaw).
  - Pitch is `pd+0x1CC` in states 5 and 11, or when `pd+0xFC` is set. Otherwise it is a fixed `0x2000` (meaning not determined). I read this only for the first projectile type in the spawner's jump table; the other types may differ.
- **Hip fire** (`controlUpdatePlayerAim`, `0x80039FCC`, every state except 5, 10 and 11):
  - Per-weapon parameters come from `weaponTable` (`0x800A1490`, 48-byte entries: range at `+0x0C`, cone at `+0x14`, more at `+0x18`–`+0x20`).
  - It picks the nearest target inside the range and inside the yaw/pitch cone around the **object's** yaw/pitch. It sets aim and fire direction to that target (`pd+0x1A4` = target) and starts the "enemy nearby" countdown.
  - With no target, aim = the object's yaw/pitch, i.e. straight ahead.
- **Crosshair.** `frontPlayerTarget` (module 13, `m013_00008`):
  - It casts a ray from the gun barrel (`controlGetGunBarrelPos` `0x8003B418`) along `pd+0x1CA/0x1CC`, using `trackNearestIntersection` `0x80018808`.
  - It runs `hitPlayerAimCheck` (enemy under the sight; lock-on while R is held).
  - It projects the hit point with `camProjectPoint` (`0x80041E58`) and draws the sight with `frontDrawTarget` (`m013_004A8`).

  So **the crosshair always appears wherever the aim direction hits the world**. Any new aim logic that writes `pd+0x1C6…0x1CC` gets a correct crosshair for free.

### 4.6 Strafing, rolling, jumping

- **Strafe** (`m015_04B4C`, Juno `m016_04934`): mode 0 (ground and aim states) ramps `pd+0x10` with C-left/right to ±2.5 and decays it otherwise. Modes 1 and 2 (crouched states) start side-roll animations instead (`pd+0x584`).
- **Jump** (slot 3 pressed in the ground state): state 3, y-velocity += 10. Air control is in `m015_03740`.
- **Crouch / roll** (slot 4 pressed in the ground state): state 1 or 2 depending on speed and stick. The exact animation mapping is not confirmed.
- **Ledges.** `controlHangOK` (`0x80035E14`) and `controlGrabOK` (`0x800364D4`) can switch to the hang state (6) from states 0, 5, 11 and 3.

### 4.7 The camera, and what makes it turn

**The dispatcher** (static `0x8002B418`, called at the end of `controlPlayer`) picks one camera per frame, first match wins (order as in the code):

1. Cutscene: `animseqCamera` `0x80079890`.
2. Special per-situation cameras, chosen by flags in `pd`:
   - module cameras m085, m082, m075, m127, m029;
   - static `0x8002BD04`.
3. Static camera: `staticcamInuse` `0x8004271C` → `staticcamPosition`.
4. More special per-situation cameras: m068, static `0x8002B960`, static `0x8002C070`, m022, m129.
5. State 11 → **aim camera** (static `0x8002C118`).
6. Hovership (`pd+1 & 3 == 3`) → `hovershipCamera` (m053).
7. State 5 → **aim camera**.
8. `lobbycamInuse` `0x8004270C` → static `0x8002C908`.
9. `pd+0x1FC ≥ 0` → fixed-point camera (static `0x8002C9A0`; uses the D-pad).
10. Otherwise → **follow camera** (static `0x8002CF6C`).

After the camera is chosen, the dispatcher applies water-line and region fix-ups.

**Follow camera** (`0x8002CF6C`):

- **Follow flag `pd+0x10C`.** Set when:
  - |forward speed| > 0.75 (0.5 in state 2);
  - or R is held;
  - or, in Normal mode, the "enemy nearby" countdown is running.
- **C-pan.** If none of those apply, C-right / C-left pan `pd+0x10A` by `ticks·256` per frame, clamped to ±45°, and clear the follow flag. When following, the pan decays back to 0 (faster with R).

  Inferred: C-left / C-right both strafe the character **and** pan the camera while it stands still.
- **Orbit heading `pd+0x104`.**
  - While following or panning: `dAngle` towards `0x8000 − obj->yaw (+ pan)`, i.e. **behind the facing**.
  - Otherwise: recomputed from where the camera already is, so **a standing player's camera does not swing round**.
- **Placement.** The camera target is placed at the orbit heading, at a distance and height from the per-player tuning block, with smoothing and collision. Finally the camera **looks at the player**: `cam->yaw = dAngle(cam->yaw, 0x8000 − Arctanf(dx, dz))`, pitch from the height difference, roll → 0 (`0x8002E998`–`0x8002EA9C`).

**Aim camera** (`0x8002C118`):
- It clears the pan and sits over the shoulder: a body-relative offset rotated by the body yaw/pitch, plus `pd+0x544…0x54C`.
- A ray (`trackNearestIntersection`) pulls it in front of walls.
- `cam->yaw` approaches `0x8000 − obj->yaw`, and `cam->pitch` approaches the body pitch adjusted by the aim pitch base `pd+0x1E2` (`0x8002C55C`–`0x8002C864`).
- **It follows the body, not the crosshair.** Inside the ±20° box the crosshair moves on screen and the camera stays still. Only rim-pushing (turning the body or raising the pitch base) moves the camera.

**What makes the camera turn, in short:**
- Follow camera: the character's **facing**, while moving, when an enemy is near, or with R held.
- Follow camera, standing still: the C-left/right pan.
- Aim camera: **body yaw and aim pitch base**.

**Also present:** `cameraTopDown` (`0x8002EB6C`), a straight-down overhead camera used by `shipControl` (m054). C-up/down zoom it.

## 5. Proposed hook architecture

### 5.1 Principles

1. **Native hooks, not MIPS patches.** This repo builds no patches ELF; Zelda's was removed. Per the frontend survey, the mod loader can't target romtool's relocated code. N64Recomp `[[patches.hook]]` entries in `recomp/jfg.toml.in` paste C into the generated function, either at its start or `before_vram` an instruction:
   - A hook can call native C++ declared in a header listed in `recomp_include`.
   - It has `rdram` and `ctx` (registers `r4`–`r7` hold the arguments, `r29` is `sp`, floats are in `ctx->fN.fl` in FR=1 mode).
   - Ending the text with `return;` replaces the function.
   - Module functions are hooked by their `mNNN_OOOOO` name, and `before_vram` uses the ELF link address.
   - `[[patches.instruction]]` swaps single instructions.
   - Avoid `ignored` plus a native definition: the function then has no lookup entry, so calls through function pointers would break.
2. **Shared chokepoints first.** Prefer the main-segment functions every character goes through:
   - `controlPlayer`, `controlReadJoypad`, `controlGetManualAim`;
   - the camera dispatcher and the follow and aim cameras.

   Per-character module hooks only where the logic lives in the module: the aim-state movement, and the ordering around animation and velocity.
3. **No RDRAM mailbox.** The new inputs (right stick, mouse deltas, mouse buttons) are read natively from the frontend's SDL state inside hook code, in the same process. A reserved RDRAM area would only be needed if MIPS-side code had to read them, which it doesn't.

   If one is ever needed, first confirm what the game uses above 4 MB. `mainThread` references `0x80400000` and module 26 references `0x8080xxxx` (unverified).
4. **The original scheme stays byte-identical when off.** Every hook starts with `if (!jfg_modern_controls()) <fall through>`. The input path keeps the current bindings. Regression check: the scripted run to Goldwood (`JFG_INPUT_SCRIPT` in the handoff) gives the same frames with the mod compiled in and set to Original.
5. **Player 1 only** (controller 0). Floyd (player index 1) and split-screen multiplayer stay on the original controls unless decided otherwise.

### 5.2 Frontend additions (input source)

- **Option.** A config option "Control scheme: Original / Modern", saved in `general.json` the way the existing options are (`src/game/config.cpp`, `src/ui/ui_config.cpp`, `assets/config_menu/general.rml`). Add modern sub-options: aim sensitivity (stick), mouse sensitivity (exists; it already turns on relative mouse mode), invert Y, aim assist strength, and possibly the camera mode once chosen.
- **Mouse buttons as inputs.** Implement the `InputType::Mouse` digital path (currently "TODO") so the left button can fire and the right button can aim.
- **Modern action bindings**, separate from the N64 buttons: Fire, Aim (hold or toggle), Jump, Crouch/Roll, Next / Previous weapon, the four quick weapons, plus movement (left stick / WASD) and look (right stick / mouse). In Modern mode the right stick **must not** also drive the C buttons: unbind it, or use `recomp::set_right_analog_suppressed(true)`, which exists but nothing calls it yet.
- **Native API.** For example `jfg::controls::frame()`. It latches, once per game frame:
  - the left-stick vector (float, deadzone applied, not clamped to ±65);
  - the right-stick vector;
  - the summed mouse delta;
  - the modern action states.

  Latch it from a hook in `joyRead` (§5.3 H0) so the mouse delta is used exactly once per game frame.

### 5.3 Hook points

| # | Hook | Where | What it does in Modern mode | Covers |
|---|---|---|---|---|
| H0 | `joyRead` `0x80043130` | entry | `jfg::controls::frame()`: latch this game frame's modern input | all |
| H1 | `controlReadJoypad` `0x80037250` | before the common exit `0x8003732C` (`$s0` = player) | Rewrite this player's control globals: movement stick at analog precision (scaled to the game's ±65 and raw ranges), and action bits for the active key map (`controlModeKeys`), so jump, crouch, fire and weapon change work unchanged. Neutralise the C-pan bits if the chosen camera doesn't want them. | all characters |
| H1b | (frontend) `get_n64_input` | n/a | Put the synthesised **R** (aim active) into the N64 pad itself, so `joyGetButtons` users (lock-on in `hitPlayerAimCheck`) and the pressed/released edges stay consistent. Alternative: also hook `joyGetButtons` `0x800434E4` at its `jr ra`. | all |
| H2 | `controlGetManualAim` `0x8003AABC` | entry, `return;` when handled | Native replacement (math in §5.4). Writes `*yawOff`/`*pitchOff` (stack arguments), body turn `pd+0x11C`, pitch base `pd+0x1E2`, and returns the zoom flag. Keeps the zoom-weapon behaviour (zoom on mouse wheel / buttons). | all aim and crouch-aim states, all characters and boss variants. **Exclude the hovership** (`pd+1 & 3 == 3`). |
| H3 | Animation-select entry: `m015_05328`, `m016_05120`, `m017_04C50` (and the m050–m052 equivalents if wanted) | entry | Movement injection: set `pd+0x04` (forward) and `pd+0x10` (strafe) from the left stick, projected onto the facing (§5.4). Only in states 11 and 5, and in state 0 if the chosen scheme decouples facing on the ground. Doing it before animation select lets the game pick walk / strafe / back-pedal animations from the new speeds (to verify). | per character (3, or 6 with bosses) |
| H4 | `controlPlayer` `0x80032A48` | entry, and after `controlModeKeys` is set (`0x80032B40`) | Per-player gate: apply modern only for player index 0, a playable character, `disablejoy == 0`, and no special camera active. Optionally force the Normal key map in Modern mode **without touching the saved setting**. | all |
| H5 | Camera (choice-dependent, §6) | follow camera `0x8002CF6C` before the orbit-target code (`~0x8002DFE8`); aim camera `0x8002C118` before the yaw/pitch approach (`0x8002C7BC`); or the call sites of these two in the dispatcher (`0x8002B750`, `0x8002B7A4`, `0x8002B80C`) for a full native replacement | Drive the orbit heading / pitch from look input, or add the crosshair offset to the aim camera, or replace the camera. Leave the higher-priority cameras (cutscenes, static, lobby, special, hovership) alone. | all |
| H6 | Aim-state entry and exit | via H1/H1b (synthesised R) | "Hold to aim", "toggle aim" or "always aim" are all just patterns of the R bit. The game's own transitions (state 0↔11, 1↔5) and the aim camera do the rest. | all |

The exact `before_vram` values for the module hooks and the dispatcher must be confirmed against the generated code when implementing. A hook placed before an instruction that is also a branch target needs checking: the text must run on every path.

### 5.4 The new math (sketch)

**Units.**
- `ticks`: the frame's tick count (the `rate` argument the game passes).
- `dt = ticks/60` s (inferred).
- Angles: s16 binary units.

**Right stick (rate-based, replaces position mapping).**
```
look = deadzone_and_curve(right_stick)                      // radial deadzone, e.g. exponent 1.5–2
yaw   += look.x * yaw_speed_deg_s   * dt * 65536/360        // body (or camera) yaw
pitch += look.y * pitch_speed_deg_s * dt * 65536/360 * inv  // clamp ±0x2AAA
```
**Mouse (delta-based, frame-rate independent).**
```
yaw   -= mouse.dx * sens_units_per_count
pitch -= mouse.dy * sens_units_per_count * inv
```
**Turning the body and the pitch.** Write the body turn to `pd+0x11C` and the pitch to `pd+0x1E2`. Set `*yawOff`/`*pitchOff` to 0 (crosshair centred) or to a small lag/lead for feel. The game then smooths and builds `pd+0x1C6…0x1CC`, and the aim camera follows the body (§4.7).

**Movement while facing elsewhere.**
```
move   = left_stick rotated into world by (camera yaw)    // same frame of reference the game uses
fwd    =  dot(move, facing_dir)  * run_speed               // game convention: forward is negative
strafe =  dot(move, right_dir)   * strafe_speed
pd+0x04 = approach(pd+0x04, -fwd, accel)                   // keep the game's caps: 4.06 run, 3.0 back, 2.5 strafe
pd+0x10 = approach(pd+0x10, strafe, accel)
```

**Aim assist.** Optional:
- Keep the game's hip-fire `controlUpdatePlayerAim` (it is skipped in states 5, 10 and 11).
- Or run a light version in the aim state that nudges `*yawOff`/`*pitchOff` toward `pd+0x1A4`.

### 5.5 Cheapest prototype: the "Expert bridge"

The game already supports C-button movement while aiming in Expert Control, so a nearly hook-free prototype is possible:
- In Modern mode, synthesise **R** while aiming.
- **Force Expert behaviour** by hooking `frontGetTargetControl` to return 1 in Modern mode. Don't write the saved byte.
- **While in the aim states only** (`pd+0x568` ∈ {5, 11}), map the left stick to C-up/down/left/right digitally, at the 0.5 threshold.
- **Outside the aim states**, send the left stick as the N64 stick. In Expert, C-up is jump, so pushing up must not become C-up there.
- Translate the modern Jump, Crouch and weapon buttons through the Expert key map.
- Replace only `controlGetManualAim` (H2), so the right stick or mouse drives the crosshair and body turn.

This gives digital-speed movement while aiming with the over-shoulder camera. It validates the input plumbing (H0/H1/H2) and the mouse feel before any per-character work.

Limits:
- Movement while aiming is not analog.
- Strafe tops out at 2.5.
- No jumping while aiming.

## 6. Camera behaviour options (for the user to decide)

All options keep the original scheme available behind the toggle. The cost estimates are relative and assume the shared plumbing (H0–H2, H4) is done.

### Option 1: camera follows the aim ("modern third-person shooter")

The right stick or mouse turns the **character** (body yaw `pd+0x11C`) and the aim pitch. The camera stays behind the aim. The left stick moves forward/back/strafe relative to where you aim. Two flavours:

- **1a, over the shoulder (always in the game's aim camera).** "Always aim" = R permanently held while on foot. The crosshair is centred and the aim camera follows the body exactly.
  - Pros:
    - It feels like modern shooters.
    - The mouse feels natural.
    - Precise aiming at the edge of the screen is no longer needed.
    - The game's aim camera, crosshair raycast and lock-on are reused as they are.
  - Cons:
    - The aim state has **no jump or crouch**. The mod would need to drop out of aim for a jump or roll and return afterwards, which touches the state machine.
    - The view is close and narrow, while JFG throws swarms of enemies from all sides.
    - The aim camera was tuned for standing still.
    - Back-pedal and strafe animations will be on screen constantly.
  - Cost: medium. H1, H2, H3 and the jump/roll handling in the per-character aim states.
- **1b, behind the back (the original follow camera, forced to stay behind the facing).** Stay in the ground state, so jump, roll, crouch and ledges all keep working. The right stick X or mouse X turns the body. Y sets the aim pitch, written into the aim fields so the crosshair and the fire direction follow it. Force the follow flag on so the camera stays behind the facing. Hold Aim (LT or right mouse button) for the over-the-shoulder 1a view when precision is needed.
  - Pros:
    - Every move still works.
    - It keeps JFG's wide view.
    - It is closest to "the original game, but you can aim and move independently".
  - Cons:
    - The camera pitch doesn't follow aim pitch unless the follow camera's height/pitch is also driven (H5).
    - Facing ≠ movement direction in the ground state relies on the ground state's strafe and back-pedal animations under conditions they weren't designed for (to verify).
  - Cost: medium. H1, H2/H3 in the ground state, a small follow-camera hook.

### Option 2: free camera, aim at screen centre ("modern action/adventure")

The right stick or mouse **orbits the camera** (yaw and pitch) around the character, independent of the facing. The left stick moves relative to the camera, and the character faces the direction it runs, as in the original game. Firing (or holding Aim) turns the character towards the camera's forward direction and switches to strafing movement while the trigger is held. Aim = the point under the screen centre, or hip-fire auto-aim limited to a cone around camera-forward. Holding Aim can switch to the game's over-the-shoulder camera as in 1a.
- Pros:
  - The most familiar modern scheme.
  - Looking around is independent of moving, which is good for exploring.
  - It works equally well on gamepad and mouse.
- Cons:
  - Needs real new camera behaviour: orbit yaw/pitch from input, collision, and coexistence with the dispatcher's special cameras. It can be done by driving the follow camera's orbit heading `pd+0x104` and its height (H5) rather than writing a new camera, but pitch control there is limited.
  - Screen-centre aiming needs a raycast from the **camera** to find the target point, then aiming the **gun** at it (the gun is offset from the camera). That means calling the game's `trackNearestIntersection` from native code or reimplementing it.
  - The turn-to-face-camera transition needs tuning to look right.
  - Some JFG rooms are built around the original camera, so free pitch may show off-map geometry.
- Cost: high.

### Option 3: original camera (behind the movement), independent twin-stick aim

The left stick moves as in the original game, and the camera keeps following the direction of travel. The right stick picks an aim direction **relative to the screen**, and the character's body turns to it while moving with strafe/back-pedal. The mouse controls an on-screen cursor: aim = the world point under the cursor (a raycast through the cursor). The game's auto-aim fills in pitch.
- Pros:
  - The classic twin-stick shooter feel, which suits JFG's swarm combat.
  - It keeps the original camera's look.
- Cons:
  - The original camera follows the **facing**, not the movement, so it must be changed to follow the velocity direction.
  - Aiming back towards the camera is awkward: you can't see what you shoot.
  - When the camera re-orbits behind a new travel direction, the screen-relative aim frame rotates under the stick.
  - Cursor aiming in a low behind-the-back perspective is imprecise with distance.
  - Vertical aim is implicit.
- Cost: medium to high.

### Option 4: top-down camera

Use the overhead `cameraTopDown` idea (or a fixed-yaw high-angle camera). The left stick moves in screen space; the right stick or mouse aims in screen space.
- Pros:
  - The purest twin-stick and mouse mapping.
  - The camera code already exists in the game, in a basic form.
- Cons:
  - JFG's levels, ceilings and region-based visibility are built for a ground-level camera. Interiors would show holes or need ceiling hiding, and draw distance and culling may break (unknown cost).
  - No vertical aiming.
  - The game would look very different.
- Cost: medium for the camera, unknown and probably high for the visuals. Listed for completeness.

### Comparison

| | 1a Shoulder | 1b Behind-the-back | 2 Free camera | 3 Twin-stick, original camera | 4 Top-down |
|---|---|---|---|---|---|
| Gamepad feel | modern TPS | classic + independent aim | modern action | twin-stick | twin-stick |
| Mouse feel | excellent | very good | excellent | fair (cursor) | very good |
| Jump, roll, ledges | need state work | unchanged | unchanged | unchanged | unchanged |
| View of the action | narrow | original | free | original | wide, but rendering risk |
| New camera work | none (reuse aim camera) | small | large | medium | medium + rendering |
| Overall cost | M | M | H | M–H | M + unknown |

Option 1b plus "hold Aim for 1a" fits the game's existing two modes (follow / aim) closely and is the smallest step that delivers both requirements. Option 2 is the most modern. **This is the user's call** (§9).

For reference only: my recollection is that Rare Replay (2015) shipped an optional twin-stick scheme for JFG. I have not verified this; Rare's choice could be a useful comparison.

## 7. Risks and unknowns

### 7.1 Risks

- **Per-character duplication.** The aim, ground and velocity logic is copied in modules 15, 16 and 17, with boss variants in 50–52. Module hooks multiply with the characters, and their addresses differ per module.
- **The aim state can't jump or crouch.** "Always aim" designs must handle leaving and re-entering state 11 (and 5).
- **Animations.** Decoupling facing from movement drives the walk / strafe / back-pedal animation choice into combinations Rare didn't plan for. Expect foot-sliding or the wrong animation until it is tuned.
- **Lock-on reads R directly** (`hitPlayerAimCheck` via `joyGetButtons`). Synthesise R at pad level (H1b) or lock-on will break.
- **Stick scaling.** The runtime maps full tilt to ±127, while the game expects about ±80. Its movement clamp is ±65 and its aim mapping saturates at ±40 (position) and ±64 (rim). This already affects the original scheme on PC pads, which reach full speed at about half tilt. The modern path reads floats natively and avoids it. The original path might want a frontend scale option.
- **Frame rate.** Variable tick count: mouse deltas once per game frame, stick rates × ticks. Mouse latching happens in `poll_inputs`, called from `osContStartReadData` inside `joyRead`. `joyRead` also has other callers (`mainThread` and two static loops), so the latch must be keyed to the gameplay frame.
- **Special cameras and cutscenes.** Respect `disablejoy` and the input-lock timers, and leave the dispatcher's higher-priority cameras alone.
- **Hovership, ship, Floyd and multiplayer** go through the same helpers (`controlGetManualAim` is also called by `hovershipControl`). The modern logic must be gated by character and player index.
- **Save data.** Don't overwrite the user's saved Control Mode. Override the key-map pointer at run time if needed.
- **Calling game functions from native code** (raycasts for Option 2 or 3) means building a `recomp_context` call by hand. It is possible but fiddly; keep it to one well-tested helper.

### 7.2 To confirm at run time

Static analysis can't settle these. They need a debug overlay or log, run on an instance that doesn't collide with the display tests:
1. The state numbering (especially 1, 2, 4, 6–8, 10 and 12) and that Vela, Juno and Lupus share it.
2. Option order: 0 = Normal Control, 1 = Expert Control.
3. Angle conventions: `Arctanf` argument order, the sign of "forward" in `pd+0x04`, and `cam->yaw ≈ 0x8000 − body yaw` when behind.
4. That `ticks` is in 1/60 s units, and its typical value in Goldwood (2–3 expected).
5. That `pd+0x541` (low nibble) is "enemy nearby", and how strongly it gates back-pedalling and the camera.
6. That writing `pd+0x04` and `pd+0x10` before animation select gives sensible strafe/back-pedal animations.
7. That C-left/right really both strafe and pan the camera when standing still.
8. That the 4 MB+ RAM area is free (only if a mailbox is ever needed).

### 7.3 Tooling recommendation (outside this sub-project)

Add a `romtool symbols` command that emits the decoded name table into `generated/`. It is ROM-derived, so it must never be committed. N64Recomp can then name about 1,290 main-segment functions and about 600 module functions. That makes generated code and crash logs readable, and lets `recomp/function_overrides.toml` drop hand-identified names. Note that the function names quoted in this document are a small set chosen for readability, not the table.

## 8. Implementation plan outline (ordered)

1. **Groundwork (Original-safe).**
   - Add `jfg_controls.h` to `recomp_include`.
   - Add the hooks H0, H1 and H4 with all-pass behaviour and a debug env var (e.g. `JFG_DEBUG_CONTROLS=1`) that logs the player state, facing, aim fields and camera yaw once per frame.
   - Verify that Original mode is unchanged (scripted run to Goldwood, screenshot diff).
   - Settle the §7.2 questions with the log.
2. **Frontend input.** The Control scheme option, mouse-button bindings, modern action bindings, right-stick unbinding in Modern, and the once-per-game-frame latch.
3. **Aim replacement (H2).** Rate-based right stick and delta-based mouse aim, sensitivity, invert, and zoom weapon on wheel / buttons. Test in the game's normal R-aim mode first.
4. **Expert-bridge prototype (§5.5).** Synthesised R, left stick → C buttons. First playable "move while aiming". Get user feedback on feel.
5. **Camera decision** by the user (§6, §9).
6. **Analog movement while aiming** (H3 in modules 16, 15 and 17; boss variants if needed). Tune speeds and animations.
7. **Chosen camera behaviour** (H5), plus "always / hold / toggle aim" patterns (H6), and jump/roll handling if Option 1a is chosen.
8. **Aim assist and polish.** Hip-fire cone, assist strength, UI prompts (button glyphs may show N64 names), and multiplayer gating.
9. **Regression and acceptance.** Original-mode screenshot diff, a scripted Modern-mode run through Goldwood, and the user's play test on both gamepad and keyboard+mouse.

## 9. Decisions for the user

1. **Camera behaviour:** Option 1a, 1b, 2, 3 or 4 (§6), or a combination such as "1b, hold Aim for 1a".
2. **Aim activation:** always aiming, hold to aim (LT / right mouse button), or toggle.
3. **Aim assist:** keep JFG's hip-fire auto-aim? How strong in Modern mode?
4. **Default bindings** for Modern (e.g. RT / left mouse button fire, LT / right mouse button aim, A / Space jump, B / Ctrl crouch-roll, LB/RB or wheel weapon cycle, D-pad / 1–4 quick weapons), and where sniper zoom goes (wheel? right-stick click?).
5. **Scope:** Modern for player 1 only (Floyd and multiplayer stay original)? Include the boss-fight variants and the hovership?

## Appendix: address quick reference

| Purpose | Name | Address |
|---|---|---|
| Pad read per frame | `joyRead` | `0x80043130` |
| Held / pressed / released | `joyGetButtons` / `joyGetPressed` / `joyGetReleased` | `0x800434E4` / `0x80043510` / `0x80043534` |
| Stick (clamped / raw) | `joyGetStickX/Y`, `joyGetAbsX/Y` | `0x80043558`, `0x800435C8`, `0x8004359C`, `0x8004360C` |
| Per-player input sample | `controlReadJoypad` | `0x80037250` |
| Input globals | `controlKeys`, `controlDkeys`, released, `controlXjoy`, `controlYjoy`, raw X/Y, `disablejoy`, `controlcam` | `0x800F6DA0`–`0x800F6DC0` |
| Key-map pointer / tables | `controlModeKeys`; Normal; Expert; `quickWeaponKey` | `0x800A18B0`; `0x800A18B4`; `0x800A18D8`; `0x800A197C` |
| Control Mode option | `frontGetTargetControl` / `frontSetTargetControl`, storage | `0x8005AAB8` / `0x8005AAD0`, `0x800FF38D+p` |
| Player update (common) | `controlPlayer` | `0x80032A48` |
| Character control | `girlControl`, `boyControl`, `dogControl` | `m015_00000`, `m016_00000`, `m017_00000` |
| Manual aim (R) | `controlGetManualAim` | `0x8003AABC` |
| Hip-fire auto-aim | `controlUpdatePlayerAim` | `0x80039FCC` |
| Fire | `controlUpdateWeapon`; projectile spawn (static) | `0x80037550`; `0x800386B0` |
| Back-pedal / half-turn | `controlWalkingBack` / `controlHalfTurn` | `0x80033D74` / `0x800346C4` |
| Angle approach | `dAngle` | `0x80033FA4` |
| Crosshair | `frontPlayerTarget`, `frontDrawTarget`; `hitPlayerAimCheck` | `m013_00008`, `m013_004A8`; `0x8007BCD0` |
| Camera list / zoom | `camGetListPtr` / `camSetZoom` | `0x80041E0C` / `0x80042438` |
| Camera dispatcher / follow / aim (static) | none (static) | `0x8002B418` / `0x8002CF6C` / `0x8002C118` |
| Top-down camera | `cameraTopDown` | `0x8002EB6C` |
| Weapon table | `weaponTable` | `0x800A1490` |
| Symbol names / values (ROM) | none | `0x1FEB040` / `0x1ED0270` |
