#include "jfg_players.h"

namespace jfg {
int pad_for_player(int player, int pad_count, bool extra_players) {
    if (!extra_players) return player == 0 ? all_pads : no_pad;
    if (player < 0 || player >= max_players || player >= pad_count) return no_pad;
    return player;
}

bool player_connected(int player, int pad_count, bool extra_players) {
    if (player == 0) return true;
    return pad_for_player(player, pad_count, extra_players) >= 0;
}
}
