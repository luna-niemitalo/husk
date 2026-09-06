// Structural convergence proof for canon::assembleBoneAnimation
// (canon_animation_builder.hpp) against commands::buildJointAnimation +
// m2::resolveVec3TrackSequence/resolveQuatTrackSequence (export_animation.cpp),
// the real production pipeline -- REFACTOR/README.md stage 3's gate, inline-
// sequence scope only. Compares canon's raw-M2-space curves directly against
// the same resolve*TrackSequence output buildJointAnimation itself consumes,
// rather than round-tripping through gltf::JointAnimation and inverting its
// writer-specific transforms (toGltf, bind-pose addition, toGltfScale) --
// canon's whole point is not needing any of those to represent this data.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_animation_builder.hpp"
#include "m2.hpp"
#include "m2_animation.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

// M2Sequence::flags bit 0x20 ("primary bone sequence": keyframe data lives
// inline in this M2, not an external .anim file) -- private to
// export_animation.cpp's anonymous namespace as kSequenceStoredInlineFlag,
// no public accessor exists, so checked directly here rather than silently
// duplicating a second copy of the constant under a different name (still
// documented against the same wowdev.wiki source that private constant
// cites).
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;

}  // namespace

namespace {

// One (sequence, bone) pick plus its resolved raw track data, used to run
// both the legacy-equivalent resolution and canon::assembleBoneAnimation
// over the exact same real inputs.
struct Pick {
    size_t sequenceIndex = 0;
    size_t boneIndex = 0;
    std::vector<std::pair<uint32_t, m2::Vec3>> translation;
    std::vector<std::pair<uint32_t, m2::Quat>> rotation;
    std::vector<std::pair<uint32_t, m2::Vec3>> scale;
};

// Scans every real inline sequence/bone pair, scoring each by `score`, and
// returns the highest-scoring one -- `score` picks out "real, non-trivial
// data for the channel(s) this check cares about," never a synthetic
// fixture.
template <typename ScoreFn>
std::optional<Pick> findBestInlinePick(const m2::Model& model, ScoreFn score) {
    std::optional<Pick> best;
    long bestScore = -1;
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        if ((model.sequences[si].flags & kSequenceStoredInlineFlag) == 0) continue;
        for (size_t bi = 0; bi < model.bones.size(); ++bi) {
            const auto& bone = model.bones[bi];
            Pick candidate;
            candidate.sequenceIndex = si;
            candidate.boneIndex = bi;
            candidate.translation = m2::resolveVec3TrackSequence(model.blob, bone.translationTrackOffset,
                                                                    static_cast<uint32_t>(si));
            candidate.rotation = m2::resolveQuatTrackSequence(model.blob, bone.rotationTrackOffset,
                                                                 static_cast<uint32_t>(si));
            candidate.scale = m2::resolveVec3TrackSequence(model.blob, bone.scaleTrackOffset,
                                                              static_cast<uint32_t>(si));
            long s = score(candidate);
            if (s > bestScore) {
                bestScore = s;
                best = std::move(candidate);
            }
        }
    }
    return bestScore > 0 ? best : std::nullopt;
}

void checkVecCurve(const canon::VecCurve& curve, const std::vector<std::pair<uint32_t, m2::Vec3>>& legacy,
                    uint32_t sequenceIndex, const char* property) {
    INFO("property ", property);
    CHECK(curve.sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(curve.sequence.index == sequenceIndex);
    REQUIRE(curve.keyframes.size() == legacy.size());
    for (size_t i = 0; i < legacy.size(); ++i) {
        INFO("keyframe ", i);
        CHECK(curve.keyframes[i].first == doctest::Approx(static_cast<float>(legacy[i].first) / 1000.0f));
        CHECK(curve.keyframes[i].second.x == doctest::Approx(legacy[i].second.x));
        CHECK(curve.keyframes[i].second.y == doctest::Approx(legacy[i].second.y));
        CHECK(curve.keyframes[i].second.z == doctest::Approx(legacy[i].second.z));
    }
}

// No hemisphere-continuity fix applied on either side: canon deliberately
// doesn't apply gltf::enforceHemisphereContinuity (see
// canon_animation_builder.hpp's doc comment -- that fix compensates for a
// discontinuity rotationZUpToYUp's own matrix round-trip introduces, not a
// property of the raw M2CompQuat data), and `legacy` here is the
// pre-toGltf resolveQuatTrackSequence output, never run through that fix
// either. A raw-value mismatch would mean raw M2 quaternion data itself is
// hemisphere-discontinuous -- a real design question, not something to
// silently patch over in this comparison.
void checkQuatCurve(const canon::QuatCurve& curve, const std::vector<std::pair<uint32_t, m2::Quat>>& legacy,
                     uint32_t sequenceIndex) {
    CHECK(curve.sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(curve.sequence.index == sequenceIndex);
    REQUIRE(curve.keyframes.size() == legacy.size());
    for (size_t i = 0; i < legacy.size(); ++i) {
        INFO("rotation keyframe ", i);
        CHECK(curve.keyframes[i].first == doctest::Approx(static_cast<float>(legacy[i].first) / 1000.0f));
        CHECK(curve.keyframes[i].second.x == doctest::Approx(legacy[i].second.x));
        CHECK(curve.keyframes[i].second.y == doctest::Approx(legacy[i].second.y));
        CHECK(curve.keyframes[i].second.z == doctest::Approx(legacy[i].second.z));
        CHECK(curve.keyframes[i].second.w == doctest::Approx(legacy[i].second.w));
    }
}

void checkInterpolation(const m2::Model& model, const m2::Bone& bone, const canon::BoneAnimationCurves& curves) {
    auto expected = [&](uint32_t offset) {
        return m2::readTrackMeta(model.blob, offset).interpolationType == 0 ? canon::Interpolation::Step
                                                                             : canon::Interpolation::Linear;
    };
    CHECK(curves.translation.interpolation == expected(bone.translationTrackOffset));
    CHECK(curves.rotation.interpolation == expected(bone.rotationTrackOffset));
    CHECK(curves.scale.interpolation == expected(bone.scaleTrackOffset));
}

}  // namespace

TEST_CASE("canon::assembleBoneAnimation converges with the real resolve*TrackSequence output "
          "for real inline sequence/bone translation+rotation data, raw M2 space" *
          doctest::skip(test::testM2().empty())) {
    std::ifstream mf(test::testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    auto model = m2::loadModel(fileBytes);
    REQUIRE(!model.bones.empty());
    REQUIRE(!model.sequences.empty());

    // bloodelffemale.m2 has no single (sequence, bone) pair with real
    // multi-keyframe translation, rotation, AND scale data simultaneously
    // (confirmed by a full scan: the highest-scoring translation+rotation
    // bone always has 0 scale keyframes, and every bone with real scale
    // data has 0 translation/rotation keyframes) -- real per-bone animation
    // data commonly only touches a subset of TRS channels. This picks the
    // best translation+rotation pair here; a second test case below picks
    // the best real scale pair separately, so all three channels still get
    // real, non-trivial coverage, just not from one bone.
    auto pick = findBestInlinePick(model, [](const Pick& c) -> long {
        if (c.translation.size() <= 1 || c.rotation.size() <= 1) return -1;  // exclude: not non-trivial on both
        return static_cast<long>(c.translation.size()) + static_cast<long>(c.rotation.size());
    });
    REQUIRE(pick.has_value());
    CHECK(pick->translation.size() > 1);
    CHECK(pick->rotation.size() > 1);

    const auto& bone = model.bones[pick->boneIndex];
    auto canonCurves =
        canon::assembleBoneAnimation(model.blob, bone, pick->boneIndex, static_cast<uint32_t>(pick->sequenceIndex));
    REQUIRE(canonCurves.has_value());

    checkVecCurve(canonCurves->translation, pick->translation, static_cast<uint32_t>(pick->sequenceIndex),
                  "translation");
    checkQuatCurve(canonCurves->rotation, pick->rotation, static_cast<uint32_t>(pick->sequenceIndex));
    checkVecCurve(canonCurves->scale, pick->scale, static_cast<uint32_t>(pick->sequenceIndex), "scale");
    checkInterpolation(model, bone, *canonCurves);
}

TEST_CASE("canon::assembleBoneAnimation converges with the real resolve*TrackSequence output "
          "for real inline sequence/bone scale data, raw M2 space" *
          doctest::skip(test::testM2().empty())) {
    std::ifstream mf(test::testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    auto model = m2::loadModel(fileBytes);
    REQUIRE(!model.bones.empty());
    REQUIRE(!model.sequences.empty());

    auto pick = findBestInlinePick(
        model, [](const Pick& c) { return static_cast<long>(c.scale.size()); });
    REQUIRE(pick.has_value());
    CHECK(pick->scale.size() > 1);

    const auto& bone = model.bones[pick->boneIndex];
    auto canonCurves =
        canon::assembleBoneAnimation(model.blob, bone, pick->boneIndex, static_cast<uint32_t>(pick->sequenceIndex));
    REQUIRE(canonCurves.has_value());

    checkVecCurve(canonCurves->translation, pick->translation, static_cast<uint32_t>(pick->sequenceIndex),
                  "translation");
    checkQuatCurve(canonCurves->rotation, pick->rotation, static_cast<uint32_t>(pick->sequenceIndex));
    checkVecCurve(canonCurves->scale, pick->scale, static_cast<uint32_t>(pick->sequenceIndex), "scale");
    checkInterpolation(model, bone, *canonCurves);
}

TEST_CASE("canon::assembleBoneAnimation returns nullopt when all three tracks are empty, same "
          "nothing-to-animate case buildJointAnimation early-returns on") {
    // Zeroed, not empty: readTrackMeta/trackSequenceInnerArrays bounds-check
    // against the blob before deciding a track is empty, so an out-of-range
    // read (a truly empty blob) throws ParseError instead of resolving
    // empty -- a genuinely different case (corrupted data) from "this track
    // legitimately has no keyframes." A zeroed M2Track<T> (globalSequence 0,
    // both outer M2Array counts 0) is the real "no data" shape and resolves
    // to {} for all three tracks, all pointing at offset 0.
    std::vector<uint8_t> zeroedBlob(64, 0);
    m2::Bone bone;  // default offsets (0) all point at the same zeroed track above
    auto result = canon::assembleBoneAnimation(zeroedBlob, bone, 0, 0);
    CHECK_FALSE(result.has_value());
}
