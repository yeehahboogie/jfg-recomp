#ifndef JFG_TOOLS_H
#define JFG_TOOLS_H
// Helpers for the player tools: the stats overlay, the cheats and level skip (src/jfg/level_hooks.cpp runs the game
// side, src/ui/ui_config.cpp the menus).
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace jfg {
// One pending request (a value >= 0) from the menu thread to the game thread. A newer request replaces one the game
// hasn't taken yet.
class Mailbox {
public:
    void post(int value) { slot.store(value); }
    std::optional<int> take() {
        const int value = slot.exchange(-1);
        return value >= 0 ? std::optional<int>(value) : std::nullopt;
    }
private:
    std::atomic<int> slot{-1};
};

// Events per second, sampled from a running count (frames counted on another thread). A new rate is computed once at
// least a quarter second has passed since the last one; between, the previous rate stands.
class RateMeter {
public:
    double sample(uint64_t count, double now);
private:
    bool started = false;
    uint64_t last_count = 0;
    double last_time = 0, current = 0;
};

struct OverlayData {
    double game_fps = 0, render_fps = 0;
    int level = -1;               // -1: not in gameplay yet
    std::string level_name;
    bool have_position = false;
    float x = 0, y = 0, z = 0;
    double audio_ms = 0;          // sound queued for output
};
// The overlay's lines of text.
std::vector<std::string> format_overlay(const OverlayData& data);

// Skip Ahead's label for exit `index` (0-based) at `distance` from the player.
std::string exit_label(int index, float distance);
}
#endif
