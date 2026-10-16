# Frame interpolation, plan 1 (phases 0–2): implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Map where every matrix in a JFG frame comes from, build the identity table and its handoff to RT64, and
make the cameras interpolate cleanly behind an opt-in toggle.

**Architecture:** game-side hooks record which RDRAM matrix addresses belong to which stable ID in a per-frame
`jfg::interp::Frame`. The frame is pushed to a FIFO when the game submits its graphics task (`func_8004DA10`) and
popped in `RT64Context::send_dl` for the matching display list. Our RT64 patch reads it through
`State::jfgFrame` and gives each matrix load an explicit transform-group ID, or `G_EX_ID_IGNORE` if it's unknown.

**Tech Stack:** C++20, N64Recomp hooks (`recomp/jfg.toml.in`), RT64 (patched via `lib-patches/rt64/0001-jfg-gbi.patch`),
RmlUi menus, Python 3.12 for diagnostics, clang 18 from `tools/toolchain.env`.

**Spec:** `docs/superpowers/specs/2026-10-16-jfg-frame-interpolation-design.md`. Plan 2 (phases 3–6: characters,
rigid objects, sky, absolute draws, sprites, split screen, default on) is written in Task 8 from Task 1's findings.

## Global Constraints

- Linux x86-64 only. Build: `source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20`.
  After editing `recomp/jfg.toml.in`, run `scripts/recompile.sh` first.
- Work on branch `main`; commit after every task with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`;
  push with `git push github main`.
- Never commit the ROM or anything derived from it: `generated/`, `build/`, RDRAM dumps, recordings, screenshots, or
  game data copied into tests (tests use made-up data).
- The RT64 patch is regenerated, never hand-edited: in `lib/rt64`, `git add -N <new files>` then
  `git diff > ../../lib-patches/rt64/0001-jfg-gbi.patch`, then `git reset -q <new files>`. New RT64 files so far:
  `src/gbi/rt64_gbi_f3djfg.cpp`, `src/gbi/rt64_gbi_f3djfg.h`; this plan adds `src/hle/rt64_jfg_frame.h`.
- IDs are 32-bit and never `G_EX_ID_IGNORE` (0) or `G_EX_ID_AUTO` (0xFFFFFFFF). Cameras: `0x100 + 0x10 * role + viewport`.
- With the toggle off, every JFG matrix and projection is `G_EX_ID_IGNORE`: nothing interpolates, whatever the
  Framerate setting.
- Anything not in the table is drawn as-is (`G_EX_ID_IGNORE`), never guessed (`G_EX_ID_AUTO`).
- Fail loudly: a frame popped for the wrong display list, or a table address recorded with two different IDs, is
  logged with `[interp]` and counted, never silently used.
- Shell variables used in `rm` or as `APP_FOLDER_PATH` are always set in the same command, and `rm` paths use
  `"${A:?}"`: an empty variable once pointed the game's settings at the repository root.
- Test runs never touch the user's saves: use `APP_FOLDER_PATH=$PWD/build/appdata-interp` (create it and copy
  `generated/jfg.z64` to `build/appdata-interp/jfg.n64.us.z64` first), and run on the virtual display
  (`DISPLAY=:99`, `SDL_AUDIODRIVER=dummy`, `unset WAYLAND_DISPLAY`; start `Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp &`
  if `xwininfo -root -display :99` fails).

## Review Focus

- **Display list buffers alternate:** the game double-buffers its display lists, so two frames in a row have
  different `data_ptr` values but the same two values recur. A pop must match the oldest pending frame with that
  address, not any frame with it (test in Task 2).
- **The renderer falls behind:** if `send_dl` is slower than the game, frames pile up; beyond 8 pending the oldest is
  dropped with a log line, and later pops still pair correctly (test in Task 2).
- **Toggle switched mid-game:** turning interpolation on or off while playing must take effect on the next frame
  without a stale table being used (the table pointer is cleared after every `send_dl`; checked in Task 5 Step 6).
- **Module screens** (title, menus) submit tasks through the same routine with no recorded identities; they must get
  an empty table, not the previous gameplay frame's (test in Task 2: `take()` after no `add()` yields no entries).
- **Camera cuts:** a cutscene switching shots must not slide the camera between them; cut frames are SKIP (Task 6
  tests and the landing recording in Task 7).

---

### Task 1: Phase 0, matrix provenance probe

Find which game function wrote every matrix a frame's display list loads, and what kind of draw each is.

Known before starting (from research on 2026-10-16):
- `mathMtxF2L` (0x80048D84, 35 call sites) converts a float matrix to the display list's fixed-point format: source
  floats at `$a0`, destination at `$a1` (`ctx->r5`).
- `camConvertMatrixList` (0x80042470) only appends `(a0, a1)` to a list at 0x800FA658 (count at 0x800A2ED0); `camTick`
  (0x800424A0) walks that list and calls `conv_mult_matrix` (0x80074E50). So some matrices are written by routines other
  than `mathMtxF2L`. Those show up as unexplained loads in Step 6 and are traced by hand in Step 7.
- `RSP::reset()` runs once per `send_dl` (graphics task) and once when RT64 starts (`rt64_state.cpp:93`).

**Files:**
- Create: `src/jfg/interp_probe.cpp`, `include/jfg_interp_probe.h`, `tools/diag/mtx-provenance.py`
- Modify: `recomp/jfg.toml.in` (two probe hooks, `recomp_include`), `CMakeLists.txt` (source list),
  `lib/rt64/src/hle/rt64_rsp.h` and `rt64_rsp.cpp` (task counter), `lib/rt64/src/gbi/rt64_gbi_f3djfg.cpp` (`dmaMatrix`
  log), `lib-patches/rt64/0001-jfg-gbi.patch` (regenerated), this plan (findings section)

**Interfaces:**
- Produces: the `JFG_DEBUG_MTXSRC=<frame>` switch.
  - Game side: `[mtxsrc] submit <n> dl <address>` at each graphics-task submit, and
    `[mtxsrc] write <frame> <dst> ra <ra> via F2L` for every `mathMtxF2L` call while frame `<frame>` is built.
  - RT64 side: `[mtxsrc] task <n>` and `[mtxsrc] task <n> load slot <s> mul <m> addr <address>` for every matrix load
    in tasks `<frame>-2` to `<frame>+2`. The script picks the task whose loads match the frame's writes, so the two
    counters needn't agree.
- Produces: `RSP::jfgTask` (`int64_t`, the index of the current graphics task, -1 before the first). Task 5 reuses it.

- [ ] **Step 1: Write the probe**

`include/jfg_interp_probe.h`:
```cpp
#ifndef JFG_INTERP_PROBE_H
#define JFG_INTERP_PROBE_H
/* JFG_DEBUG_MTXSRC=<frame>: which code wrote each matrix of one frame. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): a graphics task is submitted. */
void jfg_probe_submit(uint8_t* rdram, recomp_context* ctx);
/* Entry of mathMtxF2L (0x80048D84): a float matrix at $a0 is converted into the fixed-point matrix at $a1. */
void jfg_probe_f2l(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif
```

`src/jfg/interp_probe.cpp`:
```cpp
// JFG_DEBUG_MTXSRC=<frame>: logs every mathMtxF2L conversion while frame <frame> is built, with the caller's return
// address, and each graphics-task submit, so tools/diag/mtx-provenance.py can name the writer of every matrix the
// display list loads (RT64's dmaMatrix logs the loads).
#include <cstdio>
#include <cstdlib>

#include "recomp.h"
#include "jfg_interp_probe.h"

namespace {
const long probe_frame = std::getenv("JFG_DEBUG_MTXSRC") ? std::atol(std::getenv("JFG_DEBUG_MTXSRC")) : -1;
// The frame whose display list is being built (game thread only). func_8004DA10 runs at the start of each frame
// and submits the display list built during the previous one, so matrices written after submit n belong to the
// frame submitted next.
long building = 0;
}

extern "C" void jfg_probe_submit(uint8_t* rdram, recomp_context* ctx) {
    if (probe_frame < 0) return;
    std::fprintf(stderr, "[mtxsrc] submit %ld dl %08X\n", building, (uint32_t)ctx->r4 & 0x3FFFFFF);
    building++;
}

extern "C" void jfg_probe_f2l(uint8_t* rdram, recomp_context* ctx) {
    if (probe_frame < 0 || building != probe_frame) return;
    std::fprintf(stderr, "[mtxsrc] write %ld %08X ra %08X via F2L\n", building, (uint32_t)ctx->r5 & 0x3FFFFFF,
                 (uint32_t)ctx->r31);
}
```

- [ ] **Step 2: Hook them**

In `recomp/jfg.toml.in`, append `\n#include \"jfg_interp_probe.h\"` to `recomp_include` and add:
```toml
# Frame interpolation, phase 0: matrix provenance (JFG_DEBUG_MTXSRC). See src/jfg/interp_probe.cpp.
[[patches.hook]]
func = "func_8004DA10"          # setupOSTasks: submits the previous frame's display list
text = "jfg_probe_submit(rdram, ctx);"

[[patches.hook]]
func = "func_80048D84"          # mathMtxF2L
text = "jfg_probe_f2l(rdram, ctx);"
```
Add `${CMAKE_SOURCE_DIR}/src/jfg/interp_probe.cpp` to the `SOURCES` list in `CMakeLists.txt`, after `big_head.cpp`.

- [ ] **Step 3: Count tasks and log loads on the RT64 side**

In `lib/rt64/src/hle/rt64_rsp.h`, in `struct RSP` next to the `dkr` block, add:
```cpp
        // Jet Force Gemini: index of the graphics task being processed (send_dl resets the RSP once per task; the
        // reset at start-up leaves it at -1).
        int64_t jfgTask = -2;
```
At the start of `RSP::reset()` in `lib/rt64/src/hle/rt64_rsp.cpp`, add `jfgTask++;`.

In `lib/rt64/src/gbi/rt64_gbi_f3djfg.cpp`, next to the other `getenv` switches at the top, add:
```cpp
        // JFG_DEBUG_MTXSRC=<frame>: log every matrix load of the tasks around that frame (tools/diag/mtx-provenance.py).
        const long mtxsrcFrame = std::getenv("JFG_DEBUG_MTXSRC") ? std::atol(std::getenv("JFG_DEBUG_MTXSRC")) : -1;
        int64_t mtxsrcLastTask = -1000;
```
In `dmaMatrix`, right after `const uint32_t slot = index & 3;`, add:
```cpp
            if ((mtxsrcFrame >= 0) && (rsp->jfgTask >= mtxsrcFrame - 2) && (rsp->jfgTask <= mtxsrcFrame + 2)) {
                if (rsp->jfgTask != mtxsrcLastTask) {
                    fprintf(stderr, "[mtxsrc] task %lld\n", (long long)rsp->jfgTask);
                    mtxsrcLastTask = rsp->jfgTask;
                }
                fprintf(stderr, "[mtxsrc] task %lld load slot %u mul %d addr %08X\n", (long long)rsp->jfgTask, index, multiply, address);
            }
```
Regenerate the patch (Global Constraints recipe), then build:
```bash
scripts/recompile.sh && source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
```
Expected: build succeeds.

- [ ] **Step 4: Write the join script**

`tools/diag/mtx-provenance.py`:
```python
"""Matrix provenance for one frame (JFG_DEBUG_MTXSRC). Usage: mtx-provenance.py RUN.LOG SYMS.TXT
Joins '[mtxsrc] write' lines (who converted each matrix while the frame was built) with the '[mtxsrc] task' loads
of the graphics task that best matches them, then prints, per writer, how many loads of each kind (camera, relative,
absolute) it explains, and every load nothing explains. SYMS.TXT is build/diag/syms.txt; main code is section 0 at
0x80000450. Return addresses outside main code (relocatable modules) are printed raw."""
import re, sys
from collections import Counter, defaultdict

log = open(sys.argv[1]).read().splitlines()
syms = sorted((0x80000450 + int(off, 16), name) for sec, off, name in (l.split() for l in open(sys.argv[2])) if sec == "0")

def name_of(ra):
    if not (0x80000450 <= ra < 0x800B0000):
        return f"{ra:08X} (module)"
    best = max((s for s in syms if s[0] <= ra), default=None)
    return f"{best[1]}+{ra - best[0]:#x}" if best else f"{ra:08X}"

writes = {}
for line in log:
    m = re.match(r"\[mtxsrc\] write \d+ ([0-9A-F]{8}) ra ([0-9A-F]{8}) via (\w+)", line)
    if m:
        writes[int(m.group(1), 16)] = (name_of(int(m.group(2), 16)), m.group(3))
tasks = defaultdict(list)
for line in log:
    m = re.match(r"\[mtxsrc\] task (-?\d+) load slot (\d+) mul (\d) addr ([0-9A-F]{8})", line)
    if m:
        tasks[int(m.group(1))].append((int(m.group(2)), int(m.group(3)), int(m.group(4), 16)))
if not tasks:
    sys.exit("no '[mtxsrc] task' loads in the log")
task = max(tasks, key=lambda t: sum(1 for _, _, a in tasks[t] if a in writes))
print(f"task {task}: {len(tasks[task])} loads, {len(writes)} conversions recorded")
kinds = defaultdict(Counter)
unexplained = []
for slot, mul, addr in tasks[task]:
    kind = "camera" if slot == 0 and mul == 0 else ("relative" if mul else "absolute")
    if addr in writes:
        kinds[writes[addr]][kind] += 1
    else:
        unexplained.append((addr, slot, mul, kind))
for (writer, via), c in sorted(kinds.items(), key=lambda kv: -sum(kv[1].values())):
    print(f"{writer:48s} {via:5s} " + " ".join(f"{k}={n}" for k, n in sorted(c.items())))
print(f"unexplained loads: {len(unexplained)}")
for addr, slot, mul, kind in unexplained[:60]:
    print(f"  {addr:08X} slot {slot} mul {mul} {kind}")
```

- [ ] **Step 5: Run it on a Goldwood gameplay frame and a landing frame**

```bash
A=build/appdata-interp; mkdir -p $A && cp generated/jfg.z64 $A/jfg.n64.us.z64; rm -f $A/saves/jfg.n64.us.bin*
export APP_FOLDER_PATH=$PWD/$A DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
GOLDWOOD="30+0.3 START; 36+0.3 START; 42+0.3 A; 49+0.2 A; 50.5+0.15 - 0 -1; 51.3+0.15 - 0 -1; 52.1+0.15 - 0 -1; 52.9+0.15 - 0 -1; 54+0.2 A; 230+0.3 A; 245+0.3 A; 260+0.3 A; 275+0.3 Z; 296+4 - 0 1; 302+2 - 1 0"
JFG_INPUT_CLOCK=frames:30 JFG_INPUT_SCRIPT="$GOLDWOOD" JFG_DEBUG_MTXSRC=9300 \
  scripts/screenshots.sh build/diag/mtxsrc-goldwood 330 > /dev/null 2>&1
python3 tools/diag/mtx-provenance.py build/diag/mtxsrc-goldwood/run.log build/diag/syms.txt
```
Frame 9300 is about 310 s of game time at 30 fps, in Goldwood gameplay. `JFG_INPUT_CLOCK=frames:30` times the script by
game frames, so the run keeps pace however slowly the virtual display renders, and the screenshot at 330 s comes
after it. If the frame isn't gameplay (the `[mtxsrc] submit` lines give the count; Goldwood gameplay starts about
300 s in), pick another. Repeat with frame 8400 (the pod's flight) into `build/diag/mtxsrc-landing`.
Expected: the cameras and most loads are explained by a handful of writers.

- [ ] **Step 6: Trace the unexplained loads and record the findings**

For each group of unexplained loads, find the writer by hand. Look at the matrix in a dump (`JFG_DEBUG_RDRAM` with
`JFG_DEBUG_GBI=<seconds>` writes RDRAM, see `tools/diag/README.md`), and search the generated code for the routines
that write it. Start with `conv_mult_matrix` (0x80074E50, called by `camTick`) and the billboard and sky code.
Append a section `## Phase 0 findings` at the end of this file. It has one row per kind of draw (each camera seen,
sky, absolute draws, rigid objects, character joints, sprites and billboards, shots, HUD), and these columns: writer
function(s), loads per frame, where a stable identity can come from (camera role, object and joint, particle slot),
and the hook point to record it.

- [ ] **Step 7: Commit**

```bash
git add include/jfg_interp_probe.h src/jfg/interp_probe.cpp tools/diag/mtx-provenance.py recomp/jfg.toml.in \
    CMakeLists.txt lib-patches/rt64/0001-jfg-gbi.patch docs/superpowers/plans/2026-10-16-jfg-frame-interpolation.md
git commit -m "Interpolation phase 0: matrix provenance probe and findings

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 2: Identity table, recorder and handoff (pure logic)

**Files:**
- Create: `lib/rt64/src/hle/rt64_jfg_frame.h` (in the RT64 patch), `include/jfg_interp.h`, `src/jfg/interp.cpp`,
  `tests/interp_test.cpp`
- Modify: `scripts/test-hooks.sh`, `CMakeLists.txt` (add `src/jfg/interp.cpp` to `SOURCES`),
  `lib-patches/rt64/0001-jfg-gbi.patch` (regenerated, with the new header)

**Interfaces:**
- Produces (RT64 header, used by Tasks 4–6):
  `RT64::JfgMatrixIdentity { uint32_t address; uint32_t id; uint8_t skip; }`;
  `RT64::JFG_SKIP_POSITION = 1, JFG_SKIP_ROTATION = 2, JFG_SKIP_SCALE = 4, JFG_SKIP_ALL = 7`;
  `RT64::JfgFrameTable { bool enabled; const JfgMatrixIdentity *entries; size_t count; const JfgMatrixIdentity *find(uint32_t address) const; }`.
- Produces (port, used by Tasks 4 and 6): `jfg::interp::camera_id(int role, int viewport)`;
  `struct Frame { uint64_t sequence; uint32_t dl_address; uint16_t rate; std::vector<RT64::JfgMatrixIdentity> entries; uint32_t conflicts; }`;
  `class Recorder { void add(uint32_t address, uint32_t id, uint8_t skip = 0); Frame take(uint32_t dl_address, uint16_t rate); }`;
  `class Handoff { void push(Frame); std::optional<Frame> pop(uint32_t dl_address, uint32_t* discarded = nullptr); }`;
  `Recorder& recorder(); Handoff& handoff();`.

- [ ] **Step 1: Write the RT64 header**

`lib/rt64/src/hle/rt64_jfg_frame.h`:
```cpp
//
// RT64
//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Jet Force Gemini frame interpolation: which matrices one frame's display list loads belong to what. The port's
// hooks fill it (src/jfg/interp.cpp in the port) and the F3DJFG handlers read it through State::jfgFrame. Plain
// data, so RT64 doesn't depend on the port.
namespace RT64 {
    enum : uint8_t {
        JFG_SKIP_POSITION = 1 << 0,
        JFG_SKIP_ROTATION = 1 << 1,
        JFG_SKIP_SCALE = 1 << 2, // scale and skew
        JFG_SKIP_ALL = JFG_SKIP_POSITION | JFG_SKIP_ROTATION | JFG_SKIP_SCALE,
    };

    struct JfgMatrixIdentity {
        uint32_t address; // physical RDRAM address of the 64-byte matrix the display list loads
        uint32_t id;      // stable across frames; never G_EX_ID_IGNORE (0) or G_EX_ID_AUTO (0xFFFFFFFF)
        uint8_t skip;     // JFG_SKIP_* bits: components not interpolated into this frame
    };

    struct JfgFrameTable {
        bool enabled = false;                       // the port's Frame Interpolation toggle
        const JfgMatrixIdentity *entries = nullptr; // sorted by address, one entry per address
        size_t count = 0;

        const JfgMatrixIdentity *find(uint32_t address) const {
            const JfgMatrixIdentity *end = entries + count;
            const JfgMatrixIdentity *it = std::lower_bound(entries, end, address,
                [](const JfgMatrixIdentity &e, uint32_t a) { return e.address < a; });
            return ((it != end) && (it->address == address)) ? it : nullptr;
        }
    };
};
```

- [ ] **Step 2: Write the failing test**

`tests/interp_test.cpp`:
```cpp
#include <cassert>
#include <cstdio>
#include "jfg_interp.h"

using jfg::interp::Frame;

int main() {
    // IDs: cameras by role and viewport, never IGNORE (0) or AUTO (0xFFFFFFFF), all distinct.
    assert(jfg::interp::camera_id(0, 0) == 0x100);
    assert(jfg::interp::camera_id(1, 3) == 0x113);
    assert(jfg::interp::camera_id(2, 0) != jfg::interp::camera_id(1, 0));

    // A frame is sorted by address; the same address and ID twice is one entry; two IDs for one address is a
    // conflict and neither is kept.
    jfg::interp::Recorder rec;
    rec.add(0x2030, 7);
    rec.add(0x1000, 5, RT64::JFG_SKIP_ALL);
    rec.add(0x2030, 7);
    rec.add(0x3000, 1);
    rec.add(0x3000, 2);
    Frame f = rec.take(0x1FB0C0, 30);
    assert(f.sequence == 0 && f.dl_address == 0x1FB0C0 && f.rate == 30);
    assert(f.entries.size() == 2 && f.conflicts == 1);
    assert(f.entries[0].address == 0x1000 && f.entries[0].id == 5 && f.entries[0].skip == RT64::JFG_SKIP_ALL);
    assert(f.entries[1].address == 0x2030 && f.entries[1].id == 7);

    // The renderer's view of it.
    RT64::JfgFrameTable table{true, f.entries.data(), f.entries.size()};
    assert(table.find(0x2030)->id == 7);
    assert(table.find(0x2031) == nullptr && table.find(0x0FFF) == nullptr && table.find(0x3000) == nullptr);

    // Taking starts a new, empty frame (a module screen records nothing, so it gets nothing).
    Frame g = rec.take(0x220FE0, 20);
    assert(g.sequence == 1 && g.entries.empty() && g.conflicts == 0 && g.rate == 20);

    // Handoff: display lists alternate between two buffers; a pop takes the oldest frame for that buffer and
    // discards the frames before it.
    jfg::interp::Handoff h;
    for (uint32_t i = 0; i < 4; i++) {
        Frame x; x.sequence = i; x.dl_address = (i % 2) ? 0x220FE0 : 0x1FB0C0;
        h.push(std::move(x));
    }
    uint32_t discarded = 0;
    auto p = h.pop(0x220FE0, &discarded);
    assert(p && p->sequence == 1 && discarded == 1);
    p = h.pop(0x1FB0C0, &discarded);
    assert(p && p->sequence == 2 && discarded == 0);
    // A display list nobody pushed leaves the queue alone.
    assert(!h.pop(0x123456, &discarded) && discarded == 0);
    p = h.pop(0x220FE0, &discarded);
    assert(p && p->sequence == 3);
    assert(!h.pop(0x220FE0));

    // Beyond max_pending, the oldest frames are dropped.
    for (uint32_t i = 0; i < jfg::interp::Handoff::max_pending + 3; i++) {
        Frame x; x.sequence = 100 + i; x.dl_address = 0x1FB0C0;
        h.push(std::move(x));
    }
    p = h.pop(0x1FB0C0, &discarded);
    assert(p && p->sequence == 103 && discarded == 0);

    std::puts("interp_test: OK");
}
```
Add to `scripts/test-hooks.sh` (after the `skeleton_test` lines):
```bash
"$CXX" -std=c++20 -Wall -Iinclude -Ilib/rt64/src src/jfg/interp.cpp tests/interp_test.cpp -o build/interp_test
build/interp_test
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `c++ -std=c++20 -Wall -Iinclude -Ilib/rt64/src src/jfg/interp.cpp tests/interp_test.cpp -o build/interp_test`
Expected: FAIL, `jfg_interp.h: No such file or directory`.

- [ ] **Step 4: Write the implementation**

`include/jfg_interp.h`:
```cpp
#ifndef JFG_INTERP_H
#define JFG_INTERP_H
// Frame interpolation: per-frame matrix identities from the game's hooks, handed to RT64 with the frame's graphics
// task (docs/superpowers/specs/2026-10-16-jfg-frame-interpolation-design.md).
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "hle/rt64_jfg_frame.h"

namespace jfg::interp {
// Cameras, by role (world, sky, 2D, ... as Phase 0 found them) and viewport (0-3).
constexpr uint32_t camera_id(int role, int viewport) { return 0x100u + 0x10u * uint32_t(role) + uint32_t(viewport); }

// One game frame's identities.
struct Frame {
    uint64_t sequence = 0;   // graphics tasks submitted before this one
    uint32_t dl_address = 0; // physical address of the frame's display list (the task's data_ptr)
    uint16_t rate = 30;      // the game's frame rate for this frame
    std::vector<RT64::JfgMatrixIdentity> entries; // sorted by address, one per address
    uint32_t conflicts = 0;  // addresses recorded with two different IDs (dropped)
};

// Collects the frame being built. Game thread only.
class Recorder {
public:
    void add(uint32_t address, uint32_t id, uint8_t skip = 0);
    // The frame recorded since the last take, finished (sorted, conflicts dropped); starts a new one.
    Frame take(uint32_t dl_address, uint16_t rate);

private:
    std::vector<RT64::JfgMatrixIdentity> entries_;
    uint64_t next_sequence_ = 0;
};

// Frames waiting for the renderer, in submission order. Pushed on the game thread, popped on the gfx thread.
class Handoff {
public:
    static constexpr size_t max_pending = 8;
    void push(Frame frame);
    // The oldest pending frame for this display list. Frames queued before it are discarded and counted; if none
    // matches, nothing is removed.
    std::optional<Frame> pop(uint32_t dl_address, uint32_t* discarded = nullptr);

private:
    std::mutex mutex_;
    std::deque<Frame> pending_;
};

Recorder& recorder();
Handoff& handoff();
}
#endif
```

`src/jfg/interp.cpp`:
```cpp
#include "jfg_interp.h"

#include <algorithm>

namespace jfg::interp {
void Recorder::add(uint32_t address, uint32_t id, uint8_t skip) {
    entries_.push_back({address, id, skip});
}

Frame Recorder::take(uint32_t dl_address, uint16_t rate) {
    Frame frame;
    frame.sequence = next_sequence_++;
    frame.dl_address = dl_address;
    frame.rate = rate;
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const RT64::JfgMatrixIdentity& a, const RT64::JfgMatrixIdentity& b) { return a.address < b.address; });
    for (size_t i = 0; i < entries_.size();) {
        size_t j = i + 1;
        bool conflict = false;
        while (j < entries_.size() && entries_[j].address == entries_[i].address) {
            conflict = conflict || (entries_[j].id != entries_[i].id);
            j++;
        }
        if (conflict) {
            frame.conflicts++;
        } else {
            RT64::JfgMatrixIdentity e = entries_[i];
            for (size_t k = i + 1; k < j; k++) e.skip |= entries_[k].skip;
            frame.entries.push_back(e);
        }
        i = j;
    }
    entries_.clear();
    return frame;
}

void Handoff::push(Frame frame) {
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(frame));
    while (pending_.size() > max_pending) pending_.pop_front();
}

std::optional<Frame> Handoff::pop(uint32_t dl_address, uint32_t* discarded) {
    std::lock_guard lock(mutex_);
    if (discarded) *discarded = 0;
    auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Frame& f) { return f.dl_address == dl_address; });
    if (it == pending_.end()) return std::nullopt;
    if (discarded) *discarded = uint32_t(it - pending_.begin());
    Frame frame = std::move(*it);
    pending_.erase(pending_.begin(), it + 1);
    return frame;
}

Recorder& recorder() {
    static Recorder r;
    return r;
}

Handoff& handoff() {
    static Handoff h;
    return h;
}
}
```

- [ ] **Step 5: Run the test to make sure it passes**

Run: `c++ -std=c++20 -Wall -Iinclude -Ilib/rt64/src src/jfg/interp.cpp tests/interp_test.cpp -o build/interp_test && build/interp_test`
Expected: `interp_test: OK`. Then `scripts/test-hooks.sh` ends with `interp_test: OK`.

- [ ] **Step 6: Wire into the build, regenerate the RT64 patch, commit**

Add `${CMAKE_SOURCE_DIR}/src/jfg/interp.cpp` to `SOURCES` in `CMakeLists.txt` after `skeleton.cpp`. Regenerate
the patch with `src/hle/rt64_jfg_frame.h` added to the `git add -N` list. Build (`cmake --build build --target JFGRecompiled -j 20`); expected: success.
```bash
git add lib-patches/rt64/0001-jfg-gbi.patch include/jfg_interp.h src/jfg/interp.cpp tests/interp_test.cpp \
    scripts/test-hooks.sh CMakeLists.txt
git commit -m "Interpolation: identity table, recorder and handoff

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 3: The Frame Interpolation toggle

**Files:**
- Modify: `src/jfg/controls/settings.h`, `src/jfg/controls/settings.cpp`, `tests/controls/settings_test.cpp`,
  `src/ui/ui_config.cpp` (`bind_controls_settings`), `assets/config_menu/general.rml`

**Interfaces:**
- Produces: `jfg::controls::settings().interpolation` (bool, default `false`), saved in `general.json` under
  `"controls"` as `"interpolation"`; read by Task 4.

- [ ] **Step 1: Write the failing test**

In `tests/controls/settings_test.cpp`, after `s.show_stats = true;` add `s.interpolation = true;`; after
`assert(t.extra_players && t.show_stats);` add `assert(t.interpolation);`; after
`assert(!d.show_stats);    // the stats overlay starts hidden` add:
```cpp
    assert(!d.interpolation); // frame interpolation is opt-in until every kind of draw interpolates cleanly
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `scripts/test-hooks.sh`
Expected: compile error, `no member named 'interpolation'`.

- [ ] **Step 3: Implement**

`src/jfg/controls/settings.h`, after the `show_stats` line:
```cpp
    bool interpolation = false;    // frame interpolation (experimental): RT64 draws in-between frames for known matrices
```
`src/jfg/controls/settings.cpp`: append `X(interpolation)` to `JFG_SETTINGS_FIELDS` after `X(show_stats)`.
`src/ui/ui_config.cpp`, in `bind_controls_settings` after the `controls_show_stats` line:
```cpp
        radio("controls_interpolation", &jfg::controls::Settings::interpolation, "On", "Off");
```
`assets/config_menu/general.rml`: add `nav-down: #controls_interpolation_off` to the Stats Overlay "Off" input's style
(`style="nav-up: #controls_extra_players_off; nav-down: #controls_interpolation_off"`) and
`nav-down: #controls_interpolation_on` to its "On" input. Then, after the Stats Overlay `config-option` div, add:
```html
                    <div class="config-option" data-event-mouseover="set_cur_config_index(19)">
                        <label class="config-option__title">Frame Interpolation</label>
                        <div class="config-option__list">
                            <input
                                type="radio"
                                data-event-blur="set_cur_config_index(-1)"
                                data-event-focus="set_cur_config_index(19)"
                                name="controls_interpolation"
                                data-checked="controls_interpolation"
                                value="Off"
                                id="controls_interpolation_off"
                                style="nav-up: #controls_show_stats_off"
                            />
                            <label class="config-option__tab-label" for="controls_interpolation_off">Off</label>

                            <input
                                type="radio"
                                data-event-blur="set_cur_config_index(-1)"
                                data-event-focus="set_cur_config_index(19)"
                                name="controls_interpolation"
                                data-checked="controls_interpolation"
                                value="On"
                                id="controls_interpolation_on"
                                style="nav-up: #controls_show_stats_on"
                            />
                            <label class="config-option__tab-label" for="controls_interpolation_on">On</label>
                        </div>
                    </div>
```
and, after the `cur_config_index == 18` description paragraph:
```html
                    <p data-if="cur_config_index == 19">
                        Draws in-between frames so motion is smooth at your display's refresh rate (set the rate under Graphics → Framerate). <b>Experimental:</b> only some things are smoothed so far; the rest moves at the game's own 30 frames per second.
                    </p>
```

- [ ] **Step 4: Run the tests**

Run: `scripts/test-hooks.sh`
Expected: ends with `interp_test: OK`, and `settings_test: OK` appears.

- [ ] **Step 5: Commit**

```bash
git add src/jfg/controls/settings.h src/jfg/controls/settings.cpp tests/controls/settings_test.cpp src/ui/ui_config.cpp \
    assets/config_menu/general.rml
git commit -m "Interpolation: Frame Interpolation toggle in General (off by default)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 4: Hand each frame's table to RT64

**Files:**
- Create: `include/jfg_interp_hooks.h`, `src/jfg/interp_hooks.cpp`
- Modify: `recomp/jfg.toml.in`, `CMakeLists.txt`, `src/main/rt64_render_context.cpp` (`send_dl`),
  `lib/rt64/src/hle/rt64_state.h`, `lib-patches/rt64/0001-jfg-gbi.patch`

**Interfaces:**
- Consumes: `jfg::interp::recorder()`, `jfg::interp::handoff()`, `Frame` (Task 2); `settings().interpolation` (Task 3).
- Produces: `RT64::State::jfgFrame` (`const JfgFrameTable *`, set for the duration of one `processDisplayLists`), the
  per-task refresh rate, `jfg_interp_submit` (hook at the entry of `func_8004DA10`), and the `JFG_INTERPOLATION=1`
  override (interpolation on regardless of the setting, for scripted runs).

- [ ] **Step 1: Add the state field**

In `lib/rt64/src/hle/rt64_state.h`, add `#include "rt64_jfg_frame.h"` with the other includes, and after the
`FixedRect fullscreen2DRect;` member:
```cpp
        // Jet Force Gemini frame interpolation: the identities of the matrices in the display list being processed,
        // set by the port's renderer context around processDisplayLists (null otherwise).
        const JfgFrameTable *jfgFrame = nullptr;
```

- [ ] **Step 2: Push at submit**

`include/jfg_interp_hooks.h`:
```cpp
#ifndef JFG_INTERP_HOOKS_H
#define JFG_INTERP_HOOKS_H
/* Frame interpolation hooks. Included by generated code after recomp.h. */
#ifdef __cplusplus
extern "C" {
#endif
/* Entry of func_8004DA10 (setupOSTasks): hand the frame recorded since the last submit to the renderer. */
void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx);
#ifdef __cplusplus
}
#endif
#endif
```
`src/jfg/interp_hooks.cpp`:
```cpp
// Frame interpolation, game side: hooks record matrix identities into jfg::interp::recorder() while a frame's
// display list is built; at the next graphics-task submit the frame goes to the renderer (src/main/rt64_render_context.cpp).
#include "recomp.h"
#include "jfg_interp.h"
#include "jfg_interp_hooks.h"

namespace {
constexpr gpr frame_pacing_mode = (gpr)(int32_t)0x800FECAC; // viFrameSync's retraces per frame: 2 = 30 fps, 3 = 20 fps
}

extern "C" void jfg_interp_submit(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t dl = (uint32_t)ctx->r4 & 0x3FFFFFF;
    const int mode = MEM_BU(0, frame_pacing_mode);
    const uint16_t rate = (uint16_t)(60 / (mode > 0 ? mode : 2));
    jfg::interp::handoff().push(jfg::interp::recorder().take(dl, rate));
}
```
In `recomp/jfg.toml.in`, append `\n#include \"jfg_interp_hooks.h\"` to `recomp_include` and add the hook. The probe
hook from Task 1 is on the same function; put both calls in one hook:
```toml
[[patches.hook]]
func = "func_8004DA10"          # setupOSTasks: submits the previous frame's display list
text = "jfg_probe_submit(rdram, ctx); jfg_interp_submit(rdram, ctx);"
```
(replace Task 1's `func_8004DA10` hook with this one). Add `src/jfg/interp_hooks.cpp` to `SOURCES`.

- [ ] **Step 3: Pop in send_dl**

In `src/main/rt64_render_context.cpp`, add `#include "jfg_interp.h"` and `#include "../jfg/controls/settings.h"` (check the
path other frontend files use for `settings.h`: `grep -rn "controls/settings.h" src/ | head -3`), and replace `send_dl`
with:
```cpp
void zelda64::renderer::RT64Context::send_dl(const OSTask* task) {
    check_texture_pack_actions();
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);

    // Frame interpolation: this display list's matrix identities, recorded while the game built it.
    const uint32_t dl = task->t.data_ptr & 0x3FFFFFF;
    uint32_t discarded = 0;
    std::optional<jfg::interp::Frame> frame = jfg::interp::handoff().pop(dl, &discarded);
    if (discarded > 0 || !frame) {
        fprintf(stderr, "[interp] display list %08X: %s (%u older frames discarded)\n", dl,
                frame ? "paired late" : "no recorded frame", discarded);
    }
    // JFG_INTERPOLATION=1 switches it on for scripted runs without touching the user's settings.
    static const bool forced = std::getenv("JFG_INTERPOLATION") != nullptr;
    RT64::JfgFrameTable table;
    table.enabled = forced || jfg::controls::settings().interpolation;
    if (frame) {
        table.entries = frame->entries.data();
        table.count = frame->entries.size();
        app->state->setRefreshRate(frame->rate);
    }
    app->state->jfgFrame = &table;
    app->processDisplayLists(app->core.RDRAM, dl, 0, true);
    app->state->jfgFrame = nullptr;
}
```

- [ ] **Step 4: Build and run**

Regenerate the RT64 patch, then:
```bash
scripts/recompile.sh && source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
export APP_FOLDER_PATH=$PWD/build/appdata-interp DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
scripts/screenshots.sh build/diag/interp-t4 20 40 > /dev/null 2>&1; grep -c "\[interp\]" build/diag/interp-t4/run.log
```
Expected: the title screen renders (`t40.png`), and at most a few `[interp]` lines (the first frames before the hook
runs). If every frame logs "no recorded frame", the push and pop disagree on the address: print both and fix.

- [ ] **Step 5: Commit**

```bash
git add include/jfg_interp_hooks.h src/jfg/interp_hooks.cpp recomp/jfg.toml.in CMakeLists.txt \
    src/main/rt64_render_context.cpp lib-patches/rt64/0001-jfg-gbi.patch
git commit -m "Interpolation: hand each frame's identities and frame rate to RT64

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 5: Tag JFG matrix loads in the renderer

**Files:**
- Modify: `lib/rt64/src/hle/rt64_rsp.cpp`, `lib/rt64/src/hle/rt64_rsp.h`, `lib-patches/rt64/0001-jfg-gbi.patch`,
  `docs/superpowers/handoff-2026-10-14.md` (switch table)

**Interfaces:**
- Consumes: `State::jfgFrame` (Task 4).
- Produces: `RSP::jfgTransformGroup(uint32_t physicalAddress, bool projection) const` and the `JFG_DEBUG_INTERP=1`
  log line `[interp] task <n>: <tagged> tagged, <unknown> unknown matrix loads` once per task, used in Tasks 6–7.

- [ ] **Step 1: Declare**

In `lib/rt64/src/hle/rt64_rsp.h`, next to `void setViewProjDKR(...)`:
```cpp
        // Jet Force Gemini frame interpolation: the transform group for a matrix loaded from physicalAddress, from
        // the frame's identity table (State::jfgFrame). Unknown matrices, or interpolation switched off, get
        // G_EX_ID_IGNORE: drawn as-is, never guessed.
        TransformGroup jfgTransformGroup(uint32_t physicalAddress, bool projection) const;
        void setModelGroupDKR(uint32_t physicalAddress);
        uint32_t jfgTagged = 0;
        uint32_t jfgUnknown = 0;
```

- [ ] **Step 2: Implement the group lookup**

In `lib/rt64/src/hle/rt64_rsp.cpp`, before `RSP::setViewProjDKR`:
```cpp
    TransformGroup RSP::jfgTransformGroup(uint32_t physicalAddress, bool projection) const {
        TransformGroup group;
        const JfgFrameTable *table = state->jfgFrame;
        const JfgMatrixIdentity *entry = ((table != nullptr) && table->enabled) ? table->find(physicalAddress) : nullptr;
        if (entry == nullptr) {
            group.matrixId = G_EX_ID_IGNORE;
            return group;
        }

        auto mode = [entry](uint8_t skipBit) {
            return (entry->skip & skipBit) ? uint8_t(G_EX_COMPONENT_SKIP) : uint8_t(G_EX_COMPONENT_INTERPOLATE);
        };
        group.matrixId = entry->id;
        group.ordering = G_EX_ORDER_LINEAR;
        group.positionInterpolation = mode(JFG_SKIP_POSITION);
        group.rotationInterpolation = mode(JFG_SKIP_ROTATION);
        group.scaleInterpolation = mode(JFG_SKIP_SCALE);
        group.skewInterpolation = mode(JFG_SKIP_SCALE);
        group.perspectiveInterpolation = projection ? mode(JFG_SKIP_SCALE) : uint8_t(G_EX_COMPONENT_AUTO);
        group.vertexInterpolation = G_EX_COMPONENT_SKIP;
        group.tileInterpolation = G_EX_COMPONENT_SKIP;
        return group;
    }

    void RSP::setModelGroupDKR(uint32_t physicalAddress) {
        TransformGroup &group = extended.modelMatrixIdStack[extended.modelMatrixIdStackSize - 1];
        group = jfgTransformGroup(physicalAddress, false);
        extended.modelMatrixIdStackChanged = true;
        (group.matrixId == G_EX_ID_IGNORE) ? jfgUnknown++ : jfgTagged++;
    }
```

- [ ] **Step 3: Use it for cameras and model matrices**

In `RSP::setViewProjDKR`, replace from the comment `// Frame interpolation matches projections...` through the
closing brace of `if (group.matrixId != matrixId) { ... }` with:
```cpp
        // Jet Force Gemini cameras (slot 0) are matched by the identity the game's hooks gave their matrix. Absolute
        // matrices and billboards (interpolate = false) also become projections here; until they're rebuilt as model
        // transforms (interpolation plan 2) they're drawn as-is, so they can't be blended with a camera.
        group = interpolate ? jfgTransformGroup(physicalAddress, true) : TransformGroup();
        if (!interpolate) {
            group.matrixId = G_EX_ID_IGNORE;
        }
        extended.viewProjMatrixIdStackChanged = true;
```
In `RSP::applyMatrixSlotDKR`, in the `else` branch after `modelMatrixStack[modelMatrixStackSize - 1] = dkr.slots[slot];`
add `setModelGroupDKR(dkr.slotAddress[slot]);`, and in the absolute branch, after setting the identity model, add
```cpp
            TransformGroup &modelGroup = extended.modelMatrixIdStack[extended.modelMatrixIdStackSize - 1];
            modelGroup = TransformGroup();
            modelGroup.matrixId = G_EX_ID_IGNORE;
            extended.modelMatrixIdStackChanged = true;
```
so the identity model under an absolute projection isn't paired by the heuristic either.

In the billboard path (`setVertexDKR`, the `if (billboard)` block that calls `setViewProjDKR(shifted, ..., false)`),
after `modelMatrixStack[modelMatrixStackSize - 1] = hlslpp::float4x4::identity();` add the same four lines as above.

- [ ] **Step 4: Log per task (JFG_DEBUG_INTERP)**

In `RSP::reset()`, before the `jfgTask++;` added in Task 1 (so it reports the task that just finished), add:
```cpp
        static const bool interpDebug = std::getenv("JFG_DEBUG_INTERP") != nullptr;
        if (interpDebug && (jfgTagged + jfgUnknown) > 0) {
            fprintf(stderr, "[interp] task %lld: %u tagged, %u unknown matrix loads\n", (long long)jfgTask, jfgTagged, jfgUnknown);
        }
        jfgTagged = 0;
        jfgUnknown = 0;
```
(Add `#include <cstdlib>` and `#include <cstdio>` at the top of `rt64_rsp.cpp` if missing.)

- [ ] **Step 5: Build, regenerate the patch, and check the toggle-off behaviour**

```bash
source tools/toolchain.env && cmake --build build --target JFGRecompiled -j 20
export APP_FOLDER_PATH=$PWD/build/appdata-interp DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
rm -f build/appdata-interp/saves/jfg.n64.us.bin*
JFG_INPUT_SCRIPT="$GOLDWOOD" tools/diag/record.sh build/diag/interp-t5-off.mkv 250 45
```
(`GOLDWOOD` as in Task 1 Step 6.) The toggle defaults to off. Expected: `flicker spikes 0`, and every distinct picture
lasts at least two 60 fps frames: no in-between frames. Count with:
```bash
python3 - build/diag/interp-t5-off.mkv <<'PY'
import subprocess, sys
import numpy as np
raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", sys.argv[1], "-vf", "scale=160:100,format=gray", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
f = np.frombuffer(raw, np.uint8).reshape(-1, 100, 160).astype(np.float32)
runs, n = [], 1
for i in range(1, len(f)):
    if np.abs(f[i] - f[i - 1]).mean() < 0.05: n += 1
    else: runs.append(n); n = 1
print(f"{len(f)} frames, {len(runs)} distinct pictures, single-frame pictures {sum(1 for r in runs if r == 1)}")
PY
```
Expected: "single-frame pictures" is (near) 0 with the toggle off. Save this snippet as `tools/diag/frame-runs.py` (it's
used again in Tasks 6 and 7) and add it to the commit.

- [ ] **Step 6: Commit**

Add `JFG_DEBUG_INTERP=1` and `JFG_DEBUG_MTXSRC=<frame>` rows to the development switches table in
`docs/superpowers/handoff-2026-10-14.md`:
```markdown
| `JFG_DEBUG_INTERP=1` | Once per graphics task: how many JFG matrix loads were tagged for frame interpolation and how many were unknown (drawn as-is). |
| `JFG_DEBUG_MTXSRC=<frame>` | Logs which code converted each matrix of that frame and every matrix load of that graphics task; `tools/diag/mtx-provenance.py` joins them. |
```
```bash
git add lib-patches/rt64/0001-jfg-gbi.patch tools/diag/frame-runs.py docs/superpowers/handoff-2026-10-14.md
git commit -m "Interpolation: tag JFG matrix loads from the identity table; unknown ones are drawn as-is

With interpolation off, no JFG matrix is paired by RT64's heuristic any more.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 6: Camera identities and cut detection (phase 2)

Known before starting (research on 2026-10-16):
- `camSetProjMtx` (0x80040AA4) builds every world camera. It's called by `camSetView` (0x8004046C) once per viewport
  from `trackDraw`, again from `partDraw` (the particle pass) and from `frontPlayerTarget` (the 3D aiming sight). It
  inverts the current camera's pose, multiplies by the projection, converts the result to fixed point at `*mtx`, emits
  the slot-0 load and advances `mtx`.
  - Arguments: `$a0` is the display-list pointer (`Gfx **`, stored to `$s0`) and `$a1` the matrix-pool pointer
    (saved at `sp+0x2C`).
  - It reads the camera number from 0x800FA634 (0–3).
  - Camera structs: 0x800FA4D0 + 0x4C × number, holding s16 yaw, pitch and roll at +0/+2/+4, the position (three
    floats) at +0xC, and the FOV at +0x2C.
- The HUD's slot-0 loads (`camStandardOrtho`, HUD module m014) are fixed orthographic matrices: they stay unknown
  (drawn as-is).
- Cut signals:
  - `camInit` (0x8003F640) runs at every level load.
  - `animcamera[n]` (0x801045B8 + 4n) is the object driving a cutscene camera; a change of object is a new shot.
  - The static camera index at 0x800FB084 changes when a static camera starts or stops.
  - Anything else (in-level cut cameras, spline cameras, teleports) is caught by the jump test.

**Files:**
- Modify: `include/jfg_interp.h`, `src/jfg/interp.cpp`, `tests/interp_test.cpp`, `include/jfg_interp_hooks.h`,
  `src/jfg/interp_hooks.cpp`, `recomp/jfg.toml.in`

**Interfaces:**
- Consumes: `Recorder::add`, `camera_id` (Task 2); `jfg_interp_submit` (Task 4).
- Produces: `struct CameraPose { float position[3]; int16_t yaw, pitch, roll; uint32_t driver; }`;
  `class CutDetector { bool cut(uint32_t id, const CameraPose& pose, uint64_t frame); void reset(); }` and
  `CutDetector& cut_detector()`; hooks `jfg_interp_camera` (entry of `func_80040AA4`) and `jfg_interp_level_start`
  (entry of `func_8003F640`).

- [ ] **Step 1: Confirm the arguments**

Run: `tools/romtool/.venv/bin/python -m romtool.cli disasm jfg.z64 --address 0x80040AA4 --count 200 | grep -n "0x2C(\$sp)"`
Expected: after the conversion, a `lw` from `0x2C($sp)` is used as the base of a `lw`/`sw` pair that reads the matrix
pointer and stores it back advanced by 0x40. That confirms `$a1` is `Mtx **`. If it isn't, find the register or
global that holds the matrix pointer before using it in Step 6.

- [ ] **Step 2: Write the failing test**

Append to `tests/interp_test.cpp`, before `std::puts`:
```cpp
    // Camera cuts. Positions in world units; angles are binary (0x10000 = 360 degrees).
    using jfg::interp::CameraPose;
    jfg::interp::CutDetector cuts;
    const uint32_t cam = jfg::interp::camera_id(0, 0);
    auto pose = [](float x, int16_t yaw, uint32_t driver = 0) { return CameraPose{{x, 100.f, 0.f}, yaw, 0, 0, driver}; };
    assert(cuts.cut(cam, pose(0, 0), 10));        // first sighting: nothing to come from
    assert(!cuts.cut(cam, pose(10, 0), 11));      // steady motion
    assert(!cuts.cut(cam, pose(10, 0), 11));      // the same camera again in the same frame (particle pass): same answer
    assert(!cuts.cut(cam, pose(20, 0), 12));
    assert(!cuts.cut(cam, pose(30, 0x0300), 13)); // turning about 4 degrees a frame
    assert(cuts.cut(cam, pose(900, 0x0300), 14)); // a jump far beyond its pace
    assert(!cuts.cut(cam, pose(905, 0x0300), 15));
    assert(cuts.cut(cam, pose(910, 0x2000), 16)); // turned 45 degrees in one frame
    assert(!cuts.cut(cam, pose(915, 0x2000), 17));
    assert(cuts.cut(cam, pose(920, 0x2000, 0x80123450), 18)); // another object now drives the camera (new shot)
    assert(!cuts.cut(cam, pose(925, 0x2000, 0x80123450), 19));
    assert(cuts.cut(cam, pose(930, 0x2000, 0x80123450), 21)); // missed a frame: nothing recent to come from
    // Angles wrap: 0x7FF0 to -0x7FF0 is a 0x20 turn, not almost a full circle.
    assert(!cuts.cut(cam, pose(935, int16_t(0x7FF0), 0x80123450), 22));
    assert(!cuts.cut(cam, pose(940, int16_t(-0x7FF0), 0x80123450), 23));
    // A fast but steady camera (the pod's flight) isn't cut once it's moving; the first big step is a jump.
    assert(cuts.cut(cam, pose(1000, int16_t(-0x7FF0), 0x80123450), 24));
    for (int i = 1; i < 5; i++) assert(!cuts.cut(cam, pose(1000.f + 150.f * i, int16_t(-0x7FF0), 0x80123450), 24 + i));
    // Each camera keeps its own history; a level load forgets them all.
    assert(cuts.cut(jfg::interp::camera_id(0, 1), pose(0, 0), 30));
    cuts.reset();
    assert(cuts.cut(cam, pose(1610, int16_t(-0x7FF0), 0x80123450), 29));
```

- [ ] **Step 3: Run it to make sure it fails**

Run: `scripts/test-hooks.sh`
Expected: compile error, `CameraPose` is not a member of `jfg::interp`.

- [ ] **Step 4: Implement**

In `include/jfg_interp.h`, add `#include <unordered_map>` and, before `Recorder& recorder();`:
```cpp
// A camera's pose, from the game's camera struct, and what drives it (a cutscene camera object, a static camera;
// any change of driver is a new shot).
struct CameraPose {
    float position[3];
    int16_t yaw, pitch, roll; // binary angles, 0x10000 = 360 degrees
    uint32_t driver;
};

// Decides when a camera's motion into a frame must not be interpolated: it's new or wasn't seen last frame, it
// jumped far beyond its recent pace, it turned more than 30 degrees, or another driver took over.
class CutDetector {
public:
    static constexpr float min_jump = 64.f; // world units
    static constexpr int max_turn = 0x1555; // 30 degrees
    // Called for every camera load; a camera loaded twice in one frame gets the first answer.
    bool cut(uint32_t id, const CameraPose& pose, uint64_t frame);
    void reset();

private:
    struct Last {
        CameraPose pose;
        float step = 0.f;
        uint64_t frame = 0;
        bool cut = true;
    };
    std::unordered_map<uint32_t, Last> last_;
};

CutDetector& cut_detector();
```
In `src/jfg/interp.cpp`, add `#include <cmath>` and `#include <cstdlib>`, and:
```cpp
bool CutDetector::cut(uint32_t id, const CameraPose& pose, uint64_t frame) {
    auto it = last_.find(id);
    if (it != last_.end() && it->second.frame == frame) return it->second.cut;
    Last next{pose, 0.f, frame, true};
    if (it != last_.end() && it->second.frame + 1 == frame) {
        const Last& prev = it->second;
        const float dx = pose.position[0] - prev.pose.position[0], dy = pose.position[1] - prev.pose.position[1],
                    dz = pose.position[2] - prev.pose.position[2];
        next.step = std::sqrt(dx * dx + dy * dy + dz * dz);
        auto turn = [](int16_t a, int16_t b) { return std::abs(int(int16_t(uint16_t(a) - uint16_t(b)))); };
        const bool jumped = next.step > min_jump && next.step > 4.f * prev.step + 16.f;
        const bool turned = turn(pose.yaw, prev.pose.yaw) > max_turn || turn(pose.pitch, prev.pose.pitch) > max_turn ||
                            turn(pose.roll, prev.pose.roll) > max_turn;
        next.cut = jumped || turned || pose.driver != prev.pose.driver;
    }
    last_[id] = next;
    return next.cut;
}

void CutDetector::reset() {
    last_.clear();
}

CutDetector& cut_detector() {
    static CutDetector d;
    return d;
}
```

- [ ] **Step 5: Run the test**

Run: `scripts/test-hooks.sh`
Expected: ends with `interp_test: OK`.

- [ ] **Step 6: Hook the cameras**

In `include/jfg_interp_hooks.h`, add:
```c
/* Entry of camSetProjMtx (0x80040AA4): the current camera's view x projection is about to be written at *$a1 and
   loaded into slot 0. */
void jfg_interp_camera(uint8_t* rdram, recomp_context* ctx);
/* Entry of camInit (0x8003F640): a level is loading; every camera starts afresh. */
void jfg_interp_level_start(uint8_t* rdram, recomp_context* ctx);
```
In `src/jfg/interp_hooks.cpp`, add `#include <cstdio>`, `#include <cstdlib>`, `#include <cstring>`, and in the anonymous
namespace:
```cpp
constexpr gpr camera_number = (gpr)(int32_t)0x800FA634; // camGetNo
constexpr gpr cameras = (gpr)(int32_t)0x800FA4D0;       // 4 camera structs, 0x4C bytes each
constexpr gpr anim_cameras = (gpr)(int32_t)0x801045B8;  // animcamera[4]: the object driving a cutscene camera
constexpr gpr static_camera = (gpr)(int32_t)0x800FB084;
uint64_t frames_submitted = 0;
const bool interp_debug = std::getenv("JFG_DEBUG_INTERP") != nullptr;

float read_f32(uint8_t* rdram, gpr address) {
    const uint32_t bits = (uint32_t)MEM_W(0, address);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}
```
In `jfg_interp_submit`, after the `push`, add `frames_submitted++;`. Then:
```cpp
extern "C" void jfg_interp_camera(uint8_t* rdram, recomp_context* ctx) {
    const int32_t number = MEM_W(0, camera_number);
    if (number < 0 || number > 3) return;
    const gpr c = cameras + 0x4C * number;
    jfg::interp::CameraPose pose{};
    for (int i = 0; i < 3; i++) pose.position[i] = read_f32(rdram, c + 0xC + 4 * i);
    pose.yaw = MEM_H(0, c);
    pose.pitch = MEM_H(2, c);
    pose.roll = MEM_H(4, c);
    pose.driver = (uint32_t)MEM_W(4 * number, anim_cameras) ^ ((uint32_t)MEM_W(0, static_camera) << 1);
    const uint32_t id = jfg::interp::camera_id(0, number);
    const bool cut = jfg::interp::cut_detector().cut(id, pose, frames_submitted);
    const uint32_t matrix = (uint32_t)MEM_W(0, ctx->r5) & 0x3FFFFFF;
    jfg::interp::recorder().add(matrix, id, cut ? RT64::JFG_SKIP_ALL : 0);
    if (interp_debug && cut) {
        std::fprintf(stderr, "[interp] frame %llu: camera %d cut (at %.0f %.0f %.0f, yaw %04X)\n",
                     (unsigned long long)frames_submitted, number, pose.position[0], pose.position[1], pose.position[2],
                     (uint16_t)pose.yaw);
    }
}

extern "C" void jfg_interp_level_start(uint8_t* rdram, recomp_context* ctx) {
    jfg::interp::cut_detector().reset();
}
```
In `recomp/jfg.toml.in`, add:
```toml
# Frame interpolation: every world camera's matrix gets the camera's identity (src/jfg/interp_hooks.cpp).
[[patches.hook]]
func = "func_80040AA4"          # camSetProjMtx
text = "jfg_interp_camera(rdram, ctx);"

[[patches.hook]]
func = "func_8003F640"          # camInit (level load)
text = "jfg_interp_level_start(rdram, ctx);"
```

- [ ] **Step 7: Check the poses and the tagging in a run**

Build (`scripts/recompile.sh`, then the build command), and run the Goldwood script with logging. Set the
variables first; never run this step with `A` unset.
```bash
A=build/appdata-interp
export APP_FOLDER_PATH=$PWD/$A DISPLAY=:99 SDL_AUDIODRIVER=dummy; unset WAYLAND_DISPLAY
rm -f "${A:?}"/saves/jfg.n64.us.bin*
JFG_INPUT_CLOCK=frames:30 JFG_INPUT_SCRIPT="$GOLDWOOD" JFG_INTERPOLATION=1 JFG_DEBUG_INTERP=1 \
  scripts/screenshots.sh build/diag/interp-t6 330 > /dev/null 2>&1
grep "\[interp\]" build/diag/interp-t6/run.log | grep "cut" | head -20
grep "\[interp\] task" build/diag/interp-t6/run.log | tail -3
```
Expected:
- The cuts fall at scene changes: a handful during the intro, the teleport, the ship, the pod's flight and the
  landing. Their positions look like world coordinates (tens to thousands, not 1e-38 or 1e30).
- The per-task lines show tagged matrix loads (the cameras) during gameplay, with everything else unknown.
- Cuts logged every frame would mean the pose is read wrong: check the struct offsets against a dump
  (`JFG_DEBUG_RDRAM`).

- [ ] **Step 8: Commit**

```bash
git add include/jfg_interp.h src/jfg/interp.cpp tests/interp_test.cpp include/jfg_interp_hooks.h \
    src/jfg/interp_hooks.cpp recomp/jfg.toml.in
git commit -m "Interpolation: camera identities and cut detection

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 7: Verify the cameras

**Files:**
- Modify: `docs/superpowers/handoff-2026-10-14.md` (frame interpolation status), this plan (results)

- [ ] **Step 1: Record with interpolation on and off**

Set a 60 Hz target in the test folder's `graphics.json` (`"rr_option": "Manual", "rr_manual_value": 60`; run the game
once first if the file doesn't exist yet), so the recording catches every in-between frame. Then, with the variables
set as in Task 6 Step 7, run once with `JFG_INTERPOLATION=1` and once without, deleting the test save before each:
```bash
JFG_INPUT_CLOCK=frames:30 JFG_INPUT_SCRIPT="$GOLDWOOD" JFG_INTERPOLATION=1 tools/diag/record.sh build/diag/interp-landing-on.mkv 250 45
python3 tools/diag/frame-runs.py build/diag/interp-landing-on.mkv
JFG_INPUT_CLOCK=frames:30 JFG_INPUT_SCRIPT="$GOLDWOOD" tools/diag/record.sh build/diag/interp-landing-off.mkv 250 45
python3 tools/diag/frame-runs.py build/diag/interp-landing-off.mkv
```
Expected:
- On: `flicker spikes 0`, and many single-frame pictures (the in-betweens).
- Off: `flicker spikes 0` and almost no single-frame pictures.

- [ ] **Step 2: Look at the frames**

Extract the frames around three moments: a camera pan in gameplay, the pod's flight, and each logged cut. For example:
```bash
ffmpeg -loglevel error -y -i build/diag/interp-landing-on.mkv -vf "select='between(n\,1700\,1712)',scale=640:-1,tile=4x3" -frames:v 1 build/diag/interp-on-sheet.png
```
Check by eye:
- the world moves smoothly between game frames;
- at each cut, no frame shows a blend of two shots;
- the sky and absolute draws step at 30 fps (expected until plan 2) without flicker.

Do the same for Goldwood gameplay with the right stick turning the camera (add `310+3 - 0 0 1 0` to the script).

- [ ] **Step 3: Record the results and the status**

Add a `## Task 7 results` section to this plan: the frame-runs numbers for on and off, any frames that looked wrong
and what they were, and the cut log summary. In `docs/superpowers/handoff-2026-10-14.md`, under the player tools
section, add a Frame Interpolation entry. It says the toggle is in General, off by default; with it on, only the cameras
interpolate so far, while the sky, absolute draws, characters and sprites still move at the game's rate (plan 2); and it
links the spec and this plan.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/handoff-2026-10-14.md docs/superpowers/plans/2026-10-16-jfg-frame-interpolation.md
git commit -m "Interpolation: camera results; status in the handoff notes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

### Task 8: Write plan 2 (phases 3–6)

**Files:**
- Create: `docs/superpowers/plans/<date>-jfg-frame-interpolation-2.md`

- [ ] **Step 1: Write it from the Phase 0 findings**

Use the superpowers:writing-plans skill, with the spec (sections 3–6) and the `## Phase 0 findings` table as input.
One task per kind of draw, in the spec's order:
- characters (joint IDs from object serials, recorded at the `modGenAnimMatrices` hook shared with Big Heads,
  `src/jfg/big_head.cpp`) and rigid objects;
- the sky and absolute draws (factor the camera out: the absolute matrix is model × view-projection 0x800FAAF0, so
  the model is absolute × inverse(view-projection), drawn under the camera's projection with an ID);
- sprites, particles and shots (billboards rebuilt as ID'd model transforms, pixel-identical at rest);
- split screen, the HUD and performance;
- finally, the toggle on by default.

Each task gets its own tests and a recording check like Task 7's.

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/plans/
git commit -m "Interpolation: plan 2 (characters, objects, sky, sprites, split screen)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push github main
```

---

## Phase 0 findings (2026-10-16)

Measured with `JFG_DEBUG_MTXSRC` (Task 1) on two frames, with every matrix load explained:
- **The pod's flight**, a cutscene in level 93, frame 18740 of the Goldwood script: task 18739, 64 loads.
- **Goldwood gameplay** after a warp (`scripts/survey-level.sh 92`), frame 2601: task 2599, 155 loads.

**How the matrices are made:**
- `mathMtxF2L` (0x80048D84) converts a float matrix into the display list's fixed point. It makes cameras and
  absolute draws.
- `camConvertMatrixList` (0x80042470) queues float matrices (`$a0` start, `$a1` count). `camTick` (0x800424A0) converts
  them in place with `conv_mult_matrix` (0x80074E50). All relative (object and joint) matrices are made this way, so a
  character's joint buffer (`modGenAnimMatrices` output, instance +0x10/+0x14) is the very address the display list
  loads.
- **Two graphics tasks per gameplay frame.** The main display list alternates between 0x1FB0C0 and 0x220FE0 (about
  155 loads). A small one at 0x327A10 (about 27 loads: one camera and object matrices) is submitted by a module
  between two main submits, and the main frame's matrices are made before it. Cutscenes submit only the main one.
  Hence the Task 4 ruling: the recorded frame is taken only at main display-list submits.

| Kind | Writer | Loads (gameplay / flight) | Identity from | Hook point |
|---|---|---|---|---|
| World camera, per viewport, including the particle-pass and aiming-sight reloads | `camSetProjMtx` (0x80040AA4) through the track's matrix-push helper `func_8001448C` and `trackDraw` | 4 / 3 | camera number (0x800FA634) | `camSetProjMtx` entry (Task 6) |
| HUD cameras (orthographic) | `frontInstruments` (HUD module) | 4 / 0 | not needed (fixed) | none: drawn as-is |
| Absolute draws: track pieces, the sky, objects pre-multiplied by the camera | `func_8001448C`, called by `trackDraw` (0x800121C0) through `func_80013820`; also code after `trackUpdateLighting` and `trackDraw` itself | 67 / 5 | track block or object; the camera factored out (absolute = model × view-projection 0x800FAAF0) | plan 2: `func_8001448C` and its caller's object |
| Custom objects (absolute) | `objPrintCustomObject` | 0 / 16 | object → serial | plan 2: `objPrintCustomObject` |
| HUD elements (absolute) | `frontInstruments` | 5 / 0 | not needed | none: drawn as-is |
| Character joints and animated objects (relative) | the `camConvertMatrixList` queue: player and object update in gameplay, `animseqUpdate` in cutscenes | 74 / 38 | object serial + joint index (the queued buffer is the instance's joint buffer) | plan 2: the `modGenAnimMatrices` hook shared with Big Heads |
| Billboards (sprites, particles, shots) | no matrix of their own: vertices after vertex 0 are offset from it under the current slot | 10 sections per frame | particle slot or emitter | plan 2: `partDraw` (0x80063730) and the billboard path in the RT64 patch |
| The small module display list (0x327A10) | one camera and about 25 object matrices | gameplay only | unknown purpose | none: drawn as-is |

**Open for plan 2:**
- What the small module display list draws.
- Which object each `func_8001448C` draw belongs to: log its caller's object pointer.
- `controlPlayer+0x650` is only the nearest named symbol to the queueing call's return address. The real queueing
  site is probably an unnamed function after it, so find the call to `camConvertMatrixList` on the model-draw path
  before relying on the name.

## Task 7 results (2026-10-16)

On the virtual display (`DISPLAY=:99`, test folder `build/appdata-interp`, Framerate Manual 60):

- **Pairing works.** In Goldwood gameplay the four world-camera loads per frame are tagged (the HUD's fixed camera
  stays unknown), and RT64 pairs the camera with its previous frame by ID (`JFG_DEBUG_INTERP`: `projection p=0: cur id
  00000100 prev id 00000100 mapped 1`). RT64 decides to generate in-between frames: game rate 30, target 60, not
  paused, target framebuffer found.
- **No flicker.** The ship-landing recording with interpolation on has 0 flicker spikes over 45 s. Frames around its
  cuts and at the landing pad show no blend of two shots and no stray geometry. Cuts are logged at scene changes (about
  one every few seconds in the intro and landing), at world positions.
- **Smoothness can't be judged here.** The plan's frame-count checks (single-frame pictures, and a camera-pan
  shift test tried as a replacement) come out the same with interpolation on, off, or with Framerate Original. The
  virtual display, with vsync off, doesn't pace presents to a refresh, so captured frames don't show RT64's in-between
  frames reliably. This machine's real screen was blanked, which paces a hidden window at about 1 Hz. Ruling: the
  remaining check is by eye on real hardware.

Rulings made on the way are in the ledger (`.superpowers/sdd/2026-10-16-jfg-frame-interpolation/progress.md`) and
the commit messages. They cover the second display list, the main-only take, the slot-0 identity model, test
arithmetic and the frame-count checks.

