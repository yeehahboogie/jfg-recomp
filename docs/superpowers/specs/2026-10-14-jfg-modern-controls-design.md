# Jet Force Gemini modern controls: design

Status: design approved in conversation 2026-10-14; this spec awaits the user's review.

Branch: `modern-controls`, cut from `base-port`.

Research this design builds on: `docs/superpowers/specs/2026-10-14-jfg-controls-research.md`. Addresses, offsets and function names below come from it. Anything marked *(inferred)* there is still to confirm at run time.

## 1. Intent

**What the user wants.** JFG's original controls are awkward: one stick both moves and aims, and holding R stops you to aim. The user wants modern controls on both a twin-stick gamepad and mouse plus keyboard, with moving while aiming possible on both. The original controls must stay available.

**Decisions made with the user:**

| Topic | Decision |
|---|---|
| Camera | Free camera (research option 2) is the main mode. The right stick or mouse orbits the camera, and movement is relative to the camera. |
| Further camera modes | Wanted as selectable options later: 1a over-the-shoulder, 1b behind-the-back, 3 twin-stick with the original camera. Built after the free camera feels right. Top-down is out. |
| Hip fire (fire without aiming) | JFG's own auto-aim at the nearest enemy in front of Juno. The free camera is for looking. |
| Holding Aim | JFG's R aim mode: the game's over-the-shoulder aim camera, the crosshair moving within its box, and the body turning at the edge. The mouse or right stick drives it, and you can walk and strafe while aiming. |
| Camera when you stop looking | Drifts back behind Juno's running direction after a pause (default 1.5 s, adjustable, can be turned off). |
| Free-camera approach | A: steer the game's own follow camera (orbit heading, height, follow flag), rather than a new native camera. Fallback if its pitch range proves too tight: B, a native orbit camera. |
| Walking while aiming | Analog, via hooks in the character modules (not the Expert-mode bridge). |
| Defaults | The scheme defaults to Modern. Bindings are as in §5. |
| Scope | Player 1, on foot. Floyd, multiplayer, the hovership and cutscenes stay original. |

**Success means** playing through Goldwood comfortably with the free camera, on both a gamepad and mouse plus keyboard. With the scheme set to Original, the game behaves exactly as before.

## 2. Key facts from the research that shape the design

- **Movement is already camera-relative.** `heading = Arctanf(X, −Y) − cam->yaw` (Vela `0x82044494`–`0x82044550`, same in the other characters). Out of aim mode the character turns towards that heading and runs. Steering the camera is therefore enough for "the left stick moves relative to the camera".
- **The follow camera** (`0x8002CF6C`) orbits at heading `pd+0x104`. It swings behind the facing while the follow flag `pd+0x10C` is set (running, enemy near, R held), and otherwise keeps its heading. The per-player tuning block at `0x800F6E58 + 0x2C·player` holds its distance and height *(inferred)*.
- **The R aim mode** (state 11, and 5 when crouched) goes through `controlGetManualAim` (`0x8003AABC`), which is shared by all characters. It writes the crosshair offsets (stack outputs), the body facing `pd+0x11C` and the pitch base `pd+0x1E2`, and returns the sniper-zoom flag. The crosshair is drawn wherever the fire direction `pd+0x1CA/0x1CC` hits the world, so new aim logic gets a correct crosshair for free.
- **Velocity** is `forward·dir(facing) + strafe·perp(facing)`, built from `pd+0x04` (negative = forward) and `pd+0x10`. Moving while facing elsewhere fits the engine as it is.
- **Input** reaches player code through `controlReadJoypad` (`0x80037250`) into the control globals (`0x800F6DA0`–`0x800F6DBC`). The pad itself is read once per frame in `joyRead` (`0x80043130`).

## 3. Architecture

Native C++ in `src/jfg/controls/`, wired in through N64Recomp `[[patches.hook]]` entries in `recomp/jfg.toml.in` (calls declared in a header listed in `recomp_include`). No MIPS patches, and no reserved RDRAM.

### 3.1 Units

| Unit | File(s) | Purpose | Interface | Depends on |
|---|---|---|---|---|
| Modern input | `input_state.{h,cpp}` + frontend `src/game/controls.cpp` | Latch one snapshot per game frame: move vector, look vector, mouse delta (each delta consumed exactly once), and action states. In Modern mode, also synthesise the N64 buttons the game reads (§5.2). | `const ModernInput &jfg::controls::frame()`, latched by the `joyRead` hook. `jfg::controls::n64_buttons(actions)` for the frontend. | SDL state via the frontend's input layer; settings |
| Settings | frontend config (`src/game/config.cpp`, `src/ui/ui_config.cpp`, `assets/config_menu/*.rml`) | The options in §5.1, saved in `general.json` | `jfg::controls::settings()` | frontend |
| Camera controller | `camera.{h,cpp}` | Pluggable camera mode. One mode for now, `FreeCamera`, whose state is look yaw/pitch and the drift timer. Before the game's follow camera runs, it writes orbit heading, height and follow flag. After any frame where another camera ran, it re-syncs from the real camera. | `class CameraMode { virtual void before_follow(Ctx&); virtual void after_camera(Ctx&); }`, `FreeCamera : CameraMode`. The pure part is `FreeCameraState step(FreeCameraState, const ModernInput&, const GameView&, Settings, dt)`. | input, settings, game-view accessors |
| Aim controller | `aim.{h,cpp}` | Native replacement for `controlGetManualAim` in Modern mode. The mouse moves the crosshair offset within its box and overflow turns the body and pitch base. The right stick uses the original position mapping with the range corrected. Sniper zoom is kept (§5.2). | Pure part: `AimOutput aim_step(AimState, const ModernInput&, const AimLimits&, Settings, ticks)`. The hook wrapper reads and writes `pd` and the stack outputs. | input, settings |
| Aim movement | `aim_move.{h,cpp}` | In aim states 11 and 5, set forward and strafe speed from the move vector projected onto the facing, within the game's caps (forward and back as in the game, strafe 2.5) and its acceleration. | Pure part: `MoveOutput aim_move(float fwd, float strafe, Vec2 move, s16 facing, s16 camYaw, ticks)`. Hook wrappers per character. | input |
| Gate and wiring | `gate.{h,cpp}`, `include/jfg_controls_hooks.h`, `recomp/jfg.toml.in` | Decide each frame whether Modern applies (§6). Expose the hook entry points. | `bool jfg::controls::active_for(pd)`, set from the `controlPlayer` hook | all |

The pure parts take plain values (angles as s16, speeds as float, inputs as floats) and have no RDRAM access, so they can be unit-tested.

### 3.2 Hooks

| Hook | Function / site | Calls |
|---|---|---|
| H0 | `joyRead` `0x80043130`, entry | `jfg::controls::latch()` |
| H4 | `controlPlayer` `0x80032A48`, entry | `jfg::controls::begin_player(rdram, pd)`: computes the gate for this player |
| H2 | `controlGetManualAim` `0x8003AABC`, entry; ends with `return;` when handled | `jfg_aim_hook(rdram, ctx)`: returns early (falls through to the original) when the gate is off or the character is the hovership |
| H3 | Animation-select entry of each character: Vela `m015_05328`, Juno `m016_05120`, Lupus `m017_04C50` | `jfg_aim_move_hook(rdram, ctx)`: only in states 11 and 5 |
| H5 | Follow camera `0x8002CF6C`, before the orbit-target code (≈`0x8002DFE8`; confirm against the generated code) | `jfg_free_camera_before(rdram, ctx)` |
| H5b | Camera dispatcher `0x8002B418`, at its end (after the water-line and region fix-ups) | `jfg_camera_after(rdram, ctx)`: records which camera ran, and re-syncs the free camera when it wasn't the follow camera |

Exact `before_vram` addresses are confirmed against the generated C when implementing. A hook before a branch target is checked to run on every path into it.

## 4. Behaviour

### 4.1 Per-frame flow

1. **H0:** latch the input snapshot. The frontend has already written the N64 pad with the synthesised buttons (§5.2), so the game's own code handles jump, crouch and roll, fire, weapon changes, entering and leaving aim mode (R), and lock-on.
2. **H4:** compute the gate for player 0.
3. **Character update (game code):**
   - Camera-relative movement and turning, and hip-fire auto-aim, all unchanged.
   - In states 11 and 5: H2 (aim controller) and H3 (aim movement) run.
4. **Camera dispatcher (game code):**
   - If it runs the follow camera, H5 runs first (§4.2).
   - If it runs the aim camera, that camera follows the body the aim controller turns; nothing is added.
   - H5b runs at the end.

### 4.2 Free camera

**State:** `yaw` (s16, an orbit heading in the game's `pd+0x104` convention), `pitch` (float, −1…1), `idle` (seconds since the last look input).

**Each follow-camera frame (H5):**

1. **Look input.**
   - Mouse: `yaw −= dx · sens_mouse`, `pitch −= dy · sens_mouse_pitch · inv`.
   - Right stick: `yaw −= x · speed_stick · dt`, `pitch −= y · speed_stick_pitch · dt · inv`, after a radial deadzone and response curve.
   - `pitch` is clamped to [−1, 1].
   - Any look input above a small threshold resets `idle` to 0; otherwise `idle += dt`.
2. **Write the game state.**
   - `pd+0x104 = yaw`.
   - Height: the camera height is the tuning height scaled by `pitch`, between the configured low and high factors (pitch 0 = the game's default height). Applied by adjusting the height the follow camera reads for this frame (per-player tuning block, field to confirm), and restoring it afterwards so the saved tuning is never changed.
   - Follow flag `pd+0x10C`:
     - cleared while `idle < drift_delay`, so the game won't swing the camera;
     - left to the game's own logic once `idle ≥ drift_delay`, so it swings behind Juno's facing while she runs (drift-back);
     - drift-back disabled in settings: always cleared.
   - C-pan `pd+0x10A = 0`.
3. **After the game's follow camera has run** (in H5b): `yaw = pd+0x104`. During drift-back the state then follows the game's swing, and the next look input continues from where the camera actually is.

**Re-sync (H5b):** when the frame's camera wasn't the follow camera (aim, cutscene, static, special, hovership), set `yaw` from the real camera's yaw (`0x8000 − cam->yaw` → orbit convention, confirmed in testing), set `pitch` to 0 and `idle` to `drift_delay`. Returning to the follow camera then starts where the view already is, and drifts normally. Leaving aim mode is covered by this: the game resets `pd+0x104` behind Juno on exit.

**Fallback (approach B).** If the follow camera's height range can't give enough look-up and look-down in play, the free camera becomes a native orbit camera behind the same `CameraMode` interface. It computes position from Juno's position, yaw, pitch and distance, resolves collision with the game's ray-cast (`trackNearestIntersection` `0x80018808`, called through the recompiled function), applies its own smoothing, and writes `cam` directly. That is decided after the first playtest; nothing else in the design changes.

### 4.3 Aim mode

Entering aim is the game's own R handling, triggered by the synthesised R: hold Aim (default) or toggle. Leaving works the same way.

**Aim controller (H2),** called as `controlGetManualAim(obj, pd, yawLim, pitchLim, pitchMin, pitchMax, &yawOff, &pitchOff, ticks)`:

- **Mouse.**
  - The crosshair offsets accumulate the deltas (`yawOff −= dx · sens_aim`, `pitchOff −= dy · sens_aim · inv`), clamped to the box (`±yawLim`, `pitchMin…pitchMax` as passed).
  - The part of a delta that would push past the box edge turns the body instead (`pd+0x11C`), or moves the pitch base (`pd+0x1E2`, clamped to ±0x2AAA) for pitch.
  - With the zoom weapon zoomed in, sensitivity is scaled by the game's own factor `s = 1 / ((z − 1)·z/2 + 1)`.
- **Right stick.**
  - The game's own mapping: stick position = crosshair offset, and rim-pushing turns the body at the game's rate table.
  - The stick is rescaled so the PC range (±127) matches the N64 range the game expects (±80 for full tilt), so half tilt no longer reaches the rim.
- **Mixed input.** When both are active in a frame, the mouse wins.
- **Zoom weapon (type 7).** Zoom in and out on the mouse wheel, or LB/RB on the gamepad, changing `pd+0x1EC` by the game's steps within [1, 5]. Returns 1 when zooming, so the caller applies `camSetZoom`, as in the original.
- **Hovership:** H2 falls through to the original function.

**Aim movement (H3).**

In states 11 and 5 the move vector is taken relative to Juno's facing:
- forward = its component along the facing;
- strafe = its component across the facing.

These are scaled to the game's limits: walking forward and back as in Expert mode, and strafe ±2.5. The current `pd+0x04` and `pd+0x10` move towards them at the game's acceleration. The game then builds the velocity and picks walk, strafe and back-pedal animations from these speeds *(to verify; if animation select ignores them in aim states, H3 also sets the animation)*.

In crouch-aim (state 5), where the game rolls sideways instead of strafing, the left stick's sideways component triggers the game's side roll through its own C-left/right path, by synthesising those buttons. There's no analog crouch-walk.

## 5. Settings and bindings

### 5.1 Settings

All are new except mouse sensitivity, which already exists.

| Setting | Values | Default |
|---|---|---|
| Control scheme | Modern / Original | Modern |
| Camera mode | Free (later also Shoulder, Behind, Twin-stick) | Free |
| Camera drift-back | Off, 0.5–5 s | 1.5 s |
| Mouse sensitivity | existing slider | existing default |
| Right-stick look speed | slider | ≈ 180°/s yaw, 120°/s pitch at full tilt |
| Aim-mode sensitivity | multiplier | 0.6 |
| Invert look Y (mouse) / (stick) | on / off | off / off |
| Aim button | Hold / Toggle | Hold |

### 5.2 Bindings and synthesised N64 buttons

The modern actions are bound in the existing controls menu, alongside the N64 button bindings that Original mode uses. In Modern mode the frontend builds the N64 pad the game reads from these actions:

| Action | Gamepad | Mouse + keyboard | Becomes (N64) |
|---|---|---|---|
| Move | Left stick | WASD | analog stick, scaled to the N64 range (full tilt = ±80, so full speed needs full tilt); the float vector is also kept for H3 |
| Look / aim | Right stick | Mouse | nothing; the right stick no longer drives the C-buttons |
| Fire | RT | Left mouse | Z |
| Aim | LT | Right mouse | R (held, or toggled) |
| Jump | A | Space | jump button of the active key map |
| Crouch / roll | B | Left Ctrl | crouch button of the active key map |
| Previous / next weapon | LB / RB | Wheel down / up, Q / E | weapon-cycle buttons of the active key map |
| Quick weapons | D-pad | 1–4 | D-pad directions |
| Zoom (zoom weapon, aiming) | LB / RB | Wheel | handled in H2; not passed on as weapon cycling while zoomed |
| Pause | Start | Esc / Enter | Start |

"Key map" means the game's `controlModeKeys` table for the current Normal or Expert setting. The action-to-button translation reads it, so either setting works.

**Mouse capture.** Relative mouse mode is on only during Modern gameplay. It's released in the port's menus, in the game's pause and front-end menus, and when the window loses focus. Mouse buttons become bindable, by implementing the frontend's currently stubbed `InputType::Mouse` digital path.

## 6. Gate, edge cases, safety

**Modern applies** (per frame, from H4) only when all of these hold:
- player index 0;
- the character is a playable on-foot character: Juno, Vela or Lupus, including their boss-fight variants for H2;
- the game hasn't disabled input (`disablejoy == 0`, no input-lock timers);
- the scheme is Modern.

The camera hooks additionally act only when the dispatcher chose the follow camera.

**Stays original:**
- cutscenes, and the fixed, lobby and special cameras;
- the hovership;
- Floyd (player index 1) and split-screen multiplayer;
- the boss variants' aim movement (they lack the H3 hook until it's added).

**Unchanged mechanics:** jumping, rolling, ledges, ladders, water and knockback all run through the game's code. Modern only supplies buttons, the look direction and aim-mode movement.

**Original scheme:** every hook returns immediately, and the frontend builds the N64 pad exactly as today.

**Fail loudly:**
- Hooks are named N64Recomp entries, so a mismatch fails the build.
- The gate logs once (stderr) on an unexpected character or state value, then treats it as Original.

## 7. Testing

- **Unit tests** (`tests/controls/`, run by `scripts/test-hooks.sh`):
  - look integration: mouse, stick, inversion, deadzone and curve, pitch clamp;
  - drift timer and re-sync;
  - aim crosshair accumulation, box overflow into turning, pitch-base clamp, zoom scaling, and the stick rescale;
  - aim-move projection and caps;
  - action-to-N64 translation for both key maps.
- **Scripted runs.**
  - `JFG_INPUT_SCRIPT` gains right-stick axes and mouse deltas (new fields `RX RY MDX MDY`).
  - A debug switch (`JFG_DEBUG_CONTROLS=1`) logs per frame: gate, camera kind, `pd+0x104`, `cam` yaw, facing, state, crosshair offsets, forward and strafe speed.
  - Automated checks on those logs:
    - looking turns the camera, not Juno;
    - the camera drifts back after the delay while Juno runs;
    - holding Aim and moving gives forward and strafe speeds with the crosshair stable;
    - cutscenes are unaffected.
- **Regression.** With the scheme set to Original, the Goldwood script's state log matches a build without the mod.
- **Feel.** The user playtests on gamepad and mouse; the sensitivities, curve and drift timing are the tuning knobs.

## 8. Later

Added once the free camera plays well, as further `CameraMode` implementations and settings values:
- **1a over-the-shoulder** ("always aim");
- **1b behind-the-back** (body follows look);
- **3 twin-stick with the original camera** (screen-relative aim, mouse cursor).

Each reuses the input, aim controller and aim movement units. Also later: H3 for the boss variants, aim assist in aim mode, and the camera fallback B if needed.

## 9. Risks and open items

| Item | Mitigation |
|---|---|
| Follow camera's height and pitch range may be too small for comfortable looking | Fallback B (§4.2) behind the same interface |
| Animation select in aim states may ignore forward speed | H3 also sets the walk animation; verify early with the debug log |
| Field meanings marked *(inferred)* in the research (tuning block fields, state numbers 5 and 11, camera yaw convention) | Confirmed with `JFG_DEBUG_CONTROLS` logs before the behaviour that depends on them is built |
| Mouse deltas versus the game's 30 fps tick | Deltas accumulate between latches; each is consumed once per game frame |
| Stick range mismatch also affects the Original scheme (full speed at half tilt) | Separate base-port fix: scale the N64 stick to ±80 in the frontend. Tracked in the handoff, not part of this spec. |
