#include <cassert>
#include <cstdio>
#include "jfg_interp.h"

using jfg::interp::Frame;

int main() {
    // IDs: cameras by role and viewport, never IGNORE (0) or AUTO (0xFFFFFFFF), all distinct.
    assert(jfg::interp::camera_id(0, 0) == 0x100);
    assert(jfg::interp::camera_id(1, 3) == 0x113);
    assert(jfg::interp::camera_id(2, 0) != jfg::interp::camera_id(1, 0));

    // A frame is sorted by address; the same address and ID twice is one entry; two IDs for one address is a
    // conflict and neither is kept.
    jfg::interp::Recorder rec;
    rec.add(0x2030, 7);
    rec.add(0x1000, 5, RT64::JFG_SKIP_ALL);
    rec.add(0x2030, 7);
    rec.add(0x3000, 1);
    rec.add(0x3000, 2);
    Frame f = rec.take(0x1FB0C0, 30);
    assert(f.sequence == 0 && f.dl_address == 0x1FB0C0 && f.rate == 30);
    assert(f.entries.size() == 2 && f.conflicts == 1);
    assert(f.entries[0].address == 0x1000 && f.entries[0].id == 5 && f.entries[0].skip == RT64::JFG_SKIP_ALL);
    assert(f.entries[1].address == 0x2030 && f.entries[1].id == 7);

    // The renderer's view of it.
    RT64::JfgFrameTable table{true, f.entries.data(), f.entries.size()};
    assert(table.find(0x2030)->id == 7);
    assert(table.find(0x2031) == nullptr && table.find(0x0FFF) == nullptr && table.find(0x3000) == nullptr);

    // Taking starts a new, empty frame (a module screen records nothing, so it gets nothing).
    Frame g = rec.take(0x220FE0, 20);
    assert(g.sequence == 1 && g.entries.empty() && g.conflicts == 0 && g.rate == 20);

    // A submit for another display list (gameplay's small module one) gets an empty frame and leaves what's being
    // recorded for the main display list alone.
    rec.add(0x4000, 9);
    Frame other = rec.empty(0x327A10, 30);
    assert(other.entries.empty() && other.dl_address == 0x327A10 && other.sequence == 2);
    Frame main = rec.take(0x1FB0C0, 30);
    assert(main.entries.size() == 1 && main.entries[0].id == 9 && main.sequence == 3);
    // A second non-main submit in a row means no main display list is coming for what was recorded (a screen that only
    // submits its own display lists): it's dropped rather than carried, stale, into the next main frame.
    rec.add(0x5000, 11);
    rec.empty(0x327A10, 30);
    rec.empty(0x327A10, 30);
    Frame later = rec.take(0x220FE0, 30);
    assert(later.entries.empty());

    // Handoff: display lists alternate between two buffers; a pop takes the oldest frame for that buffer and
    // discards the frames before it.
    jfg::interp::Handoff h;
    for (uint32_t i = 0; i < 4; i++) {
        Frame x; x.sequence = i; x.dl_address = (i % 2) ? 0x220FE0 : 0x1FB0C0;
        h.push(std::move(x));
    }
    uint32_t discarded = 0;
    auto p = h.pop(0x220FE0, &discarded);
    assert(p && p->sequence == 1 && discarded == 1);
    p = h.pop(0x1FB0C0, &discarded);
    assert(p && p->sequence == 2 && discarded == 0);
    // A display list nobody pushed leaves the queue alone.
    assert(!h.pop(0x123456, &discarded) && discarded == 0);
    p = h.pop(0x220FE0, &discarded);
    assert(p && p->sequence == 3);
    assert(!h.pop(0x220FE0));

    // Beyond max_pending, the oldest frames are dropped.
    size_t dropped = 0;
    for (uint32_t i = 0; i < jfg::interp::Handoff::max_pending + 3; i++) {
        Frame x; x.sequence = 100 + i; x.dl_address = 0x1FB0C0;
        dropped += h.push(std::move(x));
    }
    assert(dropped == 3);
    p = h.pop(0x1FB0C0, &discarded);
    assert(p && p->sequence == 103 && discarded == 0);

    // Camera cuts. Positions in world units; angles are binary (0x10000 = 360 degrees).
    using jfg::interp::CameraPose;
    jfg::interp::CutDetector cuts;
    const uint32_t cam = jfg::interp::camera_id(0, 0);
    auto pose = [](float x, int16_t yaw, uint32_t driver = 0) { return CameraPose{{x, 100.f, 0.f}, yaw, 0, 0, driver}; };
    assert(cuts.cut(cam, pose(0, 0), 10));        // first sighting: nothing to come from
    assert(!cuts.cut(cam, pose(10, 0), 11));      // steady motion
    assert(!cuts.cut(cam, pose(10, 0), 11));      // the same camera again in the same frame (particle pass): same answer
    assert(!cuts.cut(cam, pose(20, 0), 12));
    assert(!cuts.cut(cam, pose(30, 0x0300), 13)); // turning about 4 degrees a frame
    assert(cuts.cut(cam, pose(900, 0x0300), 14)); // a jump far beyond its pace
    assert(!cuts.cut(cam, pose(905, 0x0300), 15));
    assert(!cuts.cut(cam, pose(910, 0x2000), 16)); // the player's own camera may turn 45 degrees in a frame
    assert(!cuts.cut(cam, pose(915, 0x2000), 17));
    assert(cuts.cut(cam, pose(920, 0x2000, 0x80123450), 18)); // another object now drives the camera (new shot)
    assert(!cuts.cut(cam, pose(925, 0x2000, 0x80123450), 19));
    assert(cuts.cut(cam, pose(930, 0x2000, 0x80123450), 21)); // missed a frame: nothing recent to come from
    // Angles wrap: 0x7FF0 to -0x7FF0 is a 0x20 turn, not almost a full circle (getting to 0x7FF0 is a big turn).
    assert(cuts.cut(cam, pose(935, int16_t(0x7FF0), 0x80123450), 22));
    assert(!cuts.cut(cam, pose(940, int16_t(-0x7FF0), 0x80123450), 23));
    // A fast but steady camera (the pod's flight) isn't cut once it's moving; the first big step is a jump.
    assert(cuts.cut(cam, pose(1090, int16_t(-0x7FF0), 0x80123450), 24));
    for (int i = 1; i < 5; i++) assert(!cuts.cut(cam, pose(1090.f + 150.f * i, int16_t(-0x7FF0), 0x80123450), 24 + i));
    // Each camera keeps its own history; a level load forgets them all.
    assert(cuts.cut(jfg::interp::camera_id(0, 1), pose(0, 0), 30));
    cuts.reset();
    assert(cuts.cut(cam, pose(1610, int16_t(-0x7FF0), 0x80123450), 29));

    // The player's own camera (no cutscene or static camera driving it) turns as fast as the mouse allows: a flick
    // is not a cut. A cutscene camera turning that far in one frame still is.
    jfg::interp::CutDetector player;
    assert(player.cut(cam, pose(0, 0), 40));
    assert(!player.cut(cam, pose(0, 0x4000), 41));   // 90 degrees in one frame, nobody driving
    assert(player.cut(cam, pose(0, 0x4000, 0x80123450), 42)); // a driver took over
    assert(player.cut(cam, pose(0, 0x0000, 0x80123450), 43)); // driven and turned 90 degrees

    // Serials: one per live pointer, a fresh one after the pointer is released (a new object at a freed address).
    jfg::interp::SerialMap serials;
    const uint32_t a = serials.get(0x801BB2C0), b = serials.get(0x801BC030);
    assert(a != 0 && b != 0 && a != b);
    assert(serials.get(0x801BB2C0) == a);
    serials.release(0x801BB2C0);
    const uint32_t a2 = serials.get(0x801BB2C0);
    assert(a2 != a && a2 != b && a2 != 0);
    serials.release(0x80000000); // releasing an unknown pointer is harmless
    serials.clear();
    assert(serials.get(0x801BC030) != b);

    // Joint IDs: by instance serial and joint, in their own range, distinct across instances and joints.
    assert(jfg::interp::joint_id(1, 0) == 0x01000040);
    assert(jfg::interp::joint_id(1, 63) == 0x0100007F);
    assert(jfg::interp::joint_id(2, 0) == 0x01000080);

    std::puts("interp_test: OK");
}
