#include <cassert>
#include <cstdio>
#include "../../src/jfg/controls/gate.h"
using jfg::controls::PlayerView;
using jfg::controls::modern_applies;
int main() {
    PlayerView juno{0, 1, 0, false};
    assert(modern_applies(juno, true));
    assert(!modern_applies(juno, false));                      // scheme Original
    assert(!modern_applies(PlayerView{1, 1, 0, false}, true)); // Floyd / player 2
    // An input lock (hit, door, interaction) keeps Modern in charge of the camera and aim; the hooks then see no input.
    assert(modern_applies(PlayerView{0, 1, 0, true}, true));
    assert(!modern_applies(PlayerView{0, 3, 0, false}, true)); // hovership (character bits 3)
    // Mouse capture follows Modern gameplay: nothing marked yet means not active; a mark is recent.
    assert(!jfg::controls::modern_recently_active(500));
    jfg::controls::mark_modern_active();
    assert(jfg::controls::modern_recently_active(500));
    std::puts("gate_test: OK");
}
