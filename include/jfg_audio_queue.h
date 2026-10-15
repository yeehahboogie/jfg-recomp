#ifndef JFG_AUDIO_QUEUE_H
#define JFG_AUDIO_QUEUE_H
#include <cstdint>
// How much of one converted chunk of sound the frontend hands to SDL. SDL and the sound server read the queue as one
// continuous stream of interleaved float frames, so every chunk must be a whole number of frames: a chunk that ends
// part-way through a frame shifts everything after it, and floats read a few bytes off play as full-scale static.
namespace jfg {
struct QueuePlan {
    uint32_t first_frame;  // converted frames to skip at the start (half the padding)
    uint32_t frames;       // frames to queue
    uint32_t skip_ratio;   // queue every skip_ratio-th frame of the rest, to stop latency building up (1 = all)
};
// converted_bytes: the converted chunk (SDL_AudioCVT::len_cvt); padding_frames: duplicated frames to drop, half at
// each end; queued_bytes: what SDL already holds (SDL_GetQueuedAudioSize). The output is `channels` floats per frame at
// `rate` Hz. Beyond 100 ms queued, the ratio doubles for every further 100 ms, up to 32.
QueuePlan plan_queue(uint32_t converted_bytes, uint32_t padding_frames, uint32_t queued_bytes, uint32_t channels,
                     uint32_t rate);
}
#endif
