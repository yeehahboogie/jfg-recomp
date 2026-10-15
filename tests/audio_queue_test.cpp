#include <cassert>
#include <cstdio>
#include "jfg_audio_queue.h"

int main() {
    // JFG's full chunk: 736 game frames + 4 duplicated, converted 22020 -> 48000 Hz stereo float = 1613 frames, of
    // which 8 are padding (4 dropped at each end).
    jfg::QueuePlan p = jfg::plan_queue(1613 * 8, 8, 0, 2, 48000);
    assert(p.first_frame == 4 && p.frames == 1605 && p.skip_ratio == 1);

    // 150 ms already queued: keep every other frame. 1605 / 2 must stay a whole number of frames (802), never the
    // 802.5 frames (6420 bytes) that misaligned the stream and turned the sound into static.
    p = jfg::plan_queue(1613 * 8, 8, 48000 * 8 * 150 / 1000, 2, 48000);
    assert(p.skip_ratio == 2 && p.frames == 802 && p.first_frame == 4);

    // Four times over: 2406 bytes before, a quarter of a float off. Whole frames now.
    p = jfg::plan_queue(1613 * 8, 8, 48000 * 8 * 450 / 1000, 2, 48000);
    assert(p.skip_ratio == 16 && p.frames == 1605 / 16);

    // The queue is measured at the output rate: 60 ms queued is under the 100 ms threshold (the old estimate used the
    // game's 22020 Hz and read it as 130 ms).
    p = jfg::plan_queue(1613 * 8, 8, 48000 * 8 * 60 / 1000, 2, 48000);
    assert(p.skip_ratio == 1 && p.frames == 1605);

    // A huge backlog caps the ratio instead of shifting by 32 or more (undefined).
    p = jfg::plan_queue(1613 * 8, 8, 48000 * 8 * 10, 2, 48000);
    assert(p.skip_ratio == 32 && p.frames == 1605 / 32);

    // An odd padding count still starts on a frame boundary.
    p = jfg::plan_queue(1613 * 8, 9, 0, 2, 48000);
    assert(p.first_frame == 4 && p.frames == 1604);

    // Fewer converted frames than padding queues nothing.
    p = jfg::plan_queue(6 * 8, 8, 0, 2, 48000);
    assert(p.frames == 0);
    std::puts("audio_queue_test: OK");
}
