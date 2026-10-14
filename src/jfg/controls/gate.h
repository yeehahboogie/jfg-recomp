#pragma once
#include <cstdint>
#include <atomic>
namespace jfg::controls {
// What the gate needs to know about the player being updated.
struct PlayerView {
    int index;          // pd+0x000
    int character;      // pd+0x001 & 3 (3 = hovership camera case)
    int state;          // pd+0x568
    bool input_disabled;
};
// Modern controls apply only to player 1 on foot, while the game accepts input.
bool modern_applies(const PlayerView& p, bool scheme_modern);

// Per-frame state shared by the hooks (player 0 only).
struct FrameState {
    bool active = false;   // modern_applies() for player 0 this frame
    uint32_t pd = 0;       // player data address (KSEG0)
    uint32_t obj = 0;      // object address
    int camera_kind = 0;   // 0 unknown, 1 follow, 2 aim, 3 other
};
extern FrameState g_frame;

// Milliseconds (steady clock) when Modern last applied to player 1; the frontend captures the mouse while it is
// recent. Written by the game thread, read by the input thread.
void mark_modern_active();
bool modern_recently_active(int grace_ms);
}
