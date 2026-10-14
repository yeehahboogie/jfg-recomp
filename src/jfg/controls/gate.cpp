#include "gate.h"
#include <chrono>
namespace jfg::controls {
FrameState g_frame;

namespace {
std::atomic<int64_t> active_ms{INT64_MIN / 2};
int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}

void mark_modern_active() { active_ms.store(now_ms()); }
bool modern_recently_active(int grace_ms) { return now_ms() - active_ms.load() <= grace_ms; }
bool modern_applies(const PlayerView& p, bool scheme_modern) {
    return scheme_modern && p.index == 0 && (p.character & 3) != 3 && !p.input_disabled;
}
}
