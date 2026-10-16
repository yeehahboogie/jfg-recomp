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
    for (uint32_t i = 0; i < jfg::interp::Handoff::max_pending + 3; i++) {
        Frame x; x.sequence = 100 + i; x.dl_address = 0x1FB0C0;
        h.push(std::move(x));
    }
    p = h.pop(0x1FB0C0, &discarded);
    assert(p && p->sequence == 103 && discarded == 0);

    std::puts("interp_test: OK");
}
