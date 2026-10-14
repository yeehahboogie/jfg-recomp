#include "input_state.h"
#include <mutex>
namespace jfg::controls {
namespace {
std::mutex mutex;
ModernInput published;   // latest state; mouse fields hold the counts since the last latch
ModernInput snapshot;
}

void publish(const ModernInput& in) {
    std::lock_guard lock{mutex};
    const float dx = published.mouse_dx + in.mouse_dx, dy = published.mouse_dy + in.mouse_dy;
    published = in;
    published.mouse_dx = dx;
    published.mouse_dy = dy;
}

void latch() {
    std::lock_guard lock{mutex};
    snapshot = published;
    published.mouse_dx = published.mouse_dy = 0;
}

const ModernInput& frame() { return snapshot; }
}
