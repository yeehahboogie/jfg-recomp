#include <cassert>
#include <cstdio>
#include "hle/rt64_jfg_groups.h"

using namespace RT64;

int main() {
    const JfgMatrixIdentity entries[] = {{0x1000, 0x100, 0}, {0x2000, 0x01000203, JFG_SKIP_ALL}};
    const JfgFrameTable table{true, entries, 2};
    const JfgFrameTable off{false, entries, 2};

    // A known camera: its ID, matched by ID, every component interpolated.
    TransformGroup cam = jfgGroupFor(&table, 0x1000, true);
    assert(cam.matrixId == 0x100 && cam.ordering == G_EX_ORDER_LINEAR);
    assert(cam.positionInterpolation == G_EX_COMPONENT_INTERPOLATE && cam.rotationInterpolation == G_EX_COMPONENT_INTERPOLATE);
    assert(cam.perspectiveInterpolation == G_EX_COMPONENT_INTERPOLATE);
    // A cut: the same ID, nothing interpolated into this frame.
    TransformGroup cut = jfgGroupFor(&table, 0x2000, false);
    assert(cut.matrixId == 0x01000203 && cut.positionInterpolation == G_EX_COMPONENT_SKIP);
    assert(cut.rotationInterpolation == G_EX_COMPONENT_SKIP && cut.scaleInterpolation == G_EX_COMPONENT_SKIP);

    // A model (not a camera) leaves its position to RT64's AUTO mode, which skips a frame on a velocity spike
    // (teleports, respawns); rotation and scale interpolate.
    TransformGroup joint = jfgGroupFor(&table, 0x1000, false);
    assert(joint.matrixId == 0x100 && joint.positionInterpolation == G_EX_COMPONENT_AUTO);
    assert(joint.rotationInterpolation == G_EX_COMPONENT_INTERPOLATE);

    // Drawn as-is means nothing about the draw is guessed or blended between frames, texture scroll included (tile
    // interpolation left at AUTO would pair tiles by RT64's heuristic even with interpolation switched off).
    for (const TransformGroup g : {jfgIgnoreGroup(), jfgGroupFor(&table, 0x1004, false), jfgGroupFor(&off, 0x1000, true),
                                   jfgGroupFor(nullptr, 0x1000, true)}) {
        assert(g.matrixId == G_EX_ID_IGNORE);
        assert(g.tileInterpolation == G_EX_COMPONENT_SKIP);
        assert(g.vertexInterpolation == G_EX_COMPONENT_SKIP);
    }
    std::puts("interp_groups_test: OK");
}
