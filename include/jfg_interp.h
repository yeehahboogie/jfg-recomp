#ifndef JFG_INTERP_H
#define JFG_INTERP_H
// Frame interpolation: per-frame matrix identities from the game's hooks, handed to RT64 with the frame's graphics
// task (docs/superpowers/specs/2026-10-16-jfg-frame-interpolation-design.md).
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
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
    // An empty frame for a display list the recording isn't for (gameplay's small module display list, submitted
    // between two main ones while the next main frame is being built); what's recorded is kept for one such submit,
    // and dropped at a second one in a row (no main display list is coming for it).
    Frame empty(uint32_t dl_address, uint16_t rate);

private:
    std::vector<RT64::JfgMatrixIdentity> entries_;
    uint64_t next_sequence_ = 0;
    bool after_empty_ = false;
};

// Frames waiting for the renderer, in submission order. Pushed on the game thread, popped on the gfx thread.
class Handoff {
public:
    static constexpr size_t max_pending = 8;
    // Returns how many old frames were dropped to stay within max_pending.
    size_t push(Frame frame);
    // The oldest pending frame for this display list. Frames queued before it are discarded and counted; if none
    // matches, nothing is removed.
    std::optional<Frame> pop(uint32_t dl_address, uint32_t* discarded = nullptr);

private:
    std::mutex mutex_;
    std::deque<Frame> pending_;
};

// A camera's pose, from the game's camera struct, and what drives it (a cutscene camera object, a static camera;
// any change of driver is a new shot).
struct CameraPose {
    float position[3];
    int16_t yaw, pitch, roll; // binary angles, 0x10000 = 360 degrees
    uint32_t driver;
};

// Decides when a camera's motion into a frame must not be interpolated: it's new or wasn't seen last frame, it
// jumped far beyond its recent pace, another driver took over, or a driven camera (cutscene, static) turned more
// than 30 degrees. The player's own camera turns as fast as input allows.
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
Recorder& recorder();
Handoff& handoff();
}
#endif
