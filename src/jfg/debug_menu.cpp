// Zelda64Recomp's debug menu offers Majora's Mask warps and time-of-day controls. JFG has neither, so the
// menu gets an empty warp table and no-op actions.
#include <cstdint>

#include "zelda_debug.h"

std::vector<zelda64::AreaWarps> zelda64::game_warps{};

void zelda64::do_warp(int, int, int) {}

void zelda64::set_time(uint8_t, uint8_t, uint8_t) {}
