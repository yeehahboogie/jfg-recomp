# Frame interpolation, plan 2 (characters, objects, the sky): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give character joints, objects and the sky stable identities, and draw absolute world draws (objects, guns,
the sky) as model transforms under the interpolated camera. Everything the world contains then moves with the camera
instead of shaking against it; sprites and particles are left to plan 3.

**Architecture:** builds on plan 1 (`docs/superpowers/plans/2026-10-16-jfg-frame-interpolation.md`).
- **Object and instance serials** are kept by a `SerialMap` and released when the game frees an object or model
  instance.
- **Joint matrices** are recorded at the end of `modGenAnimMatrices`, the hook Big Heads already uses.
- **Absolute world draws** are recorded at `mathMtxF2L` when its source is model × view-projection (0x800FB038), with
  the owning object taken from an `objPrintObject` owner stack. They're flagged "under the camera", and RT64 rebuilds
  their model matrix as absolute × inverse(camera), then draws it under the ID'd camera.

**Tech Stack:** C++20, N64Recomp hooks (`recomp/jfg.toml.in`), RT64 (patched via `lib-patches/rt64/0001-jfg-gbi.patch`),
hlslpp (in RT64's contrib), clang 18 from `tools/toolchain.env`.

**Spec:** `docs/superpowers/specs/2026-10-16-jfg-frame-interpolation-design.md` (phases 3–4, sections 3.2, 4 and 6).
**Findings this plan relies on:** plan 1's "Phase 0 findings" and its correction; the plan 2 research below.

## Global Constraints

Plan 1's Global Constraints all still apply: build and RT64-patch recipes, never committing ROM-derived data, test
runs in `build/appdata-interp` on `DISPLAY=:99`, variables set in the same command, and `rm` paths guarded with
`"${A:?}"`. In addition:
- The RT64 patch now has four new files: `src/gbi/rt64_gbi_f3djfg.cpp`, `src/gbi/rt64_gbi_f3djfg.h`,
  `src/hle/rt64_jfg_frame.h` and `src/hle/rt64_jfg_groups.h`. Mark all of them with `git add -N` before regenerating.
- The Frame Interpolation menu row stays hidden in this plan; `JFG_INTERPOLATION=1` switches it on for runs.
- **IDs** (spec section 3.2; this plan refines the joint range, see Ruling 1):
  - cameras: `0x100 + 0x10 * role + viewport`;
  - joints: `0x01000000 + 0x40 * instanceSerial + joint` (joint < 64);
  - absolute object pushes: `0x02000000 + 0x10 * objectSerial + push` (push < 16; later pushes are drawn as-is).
- **Ruling 1:** joints are keyed by model-instance serial rather than object serial. An object can switch instances
  (obj+0x3A), and its gun is a separate object, so the instance is what owns a joint buffer.
- **Ruling 2:** object and joint groups use RT64's AUTO position mode, which skips a frame when the velocity jumps
  (teleports, respawns), instead of a detector of our own. Cameras keep explicit cut detection.

## Research (2026-10-16)

- **Objects:**
  - `objSetupObject` (0x80005F14) creates objects; every return goes through 0x80006E04 with the object in `$v0`.
  - `func_80007C68` (`$a0` = object) destroys them. `objFreeObject` only queues the free.
  - Sub-objects in obj+0x60 are freed without `func_80007C68`, so a reused address must also be caught at creation.
  - No stable serial exists in the object struct.
- **Instances:**
  - `modGenAnimMatrices` (0x8003D21C) is the only producer of joint matrices: its `camConvertMatrixList` call at
    0x8003DD94 queues `*(inst + 0x10 + 4*byte(inst+0xB))`, `byte(model+0x4F)` matrices.
  - At 0x8003DD9C (the Big Heads hook) `$s4` is the instance, `$s3` the model, and sp+0xF8 the object.
  - The display list loads joint j from buffer + 0x40·j. Buffers alternate between two addresses.
  - `modFreeModel` (0x8003C7C4, `$a0` = instance) frees an instance.
- **Absolute world draws:**
  - All go through `mathMtxF2L` (0x80048D84) with `$a0 = 0x800FB038` (model 0x800FAAB0 × view-projection 0x800FAAF0)
    and `$a1` = the pool address. Callers: `camPushModelMtx`, `camPushFloatModelMtx`, `camPushMuzzleMtx`,
    `camScaleModelMtx`.
  - `objPrintObject` (0x8000DB94, `$a3` = object) draws an object; its single exit is 0x8000DC1C, which both paths reach.
  - The sky object (`*0x800F2F88`) goes through it too.
  - HUD 3D objects push under a different view-projection.
  - Model particles (`partDraw` list 3) push outside any `objPrintObject`, so with no owner they're left as-is.
- **Cameras:** `camSetProjMtx` computes the world camera's view-projection into 0x800FAAF0, then calls `mathMtxF2L`
  with `$a0 = 0x800FAAF0`.
- **Convention:** row vectors, v' = v·M. So model = absolute × inverse(view-projection), and RT64 multiplies
  `mul(model, viewProj)`. Checked on a dump: an object, a 0.516-scale object, the gun and the sky (identity rotation,
  translation = the eye) all come out as clean world matrices.

## Review Focus

- **A model particle drawn after an object:** its `camPushModelMtx` happens outside `objPrintObject` and must not
  take the last object's identity. The owner stack is empty there (Task 4 test: an add outside any owner records
  nothing).
- **An object whose address is reused:** a new object at a freed address must not pair with the old one. The serial
  changes at creation and at destruction (Task 2 tests).
- **The HUD's 3D objects:** they push under a HUD view-projection and must not be rebuilt under the world camera.
  Only pushes whose view-projection equals the world camera's are flagged (Task 4).
- **Split screen:** each viewport's camera has its own ID, and the same object is drawn once per viewport. Duplicate
  IDs pair in submission order (checked in Task 5 with `JFG_FAKE_PLAYERS=2`).
- **More than 16 pushes for one object:** later pushes are drawn as-is, never given wrapped IDs (Task 4 test).

---

### Task 1: Plan 1 review leftovers

**Files:**
- Modify: `include/jfg_interp.h`, `src/jfg/interp.cpp`, `tests/interp_test.cpp`, `src/jfg/interp_hooks.cpp`,
  `src/main/rt64_render_context.cpp`, `lib/rt64/src/hle/rt64_rsp.h`, `lib/rt64/src/hle/rt64_rsp.cpp`,
  `lib/rt64/src/gbi/rt64_gbi_f3djfg.cpp`, `lib-patches/rt64/0001-jfg-gbi.patch`

**Interfaces:**
- Produces:
  - `Handoff::push` returns the number of frames dropped (`size_t`).
  - `CutDetector` no longer cuts on a turn when no driver is set: the player's own camera can turn as fast as the
    mouse allows.
  - `RSP::dkr.viewProj0Address`.

- [ ] **Step 1: Write the failing tests**

In `tests/interp_test.cpp`:
- In the "Beyond max_pending" block, replace the push loop with one that sums the return values, then add
  `assert(dropped == 3);`:
  ```cpp
      size_t dropped = 0;
      for (uint32_t i = 0; i < jfg::interp::Handoff::max_pending + 3; i++) {
          Frame x; x.sequence = 100 + i; x.dl_address = 0x1FB0C0;
          dropped += h.push(std::move(x));
      }
      assert(dropped == 3);
  ```
- Before `std::puts`, add:
  ```cpp
      // The player's own camera (no cutscene or static camera driving it) turns as fast as the mouse allows: a flick
      // is not a cut. A cutscene camera turning that far in one frame still is.
      jfg::interp::CutDetector player;
      assert(player.cut(cam, pose(0, 0), 40));
      assert(!player.cut(cam, pose(0, 0x4000), 41));   // 90 degrees in one frame, nobody driving
      assert(player.cut(cam, pose(0, 0x4000, 0x80123450), 42)); // a driver took over
      assert(player.cut(cam, pose(0, 0x0000, 0x80123450), 43)); // driven and turned 90 degrees
  ```

- [ ] **Step 2: Run them to make sure they fail**

Run: `scripts/test-hooks.sh`
Expected: compile error (`push` returns void), then, once it compiles, `interp_test` fails at the 90-degree turn.

- [ ] **Step 3: Implement**

- `include/jfg_interp.h`: `size_t push(Frame frame);` with the comment `// Returns how many old frames were dropped
  to stay within max_pending.`
- `src/jfg/interp.cpp`: count the `pop_front()` calls in `push` and return the count.
- In `CutDetector::cut`, change the `turned` line to:
  ```cpp
          // Only a driven camera (cutscene, static) cuts on a turn: the player's own camera turns as fast as input allows.
          const bool turned = pose.driver != 0 && (turn(pose.yaw, prev.pose.yaw) > max_turn ||
                              turn(pose.pitch, prev.pose.pitch) > max_turn || turn(pose.roll, prev.pose.roll) > max_turn);
  ```
  and update the class comment to say so.
- `src/jfg/interp_hooks.cpp`, `jfg_interp_submit`, after building the frame:
  ```cpp
      if (frame.conflicts > 0 && interp_debug) {
          std::fprintf(stderr, "[interp] frame %llu: %u addresses recorded with two identities (drawn as-is)\n",
                       (unsigned long long)frames_submitted, frame.conflicts);
      }
      const size_t dropped = jfg::interp::handoff().push(std::move(frame));
      if (dropped > 0) {
          std::fprintf(stderr, "[interp] renderer behind: dropped %zu recorded frames\n", dropped);
      }
  ```
  Restructure so the frame is a named variable: `jfg::interp::Frame frame = main ? recorder.take(dl, rate) :
  recorder.empty(dl, rate);`. Move `interp_debug` above `jfg_interp_submit` if needed.
- `src/main/rt64_render_context.cpp`, `send_dl`: rate-limit the mismatch message with a static counter, logging the
  first 5 and then every 300th:
  ```cpp
      static uint32_t mismatches = 0;
      if ((discarded > 0 || !frame) && (mismatches++ < 5 || mismatches % 300 == 0)) {
  ```
- RT64:
  - `lib/rt64/src/hle/rt64_rsp.h`, in the `dkr` block, after `slotAddress`: add `uint32_t viewProj0Address = 0; //
    where the camera (slot 0, no multiply) was loaded from`.
  - `lib/rt64/src/hle/rt64_rsp.cpp`: reset it in `RSP::reset()` (`dkr.viewProj0Address = 0;`), and in
    `applyMatrixSlotDKR` use `dkr.viewProj0Address` instead of `dkr.slotAddress[0]` in the camera-restore call.
  - `lib/rt64/src/gbi/rt64_gbi_f3djfg.cpp`, `dmaMatrix`, in the slot-0-camera branch: set
    `rsp->dkr.viewProj0Address = address;` and pass it to `setViewProjDKR`.

- [ ] **Step 4: Run the tests**

Run: `scripts/test-hooks.sh`
Expected: ends with `interp_groups_test: OK`, and `interp_test: OK` appears.

- [ ] **Step 5: Build, regenerate the patch, commit**

```bash
source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
git add include/jfg_interp.h src/jfg/interp.cpp tests/interp_test.cpp src/jfg/interp_hooks.cpp \
    src/main/rt64_render_context.cpp lib-patches/rt64/0001-jfg-gbi.patch
git commit -m "Interpolation: report dropped frames and conflicts; player cameras turn freely; camera address kept apart

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 2: Object and instance serials

**Files:**
- Modify: `include/jfg_interp.h`, `src/jfg/interp.cpp`, `tests/interp_test.cpp`, `include/jfg_interp_hooks.h`,
  `src/jfg/interp_hooks.cpp`, `recomp/jfg.toml.in`

**Interfaces:**
- Produces:
  - `class SerialMap { uint32_t get(uint32_t pointer); void release(uint32_t pointer); void clear(); }`.
    `get` assigns a fresh serial on first sight and never returns 0.
  - `SerialMap& object_serials()`, `SerialMap& instance_serials()`.
  - Hooks: `jfg_interp_object_created` (`func_80005F14` before 0x80006E04), `jfg_interp_object_freed` (entry of
    `func_80007C68`), `jfg_interp_instance_freed` (entry of `func_8003C7C4`).

- [ ] **Step 1: Write the failing test**

Before `std::puts` in `tests/interp_test.cpp`:
```cpp
    // Serials: one per live pointer, a fresh one after the pointer is released (a new object at a freed address).
    jfg::interp::SerialMap serials;
    const uint32_t a = serials.get(0x801BB2C0), b = serials.get(0x801BC030);
    assert(a != 0 && b != 0 && a != b);
    assert(serials.get(0x801BB2C0) == a);
    serials.release(0x801BB2C0);
    const uint32_t a2 = serials.get(0x801BB2C0);
    assert(a2 != a && a2 != b && a2 != 0);
    serials.release(0x80000000); // releasing an unknown pointer is harmless
    serials.clear();
    assert(serials.get(0x801BC030) != b);
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `scripts/test-hooks.sh`
Expected: compile error, `SerialMap` is not a member of `jfg::interp`.

- [ ] **Step 3: Implement**

`include/jfg_interp.h`, before `CutDetector& cut_detector();`:
```cpp
// Serial numbers for game objects and model instances, by pointer: stable while the pointer is live, fresh once the
// game frees it (an object or instance later created at the same address must not pair with the old one).
class SerialMap {
public:
    uint32_t get(uint32_t pointer);
    void release(uint32_t pointer);
    void clear();

private:
    std::unordered_map<uint32_t, uint32_t> serials_;
    uint32_t next_ = 1;
};

SerialMap& object_serials();
SerialMap& instance_serials();
```
`src/jfg/interp.cpp`:
```cpp
uint32_t SerialMap::get(uint32_t pointer) {
    auto [it, inserted] = serials_.try_emplace(pointer, next_);
    if (inserted) next_ = next_ + 1 == 0 ? 1 : next_ + 1;
    return it->second;
}

void SerialMap::release(uint32_t pointer) {
    serials_.erase(pointer);
}

void SerialMap::clear() {
    serials_.clear();
}

SerialMap& object_serials() {
    static SerialMap m;
    return m;
}

SerialMap& instance_serials() {
    static SerialMap m;
    return m;
}
```

- [ ] **Step 4: Run the test**

Run: `scripts/test-hooks.sh`
Expected: ends with `interp_groups_test: OK`, and `interp_test: OK` appears.

- [ ] **Step 5: Hook the game's object and instance lifetimes**

`include/jfg_interp_hooks.h`:
```c
/* objSetupObject (0x80005F14) returning at 0x80006E04 with the new object in $v0. */
void jfg_interp_object_created(uint8_t* rdram, recomp_context* ctx);
/* Entry of func_80007C68 (objDoFrees / objFreeAll destroying the object in $a0). */
void jfg_interp_object_freed(uint8_t* rdram, recomp_context* ctx);
/* Entry of modFreeModel (0x8003C7C4): the model instance in $a0 is freed. */
void jfg_interp_instance_freed(uint8_t* rdram, recomp_context* ctx);
```
`src/jfg/interp_hooks.cpp`:
```cpp
extern "C" void jfg_interp_object_created(uint8_t* rdram, recomp_context* ctx) {
    if (ctx->r2 != 0) jfg::interp::object_serials().release((uint32_t)ctx->r2);
}

extern "C" void jfg_interp_object_freed(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::object_serials().release((uint32_t)ctx->r4);
}

extern "C" void jfg_interp_instance_freed(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::instance_serials().release((uint32_t)ctx->r4);
}
```
In `jfg_interp_level_start`, also call `jfg::interp::object_serials().clear();` and
`jfg::interp::instance_serials().clear();`.

`recomp/jfg.toml.in`:
```toml
# Frame interpolation: object and model-instance lifetimes, so a reused address gets a fresh identity.
[[patches.hook]]
func = "func_80005F14"          # objSetupObject: the common return, new object in $v0
before_vram = 0x80006E04
text = "jfg_interp_object_created(rdram, ctx);"

[[patches.hook]]
func = "func_80007C68"          # destroys an object (objDoFrees, objFreeAll)
text = "jfg_interp_object_freed(rdram, ctx);"

[[patches.hook]]
func = "func_8003C7C4"          # modFreeModel
text = "jfg_interp_instance_freed(rdram, ctx);"
```

- [ ] **Step 6: Build and run**

```bash
scripts/recompile.sh && source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
A=build/appdata-interp; export APP_FOLDER_PATH=$PWD/$A DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
JFG_SURVEY_SHOTS="72" scripts/survey-level.sh 92 build/diag/interp2-t2
```
Expected: `92 ok errors=0`. `recompile.sh` fails loudly if a hook's address isn't in its function, for example if
0x80006E04 isn't inside `func_80005F14`; if so, check the address with the disassembler.

- [ ] **Step 7: Commit**

```bash
git add include/jfg_interp.h src/jfg/interp.cpp tests/interp_test.cpp include/jfg_interp_hooks.h \
    src/jfg/interp_hooks.cpp recomp/jfg.toml.in
git commit -m "Interpolation: object and model-instance serials, released when the game frees them

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 3: Character joints

**Files:**
- Modify: `lib/rt64/src/hle/rt64_jfg_groups.h`, `tests/interp_groups_test.cpp`, `include/jfg_interp.h` (`joint_id`),
  `tests/interp_test.cpp`, `src/jfg/big_head.cpp`, `src/jfg/interp_hooks.cpp`, `include/jfg_interp_hooks.h`,
  `lib-patches/rt64/0001-jfg-gbi.patch`

**Interfaces:**
- Consumes: `instance_serials()` (Task 2), `Recorder::add` (plan 1).
- Produces:
  - `jfg::interp::joint_id(uint32_t instanceSerial, int joint)`.
  - `jfg_interp_joints(rdram, ctx)`, called from the Big Heads hook at the end of `modGenAnimMatrices`.
  - Model groups now use AUTO position (Ruling 2).

- [ ] **Step 1: Write the failing tests**

`tests/interp_test.cpp`, before `std::puts`:
```cpp
    // Joint IDs: by instance serial and joint, in their own range, distinct across instances and joints.
    assert(jfg::interp::joint_id(1, 0) == 0x01000040);
    assert(jfg::interp::joint_id(1, 63) == 0x0100007F);
    assert(jfg::interp::joint_id(2, 0) == 0x01000080);
```
`tests/interp_groups_test.cpp`, after the cut checks:
```cpp
    // A model (not a camera) leaves its position to RT64's AUTO mode, which skips a frame on a velocity spike
    // (teleports, respawns); rotation and scale interpolate.
    TransformGroup joint = jfgGroupFor(&table, 0x1000, false);
    assert(joint.matrixId == 0x100 && joint.positionInterpolation == G_EX_COMPONENT_AUTO);
    assert(joint.rotationInterpolation == G_EX_COMPONENT_INTERPOLATE);
```

- [ ] **Step 2: Run them to make sure they fail**

Run: `scripts/test-hooks.sh`
Expected: compile error (`joint_id`), and once that exists, `interp_groups_test` fails on `G_EX_COMPONENT_AUTO`.

- [ ] **Step 3: Implement**

- `include/jfg_interp.h`, after `camera_id`:
  ```cpp
  // A character joint: the model instance's serial and the joint index (joints < 64).
  constexpr uint32_t joint_id(uint32_t instance_serial, int joint) {
      return 0x01000000u + 0x40u * instance_serial + uint32_t(joint);
  }
  ```
- `lib/rt64/src/hle/rt64_jfg_groups.h`, in `jfgGroupFor`, replace the position line with:
  ```cpp
          // Cameras interpolate position (their cuts are flagged). Models leave it to AUTO: skipped on a velocity spike.
          group.positionInterpolation = projection ? mode(JFG_SKIP_POSITION)
              : ((entry->skip & JFG_SKIP_POSITION) ? uint8_t(G_EX_COMPONENT_SKIP) : uint8_t(G_EX_COMPONENT_AUTO));
  ```
- `include/jfg_interp_hooks.h`:
  ```c
  /* At the end of modGenAnimMatrices (0x8003DD9C): the instance's joint matrices are ready ($s4 instance, $s3 model). */
  void jfg_interp_joints(uint8_t* rdram, recomp_context* ctx);
  ```
- `src/jfg/interp_hooks.cpp`:
  ```cpp
  extern "C" void jfg_interp_joints(uint8_t* rdram, recomp_context* ctx) {
      const gpr instance = ctx->r20;
      const gpr model = ctx->r19;
      const int count = MEM_BU(0x4F, model);
      if (count <= 0 || count > 64) return;
      const uint32_t buffer = (uint32_t)MEM_W(0x10 + 4 * MEM_B(0xB, instance), instance) & 0x3FFFFFF;
      if (buffer == 0) return;
      const uint32_t serial = jfg::interp::instance_serials().get((uint32_t)instance);
      for (int j = 0; j < count; j++) {
          jfg::interp::recorder().add(buffer + 0x40 * j, jfg::interp::joint_id(serial, j));
      }
  }
  ```
- `src/jfg/big_head.cpp`: add `#include "jfg_interp_hooks.h"`, and make the first line of `jfg_anim_matrices_done`
  `jfg_interp_joints(rdram, ctx);`, before the `big_head` check. Both share the hook point; recording doesn't touch
  the matrices.

- [ ] **Step 4: Run the tests**

Run: `scripts/test-hooks.sh`
Expected: all OK.

- [ ] **Step 5: Build, regenerate the patch, check tagging in a run**

```bash
source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
A=build/appdata-interp; export APP_FOLDER_PATH=$PWD/$A DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
JFG_INTERPOLATION=1 JFG_DEBUG_INTERP=1 JFG_SURVEY_SHOTS="72" scripts/survey-level.sh 92 build/diag/interp2-t3
grep "\[interp\] task" build/diag/interp2-t3/run.log | sed -n '900p'
```
Expected: a gameplay task line shows `models N tagged` with N above zero (Juno's joints: 21 or so), and
`92 ok errors=0`.

- [ ] **Step 6: Commit**

```bash
git add include/jfg_interp.h tests/interp_test.cpp tests/interp_groups_test.cpp include/jfg_interp_hooks.h \
    src/jfg/interp_hooks.cpp src/jfg/big_head.cpp lib-patches/rt64/0001-jfg-gbi.patch
git commit -m "Interpolation: character joints by model-instance serial

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 4: Absolute world draws under the camera (objects, guns, the sky)

**Files:**
- Modify: `lib/rt64/src/hle/rt64_jfg_frame.h` (a flags field), `lib/rt64/src/hle/rt64_jfg_groups.h`
  (`jfgModelUnderCamera`), `tests/interp_groups_test.cpp`, `lib/rt64/src/hle/rt64_rsp.cpp` (`applyMatrixSlotDKR`),
  `include/jfg_interp.h`, `src/jfg/interp.cpp`, `tests/interp_test.cpp`, `include/jfg_interp_hooks.h`,
  `src/jfg/interp_hooks.cpp`, `recomp/jfg.toml.in`, `lib-patches/rt64/0001-jfg-gbi.patch`

**Interfaces:**
- Produces:
  - `RT64::JfgMatrixIdentity::flags` with `JFG_FLAG_UNDER_CAMERA = 1`.
  - `Recorder::add(address, id, skip, flags = 0)`.
  - `jfg::interp::object_push_id(objectSerial, push)`.
  - `class OwnerStack { void push(uint32_t object); void pop(); bool empty() const; uint32_t next_push(); uint32_t
    owner() const; }`.
  - `RT64::jfgModelUnderCamera(absolute, viewProj)`.
  - Hooks: `jfg_interp_owner_enter` (entry of `func_8000DB94`), `jfg_interp_owner_leave` (`func_8000DB94` before
    0x8000DC1C), `jfg_interp_f2l` (entry of `func_80048D84`, added to the probe's hook).

- [ ] **Step 1: Write the failing tests**

`tests/interp_groups_test.cpp`, before `std::puts`:
```cpp
    // An absolute draw is model x view-projection (row vectors); the model comes back as absolute x inverse(vp).
    const hlslpp::float4x4 vp(1.5f, 0.0f, 0.0f, 0.0f,
                              0.0f, 2.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, -1.001f, -1.0f,
                              -40.0f, 50.0f, 1400.0f, 1420.0f);
    const hlslpp::float4x4 model(0.0f, 0.0f, -0.5f, 0.0f,
                                 0.0f, 0.5f, 0.0f, 0.0f,
                                 0.5f, 0.0f, 0.0f, 0.0f,
                                 3.0f, 176.0f, 1495.0f, 1.0f);
    const hlslpp::float4x4 absolute = hlslpp::mul(model, vp);
    const hlslpp::float4x4 back = jfgModelUnderCamera(absolute, vp);
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) assert(std::fabs(back[r][c] - model[r][c]) < 1e-3f);
    }
```
(add `#include <cmath>`).

`tests/interp_test.cpp`, before `std::puts`:
```cpp
    // The owner of absolute pushes: the object objPrintObject is drawing (nested draws restore the outer one), and
    // nobody outside it. Each owner numbers its pushes from 0; beyond 15 they get no identity.
    jfg::interp::OwnerStack owners;
    assert(owners.empty());
    owners.push(0x801BB2C0);
    assert(owners.owner() == 0x801BB2C0 && owners.next_push() == 0 && owners.next_push() == 1);
    owners.push(0x801BDEB0);              // its gun, drawn inside it
    assert(owners.owner() == 0x801BDEB0 && owners.next_push() == 0);
    owners.pop();
    assert(owners.owner() == 0x801BB2C0 && owners.next_push() == 2);
    for (int i = 3; i < 16; i++) owners.next_push();
    assert(owners.next_push() == jfg::interp::OwnerStack::no_push); // the 17th push
    owners.pop();
    assert(owners.empty());
    owners.pop();                         // unbalanced pops are harmless
    assert(owners.empty());
    assert(jfg::interp::object_push_id(1, 0) == 0x02000010 && jfg::interp::object_push_id(1, 15) == 0x0200001F);
    // A flagged entry keeps its flag through take().
    rec.add(0x6000, 0x02000010, 0, RT64::JFG_FLAG_UNDER_CAMERA);
    Frame flagged = rec.take(0x1FB0C0, 30);
    assert(flagged.entries.size() == 1 && flagged.entries[0].flags == RT64::JFG_FLAG_UNDER_CAMERA);
```

- [ ] **Step 2: Run them to make sure they fail**

Run: `scripts/test-hooks.sh`
Expected: compile errors (`jfgModelUnderCamera`, `OwnerStack`, `flags`).

- [ ] **Step 3: Implement the pure parts**

- `lib/rt64/src/hle/rt64_jfg_frame.h`:
  - Add `enum : uint8_t { JFG_FLAG_UNDER_CAMERA = 1 << 0 };` with the comment `// An absolute world draw (model x the
    world camera's view-projection): drawn as its model under the camera.`
  - Add `uint8_t flags;` after `skip` in `JfgMatrixIdentity`.
- `lib/rt64/src/hle/rt64_jfg_groups.h`: add `#include "common/rt64_math.h"` if `hlslpp` isn't already available
  through `rt64_transform_group.h`, and:
  ```cpp
      // An absolute draw's model matrix: absolute = model x viewProj (row vectors), so model = absolute x inverse(viewProj).
      inline hlslpp::float4x4 jfgModelUnderCamera(const hlslpp::float4x4 &absolute, const hlslpp::float4x4 &viewProj) {
          return hlslpp::mul(absolute, hlslpp::inverse(viewProj));
      }
  ```
- `include/jfg_interp.h`:
  - Change `Recorder::add` to `void add(uint32_t address, uint32_t id, uint8_t skip = 0, uint8_t flags = 0);`.
  - After `joint_id`:
    ```cpp
    // An absolute world draw: the owning object's serial and the push number within its draw (pushes < 16).
    constexpr uint32_t object_push_id(uint32_t object_serial, uint32_t push) {
        return 0x02000000u + 0x10u * object_serial + push;
    }

    // The objects objPrintObject is drawing, innermost last; absolute pushes belong to the innermost.
    class OwnerStack {
    public:
        static constexpr uint32_t no_push = 0xFFFFFFFFu;
        void push(uint32_t object) { stack_.push_back({object, 0}); }
        void pop() { if (!stack_.empty()) stack_.pop_back(); }
        bool empty() const { return stack_.empty(); }
        uint32_t owner() const { return stack_.back().object; }
        // The next push number for the innermost owner, or no_push beyond 15.
        uint32_t next_push() { uint32_t& n = stack_.back().pushes; return n < 16 ? n++ : no_push; }

    private:
        struct Owner { uint32_t object; uint32_t pushes; };
        std::vector<Owner> stack_;
    };

    OwnerStack& owners();
    ```
- `src/jfg/interp.cpp`:
  - `add` stores `{address, id, skip, flags}`. In `take`, OR the flags of duplicates together, as the skips are.
  - Add `OwnerStack& owners() { static OwnerStack o; return o; }`.

- [ ] **Step 4: Run the tests**

Run: `scripts/test-hooks.sh`
Expected: all OK.

- [ ] **Step 5: Hook the owners and the conversions**

`include/jfg_interp_hooks.h`:
```c
/* Entry and exit of objPrintObject (0x8000DB94, object in $a3; single exit at 0x8000DC1C). */
void jfg_interp_owner_enter(uint8_t* rdram, recomp_context* ctx);
void jfg_interp_owner_leave(uint8_t* rdram, recomp_context* ctx);
/* Entry of mathMtxF2L (0x80048D84): the float matrix at $a0 is converted into the fixed-point matrix at $a1. */
void jfg_interp_f2l(uint8_t* rdram, recomp_context* ctx);
```
`src/jfg/interp_hooks.cpp`, in the anonymous namespace:
```cpp
constexpr gpr model_view_proj = (gpr)(int32_t)0x800FB038; // model x view-projection: an absolute world draw's source
constexpr gpr view_proj = (gpr)(int32_t)0x800FAAF0;       // the current view-projection
uint8_t world_view_proj[64];                              // the world camera's, as camSetProjMtx made it
bool camera_conversion_next = false;                      // set at camSetProjMtx entry; its mathMtxF2L follows

void read_matrix(uint8_t* rdram, gpr address, uint8_t out[64]) {
    for (int i = 0; i < 16; i++) {
        const uint32_t word = (uint32_t)MEM_W(4 * i, address);
        std::memcpy(out + 4 * i, &word, 4);
    }
}
```
In `jfg_interp_camera`, add `camera_conversion_next = true;`. Then:
```cpp
extern "C" void jfg_interp_owner_enter(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::owners().push((uint32_t)ctx->r7);
}

extern "C" void jfg_interp_owner_leave(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::owners().pop();
}

extern "C" void jfg_interp_f2l(uint8_t* rdram, recomp_context* ctx) {
    const gpr source = ctx->r4;
    if (source == view_proj && camera_conversion_next) {
        camera_conversion_next = false;
        read_matrix(rdram, view_proj, world_view_proj);
        return;
    }
    if (source != model_view_proj || jfg::interp::owners().empty()) return;
    // Only draws under the world camera (HUD 3D objects use their own view-projection).
    uint8_t current[64];
    read_matrix(rdram, view_proj, current);
    if (std::memcmp(current, world_view_proj, sizeof current) != 0) return;
    const uint32_t push = jfg::interp::owners().next_push();
    if (push == jfg::interp::OwnerStack::no_push) return;
    const uint32_t serial = jfg::interp::object_serials().get(jfg::interp::owners().owner());
    // $a1 is the destination matrix itself (mathMtxF2L stores through it: sh $t1, 0x18($a1) at 0x80048DD4).
    jfg::interp::recorder().add((uint32_t)ctx->r5 & 0x3FFFFFF, jfg::interp::object_push_id(serial, push), 0,
                                RT64::JFG_FLAG_UNDER_CAMERA);
}
```

`recomp/jfg.toml.in`: change the probe's `func_80048D84` hook text to
`"jfg_probe_f2l(rdram, ctx); jfg_interp_f2l(rdram, ctx);"`, and add:
```toml
# Frame interpolation: the object objPrintObject is drawing owns the absolute pushes made inside it.
[[patches.hook]]
func = "func_8000DB94"          # objPrintObject (object in $a3)
text = "jfg_interp_owner_enter(rdram, ctx);"

[[patches.hook]]
func = "func_8000DB94"          # objPrintObject's single exit
before_vram = 0x8000DC1C
text = "jfg_interp_owner_leave(rdram, ctx);"
```

- [ ] **Step 6: Draw flagged absolute matrices as models under the camera**

In `lib/rt64/src/hle/rt64_rsp.cpp`, `applyMatrixSlotDKR`, replace the absolute branch's body with:
```cpp
        if (dkr.slotAbsolute[slot]) {
            const JfgFrameTable *table = state->jfgFrame;
            const JfgMatrixIdentity *entry = ((table != nullptr) && table->enabled) ? table->find(dkr.slotAddress[slot]) : nullptr;
            if ((entry != nullptr) && (entry->flags & JFG_FLAG_UNDER_CAMERA) && (dkr.viewProj0Address != 0)) {
                // An absolute world draw (model x the world camera): its model under the ID'd camera, so it moves with
                // the interpolated camera instead of against it.
                if (!dkr.viewProjIsSlot0) {
                    setViewProjDKR(dkr.viewProj0, dkr.viewProj0Address);
                    dkr.viewProjIsSlot0 = true;
                }
                modelMatrixStack[modelMatrixStackSize - 1] = jfgModelUnderCamera(dkr.slots[slot], dkr.viewProj0);
                setModelGroupDKR(dkr.slotAddress[slot]);
            }
            else {
                // A complete transform: make it the projection with an identity model, drawn as-is.
                setViewProjDKR(dkr.slots[slot], dkr.slotAddress[slot], false);
                dkr.viewProjIsSlot0 = false;
                modelMatrixStack[modelMatrixStackSize - 1] = hlslpp::float4x4::identity();
                setModelGroupIgnoreDKR();
            }
        }
```

- [ ] **Step 7: Build, regenerate, check tagging and the picture at rest**

```bash
scripts/recompile.sh && source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
A=build/appdata-interp; export APP_FOLDER_PATH=$PWD/$A DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
for mode in on off; do
  env $([ $mode = on ] && echo JFG_INTERPOLATION=1) JFG_DEBUG_INTERP=1 JFG_SURVEY_SHOTS="70 72" \
    scripts/survey-level.sh 92 build/diag/interp2-t4-$mode
done
grep "\[interp\] task" build/diag/interp2-t4-on/run.log | sed -n '900p'
python3 -c "
from PIL import Image; import numpy as np
a = np.asarray(Image.open('build/diag/interp2-t4-on/t72.png').convert('L'), np.float32)
b = np.asarray(Image.open('build/diag/interp2-t4-off/t72.png').convert('L'), np.float32)
print('mean abs diff', np.abs(a - b).mean())"
```
Expected:
- The task line shows more models tagged than in Task 3 (objects, the gun and the sky on top of the joints).
- The two screenshots differ only where things move between capture moments: the mean absolute difference is small.
  Look at both by eye. Objects, the gun and the sky must be in the same places, with nothing missing. Plan 1's
  original absolute path once dropped draws when it inverted the camera, and that's the failure this check catches.

- [ ] **Step 8: Commit**

```bash
git add lib-patches/rt64/0001-jfg-gbi.patch tests/interp_groups_test.cpp include/jfg_interp.h src/jfg/interp.cpp \
    tests/interp_test.cpp include/jfg_interp_hooks.h src/jfg/interp_hooks.cpp recomp/jfg.toml.in
git commit -m "Interpolation: absolute world draws (objects, guns, the sky) drawn as models under the camera

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 5: Verify, document, and write plan 3

**Files:**
- Modify: `docs/superpowers/handoff-2026-10-14.md`, this plan (results)
- Create: `docs/superpowers/plans/<date>-jfg-frame-interpolation-3.md`

- [ ] **Step 1: Recordings**

With interpolation on (`JFG_INTERPOLATION=1`), record the ship landing (`tools/diag/record.sh ... 250 45` with the
Goldwood script) and Goldwood gameplay, both as in plan 1 Task 7. Also record split screen: `JFG_FAKE_PLAYERS=2`, with
the multiplayer path described in `docs/superpowers/plans/2026-10-15-jfg-whole-game.md`.
Expected:
- 0 flicker spikes in each.
- Frames around cuts are clean.
- Objects, characters and the sky sit where they should in every frame.

- [ ] **Step 2: Results and status**

Add a `## Task 5 results` section to this plan with the numbers and anything seen. Update the frame interpolation
entry in `docs/superpowers/handoff-2026-10-14.md`: what interpolates now, and what's left (sprites and particles,
the reflection pass camera, the refraction display list, stars and lens flares).

- [ ] **Step 3: Write plan 3**

Use the superpowers:writing-plans skill. Cover:
- **Sprites:** `camDoSprite` (0x80041174) with `$a3` = the ObjTrans owner (a particle slot, object or camlight entry)
  and `*$a1` = its slot-2 matrix. Billboards are rebuilt as model transforms under the camera, using the research's
  exact construction, model rows (l_x, l_y, forward, anchor). Note the w-offset caveat close to the camera.
- **Particle slot generations:** the allocator `func_80060EC4`.
- The reflection pass camera (module 137) and the refraction display list.
- Split screen fixes found in Step 1.
- Bringing the menu row back, off by default. The default-on decision waits for the user's look on real hardware.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/
git commit -m "Interpolation: plan 2 results; plan 3 (sprites, particles, menu)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```
