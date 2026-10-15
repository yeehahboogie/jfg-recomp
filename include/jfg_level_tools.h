#ifndef JFG_LEVEL_TOOLS_H
#define JFG_LEVEL_TOOLS_H
#include <string>
// Development aids for reaching and surveying every level (docs/superpowers/plans/2026-10-15-jfg-whole-game.md).
namespace jfg {
struct WarpSpec {
    int level;
    int entry;
};
// JFG_WARP=<level>[:<entry>], decimal or 0x-prefixed hex. Throws std::invalid_argument when malformed.
WarpSpec parse_warp(const std::string& text);
}
#endif
