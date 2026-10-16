#include <cassert>
#include <cstdio>
#include "jfg_tools.h"

int main() {
    // Mailbox: one pending request from the menu to the game thread; a newer request replaces an untaken one.
    jfg::Mailbox box;
    assert(!box.take());
    box.post(5);
    assert(box.take() == 5 && !box.take());
    box.post(1);
    box.post(2);
    assert(box.take() == 2 && !box.take());

    // Rate meter: sampled from a running count (frames counted on the game or render thread). A new rate is computed
    // once at least a quarter second has passed; between, the previous rate stands.
    jfg::RateMeter meter;
    assert(meter.sample(0, 0.0) == 0);       // first sample: no rate yet
    assert(meter.sample(3, 0.1) == 0);       // too soon
    assert(meter.sample(15, 0.5) == 30);     // 15 frames in 0.5 s
    assert(meter.sample(16, 0.6) == 30);     // too soon: unchanged
    assert(meter.sample(45, 1.5) == 30);     // 30 frames in 1.0 s
    assert(meter.sample(45, 2.5) == 0);      // stalled

    // Overlay lines.
    jfg::OverlayData d{};
    d.game_fps = 30; d.render_fps = 60; d.level = 92; d.level_name = "Forest Day Landing";
    d.have_position = true; d.x = 715.4f; d.y = 81.0f; d.z = -349.2f; d.audio_ms = 32.4;
    auto lines = jfg::format_overlay(d);
    assert(lines.size() == 5);
    assert(lines[0] == "Game 30 fps");
    assert(lines[1] == "Render 60 fps  16.7 ms");
    assert(lines[2] == "Level 92  Forest Day Landing");
    assert(lines[3] == "Position 715, 81, -349");
    assert(lines[4] == "Audio 32 ms");
    // Not in gameplay yet: no level or position; no frames: no frame time.
    jfg::OverlayData e{};
    e.level = -1; e.render_fps = 0;
    lines = jfg::format_overlay(e);
    assert(lines[1] == "Render 0 fps" && lines[2] == "Level -" && lines[3] == "Position -");

    // Exit labels for Skip Ahead.
    assert(jfg::exit_label(0, 340.4f) == "Exit 1 (340 away)");
    assert(jfg::exit_label(2, 0.0f) == "Exit 3 (0 away)");
    std::puts("tools_test: OK");
}
