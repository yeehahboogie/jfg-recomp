#ifndef JFG_PLAYERS_H
#define JFG_PLAYERS_H
// Which gamepad each N64 controller port (player) reads. With extra players off, every gamepad drives player 1 and
// only player 1 is connected. With it on, gamepad n (in connection order) is player n + 1; player 1 also keeps the
// keyboard and mouse, so it is connected even without a gamepad.
namespace jfg {
constexpr int all_pads = -1;  // read every connected gamepad
constexpr int no_pad = -2;    // read no gamepad
constexpr int max_players = 4;
int pad_for_player(int player, int pad_count, bool extra_players);
bool player_connected(int player, int pad_count, bool extra_players);
}
#endif
