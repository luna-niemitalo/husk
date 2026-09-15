// Structural convergence proof for m2input::buildCanonModel (m2_canon_input.hpp)
// -- the M2 input module's own orchestration: which sequences produce a
// clip, which batches share a material (deduped), wiring together every
// already independently-convergence-proven canon:: piece (assembleSkeleton,
// assembleMesh, assembleMaterial, assembleBoneAnimation) for one real M2 +
// .skin fixture. This file checks buildCanonModel's own wiring (sizes,
// index correspondence, filter conditions), not those pieces' internal
// logic -- each already has its own dedicated convergence test. (This file
// used to test canon::assembleModel directly, back when that function did
// this orchestration itself -- see canon_model.hpp's own doc comment for
// why that moved here, to the m2-specific input module, and
// tests/test_canon_model.cpp for the new, much smaller test file covering
// canon::assembleModel's own remaining pure-composition job.)

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <set>
#include <unordered_set>

#include "canon_model.hpp"
#include "m2.hpp"
#include "m2_animation.hpp"
#include "m2_animation_input.hpp"  // assembleBoneAnimation
#include "m2_canon_input.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"
#include "test_m2_fixtures.hpp"  // putU16/putU32/putArray/vec3Bytes

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Minimal one-sequence-slot M2Track<Vec3> at `trackOff`, holding exactly
// one real keyframe at sequence index 0 -- enough to make a bone's
// translation track genuinely non-empty (not the "zeroed, not empty"
// all-nullopt case tests/test_canon_animation_convergence.cpp documents),
// so a test exercising the empty-clip skip (canon_model.cpp's pass 1) can
// also exercise the real "clip has data, gets emitted/reused" path. A
// smaller, single-sequence special case of test_m2_animation_tracks.cpp's
// own putFullTrack, not shared with it since that helper is private to
// that file's own anonymous namespace (this project's own established
// per-TU fixture convention).
void putOneKeyframeTrack(std::vector<uint8_t>& buf, size_t trackOff, uint32_t timestampMs,
                          const m2::Vec3& value) {
    if (buf.size() < trackOff + 0x14) buf.resize(trackOff + 0x14, 0);
    putU16(buf, trackOff + 0x00, 1);       // interpolation_type: linear
    putU16(buf, trackOff + 0x02, 0xFFFF);  // global_sequence: none

    size_t tsOuterOff = buf.size();
    buf.resize(tsOuterOff + 8, 0);  // one M2Array<uint32> entry (sequence 0)
    size_t valOuterOff = buf.size();
    buf.resize(valOuterOff + 8, 0);  // one M2Array<Vec3> entry (sequence 0)

    size_t tsOff = buf.size();
    putU32(buf, buf.size(), timestampMs);
    size_t valOff = buf.size();
    auto bytes = vec3Bytes(value);
    buf.insert(buf.end(), bytes.begin(), bytes.end());

    putArray(buf, tsOuterOff, 1, static_cast<uint32_t>(tsOff));
    putArray(buf, valOuterOff, 1, static_cast<uint32_t>(valOff));
    putArray(buf, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(buf, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));
}

// Mirrors canon_model.cpp's own private constants/helpers -- computing an
// expected fact via the same lower-level m2:: production functions
// assembleModel itself calls, never by calling assembleModel back at
// itself. Same convention this file already established for
// kSequenceStoredInlineFlag/expectedInlineCount below.
constexpr uint32_t kSequenceStoredInlineFlag = 0x20;
constexpr uint32_t kSequenceAliasFlag = 0x40;

// Same bounded-hop resolution as canon_model.cpp's own resolveAliasChain,
// but returns `sequences.size()` (an always-invalid sentinel) instead of
// throwing on a cycle/out-of-range hop -- a test computing an *expected*
// count has no real corrupted-data case to react to here, only real
// fixtures already loaded successfully elsewhere.
size_t resolveAliasChain(const std::vector<m2::Sequence>& sequences, size_t startIndex) {
    size_t cur = startIndex;
    for (size_t hop = 0; hop <= sequences.size(); ++hop) {
        if ((sequences[cur].flags & kSequenceAliasFlag) == 0) return cur;
        uint16_t next = sequences[cur].aliasNext;
        if (next >= sequences.size()) return sequences.size();
        cur = next;
    }
    return sequences.size();
}

// Every distinct global-sequence index referenced by any bone track that
// ALSO has at least one bone with real (non-empty) resolved data for it --
// independently mirrors canon_model.cpp's own globalSequenceIndices +
// per-clip anyBoneAnimated gating, via the same m2::readTrackMeta/
// resolve*GlobalSequenceTrack production functions assembleModel itself
// calls.
std::set<uint16_t> expectedGlobalSequenceClips(const m2::Model& model) {
    std::set<uint16_t> referenced;
    for (const auto& bone : model.bones) {
        for (uint32_t off : {bone.translationTrackOffset, bone.rotationTrackOffset, bone.scaleTrackOffset}) {
            uint16_t gs = m2::readTrackMeta(model.blob, off).globalSequence;
            if (gs != m2::TrackMeta::kNoGlobalSequence) referenced.insert(gs);
        }
    }
    std::set<uint16_t> withData;
    for (uint16_t gs : referenced) {
        for (const auto& bone : model.bones) {
            bool any = false;
            if (m2::readTrackMeta(model.blob, bone.translationTrackOffset).globalSequence == gs) {
                any |= !m2::resolveVec3GlobalSequenceTrack(model.blob, bone.translationTrackOffset).empty();
            }
            if (m2::readTrackMeta(model.blob, bone.rotationTrackOffset).globalSequence == gs) {
                any |= !m2::resolveQuatGlobalSequenceTrack(model.blob, bone.rotationTrackOffset).empty();
            }
            if (m2::readTrackMeta(model.blob, bone.scaleTrackOffset).globalSequence == gs) {
                any |= !m2::resolveVec3GlobalSequenceTrack(model.blob, bone.scaleTrackOffset).empty();
            }
            if (any) {
                withData.insert(gs);
                break;
            }
        }
    }
    return withData;
}

// True when at least one bone has real (non-empty) resolved translation/
// rotation/scale data for sequence index `si` -- independently mirrors
// canon_model.cpp pass 1's own anyBoneAnimated gate (the empty-clip skip),
// via the same m2::resolve*TrackSequence production functions
// assembleBoneAnimation itself calls.
bool sequenceHasRealBoneData(const m2::Model& model, size_t si) {
    for (const auto& bone : model.bones) {
        if (!m2::resolveVec3TrackSequence(model.blob, bone.translationTrackOffset, static_cast<uint32_t>(si))
                 .empty()) {
            return true;
        }
        if (!m2::resolveQuatTrackSequence(model.blob, bone.rotationTrackOffset, static_cast<uint32_t>(si))
                 .empty()) {
            return true;
        }
        if (!m2::resolveVec3TrackSequence(model.blob, bone.scaleTrackOffset, static_cast<uint32_t>(si)).empty()) {
            return true;
        }
    }
    return false;
}

// Same track-metadata shape as putOneKeyframeTrack above, but splits where
// the bytes live: the M2Track struct/ranges/inner-Array descriptors stay in
// `modelBlob` (resolveTrackGeneric, m2_animation.cpp, always reads those
// from `blob`), while the actual timestamp/value bytes the descriptors
// point at go into `externalBlob` instead, with their offsets relative to
// `externalBlob`'s own buffer -- mirrors the real inline-struct/external-
// bytes split an AFSB-sourced external .anim file has (buildAnimations's
// own `loadedAnimBlob`, export_animation.cpp), just synthetic.
void putOneKeyframeTrackExternal(std::vector<uint8_t>& modelBlob, std::vector<uint8_t>& externalBlob,
                                  size_t trackOff, uint32_t timestampMs, const m2::Vec3& value) {
    if (modelBlob.size() < trackOff + 0x14) modelBlob.resize(trackOff + 0x14, 0);
    putU16(modelBlob, trackOff + 0x00, 1);       // interpolation_type: linear
    putU16(modelBlob, trackOff + 0x02, 0xFFFF);  // global_sequence: none

    size_t tsOuterOff = modelBlob.size();
    modelBlob.resize(tsOuterOff + 8, 0);  // one M2Array<uint32> entry (sequence 0)
    size_t valOuterOff = modelBlob.size();
    modelBlob.resize(valOuterOff + 8, 0);  // one M2Array<Vec3> entry (sequence 0)

    size_t tsOff = externalBlob.size();
    putU32(externalBlob, externalBlob.size(), timestampMs);
    size_t valOff = externalBlob.size();
    auto bytes = vec3Bytes(value);
    externalBlob.insert(externalBlob.end(), bytes.begin(), bytes.end());

    putArray(modelBlob, tsOuterOff, 1, static_cast<uint32_t>(tsOff));
    putArray(modelBlob, valOuterOff, 1, static_cast<uint32_t>(valOff));
    putArray(modelBlob, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(modelBlob, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));
}

}  // namespace

TEST_CASE("m2input::buildCanonModel wires together skeleton/mesh/materials/animations on a real "
          "fixture" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    REQUIRE(!model.bones.empty());
    REQUIRE(!batches.empty());

    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, triangleIndices);

    CHECK(result.skeleton.joints.size() == model.bones.size());

    // primitiveMaterials.size() == mesh.primitives.size(): the real
    // correspondence buildCanonModel maintains (canon_model.hpp's own
    // Model::primitiveMaterials doc comment) -- every primitiveMaterials
    // entry must also resolve to a real materials[] entry via
    // canon::resolveMaterialIndex, and materials itself must be genuinely
    // DEDUPED (fewer entries than primitives) on this real fixture, not
    // the old false "1:1" assumption this test used to check before
    // dedup existed (REFACTOR/AUDIT.md §7's material-dedup finding).
    CHECK(result.primitiveMaterials.size() == result.mesh.primitives.size());
    CHECK(result.materials.size() < result.mesh.primitives.size());
    std::optional<size_t> unresolvedPrimitive;
    for (size_t i = 0; i < result.mesh.primitives.size(); ++i) {
        if (!canon::resolveMaterialIndex(result, i).has_value() && !unresolvedPrimitive) {
            unresolvedPrimitive = i;
        }
    }
    CHECK_FALSE(unresolvedPrimitive.has_value());

    // Independently counted real inline-sequence count -- same bit test
    // assembleModel itself uses (canon_model.cpp's isRealInlineSequence),
    // counted here from the raw parsed sequence array rather than by
    // reading result.animations.size() back at itself. Also gated on
    // sequenceHasRealBoneData: a real-inline sequence with no bone carrying
    // any actual keyframe data for it produces no clip (canon_model.cpp
    // pass 1's empty-clip skip, mirroring commands::buildAnimations's own
    // `if (!anim.joints.empty())` gate) -- confirmed as a real, non-empty
    // case on this fixture, not a hypothetical (see REFACTOR/AUDIT.md §7.2).
    size_t expectedInlineCount = 0;
    std::unordered_set<size_t> inlineIndicesWithData;
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        if ((model.sequences[si].flags & kSequenceStoredInlineFlag) == 0) continue;
        if (!sequenceHasRealBoneData(model, si)) continue;
        ++expectedInlineCount;
        inlineIndicesWithData.insert(si);
    }

    // Independently counted pure-alias sequences whose alias chain
    // terminates at a real-inline sequence that ITSELF has real bone data
    // -- same resolveAliasChain shape canon_model.cpp's own pass 3 uses,
    // computed here via the sentinel-returning test helper above instead
    // of assembleModel's own private one. A terminal with no real data
    // isn't in inlineIndicesWithData at all, so its aliases are correctly
    // excluded here too, same "nothing to reuse" case pass 3 itself skips.
    size_t expectedAliasCount = 0;
    for (size_t si = 0; si < model.sequences.size(); ++si) {
        const auto& seq = model.sequences[si];
        bool pureAlias = (seq.flags & kSequenceStoredInlineFlag) == 0 && (seq.flags & kSequenceAliasFlag) != 0;
        if (!pureAlias) continue;
        if (inlineIndicesWithData.count(resolveAliasChain(model.sequences, si)) != 0) ++expectedAliasCount;
    }

    size_t expectedGlobalCount = expectedGlobalSequenceClips(model).size();

    CHECK(result.animations.size() == expectedInlineCount + expectedAliasCount + expectedGlobalCount);

    // Every clip's SequenceRef::Kind falls into exactly one of the three
    // independently-counted buckets above -- confirms the total isn't
    // matching by coincidence (e.g. inline count and alias count silently
    // swapped).
    size_t actualInline = 0, actualGlobal = 0;
    for (const auto& clip : result.animations) {
        if (clip.sequence.kind == canon::SequenceRef::Kind::Sequence) {
            ++actualInline;
        } else {
            ++actualGlobal;
        }
    }
    CHECK(actualInline == expectedInlineCount + expectedAliasCount);
    CHECK(actualGlobal == expectedGlobalCount);

    if (!result.animations.empty()) {
        const auto& clip = result.animations.front();
        CHECK(clip.boneCurves.size() == result.skeleton.joints.size());

        // Spot-check bone 0's curves for this clip against
        // assembleBoneAnimation's own already-proven output directly --
        // reusing a proven fact, not deriving from the function under test.
        auto expected = m2input::assembleBoneAnimation(model.blob, model.bones[0], 0, clip.sequence.index);
        REQUIRE(clip.boneCurves[0].has_value() == expected.has_value());
        if (expected.has_value()) {
            CHECK(clip.boneCurves[0]->translation.keyframes.size() == expected->translation.keyframes.size());
            CHECK(clip.boneCurves[0]->rotation.keyframes.size() == expected->rotation.keyframes.size());
            CHECK(clip.boneCurves[0]->scale.keyframes.size() == expected->scale.keyframes.size());
        }
    }
}

TEST_CASE("m2input::buildCanonModel throws on a corrupted bone index, mirroring assembleMesh") {
    m2::Model model;
    model.bones.resize(2);
    model.vertices.resize(1);
    model.vertices[0].boneWeights[0] = 255;
    model.vertices[0].boneIndices[0] = 9;  // out of range for 2 bones

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    CHECK_THROWS_AS(m2input::buildCanonModel(model, batches, submeshes, {}), std::runtime_error);
}

TEST_CASE("m2input::buildCanonModel resolves a pure-alias (non-inline) sequence to its real-inline "
          "terminal's already-assembled boneCurves, tagged under the alias's own index") {
    m2::Model model;
    model.bones.resize(1);
    model.blob.assign(64, 0);
    // Bone 0's translation track carries one real keyframe at sequence
    // index 0 -- pass 1 (canon_model.cpp) now skips a sequence with no
    // real bone data at all (the empty-clip fix), so the inline sequence
    // needs genuine data for this test to exercise the alias-reuse path
    // rather than the "nothing to reuse" one.
    size_t trackOff = model.blob.size();
    putOneKeyframeTrack(model.blob, trackOff, 0, {1, 2, 3});
    model.bones[0].translationTrackOffset = static_cast<uint32_t>(trackOff);
    model.vertices.clear();

    m2::Sequence inlineSeq;
    inlineSeq.flags = kSequenceStoredInlineFlag;
    m2::Sequence aliasSeq;
    aliasSeq.flags = kSequenceAliasFlag;  // no inline bit -- pure alias
    aliasSeq.aliasNext = 0;               // -> inlineSeq (index 0)
    model.sequences = {inlineSeq, aliasSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, {});
    REQUIRE(result.animations.size() == 2);
    CHECK(result.animations[0].sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(result.animations[0].sequence.index == 0);
    CHECK(result.animations[1].sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(result.animations[1].sequence.index == 1);
    // The alias clip's boneCurves is the SAME already-assembled data pass 1
    // built for the inline terminal -- not re-derived, not merely
    // same-shaped. Confirms real keyframe data actually made it across.
    REQUIRE(result.animations[1].boneCurves.size() == result.animations[0].boneCurves.size());
    REQUIRE(result.animations[0].boneCurves[0].has_value());
    REQUIRE(result.animations[1].boneCurves[0].has_value());
    REQUIRE(result.animations[1].boneCurves[0]->translation.keyframes.size() == 1);
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.x == doctest::Approx(1));
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.y == doctest::Approx(2));
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.z == doctest::Approx(3));
}

TEST_CASE("m2input::buildCanonModel skips a real-inline sequence entirely when no bone has any real "
          "keyframe data for it, mirroring commands::buildAnimations's own "
          "'if (!anim.joints.empty())' gate") {
    m2::Model model;
    model.bones.resize(1);  // default (zero) track offsets all point at the zeroed blob below --
                             // the real "no data at all" case, per test_canon_animation_convergence.cpp's
                             // own "zeroed, not empty" note.
    model.blob.assign(64, 0);
    model.vertices.clear();

    m2::Sequence inlineSeq;
    inlineSeq.flags = kSequenceStoredInlineFlag;
    model.sequences = {inlineSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, {});
    CHECK(result.animations.empty());
}

TEST_CASE("m2input::buildCanonModel does not emit a clip for a pure-alias sequence whose terminal "
          "isn't real-inline (external-.anim-only, out of canon's scope)") {
    m2::Model model;
    model.bones.resize(1);
    model.blob.assign(64, 0);
    model.vertices.clear();

    m2::Sequence externalSeq;  // neither kSequenceStoredInlineFlag nor kSequenceAliasFlag
    m2::Sequence aliasSeq;
    aliasSeq.flags = kSequenceAliasFlag;
    aliasSeq.aliasNext = 0;  // -> externalSeq (index 0), not real-inline
    model.sequences = {externalSeq, aliasSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, {});
    CHECK(result.animations.empty());
}

TEST_CASE("m2input::buildCanonModel with an empty externalAnimBlobs map behaves exactly like the "
          "4-argument overload (regression guard for the new defaulted parameter)") {
    m2::Model model;
    model.bones.resize(1);
    model.blob.assign(64, 0);
    model.vertices.clear();

    m2::Sequence externalSeq;  // neither inline nor alias -- unresolvable without a blob
    m2::Sequence aliasSeq;
    aliasSeq.flags = kSequenceAliasFlag;
    aliasSeq.aliasNext = 0;  // -> externalSeq, still not real-inline
    model.sequences = {externalSeq, aliasSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    canon::Model implicit = m2input::buildCanonModel(model, batches, submeshes, {});
    canon::Model explicitEmpty = m2input::buildCanonModel(model, batches, submeshes, {}, m2input::ExternalAnimBlobs{});
    CHECK(implicit.animations.size() == explicitEmpty.animations.size());
    CHECK(implicit.animations.empty());
}

TEST_CASE("m2input::buildCanonModel resolves a non-inline, non-alias sequence against a "
          "caller-supplied external .anim blob, producing a real clip with real keyframe data") {
    m2::Model model;
    model.bones.resize(1);
    model.blob.assign(64, 0);
    model.vertices.clear();

    std::vector<uint8_t> externalBlob;
    size_t trackOff = model.blob.size();
    putOneKeyframeTrackExternal(model.blob, externalBlob, trackOff, 500, {4, 5, 6});
    model.bones[0].translationTrackOffset = static_cast<uint32_t>(trackOff);

    m2::Sequence externalSeq;  // flags == 0: neither kSequenceStoredInlineFlag nor kSequenceAliasFlag
    model.sequences = {externalSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    // Sanity check first: with no externalAnimBlobs entry at all, this
    // sequence stays unresolvable (today's pre-existing behavior) -- proves
    // the clip below genuinely comes from the supplied blob, not from some
    // other accidental resolution path.
    canon::Model withoutBlob = m2input::buildCanonModel(model, batches, submeshes, {});
    CHECK(withoutBlob.animations.empty());

    m2input::ExternalAnimBlobs blobs;
    blobs.emplace(0, externalBlob);
    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, {}, blobs);

    REQUIRE(result.animations.size() == 1);
    CHECK(result.animations[0].sequence.kind == canon::SequenceRef::Kind::Sequence);
    CHECK(result.animations[0].sequence.index == 0);
    REQUIRE(result.animations[0].boneCurves.size() == 1);
    REQUIRE(result.animations[0].boneCurves[0].has_value());
    REQUIRE(result.animations[0].boneCurves[0]->translation.keyframes.size() == 1);
    CHECK(result.animations[0].boneCurves[0]->translation.keyframes[0].first == doctest::Approx(0.5));
    CHECK(result.animations[0].boneCurves[0]->translation.keyframes[0].second.x == doctest::Approx(4));
    CHECK(result.animations[0].boneCurves[0]->translation.keyframes[0].second.y == doctest::Approx(5));
    CHECK(result.animations[0].boneCurves[0]->translation.keyframes[0].second.z == doctest::Approx(6));
}

TEST_CASE("m2input::buildCanonModel resolves a pure-alias sequence whose terminal is "
          "external-.anim-sourced (not real-inline) to that terminal's already-assembled "
          "boneCurves, tagged under the alias's own index") {
    m2::Model model;
    model.bones.resize(1);
    model.blob.assign(64, 0);
    model.vertices.clear();

    std::vector<uint8_t> externalBlob;
    size_t trackOff = model.blob.size();
    putOneKeyframeTrackExternal(model.blob, externalBlob, trackOff, 250, {7, 8, 9});
    model.bones[0].translationTrackOffset = static_cast<uint32_t>(trackOff);

    m2::Sequence externalSeq;  // flags == 0: external-.anim-only terminal, not real-inline
    m2::Sequence aliasSeq;
    aliasSeq.flags = kSequenceAliasFlag;  // no inline bit -- pure alias
    aliasSeq.aliasNext = 0;               // -> externalSeq (index 0)
    model.sequences = {externalSeq, aliasSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    m2input::ExternalAnimBlobs blobs;
    blobs.emplace(0, externalBlob);  // keyed by the TERMINAL's index, not the alias's
    canon::Model result = m2input::buildCanonModel(model, batches, submeshes, {}, blobs);

    REQUIRE(result.animations.size() == 2);
    CHECK(result.animations[0].sequence.index == 0);  // the terminal's own clip
    CHECK(result.animations[1].sequence.index == 1);  // the alias's own clip, reusing it
    REQUIRE(result.animations[1].boneCurves.size() == result.animations[0].boneCurves.size());
    REQUIRE(result.animations[1].boneCurves[0].has_value());
    REQUIRE(result.animations[1].boneCurves[0]->translation.keyframes.size() == 1);
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.x == doctest::Approx(7));
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.y == doctest::Approx(8));
    CHECK(result.animations[1].boneCurves[0]->translation.keyframes[0].second.z == doctest::Approx(9));
}
