#include "jfg_audio_queue.h"

namespace jfg {
QueuePlan plan_queue(uint32_t converted_bytes, uint32_t padding_frames, uint32_t queued_bytes, uint32_t channels,
                     uint32_t rate) {
    const uint32_t frame_bytes = channels * sizeof(float);
    const uint32_t converted_frames = converted_bytes / frame_bytes;
    QueuePlan plan{};
    plan.first_frame = padding_frames / 2;
    plan.frames = converted_frames > padding_frames ? converted_frames - padding_frames : 0;
    const uint64_t queued_us = uint64_t(queued_bytes / frame_bytes) * 1000000 / rate;
    uint64_t skip_factor = queued_us / 100000;
    if (skip_factor > 5) skip_factor = 5;
    plan.skip_ratio = 1u << skip_factor;
    plan.frames /= plan.skip_ratio;
    return plan;
}
}
