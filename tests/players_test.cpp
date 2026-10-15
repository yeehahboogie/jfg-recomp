#include <cassert>
#include <cstdio>
#include "jfg_players.h"

int main() {
    using jfg::all_pads; using jfg::no_pad;
    // Off (the default): every gamepad drives player 1, and only player 1 is connected.
    assert(jfg::pad_for_player(0, 3, false) == all_pads);
    assert(jfg::pad_for_player(1, 3, false) == no_pad);
    assert(jfg::player_connected(0, 0, false) && !jfg::player_connected(1, 3, false));

    // On: gamepad n (in connection order) is player n + 1; player 1 also keeps the keyboard and mouse.
    assert(jfg::pad_for_player(0, 3, true) == 0);
    assert(jfg::pad_for_player(1, 3, true) == 1 && jfg::pad_for_player(2, 3, true) == 2);
    assert(jfg::pad_for_player(3, 3, true) == no_pad);
    assert(jfg::player_connected(2, 3, true) && !jfg::player_connected(3, 3, true));
    // Player 1 is always connected, even with no gamepad (keyboard and mouse).
    assert(jfg::player_connected(0, 0, true) && jfg::pad_for_player(0, 0, true) == no_pad);
    // Only four N64 ports.
    assert(!jfg::player_connected(4, 8, true) && jfg::pad_for_player(4, 8, true) == no_pad);
    std::puts("players_test: OK");
}
