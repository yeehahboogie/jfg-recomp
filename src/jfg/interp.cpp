#include "jfg_interp.h"

#include <algorithm>

namespace jfg::interp {
void Recorder::add(uint32_t address, uint32_t id, uint8_t skip) {
    entries_.push_back({address, id, skip});
}

Frame Recorder::take(uint32_t dl_address, uint16_t rate) {
    Frame frame;
    frame.sequence = next_sequence_++;
    frame.dl_address = dl_address;
    frame.rate = rate;
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const RT64::JfgMatrixIdentity& a, const RT64::JfgMatrixIdentity& b) { return a.address < b.address; });
    for (size_t i = 0; i < entries_.size();) {
        size_t j = i + 1;
        bool conflict = false;
        while (j < entries_.size() && entries_[j].address == entries_[i].address) {
            conflict = conflict || (entries_[j].id != entries_[i].id);
            j++;
        }
        if (conflict) {
            frame.conflicts++;
        } else {
            RT64::JfgMatrixIdentity e = entries_[i];
            for (size_t k = i + 1; k < j; k++) e.skip |= entries_[k].skip;
            frame.entries.push_back(e);
        }
        i = j;
    }
    entries_.clear();
    return frame;
}

void Handoff::push(Frame frame) {
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(frame));
    while (pending_.size() > max_pending) pending_.pop_front();
}

std::optional<Frame> Handoff::pop(uint32_t dl_address, uint32_t* discarded) {
    std::lock_guard lock(mutex_);
    if (discarded) *discarded = 0;
    auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Frame& f) { return f.dl_address == dl_address; });
    if (it == pending_.end()) return std::nullopt;
    if (discarded) *discarded = uint32_t(it - pending_.begin());
    Frame frame = std::move(*it);
    pending_.erase(pending_.begin(), it + 1);
    return frame;
}

Recorder& recorder() {
    static Recorder r;
    return r;
}

Handoff& handoff() {
    static Handoff h;
    return h;
}
}
