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

// A character joint: the model instance's serial and the joint index (joints < 64).
constexpr uint32_t joint_id(uint32_t instance_serial, int joint) {
    return 0x01000000u + 0x40u * instance_serial + uint32_t(joint);
}

// An absolute world draw: the owning object's serial and the push number within its draw (pushes < 16).
constexpr uint32_t object_push_id(uint32_t object_serial, uint32_t push) {
    return 0x02000000u + 0x10u * object_serial + push;
}

// The objects objPrintObject is drawing, innermost last; absolute pushes belong to the innermost.
class OwnerStack {
public:
    static constexpr uint32_t no_push = 0xFFFFFFFFu;
    void push(uint32_t object) { stack_.push_back({object, 0}); }
    void pop() {
        if (!stack_.empty()) stack_.pop_back();
    }
    bool empty() const { return stack_.empty(); }
    uint32_t owner() const { return stack_.back().object; }
    // The next push number for the innermost owner, or no_push beyond 15.
    uint32_t next_push() {
        uint32_t& n = stack_.back().pushes;
        return n < 16 ? n++ : no_push;
    }

private:
    struct Owner {
        uint32_t object;
        uint32_t pushes;
    };
    std::vector<Owner> stack_;
};

OwnerStack& owners();

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
    void add(uint32_t address, uint32_t id, uint8_t skip = 0, uint8_t flags = 0);
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
Recorder& recorder();
Handoff& handoff();
}
#endif
