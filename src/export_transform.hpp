#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "gltf.hpp"
#include "keyframe_validation.hpp"
#include "m2.hpp"
#include "phys.hpp"

// Coordinate/validation helpers shared by every export_*.cpp phase
// (skeleton/animation/materials all convert WoW's Z-up M2/.phys data into
// glTF's Y-up space, and validate keyframe data the same way) -- split out
// of cmd_export.cpp per FILE_SPLIT_TODO.md's Item 1.
namespace husk::commands {

gltf::Vec3 toGltf(const m2::Vec3& v);
gltf::Vec3 toGltf(const phys::Vec3& v);

// Converts an M2 bone-rotation quaternion (already decompressed, see
// m2::Quat) from WoW's Z-up space to glTF's Y-up space. A thin wrapper --
// gltf::rotationZUpToYUp (gltf.hpp/gltf.cpp) is the single source of truth
// for this conversion, mechanically derived from the same matrix
// gltf::zUpToYUp uses for positions, not a separately hand-typed formula.
gltf::Quat toGltf(const m2::Quat& q);

// Converts an M2 bone scale vector from Z-up to Y-up. A thin wrapper --
// gltf::scaleZUpToYUp is the single source of truth (see toGltf(m2::Quat)
// above for why that matters, and gltf.hpp's own doc comment for why scale
// isn't gltf::zUpToYUp itself: it needs the same matrix's permutation with
// signs dropped, not a position/direction's signed transform).
gltf::Vec3 toGltfScale(const m2::Vec3& s);

bool isFinite(const m2::Vec3& v);
bool isFinite(const m2::Quat& q);

// One implementation, shared with the canon path (keyframe_validation.hpp).
using m2input::repairDuplicateTimestampsAndValidate;

// Resolves an animated glTF material curve from an M2Track<T> -- one
// `Curve` per real M2Sequence with non-empty data, plus a synthetic
// global-sequence entry, both timestamp-scaled to seconds. Shared by every
// "M2Track -> glTF curve" resolution across export_texture_resolution.cpp
// (materials' own tint/fade/UV-transform curves) and export_extras.cpp
// (M2Light's ambient/diffuse/attenuation/visibility curves) -- these were
// found duplicated near-verbatim across both files (six functions, one
// shape) before this template existed; see DESIGN.md's own writeup for the
// exact duplication found and why this is the one place it's written now.
// `resolveSequence`/`resolveGlobal` are the matching
// m2::resolveXTrackSequence/resolveXGlobalSequenceTrack pair for the
// track's own wire type (a plain function reference for the ones whose
// signature is exactly `(blob, trackOffset, sequenceIndex[, externalBlob])`/
// `(blob, trackOffset[, externalBlob])`, or a small lambda for
// resolveRawIntTrackSequence's own extra `elementSize` parameter -- see
// this function's callers for both shapes). `toValue` converts one raw
// (timestamp, T) pair's *value* into `Curve`'s own keyframe value type,
// applying whatever scaling a given track kind needs (fixed16 decode, a
// raw byte cast to float, a Vec3/Quat repacked into gltf's own value
// type, or a no-op) -- the only real difference between any two callers.
// `externalDataBlob` is always nullptr at every real call site today (none
// of materials'/lights' animated curves are ever .anim-sourced), passed
// explicitly rather than omitted since a function *pointer* (unlike a
// direct call) can't rely on the callee's own default argument.
template <typename Curve, typename ResolveSeqFn, typename ResolveGlobalFn, typename ToValueFn>
std::vector<Curve> resolveAnimatedCurveGeneric(const std::vector<uint8_t>& blob, uint32_t trackOffset,
                                                size_t sequenceCount, ResolveSeqFn resolveSequence,
                                                ResolveGlobalFn resolveGlobal, ToValueFn toValue) {
    std::vector<Curve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = resolveSequence(blob, trackOffset, static_cast<uint32_t>(si), nullptr);
        if (raw.empty()) continue;
        Curve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, v] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, toValue(v));
        }
        curves.push_back(std::move(curve));
    }
    auto global = resolveGlobal(blob, trackOffset, nullptr);
    if (!global.empty()) {
        Curve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, v] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, toValue(v));
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

}  // namespace husk::commands
