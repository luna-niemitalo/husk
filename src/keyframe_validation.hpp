#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "m2.hpp"

namespace husk::m2input {

inline bool isFinite(const m2::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

inline bool isFinite(const m2::Quat& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

// Checks one bone property's keyframes before they're trusted: every value
// finite, timestamps strictly increasing. Throws naming the bone, property
// and keyframe, so a corrupted or truncated .anim/.skel/M2 fails loudly.
//
// An exact-duplicate timestamp is repaired, not rejected: the later one
// moves forward 1ms (cascading through a run), keeping both authored
// values where collapsing would drop one (DESIGN.md "A duplicate animation
// keyframe timestamp is repaired, not rejected"). Disorder is judged
// against the original timestamps, so a run T, T, T isn't misread once the
// second becomes T+1. A timestamp below its predecessor is real disorder
// and throws; so does a nudge that collides with the next keyframe.
template <typename T>
void repairDuplicateTimestampsAndValidate(std::vector<std::pair<uint32_t, T>>& keyframes, size_t boneIndex,
                                          const char* property) {
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (!isFinite(keyframes[i].second)) {
            throw std::runtime_error("bone " + std::to_string(boneIndex) + "'s " + property + " keyframe " +
                                     std::to_string(i) +
                                     " has a non-finite (NaN/Inf) value -- corrupted read or truncated file?");
        }
    }

    std::vector<uint32_t> originalTimes;
    originalTimes.reserve(keyframes.size());
    for (const auto& kf : keyframes) originalTimes.push_back(kf.first);

    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (originalTimes[i] < originalTimes[i - 1]) {
            throw std::runtime_error("bone " + std::to_string(boneIndex) + "'s " + property + " keyframe " +
                                     std::to_string(i) + "'s timestamp (" + std::to_string(originalTimes[i]) +
                                     "ms) isn't strictly greater than keyframe " + std::to_string(i - 1) + "'s (" +
                                     std::to_string(originalTimes[i - 1]) +
                                     "ms) -- corrupted read or truncated file?");
        }
        if (originalTimes[i] == originalTimes[i - 1]) keyframes[i].first = keyframes[i - 1].first + 1;
    }

    for (size_t i = 1; i < keyframes.size(); ++i) {
        if (keyframes[i].first <= keyframes[i - 1].first) {
            throw std::runtime_error("bone " + std::to_string(boneIndex) + "'s " + property + " keyframe " +
                                     std::to_string(i) +
                                     "'s timestamp couldn't be repaired into strictly-increasing order "
                                     "(duplicate-timestamp nudging collided with a following keyframe) -- "
                                     "corrupted read or truncated file?");
        }
    }
}

}  // namespace husk::m2input
