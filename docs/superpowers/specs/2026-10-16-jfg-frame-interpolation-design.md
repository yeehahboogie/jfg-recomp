# Jet Force Gemini frame interpolation: design

Status: approved in conversation on 2026-10-16 (approach, sections 1–4); this document is for the user's review
before the implementation plan.

## 1. Intent

**What the user wants.** Smooth motion at the display's refresh rate (60, 120 or 144 Hz) instead of the game's
30 fps steps (20 in heavy scenes). The game logic keeps running at its own rate; RT64 draws the in-between frames.

**Decisions made with the user:**

- **Scope before it's on by default: everything.** The camera, characters, rigid objects, the sky, sprites, particles
  and shots all interpolate cleanly before the feature becomes the default. Until then it's an opt-in toggle.
- **Approach C:** game-side hooks record which matrices belong to what. That table travels with the frame's graphics
  task to the renderer, and our RT64 patch tags draws with stable IDs. The game's display lists are not modified.
- **Assumed, not yet confirmed by the user:** gameplay and cutscenes, single player and split screen. Framerate
  "Original" still turns interpolation off. No flicker or skybox jitter may come back: those got interpolation
  switched off in the first place.

**Success means:**
- with the toggle on, camera turns, pans and character motion are smooth in Goldwood gameplay, the intro cutscenes and
  the ship landing;
- the flicker detector (`tools/diag/record.sh`) reports 0 spikes on those recordings, and they show more than one
  distinct picture per game frame;
- the skybox, sprites and camera cuts look right frame by frame;
- with the toggle off, nothing interpolates and the picture is exactly as today's with Framerate "Original".

## 2. Key facts from the research that shape the design

Researched on 2026-10-16; line numbers are in the patched `lib/rt64` tree.

**How RT64 matches frames** (`lib/rt64/src/hle/rt64_game_frame.cpp`, `rt64_transform_group.h`,
`include/rt64_extended_gbi.h`):
- Draws carry a transform group: an ID, per-component modes (position, rotation, scale, skew, perspective, vertex,
  tile: SKIP, INTERPOLATE or AUTO) and an ordering mode. `G_EX_ID_IGNORE` = 0 (never matched, drawn at the current
  matrix) and `G_EX_ID_AUTO` = 0xFFFFFFFF (matched by a heuristic).
- **Explicit IDs with ORDER_LINEAR** are matched by ID between frames (an ID-to-transform multimap merge-join,
  :294-310). That's the mechanism this design uses.
- **AUTO** draws are paired by a hash of their render state and a scored nearest match (:514-587). Every JFG model
  matrix is AUTO today.
- **Projections** (cameras) are grouped into scenes and paired by index within a scene. With AUTO the first mapping
  found is copied onto every later projection in the scene (:430-447). With an explicit ID, a projection maps only to
  the same ID.
- **Interpolation runs only when** the game's original rate is above 0 and below the target. The original rate comes
  from `gEXSetRefreshRate`, or from the VI history, which gives 0 unless the last three VI factors are equal
  (`rt64_vi.cpp:205-213`). JFG switches between 30 and 20 fps, so the rate must be passed explicitly.

**How JFG's display lists use matrices** (our `lib-patches/rt64/0001-jfg-gbi.patch`, `dmaMatrix` and
`applyMatrixSlotDKR`):
- JFG's microcode has matrix slots, not F3DEX2's stacks. Slot 0 without multiply is a camera (view × projection,
  kept as `viewProj0`). Other slots either multiply onto slot 0 (an ordinary model matrix) or are absolute (a complete
  transform).
- Our patch turns each **absolute** draw into its own projection with an identity model, and each **billboard**
  (vertices after vertex 0 offset from it) into a projection shifted by vertex 0's clip position. So the number and
  order of projections change from frame to frame. That is why index pairing blended the camera with the wrong ones
  (flicker, skybox jitter), and why commit `6951418` set every JFG projection to `G_EX_ID_IGNORE`.
- One Goldwood gameplay frame (RDRAM dump at 310 s, walked with `tools/diag/dlwalk.py`) has 6 camera loads, 90
  relative model matrices, 14 absolute matrices and 10 billboard sections. Matrices come from a per-frame pool (around
  `0x203xxx`) and from object buffers (for example `0x2FC1A8`).
- **Character joint matrices are double-buffered:** `modGenAnimMatrices` (`0x8003D21C`) writes each instance's
  joints to one of two buffers (instance +0x10/+0x14, chosen by the byte at +0xB), alternating each frame. A
  matrix's address therefore doesn't identify it from one frame to the next; the object and joint do. The Big Heads
  hook (`src/jfg/big_head.cpp`) already runs at this point with the instance, model and object in hand.

**What worked for Zelda64Recomp** (its removed patches, `git show b9755c1:patches/...`): stable IDs per camera
(`CAMERA_TRANSFORM_ID`), per actor (a spawn index stored in the actor, not its pointer) and limb, per skybox and per
particle slot. On camera cuts, position, rotation, scale and skew are SKIP for that frame. Teleports and reused
particle slots also skip, and `gEXSetRefreshRate` is called each frame. We reuse the scheme but not its delivery:
those patches wrote `gEXMatrixGroup` commands into the display lists. JFG's counted display lists (DMA_DL) make that
unsafe, and it would cost display-list space.

## 3. Architecture

```
game thread                                         renderer (RT64 patch)
-----------                                         ---------------------
hooks -> FrameIdentities (address -> ID, flags)  ->  matrix load: look up address -> transform group (ID, modes)
         game frame rate (30 / 20)                   camera load: ID'd projection, ORDER_LINEAR
on graphics-task submit: hand the table over  -->    the task's table, popped in submission order
```

### 3.1 Units

| Unit | Where | Does | Depends on |
|---|---|---|---|
| Identity table | `include/jfg_interp.h`, `src/jfg/interp.cpp` | Pure logic: `FrameIdentities` (address ranges → ID + skip flags, plus the game rate), object serials, ID layout, cut and teleport detection. Unit-tested. | nothing |
| Game hooks | `src/jfg/interp_hooks.cpp`, `recomp/jfg.toml.in` | Fill the current frame's table: cameras per viewport, character joints (at the `modGenAnimMatrices` hook, shared with Big Heads), rigid objects, sky, sprites and particles, object spawn and free. Exact hook points come from Phase 0. | identity table |
| Handoff | `src/main/rt64_render_context.cpp` (and a runtime patch if the task path needs it) | When the game submits a graphics task, move the finished table into a FIFO that the renderer pops for that task. | identity table |
| Renderer tagging | `lib-patches/rt64/0001-jfg-gbi.patch` | On each JFG matrix load, look up the address and set the transform group: explicit ID, component modes from the flags, ORDER_LINEAR. Not found means `G_EX_ID_IGNORE`. Cameras become ID'd projections. Billboards and absolute draws become ID'd model transforms under the camera instead of extra projections. Sets the original refresh rate from the table. | handoff |
| Setting | `src/ui/ui_config.cpp`, `assets/config_menu/graphics.rml`, graphics config | "Frame Interpolation (experimental)": Off (default) or On. | — |

### 3.2 ID layout

32-bit IDs, never 0 (`G_EX_ID_IGNORE`) or 0xFFFFFFFF (`G_EX_ID_AUTO`):

| Range | What |
|---|---|
| `0x00000100 + 0x10 * role + viewport` | Cameras, by role (world, sky, 2D, and any others Phase 0 finds) and viewport (0–3). |
| `0x01000000 + 0x100 * serial + joint` | Character joints: an object serial (below) and the joint index (up to 64). |
| `0x02000000 + 0x10 * serial + part` | Rigid objects and absolute draws that belong to an object. |
| `0x03000000 + index` | Sky pieces. |
| `0x04000000 + 0x100 * emitter + slot` | Sprites, particles and shots. |

**Object serials** are assigned by the recorder when an object first appears, preferably from the game's own object
create and free functions (found in Phase 0). An object reusing a freed slot gets a new serial. A new serial has
nothing to pair with, so that object is drawn as-is for its first frame.

### 3.3 Per-frame flow

1. The game starts building frame N. The recorder starts a new `FrameIdentities` and fills it as hooks fire.
2. Hooks add entries keyed by the RDRAM range the display list will load (for joints, the instance's current buffer
   plus 0x40 × joint), with skip flags where needed (section 4).
3. When the game submits frame N's graphics task, the table and the current game rate are moved into the handoff
   FIFO.
4. When RT64 processes that task, it pops the table and tags every matrix load as it goes. The table is owned by the
   task, so no state is shared between the game and render threads.
5. RT64 pairs frame N with frame N−1 by ID and draws the in-between frames as it does today.

## 4. Behaviour and edge cases

- **Toggle off:** every JFG matrix and projection is `G_EX_ID_IGNORE`, so nothing interpolates whatever the
  Framerate setting says: in-between frames repeat the current one. This also ends today's heuristic
  interpolation of JFG model matrices, a possible cause of the black-polygon flashes reported during the ship
  landing (tracked separately).
- **Toggle on:** only matrices in the table interpolate; anything else is drawn as-is.
- **Camera cuts:** the recorder compares each camera with the same ID in the previous frame. A jump beyond a
  threshold (position, or angle, scaled to the scene) sets SKIP for position, rotation, scale and skew for that frame.
  If Phase 0 finds an explicit cut event in the game's camera code, that is used instead.
- **Spawns and teleports:** a new serial has no partner. An object whose position jumps beyond a threshold gets SKIP
  for that frame.
- **Level loads** clear the serial map and the previous-camera state.
- **The game's rate** (30 or 20, from the frame-pacing mode byte `0x800FECAC`) goes with every table, so RT64 always
  has an original rate.
- **Pause and menus:** frames repeat, so nothing moves.
- **Mismatches** (a different joint count, a missing entry, an address in two entries) mean `G_EX_ID_IGNORE` for that
  matrix, and the debug log names it.
- **Split screen:** each viewport's cameras have their own IDs; objects are shared across viewports (same ID in each
  viewport's pass, matched in submission order).

## 5. Phases

Each phase leaves the game working, with the toggle off by default throughout.

0. **Map the draw pipeline.** A debug pass labels every matrix load in a frame with the game function that wrote it
   and its kind. The result is a table, added to the plan: for each kind (each viewport's cameras, sky, absolute
   draws, rigid objects, character joints, sprites and particles, shots, HUD), its source, the hook point and the
   identity source. It also records the exact billboard and absolute-matrix semantics.
1. **Plumbing:** the identity table, handoff, renderer lookup, game rate, toggle and debug switch. The table is
   empty, so behaviour with the toggle on equals off.
2. **Cameras**, every viewport, with cut detection.
3. **Characters and rigid objects.**
4. **The sky and absolute draws** (re-expressed as model transforms under the camera).
5. **Sprites, particles and shots** (billboards rebuilt as ID'd model transforms under the camera, pixel-identical to
   today's at rest).
6. **Split screen, the HUD (left at 30 unless something visibly steps) and performance.** Then the toggle defaults to
   on.

## 6. Testing

- **Unit tests** (`tests/interp_test.cpp`, in `scripts/test-hooks.sh`): ID layout, serial assignment and reuse,
  table lookup (ranges, overlaps, misses), cut and teleport detection.
- **Debug switch** `JFG_DEBUG_INTERP=1`: per frame, the number of matched, unmatched and skipped groups, and every
  matrix load the table didn't know.
- **Recordings** with `tools/diag/record.sh` (60 fps) of the intro cutscenes, the ship landing and Goldwood
  gameplay, toggle on versus off:
  - 0 flicker spikes;
  - more distinct pictures than game frames with the toggle on (interpolation running), and exactly one per game
    frame with it off;
  - frame-by-frame checks of the sky, sprites, cuts and spawns.
- **Rendering at rest:** with the toggle on, a paused frame must match the toggle-off picture, so the billboard and
  absolute-draw rewrites change nothing visible.
- **Frame time:** with the toggle on and off, from the stats overlay.

## 7. Risks

- **Billboard and absolute rewrites** change how those draws are built. Pixel comparison at rest (section 6) guards
  against visible change.
- **Hook coverage:** some matrix writers may sit in many modules. Phase 0 sizes this before Phase 3 starts. Anything
  not covered is drawn as-is, so it steps at 30 fps but doesn't flicker.
- **Frame pairing across the handoff:** if RT64 skips or repeats a task, the FIFO must stay aligned. The handoff
  tags each table with the task's display-list address and a sequence number, and a mismatch is logged loudly.
