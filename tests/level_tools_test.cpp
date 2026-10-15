#include <cassert>
#include <cstdio>
#include <stdexcept>
#include "jfg_level_tools.h"

int main() {
    // JFG_WARP=<level>[:<entry>], decimal or 0x-prefixed hex.
    jfg::WarpSpec w = jfg::parse_warp("42");
    assert(w.level == 42 && w.entry == 0);
    w = jfg::parse_warp("0x11E:3");
    assert(w.level == 0x11E && w.entry == 3);
    bool threw = false;
    try { jfg::parse_warp("goldwood"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    threw = false;
    try { jfg::parse_warp("12:"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    threw = false;
    try { jfg::parse_warp("-1"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    std::puts("level_tools_test: OK");
}
