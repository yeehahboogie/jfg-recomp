#ifndef JFG_PLAYER_TOOLS_H
#define JFG_PLAYER_TOOLS_H
// The game side of the cheats, level skip and stats overlay. The menus set the cheats and post requests; the game
// thread applies them at player 1's next gameplay frame (src/jfg/level_hooks.cpp) and publishes what the overlay
// and the Cheats tab show.
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace jfg::tools {
struct Cheats {
    std::atomic<bool> invulnerable{false}, all_weapons{false}, all_levels{false}, infinite_ammo{false}, big_head{false};
};
Cheats& cheats();

// Requests, carried out at the next gameplay frame; the menus only post them during gameplay.
void request_refill_health();
void request_warp(int level);
void request_exit(int index);

struct GameInfo {
    bool in_gameplay = false;           // player 1 had a frame in the last half second
    int level = -1;
    float x = 0, y = 0, z = 0;          // player 1's position
    std::vector<float> exit_distances;  // the current level's exits, in order, from player 1
};
GameInfo game_info();
// Every level's name, by number; empty until the first gameplay frame.
std::vector<std::string> level_names();

// Game frames: counted once per player 1 controller read (the game reads once per frame).
void count_game_frame();
uint64_t game_frames();

// Provided by the frontend: frames presented on screen so far (src/main/rt64_render_context.cpp), and the sound
// queued for output in milliseconds (src/main/main.cpp).
uint64_t render_frames();
double audio_queued_ms();
}
#endif
