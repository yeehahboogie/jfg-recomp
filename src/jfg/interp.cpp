#include "jfg_interp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace jfg::interp {
void Recorder::add(uint32_t address, uint32_t id, uint8_t skip, uint8_t flags) {
    entries_.push_back({address, id, skip, flags});
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
            for (size_t k = i + 1; k < j; k++) {
                e.skip |= entries_[k].skip;
                e.flags |= entries_[k].flags;
            }
            frame.entries.push_back(e);
        }
        i = j;
    }
    entries_.clear();
    after_empty_ = false;
    return frame;
}

Frame Recorder::empty(uint32_t dl_address, uint16_t rate) {
    if (after_empty_) entries_.clear();
    after_empty_ = true;
    Frame frame;
    frame.sequence = next_sequence_++;
    frame.dl_address = dl_address;
    frame.rate = rate;
    return frame;
}

size_t Handoff::push(Frame frame) {
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(frame));
    size_t dropped = 0;
    while (pending_.size() > max_pending) {
        pending_.pop_front();
        dropped++;
    }
    return dropped;
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

bool CutDetector::cut(uint32_t id, const CameraPose& pose, uint64_t frame) {
    auto it = last_.find(id);
    if (it != last_.end() && it->second.frame == frame) return it->second.cut;
    Last next{pose, 0.f, frame, true};
    if (it != last_.end() && it->second.frame + 1 == frame) {
        const Last& prev = it->second;
        const float dx = pose.position[0] - prev.pose.position[0], dy = pose.position[1] - prev.pose.position[1],
                    dz = pose.position[2] - prev.pose.position[2];
        next.step = std::sqrt(dx * dx + dy * dy + dz * dz);
        auto turn = [](int16_t a, int16_t b) { return std::abs(int(int16_t(uint16_t(a) - uint16_t(b)))); };
        const bool jumped = next.step > min_jump && next.step > 4.f * prev.step + 16.f;
        // Only a driven camera (cutscene, static) cuts on a turn: the player's own camera turns as fast as input allows.
        const bool turned = pose.driver != 0 && (turn(pose.yaw, prev.pose.yaw) > max_turn ||
                            turn(pose.pitch, prev.pose.pitch) > max_turn || turn(pose.roll, prev.pose.roll) > max_turn);
        next.cut = jumped || turned || pose.driver != prev.pose.driver;
    }
    last_[id] = next;
    return next.cut;
}

void CutDetector::reset() {
    last_.clear();
}

CutDetector& cut_detector() {
    static CutDetector d;
    return d;
}

uint32_t SerialMap::get(uint32_t pointer) {
    auto [it, inserted] = serials_.try_emplace(pointer, next_);
    if (inserted) next_ = next_ + 1 == 0 ? 1 : next_ + 1;
    return it->second;
}

void SerialMap::release(uint32_t pointer) {
    serials_.erase(pointer);
}

void SerialMap::clear() {
    serials_.clear();
}

SerialMap& object_serials() {
    static SerialMap m;
    return m;
}

SerialMap& instance_serials() {
    static SerialMap m;
    return m;
}

OwnerStack& owners() {
    static OwnerStack o;
    return o;
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
