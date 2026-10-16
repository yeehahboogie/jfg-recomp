// Big head mode (Cheats tab). modGenAnimMatrices (0x8003D21C) generates a model's joint matrices into one of two
// output buffers (instance +0x10/+0x14, chosen by the byte at instance +0xB): row-major 4x4 float matrices, one per
// joint, which the game converts to the fixed-point matrices the display list loads. Joint vertices are local to their
// joint, so scaling a joint's matrix in its own space grows that part around its pivot.
//
// The model (a2's model, $s3) starts with its asset name ("Boy", "bryanimgirl"); its skeleton (+0x54, joint count at
// +0x4F) is 16-byte joints: u8 parent (0xFF = root), 3 bytes, then the bind-pose offset from the parent as floats x,
// y, z. jfg::skeleton finds the head from it; the name is only logged.
//
// JFG_DEBUG_BIGHEAD=1 logs the head chosen for each model, once.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "recomp.h"
#include "jfg_big_head.h"
#include "jfg_player_tools.h"
#include "jfg_skeleton.h"

namespace {
constexpr float head_scale = 2.0f;
constexpr int min_joints = 8; // characters; leaves animated scenery (doors, lifts) alone
constexpr int max_joints = 64;

float read_float(uint8_t* rdram, gpr address) {
    const uint32_t bits = (uint32_t)MEM_W(0, address);
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

void write_float(uint8_t* rdram, gpr address, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    MEM_W(0, address) = (int32_t)bits;
}

// The model's asset name: up to 16 printable characters at model +0x00, or "" when it has none.
std::string model_name(uint8_t* rdram, gpr model) {
    std::string name;
    for (int i = 0; i < 16; i++) {
        const char c = (char)MEM_BU(i, model);
        if (c == 0) {
            return name;
        }
        if (c < 0x20 || c > 0x7E) {
            return "";
        }
        name += c;
    }
    return "";
}

void log_once(uint8_t* rdram, gpr model, int count, int head, const std::vector<bool>& members) {
    static const bool enabled = std::getenv("JFG_DEBUG_BIGHEAD") != nullptr;
    static std::mutex mutex;
    static std::set<uint32_t> seen;
    if (!enabled) {
        return;
    }
    std::lock_guard lock(mutex);
    if (!seen.insert((uint32_t)model).second) {
        return;
    }
    int below = -1;
    for (bool m : members) below += m;
    std::fprintf(stderr, "[bighead] model %08X '%s': %d joints, head joint %d (+%d below it)\n", (uint32_t)model,
                 model_name(rdram, model).c_str(), count, head, below);
}
}

extern "C" void jfg_anim_matrices_done(uint8_t* rdram, recomp_context* ctx) {
    if (!jfg::tools::cheats().big_head) {
        return;
    }

    const gpr instance = ctx->r20; // $s4
    const gpr model = ctx->r19;    // $s3
    const int count = MEM_BU(0x4F, model);
    const gpr table = MEM_W(0x54, model);
    if (count < min_joints || count > max_joints || table == 0) {
        return;
    }

    std::vector<jfg::skeleton::Joint> joints(count);
    for (int j = 0; j < count; j++) {
        const gpr entry = table + 16 * j;
        const int parent = MEM_BU(0, entry);
        joints[j] = {parent == 0xFF ? -1 : parent, read_float(rdram, entry + 4), read_float(rdram, entry + 8)};
    }
    const int head = jfg::skeleton::find_head(joints);
    if (head < 0) {
        return;
    }
    const std::vector<bool> members = jfg::skeleton::subtree(joints, head);
    log_once(rdram, model, count, head, members);

    const gpr buffer = MEM_W(0x10 + 4 * MEM_B(0xB, instance), instance);
    const gpr head_mtx = buffer + 0x40 * head;
    const float pivot[3] = {read_float(rdram, head_mtx + 48), read_float(rdram, head_mtx + 52),
                            read_float(rdram, head_mtx + 56)};
    for (int j = 0; j < count; j++) {
        if (!members[j]) {
            continue;
        }
        float m[16];
        for (int i = 0; i < 16; i++) m[i] = read_float(rdram, buffer + 0x40 * j + 4 * i);
        jfg::skeleton::scale_about(m, head_scale, pivot);
        for (int i = 0; i < 16; i++) write_float(rdram, buffer + 0x40 * j + 4 * i, m[i]);
    }
}
