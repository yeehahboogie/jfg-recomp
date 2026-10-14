#include "gate.h"
namespace jfg::controls {
FrameState g_frame;
bool modern_applies(const PlayerView& p, bool scheme_modern) {
    return scheme_modern && p.index == 0 && (p.character & 3) != 3 && !p.input_disabled;
}
}
