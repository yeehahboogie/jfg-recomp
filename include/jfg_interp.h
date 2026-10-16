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
// Cameras, by role (world, sky, 2D, ... as the matrix provenance survey found them) and viewport (0-3).
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
