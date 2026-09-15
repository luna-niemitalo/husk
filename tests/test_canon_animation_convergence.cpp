// Structural convergence proof for m2input::assembleBoneAnimation
// (m2_animation_input.hpp) against commands::buildJointAnimation +
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
#include <optional>

#include "m2_animation_input.hpp"
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
// both the legacy-equivalent resolution and m2input::assembleBoneAnimation
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

// One CHECK per kind of fact verified across the whole curve, not one per
// keyframe per component -- a real picked curve (findBestInlinePick favors
// the highest keyframe count) can run to dozens/hundreds of keyframes, and
// asserting per-keyframe-per-component multiplies this test's assertion
// count by that curve length for no added coverage (every keyframe is
// still compared; only the assertion *count* changes). Same
// aggregate-then-diagnose discipline test_canon_skeleton_convergence.cpp's
// per-joint loop already establishes; INFO below names the first offending
// keyframe/component on failure.
void checkVecCurve(const canon::VecCurve& curve, const std::vector<std::pair<uint32_t, m2::Vec3>>& legacy,
                    uint32_t sequenceIndex, const char* property) {
    INFO("property ", property);
    CHECK(curve.sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(curve.sequence.index == sequenceIndex);
    REQUIRE(curve.keyframes.size() == legacy.size());
    std::optional<size_t> timeMismatch, xMismatch, yMismatch, zMismatch;
    for (size_t i = 0; i < legacy.size(); ++i) {
        if (curve.keyframes[i].first != doctest::Approx(static_cast<float>(legacy[i].first) / 1000.0f) &&
            !timeMismatch) {
            timeMismatch = i;
        }
        if (curve.keyframes[i].second.x != doctest::Approx(legacy[i].second.x) && !xMismatch) xMismatch = i;
        if (curve.keyframes[i].second.y != doctest::Approx(legacy[i].second.y) && !yMismatch) yMismatch = i;
        if (curve.keyframes[i].second.z != doctest::Approx(legacy[i].second.z) && !zMismatch) zMismatch = i;
    }
    INFO("first keyframe with a mismatched time (if any): ", timeMismatch.value_or(-1));
    CHECK(!timeMismatch.has_value());
    INFO("first keyframe with a mismatched x (if any): ", xMismatch.value_or(-1));
    CHECK(!xMismatch.has_value());
    INFO("first keyframe with a mismatched y (if any): ", yMismatch.value_or(-1));
    CHECK(!yMismatch.has_value());
    INFO("first keyframe with a mismatched z (if any): ", zMismatch.value_or(-1));
    CHECK(!zMismatch.has_value());
}

// No hemisphere-continuity fix applied on either side: canon deliberately
// doesn't apply gltf::enforceHemisphereContinuity (see
// m2_animation_input.hpp's doc comment -- that fix compensates for a
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
    std::optional<size_t> timeMismatch, xMismatch, yMismatch, zMismatch, wMismatch;
    for (size_t i = 0; i < legacy.size(); ++i) {
        if (curve.keyframes[i].first != doctest::Approx(static_cast<float>(legacy[i].first) / 1000.0f) &&
            !timeMismatch) {
            timeMismatch = i;
        }
        if (curve.keyframes[i].second.x != doctest::Approx(legacy[i].second.x) && !xMismatch) xMismatch = i;
        if (curve.keyframes[i].second.y != doctest::Approx(legacy[i].second.y) && !yMismatch) yMismatch = i;
        if (curve.keyframes[i].second.z != doctest::Approx(legacy[i].second.z) && !zMismatch) zMismatch = i;
        if (curve.keyframes[i].second.w != doctest::Approx(legacy[i].second.w) && !wMismatch) wMismatch = i;
    }
    INFO("first rotation keyframe with a mismatched time (if any): ", timeMismatch.value_or(-1));
    CHECK(!timeMismatch.has_value());
    INFO("first rotation keyframe with a mismatched x (if any): ", xMismatch.value_or(-1));
    CHECK(!xMismatch.has_value());
    INFO("first rotation keyframe with a mismatched y (if any): ", yMismatch.value_or(-1));
    CHECK(!yMismatch.has_value());
    INFO("first rotation keyframe with a mismatched z (if any): ", zMismatch.value_or(-1));
    CHECK(!zMismatch.has_value());
    INFO("first rotation keyframe with a mismatched w (if any): ", wMismatch.value_or(-1));
    CHECK(!wMismatch.has_value());
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

TEST_CASE("m2input::assembleBoneAnimation converges with the real resolve*TrackSequence output "
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
        m2input::assembleBoneAnimation(model.blob, bone, pick->boneIndex, static_cast<uint32_t>(pick->sequenceIndex));
    REQUIRE(canonCurves.has_value());

    checkVecCurve(canonCurves->translation, pick->translation, static_cast<uint32_t>(pick->sequenceIndex),
                  "translation");
    checkQuatCurve(canonCurves->rotation, pick->rotation, static_cast<uint32_t>(pick->sequenceIndex));
    checkVecCurve(canonCurves->scale, pick->scale, static_cast<uint32_t>(pick->sequenceIndex), "scale");
    checkInterpolation(model, bone, *canonCurves);
}

TEST_CASE("m2input::assembleBoneAnimation converges with the real resolve*TrackSequence output "
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
        m2input::assembleBoneAnimation(model.blob, bone, pick->boneIndex, static_cast<uint32_t>(pick->sequenceIndex));
    REQUIRE(canonCurves.has_value());

    checkVecCurve(canonCurves->translation, pick->translation, static_cast<uint32_t>(pick->sequenceIndex),
                  "translation");
    checkQuatCurve(canonCurves->rotation, pick->rotation, static_cast<uint32_t>(pick->sequenceIndex));
    checkVecCurve(canonCurves->scale, pick->scale, static_cast<uint32_t>(pick->sequenceIndex), "scale");
    checkInterpolation(model, bone, *canonCurves);
}

namespace {

// One (globalSequenceIndex, bone) pick plus its resolved raw track data --
// the global-sequence-scoped twin of Pick/findBestInlinePick above.
struct GlobalPick {
    uint16_t globalSequenceIndex = 0;
    size_t boneIndex = 0;
    std::vector<std::pair<uint32_t, m2::Vec3>> translation;
    std::vector<std::pair<uint32_t, m2::Quat>> rotation;
    std::vector<std::pair<uint32_t, m2::Vec3>> scale;
};

// Scans every (bone, track-property) with a real global-sequence-driven
// track, scoring each candidate by `score` -- mirrors findBestInlinePick's
// own "highest-scoring real, non-trivial data" search, just over the
// global-sequence-gated resolve*GlobalSequenceTrack family instead of
// resolve*TrackSequence.
template <typename ScoreFn>
std::optional<GlobalPick> findBestGlobalPick(const m2::Model& model, ScoreFn score) {
    std::optional<GlobalPick> best;
    long bestScore = -1;
    for (size_t bi = 0; bi < model.bones.size(); ++bi) {
        const auto& bone = model.bones[bi];
        for (uint32_t off : {bone.translationTrackOffset, bone.rotationTrackOffset, bone.scaleTrackOffset}) {
            uint16_t gs = m2::readTrackMeta(model.blob, off).globalSequence;
            if (gs == m2::TrackMeta::kNoGlobalSequence) continue;

            GlobalPick candidate;
            candidate.globalSequenceIndex = gs;
            candidate.boneIndex = bi;
            if (m2::readTrackMeta(model.blob, bone.translationTrackOffset).globalSequence == gs) {
                candidate.translation = m2::resolveVec3GlobalSequenceTrack(model.blob, bone.translationTrackOffset);
            }
            if (m2::readTrackMeta(model.blob, bone.rotationTrackOffset).globalSequence == gs) {
                candidate.rotation = m2::resolveQuatGlobalSequenceTrack(model.blob, bone.rotationTrackOffset);
            }
            if (m2::readTrackMeta(model.blob, bone.scaleTrackOffset).globalSequence == gs) {
                candidate.scale = m2::resolveVec3GlobalSequenceTrack(model.blob, bone.scaleTrackOffset);
            }
            long s = score(candidate);
            if (s > bestScore) {
                bestScore = s;
                best = std::move(candidate);
            }
        }
    }
    return bestScore > 0 ? best : std::nullopt;
}

}  // namespace

TEST_CASE("m2input::assembleBoneAnimationGlobal converges with the real resolve*GlobalSequenceTrack "
          "output for a real global-sequence-driven bone track, raw M2 space" *
          doctest::skip(test::testM2().empty())) {
    std::ifstream mf(test::testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    auto model = m2::loadModel(fileBytes);
    REQUIRE(!model.bones.empty());

    auto pick = findBestGlobalPick(model, [](const GlobalPick& c) -> long {
        return static_cast<long>(c.translation.size()) + static_cast<long>(c.rotation.size()) +
               static_cast<long>(c.scale.size());
    });
    if (!pick) {
        // Unlike the inline-sequence pick above (bloodelffemale.m2 is known
        // to have real inline keyframe data -- REQUIRE is warranted),
        // global-sequence tracks are a genuinely optional feature (eye
        // glow/torch flicker on some models, absent on others) -- not
        // finding one in this specific fixture isn't a convergence failure,
        // just nothing to check here.
        MESSAGE("no real global-sequence-driven bone track found in this fixture -- skipping");
        return;
    }

    const auto& bone = model.bones[pick->boneIndex];
    auto canonCurves = m2input::assembleBoneAnimationGlobal(model.blob, bone, pick->boneIndex, pick->globalSequenceIndex);
    REQUIRE(canonCurves.has_value());

    CHECK(canonCurves->translation.sequence.kind == canon::SequenceRef::Kind::GlobalSequence);
    CHECK(canonCurves->translation.sequence.index == pick->globalSequenceIndex);
    CHECK(canonCurves->rotation.sequence.kind == canon::SequenceRef::Kind::GlobalSequence);
    CHECK(canonCurves->rotation.sequence.index == pick->globalSequenceIndex);
    CHECK(canonCurves->scale.sequence.kind == canon::SequenceRef::Kind::GlobalSequence);
    CHECK(canonCurves->scale.sequence.index == pick->globalSequenceIndex);

    REQUIRE(canonCurves->translation.keyframes.size() == pick->translation.size());
    for (size_t i = 0; i < pick->translation.size(); ++i) {
        CHECK(canonCurves->translation.keyframes[i].first ==
              doctest::Approx(static_cast<float>(pick->translation[i].first) / 1000.0f));
        CHECK(canonCurves->translation.keyframes[i].second.x == doctest::Approx(pick->translation[i].second.x));
        CHECK(canonCurves->translation.keyframes[i].second.y == doctest::Approx(pick->translation[i].second.y));
        CHECK(canonCurves->translation.keyframes[i].second.z == doctest::Approx(pick->translation[i].second.z));
    }
    REQUIRE(canonCurves->rotation.keyframes.size() == pick->rotation.size());
    for (size_t i = 0; i < pick->rotation.size(); ++i) {
        CHECK(canonCurves->rotation.keyframes[i].first ==
              doctest::Approx(static_cast<float>(pick->rotation[i].first) / 1000.0f));
        CHECK(canonCurves->rotation.keyframes[i].second.x == doctest::Approx(pick->rotation[i].second.x));
        CHECK(canonCurves->rotation.keyframes[i].second.y == doctest::Approx(pick->rotation[i].second.y));
        CHECK(canonCurves->rotation.keyframes[i].second.z == doctest::Approx(pick->rotation[i].second.z));
        CHECK(canonCurves->rotation.keyframes[i].second.w == doctest::Approx(pick->rotation[i].second.w));
    }
    REQUIRE(canonCurves->scale.keyframes.size() == pick->scale.size());
    for (size_t i = 0; i < pick->scale.size(); ++i) {
        CHECK(canonCurves->scale.keyframes[i].first ==
              doctest::Approx(static_cast<float>(pick->scale[i].first) / 1000.0f));
        CHECK(canonCurves->scale.keyframes[i].second.x == doctest::Approx(pick->scale[i].second.x));
        CHECK(canonCurves->scale.keyframes[i].second.y == doctest::Approx(pick->scale[i].second.y));
        CHECK(canonCurves->scale.keyframes[i].second.z == doctest::Approx(pick->scale[i].second.z));
    }
}

TEST_CASE("m2input::assembleBoneAnimationGlobal returns nullopt when a bone has no global-sequence-"
          "driven track data, same nothing-to-animate case as assembleBoneAnimation") {
    std::vector<uint8_t> zeroedBlob(64, 0);
    m2::Bone bone;  // default offsets (0) all point at the same zeroed track
    // Zeroed track's own globalSequence reads as 0 (not kNoGlobalSequence --
    // see assembleBoneAnimation's own "zeroed, not empty" test above), so
    // this picks globalSequenceIndex 0 to exercise the real "referenced but
    // no actual keyframe data" case, not a mismatched-index no-op.
    auto result = m2input::assembleBoneAnimationGlobal(zeroedBlob, bone, 0, 0);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("m2input::assembleBoneAnimation returns nullopt when all three tracks are empty, same "
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
    auto result = m2input::assembleBoneAnimation(zeroedBlob, bone, 0, 0);
    CHECK_FALSE(result.has_value());
}
