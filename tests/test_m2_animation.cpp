// Tests for husk::m2's animation module (src/m2_animation.hpp/.cpp): Sequence/track-and-curve resolvers/extractAnimBlob.
// Split out of the former tests/test_m2.cpp -- see FILE_SPLIT_TODO.md Item 5.

#include "test_m2_fixtures.hpp"

// M2Sequence (wowdev.wiki M2#Animation_sequences), 0x40 bytes: 0x00 id
// (u16), 0x02 variationIndex (u16), 0x04 duration (u32), 0x0C flags (u32)
// -- movespeed/frequency/replay/blendTime/bounds/variationNext/aliasNext
// left zeroed here since this helper is only used for the 4-field
// happy-path/throw tests below; putSequenceFull (below) covers every
// field. The 0x40 stride itself (not just these 4 fields) is the part
// worth getting right -- see Sequence's doc comment in m2.hpp for the
// real-data story of why it's 64 bytes, not the 36 a naive reading of the
// wiki struct listing suggests.
// TODO: Remove: former M2_GAPS_TODO.md Item 1 citation.
void putSequence(std::vector<uint8_t>& buf, size_t off, uint16_t id, uint16_t variationIndex,
                  uint32_t duration, uint32_t flags) {
    if (buf.size() < off + 0x40) buf.resize(off + 0x40, 0);
    putU16(buf, off + 0x00, id);
    putU16(buf, off + 0x02, variationIndex);
    putU32(buf, off + 0x04, duration);
    putU32(buf, off + 0x0C, flags);
}

TEST_CASE("parseColors: an unambiguously constant track (one sub-array, one keyframe) resolves") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({0.5f, 0.25f, 0.75f})}},
              /*alphaSubArrays=*/{{fixed16Bytes(16384)}});  // ~0.5

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    REQUIRE(colors[0].color.has_value());
    CHECK(colors[0].color->x == doctest::Approx(0.5f));
    CHECK(colors[0].color->y == doctest::Approx(0.25f));
    CHECK(colors[0].color->z == doctest::Approx(0.75f));
    REQUIRE(colors[0].alpha.has_value());
    CHECK(*colors[0].alpha == doctest::Approx(16384.0f / 32767.0f));
    // colorTrackOffset/alphaTrackOffset are always set,
    // even for an unambiguously constant track -- a caller only consults
    // them when *Animated is true, but they're set unconditionally so
    // there's no separate "did this get populated" state to track.
    CHECK(colors[0].colorTrackOffset == off + 0x00);
    CHECK(colors[0].alphaTrackOffset == off + 0x14);
}


TEST_CASE(
    "parseColors: more than one animation sub-array means real per-sequence animation, not a "
    "static value -- regression test for a real bug found against bloodelffemale.m2, where "
    "naively reading element [0][0] picked up sequence 0's alpha keyframe (0, fully "
    "transparent) as if it were a sensible default and would have rendered the model invisible") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({1, 1, 1})}, {vec3Bytes({1, 1, 1})}},
              /*alphaSubArrays=*/{{fixed16Bytes(0)}, {fixed16Bytes(32767)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].alpha.has_value());
}


TEST_CASE("parseColors: more than one keyframe in an otherwise-single sub-array is also treated "
          "as animated, not a static value") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off,
              /*colorSubArrays=*/{{vec3Bytes({1, 1, 1})}},
              /*alphaSubArrays=*/{{fixed16Bytes(0), fixed16Bytes(32767)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].alpha.has_value());
}


TEST_CASE("parseColors: a track with no sub-arrays at all has no value") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putColor(blob, off, /*colorSubArrays=*/{}, /*alphaSubArrays=*/{});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK_FALSE(colors[0].color.has_value());
    CHECK_FALSE(colors[0].alpha.has_value());
}


TEST_CASE("parseColors: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 12345;
    CHECK(husk::m2::parseColors(blob, array).empty());
}


TEST_CASE("parseColors: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x28 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseColors(blob, array), husk::m2::ParseError);
}


TEST_CASE("parseTextureWeights: an unambiguously constant track resolves") {
    size_t off = 500;
    std::vector<uint8_t> blob(off, 0);
    putTextureWeight(blob, off, {{fixed16Bytes(6553)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto weights = husk::m2::parseTextureWeights(blob, array);

    REQUIRE(weights.size() == 1);
    REQUIRE(weights[0].weight.has_value());
    CHECK(*weights[0].weight == doctest::Approx(6553.0f / 32767.0f));
    // weightTrackOffset, always set -- see parseColors'
    // matching colorTrackOffset/alphaTrackOffset test above.
    CHECK(weights[0].weightTrackOffset == off + 0x00);
}


TEST_CASE("parseTextureWeights: an animated (multi-sub-array) track has no value") {
    size_t off = 500;
    std::vector<uint8_t> blob(off, 0);
    putTextureWeight(blob, off, {{fixed16Bytes(32767)}, {fixed16Bytes(0)}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto weights = husk::m2::parseTextureWeights(blob, array);

    REQUIRE(weights.size() == 1);
    CHECK_FALSE(weights[0].weight.has_value());
}


TEST_CASE("parseTextureWeights: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 999;
    CHECK(husk::m2::parseTextureWeights(blob, array).empty());
}


TEST_CASE("parseTextureWeights: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(5, 0);  // 0x14 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextureWeights(blob, array), husk::m2::ParseError);
}


TEST_CASE("parseTextureTransforms: an unambiguously constant transform (translation/rotation/"
          "scaling all one sub-array, one keyframe) resolves all three") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off,
                         /*translationSubArrays=*/{{vec3Bytes({0.1f, 0.2f, 0.0f})}},
                         /*rotationSubArrays=*/{{quatFloatBytes(0, 0, 0.7071f, 0.7071f)}},
                         /*scalingSubArrays=*/{{vec3Bytes({2, 2, 1})}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    REQUIRE(transforms[0].translation.has_value());
    CHECK(transforms[0].translation->x == doctest::Approx(0.1f));
    CHECK(transforms[0].translation->y == doctest::Approx(0.2f));
    REQUIRE(transforms[0].rotation.has_value());
    CHECK(transforms[0].rotation->z == doctest::Approx(0.7071f));
    CHECK(transforms[0].rotation->w == doctest::Approx(0.7071f));
    REQUIRE(transforms[0].scaling.has_value());
    CHECK(transforms[0].scaling->x == doctest::Approx(2.0f));
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK_FALSE(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scalingAnimated);
}


TEST_CASE("parseTextureTransforms: a genuinely animated (multi-sub-array) rotation track has no "
          "value and is flagged animated, independent of the other two (still-constant) tracks") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off,
                         /*translationSubArrays=*/{{vec3Bytes({0, 0, 0})}},
                         /*rotationSubArrays=*/
                         {{quatFloatBytes(0, 0, 0, 1)}, {quatFloatBytes(0, 0, 1, 0)}},
                         /*scalingSubArrays=*/{{vec3Bytes({1, 1, 1})}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    CHECK(transforms[0].translation.has_value());
    CHECK_FALSE(transforms[0].rotation.has_value());
    CHECK(transforms[0].scaling.has_value());
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scalingAnimated);
}


TEST_CASE("parseTextureTransforms: a track with no sub-arrays at all has no value and is not "
          "flagged animated (genuinely empty, not dropped animation)") {
    size_t off = 1000;
    std::vector<uint8_t> blob(off, 0);
    putTextureTransform(blob, off, {}, {}, {});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto transforms = husk::m2::parseTextureTransforms(blob, array);

    REQUIRE(transforms.size() == 1);
    CHECK_FALSE(transforms[0].translation.has_value());
    CHECK_FALSE(transforms[0].translationAnimated);
    CHECK_FALSE(transforms[0].rotation.has_value());
    CHECK_FALSE(transforms[0].rotationAnimated);
    CHECK_FALSE(transforms[0].scaling.has_value());
    CHECK_FALSE(transforms[0].scalingAnimated);
}


TEST_CASE("parseTextureTransforms: empty array returns an empty vector without touching the "
          "blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseTextureTransforms(blob, array).empty());
}


TEST_CASE("parseTextureTransforms: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x3C bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextureTransforms(blob, array), husk::m2::ParseError);
}


TEST_CASE("parseSequences: reads id/variationIndex/duration/flags for every entry") {
    std::vector<uint8_t> blob(200, 0);
    putSequence(blob, 200, 100, 0, 5000, 0x20);
    putSequence(blob, 200 + 0x40, 101, 1, 3200, 0);

    husk::m2::Array array;
    array.count = 2;
    array.offset = 200;
    auto sequences = husk::m2::parseSequences(blob, array);

    REQUIRE(sequences.size() == 2);
    CHECK(sequences[0].id == 100);
    CHECK(sequences[0].variationIndex == 0);
    CHECK(sequences[0].duration == 5000);
    CHECK(sequences[0].flags == 0x20);
    CHECK(sequences[1].id == 101);
    CHECK(sequences[1].variationIndex == 1);
    CHECK(sequences[1].duration == 3200);
    CHECK(sequences[1].flags == 0);
}

// Writes every field of one M2Sequence record, at the exact offsets
// m2.hpp's Sequence doc comment gives: /*0x08*/ movespeed, /*0x10*/
// frequency, /*0x14*/ replay (2x u32), /*0x1C*/ blendTimeIn/Out, /*0x20*/
// bounds (2x Vec3), /*0x38*/ boundsRadius, /*0x3C*/ variationNext,
// /*0x3E*/ aliasNext.
// TODO: Remove: former M2_GAPS_TODO.md Item 1 citation.
void putSequenceFull(std::vector<uint8_t>& buf, size_t off, uint16_t id, uint16_t variationIndex,
                      uint32_t duration, uint32_t flags, float movespeed, int16_t frequency,
                      uint32_t replayMin, uint32_t replayMax, uint16_t blendTimeIn,
                      uint16_t blendTimeOut, float boundsRadius, int16_t variationNext,
                      uint16_t aliasNext) {
    if (buf.size() < off + 0x40) buf.resize(off + 0x40, 0);
    putU16(buf, off + 0x00, id);
    putU16(buf, off + 0x02, variationIndex);
    putU32(buf, off + 0x04, duration);
    std::memcpy(buf.data() + off + 0x08, &movespeed, 4);
    putU32(buf, off + 0x0C, flags);
    std::memcpy(buf.data() + off + 0x10, &frequency, 2);
    putU32(buf, off + 0x14, replayMin);
    putU32(buf, off + 0x18, replayMax);
    putU16(buf, off + 0x1C, blendTimeIn);
    putU16(buf, off + 0x1E, blendTimeOut);
    float boundsVals[6] = {1, 2, 3, 4, 5, 6};  // min (1,2,3), max (4,5,6)
    std::memcpy(buf.data() + off + 0x20, boundsVals, sizeof(boundsVals));
    std::memcpy(buf.data() + off + 0x38, &boundsRadius, 4);
    std::memcpy(buf.data() + off + 0x3C, &variationNext, 2);
    putU16(buf, off + 0x3E, aliasNext);
}


TEST_CASE("parseSequences: reads every M2Sequence field at its real offset") {
    std::vector<uint8_t> blob(200, 0);
    putSequenceFull(blob, 200, 100, 0, 5000, 0x20, 4.5f, -3, 10, 20, 30, 40, 7.5f, -1, 0);

    husk::m2::Array array;
    array.count = 1;
    array.offset = 200;
    auto sequences = husk::m2::parseSequences(blob, array);

    REQUIRE(sequences.size() == 1);
    const auto& s = sequences[0];
    CHECK(s.movespeed == doctest::Approx(4.5f));
    CHECK(s.frequency == -3);
    CHECK(s.replay.minimum == 10);
    CHECK(s.replay.maximum == 20);
    CHECK(s.blendTimeIn == 30);
    CHECK(s.blendTimeOut == 40);
    CHECK(s.bounds.min.x == doctest::Approx(1));
    CHECK(s.bounds.min.y == doctest::Approx(2));
    CHECK(s.bounds.min.z == doctest::Approx(3));
    CHECK(s.bounds.max.x == doctest::Approx(4));
    CHECK(s.bounds.max.y == doctest::Approx(5));
    CHECK(s.bounds.max.z == doctest::Approx(6));
    CHECK(s.boundsRadius == doctest::Approx(7.5f));
    CHECK(s.variationNext == -1);
    CHECK(s.aliasNext == 0);
}


TEST_CASE("parseSequences: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 54321;
    CHECK(husk::m2::parseSequences(blob, array).empty());
}


TEST_CASE("parseSequences: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    husk::m2::Array array;
    array.count = 1;   // 0x40 bytes needed
    array.offset = 0;  // but the blob is only 20 bytes
    CHECK_THROWS_AS(husk::m2::parseSequences(blob, array), husk::m2::ParseError);
}

// Builds a *full* M2Track<T> at `trackOff` -- both the `timestamps`
// (trackOff+0x04) and `values` (trackOff+0x0C) nested M2Array<M2Array<>>
// fields, in lockstep -- from per-sequence keyframe lists: sequences[i] is
// the (timestamp ms, raw value bytes) list for animation sub-array i (empty
// means that sequence's own M2Arrays both have count 0). Unlike
// putTrackValues (above, used for the parseColors/parseTextureWeights
// "unambiguously constant" tests, which never read timestamps at all),
// this is for resolveVec3TrackSequence/resolveQuatTrackSequence, which do.
void putFullTrack(std::vector<uint8_t>& buf, size_t trackOff,
                   const std::vector<std::vector<std::pair<uint32_t, std::vector<uint8_t>>>>& sequences) {
    if (buf.size() < trackOff + 0x14) buf.resize(trackOff + 0x14, 0);
    // M2TrackBase's own header (wowdev.wiki "Standard animation block"):
    // interpolation_type=1 (linear, the ordinary case) and
    // global_sequence=0xFFFF ("none" -- see m2::TrackMeta::kNoGlobalSequence).
    // A real per-sequence-indexed track (what every fixture below models)
    // always has the latter -- leaving this zero-filled would make it look
    // like a global-sequence track instead, which resolveVec3TrackSequence/
    // resolveQuatTrackSequence now (correctly) refuse to resolve by
    // sequence index at all.
    putU16(buf, trackOff + 0x00, 1);
    putU16(buf, trackOff + 0x02, 0xFFFF);

    size_t tsOuterOff = buf.size();
    buf.resize(tsOuterOff + sequences.size() * 8, 0);
    size_t valOuterOff = buf.size();
    buf.resize(valOuterOff + sequences.size() * 8, 0);

    for (size_t i = 0; i < sequences.size(); ++i) {
        const auto& kfs = sequences[i];
        if (kfs.empty()) {
            putArray(buf, tsOuterOff + i * 8, 0, 0);
            putArray(buf, valOuterOff + i * 8, 0, 0);
            continue;
        }
        size_t tsOff = buf.size();
        for (const auto& kf : kfs) putU32(buf, buf.size(), kf.first);
        size_t valOff = buf.size();
        for (const auto& kf : kfs) buf.insert(buf.end(), kf.second.begin(), kf.second.end());

        putArray(buf, tsOuterOff + i * 8, static_cast<uint32_t>(kfs.size()),
                 static_cast<uint32_t>(tsOff));
        putArray(buf, valOuterOff + i * 8, static_cast<uint32_t>(kfs.size()),
                 static_cast<uint32_t>(valOff));
    }

    putArray(buf, trackOff + 0x04, static_cast<uint32_t>(sequences.size()),
             static_cast<uint32_t>(tsOuterOff));
    putArray(buf, trackOff + 0x0C, static_cast<uint32_t>(sequences.size()),
             static_cast<uint32_t>(valOuterOff));
}

TEST_CASE("resolveVec3TrackSequence: reads timestamp/value keyframe pairs for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Sequence 0: 2 keyframes. Sequence 1: 1 keyframe.
    putFullTrack(blob, trackOff,
                 {
                     {{0, vec3Bytes({1, 2, 3})}, {1000, vec3Bytes({4, 5, 6})}},
                     {{500, vec3Bytes({7, 8, 9})}},
                 });

    auto seq0 = husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 2);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second.x == doctest::Approx(1));
    CHECK(seq0[0].second.z == doctest::Approx(3));
    CHECK(seq0[1].first == 1000);
    CHECK(seq0[1].second.y == doctest::Approx(5));

    auto seq1 = husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(seq1.size() == 1);
    CHECK(seq1[0].first == 500);
    CHECK(seq1[0].second.x == doctest::Approx(7));
}


TEST_CASE("resolveVec3TrackSequence: a sequence index with no inline data (count 0) is empty, not "
          "an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Sequence 0 has real data; sequence 1 has none inline (e.g. its real
    // data lives in an external .anim file husk doesn't parse).
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 1, 1})}}, {}});

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1).empty());
}


TEST_CASE("readTrackMeta: reads interpolation_type/global_sequence, per wowdev.wiki's "
          "M2TrackBase") {
    std::vector<uint8_t> blob(20, 0);
    uint16_t interp = 3;
    uint16_t globalSeq = 42;
    std::memcpy(blob.data() + 0x00, &interp, 2);
    std::memcpy(blob.data() + 0x02, &globalSeq, 2);

    auto meta = husk::m2::readTrackMeta(blob, 0);
    CHECK(meta.interpolationType == 3);
    CHECK(meta.globalSequence == 42);
}


TEST_CASE("resolveVec3TrackSequence: a track with a real global_sequence (not the 0xFFFF \"none\" "
          "sentinel) resolves to empty for every sequence index -- it must not be silently "
          "attributed to whichever M2Sequence occupies outer-array position 0") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});
    // Override putFullTrack's default "none" sentinel with a real global
    // sequence index (7) -- same shape a genuine glow-pulse/idle-loop track
    // would have, per wowdev.wiki's "Global Sequences" section.
    uint16_t globalSeq = 7;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 1).empty());
}


TEST_CASE("resolveQuatTrackSequence: a track with a real global_sequence also resolves to empty, "
          "not misattributed") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, quatWireBytes(0, 32767, static_cast<int16_t>(65535u), 0)}}});
    uint16_t globalSeq = 3;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}

// resolveVec3GlobalSequenceTrack/resolveQuatGlobalSequenceTrack: the
// real-data counterpart to the two "resolves to empty, not misattributed"
// tests just above -- resolveVec3TrackSequence/resolveQuatTrackSequence
// correctly refuse to resolve a global-sequence track *by M2Sequence
// index*, but that means such a track needs its *own* resolution path to
// ever produce real keyframes at all. `putFullTrack`'s single sub-array
// (index 0) is exactly the shape wowdev.wiki describes for a
// global-sequence track ("blocks that use global sequences also only have
// one track").
// TODO: Remove: FAILURES2.md #7 citation (the finding this is a regression test for).

TEST_CASE("resolveVec3GlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff,
                 {{{0, vec3Bytes({1, 2, 3})}, {1000, vec3Bytes({4, 5, 6})}}});
    uint16_t globalSeq = 7;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto keyframes = husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(keyframes.size() == 2);
    CHECK(keyframes[0].first == 0);
    CHECK(keyframes[0].second.x == doctest::Approx(1));
    CHECK(keyframes[1].first == 1000);
    CHECK(keyframes[1].second.y == doctest::Approx(5));
}


TEST_CASE("resolveVec3GlobalSequenceTrack: a track that's NOT global-sequence-driven (the 0xFFFF "
          "\"none\" sentinel) resolves to empty") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // putFullTrack's own default is "none" -- no override needed.
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    CHECK(husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff)).empty());
}


TEST_CASE("resolveQuatGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Identity quaternion (0,0,0,1) is wire value (32767,32767,32767,65535)
    // -- see husk::m2::Quat's doc comment.
    putFullTrack(blob, trackOff,
                 {{{0, quatWireBytes(32767, 32767, 32767, static_cast<int16_t>(65535u))}}});
    uint16_t globalSeq = 3;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto keyframes = husk::m2::resolveQuatGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(keyframes.size() == 1);
    CHECK(keyframes[0].first == 0);
    CHECK(keyframes[0].second.w == doctest::Approx(1));
}


TEST_CASE("resolveVec3GlobalSequenceTrack: interpolation_type 2 or 3 throws, same as "
          "resolveVec3TrackSequence") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});
    uint16_t globalSeq = 1;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);
    uint16_t interp = 2;
    std::memcpy(blob.data() + trackOff + 0x00, &interp, 2);

    CHECK_THROWS_AS(husk::m2::resolveVec3GlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff)),
                     husk::m2::ParseError);
}


TEST_CASE("resolveFloatTrackSequence: reads timestamp/value keyframe pairs for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff,
                 {
                     {{0, floatBytes(0.5f)}, {1000, floatBytes(1.5f)}},
                     {{500, floatBytes(2.5f)}},
                 });

    auto seq0 = husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 2);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second == doctest::Approx(0.5f));
    CHECK(seq0[1].first == 1000);
    CHECK(seq0[1].second == doctest::Approx(1.5f));

    auto seq1 = husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(seq1.size() == 1);
    CHECK(seq1[0].second == doctest::Approx(2.5f));
}


TEST_CASE("resolveFloatTrackSequence: a track with a real global_sequence resolves to empty for "
          "every sequence index, same non-misattribution guarantee as resolveVec3TrackSequence") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, floatBytes(1.0f)}}});
    uint16_t globalSeq = 5;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    CHECK(husk::m2::resolveFloatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}


TEST_CASE("resolveFloatGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, floatBytes(3.25f)}, {200, floatBytes(4.5f)}}});
    uint16_t globalSeq = 2;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto kfs = husk::m2::resolveFloatGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff));
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == doctest::Approx(3.25f));
    CHECK(kfs[1].first == 200);
    CHECK(kfs[1].second == doctest::Approx(4.5f));
}


TEST_CASE("resolveRawIntTrackSequence: reads raw uint8_t and uint16_t keyframe values, zero-"
          "extended, for one sequence index") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff16 = 100;
    putFullTrack(blob, trackOff16, {{{0, rawIntBytes(0xBEEFu, 2)}, {10, rawIntBytes(1, 2)}}});
    auto kfs16 = husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff16), 0, 2);
    REQUIRE(kfs16.size() == 2);
    CHECK(kfs16[0].second == 0xBEEFu);
    CHECK(kfs16[1].second == 1u);

    size_t trackOff8 = 200;
    putFullTrack(blob, trackOff8, {{{0, rawIntBytes(0xAB, 1)}}});
    auto kfs8 = husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff8), 0, 1);
    REQUIRE(kfs8.size() == 1);
    CHECK(kfs8[0].second == 0xABu);
}


TEST_CASE("resolveRawIntTrackSequence: an unsupported element size throws") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, rawIntBytes(1, 2)}}});
    CHECK_THROWS_AS(husk::m2::resolveRawIntTrackSequence(blob, static_cast<uint32_t>(trackOff), 0, 4),
                     husk::m2::ParseError);
}

// parseColors' stored colorTrackOffset/
// alphaTrackOffset are real, directly-usable M2Track byte offsets --
// exactly what cmd_export.cpp's resolveAnimatedColorCurve/
// resolveAnimatedFixed16Curve feed straight into resolveVec3TrackSequence/
// resolveRawIntTrackSequence. Builds a full M2Color record's worth of real
// per-sequence keyframe data (both tracks genuinely animated, 2 sub-arrays
// each) and confirms the round trip through parseColors -> stored offset ->
// resolver reproduces the exact keyframes, for both sequence indices.

TEST_CASE("parseColors: colorTrackOffset/alphaTrackOffset resolve real per-sequence keyframes via "
          "resolveVec3TrackSequence/resolveRawIntTrackSequence") {
    size_t off = 1000;
    // Slack past the M2Color record's own 0x28 bytes so the first track's
    // own appended inner-array data (which putFullTrack places starting at
    // the buffer's current end) never grows back into the second track's
    // still-unwritten header/outer-array region at off+0x14 -- both tracks
    // share one contiguous record, unlike putFullTrack's other test cases
    // above, which each use a fresh, far-apart trackOff.
    std::vector<uint8_t> blob(off + 0x28 + 256, 0);
    putFullTrack(blob, off + 0x00,
                 {{{0, vec3Bytes({1, 0, 0})}}, {{500, vec3Bytes({0, 1, 0})}}});
    putFullTrack(blob, off + 0x14,
                 {{{0, fixed16Bytes(32767)}}, {{500, fixed16Bytes(0)}}});

    husk::m2::Array array;
    array.count = 1;
    array.offset = static_cast<uint32_t>(off);
    auto colors = husk::m2::parseColors(blob, array);

    REQUIRE(colors.size() == 1);
    CHECK(colors[0].colorAnimated);
    CHECK(colors[0].alphaAnimated);
    CHECK(colors[0].colorTrackOffset == off + 0x00);
    CHECK(colors[0].alphaTrackOffset == off + 0x14);

    auto colorSeq0 = husk::m2::resolveVec3TrackSequence(blob, colors[0].colorTrackOffset, 0);
    REQUIRE(colorSeq0.size() == 1);
    CHECK(colorSeq0[0].first == 0);
    CHECK(colorSeq0[0].second.x == doctest::Approx(1));

    auto colorSeq1 = husk::m2::resolveVec3TrackSequence(blob, colors[0].colorTrackOffset, 1);
    REQUIRE(colorSeq1.size() == 1);
    CHECK(colorSeq1[0].first == 500);
    CHECK(colorSeq1[0].second.y == doctest::Approx(1));

    auto alphaSeq0 =
        husk::m2::resolveRawIntTrackSequence(blob, colors[0].alphaTrackOffset, 0, 2);
    REQUIRE(alphaSeq0.size() == 1);
    CHECK(alphaSeq0[0].second == 32767u);

    auto alphaSeq1 =
        husk::m2::resolveRawIntTrackSequence(blob, colors[0].alphaTrackOffset, 1, 2);
    REQUIRE(alphaSeq1.size() == 1);
    CHECK(alphaSeq1[0].second == 0u);
}


TEST_CASE("resolveRawIntGlobalSequenceTrack: reads real keyframes for a global-sequence-driven "
          "track") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, rawIntBytes(7, 1)}}});
    uint16_t globalSeq = 9;
    std::memcpy(blob.data() + trackOff + 0x02, &globalSeq, 2);

    auto kfs = husk::m2::resolveRawIntGlobalSequenceTrack(blob, static_cast<uint32_t>(trackOff), 1);
    REQUIRE(kfs.size() == 1);
    CHECK(kfs[0].second == 7u);
}

// Builds one FBlock (wowdev.wiki's "Fake-AnimationBlock": flat
// {nTimestamps, ofsTimestamps, nKeys, ofsKeys}, no per-sequence outer/inner
// indirection -- see husk::m2::FBlockMeta's doc comment) from parallel
// (uint16_t timestamp, raw value bytes) pairs.
void putFBlock(std::vector<uint8_t>& buf, size_t blockOff,
               const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& keyframes) {
    if (buf.size() < blockOff + 0x10) buf.resize(blockOff + 0x10, 0);
    size_t tsOff = buf.size();
    for (const auto& kf : keyframes) putU16(buf, buf.size(), kf.first);
    size_t valOff = buf.size();
    for (const auto& kf : keyframes) buf.insert(buf.end(), kf.second.begin(), kf.second.end());

    putArray(buf, blockOff + 0x00, static_cast<uint32_t>(keyframes.size()), static_cast<uint32_t>(tsOff));
    putArray(buf, blockOff + 0x08, static_cast<uint32_t>(keyframes.size()),
             static_cast<uint32_t>(valOff));
}


TEST_CASE("readFBlockMeta: reads the flat nTimestamps/ofsTimestamps/nKeys/ofsKeys header") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, floatBytes(1.0f)}, {32767, floatBytes(2.0f)}});

    auto meta = husk::m2::readFBlockMeta(blob, 50);
    CHECK(meta.timestamps.count == 2);
    CHECK(meta.keys.count == 2);
}


TEST_CASE("resolveFBlockVec3: reads timestamp/value keyframe pairs directly, no sequence "
          "indirection") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, vec3Bytes({255, 119, 0})}, {32767, vec3Bytes({151, 11, 11})}});

    auto kfs = husk::m2::resolveFBlockVec3(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].first == 0);
    CHECK(kfs[0].second.x == doctest::Approx(255));
    CHECK(kfs[1].first == 32767);
    CHECK(kfs[1].second.z == doctest::Approx(11));
}


TEST_CASE("resolveFBlockVec2: reads C2Vector keyframes") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, vec2Bytes({0.1f, 0.2f})}});

    auto kfs = husk::m2::resolveFBlockVec2(blob, 50);
    REQUIRE(kfs.size() == 1);
    CHECK(kfs[0].second.x == doctest::Approx(0.1f));
    CHECK(kfs[0].second.y == doctest::Approx(0.2f));
}


TEST_CASE("resolveFBlockFixed16: decodes fixed16 keyframes to 0.0..1.0 floats, same scale as "
          "readFixed16TrackValue") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, fixed16Bytes(0)}, {16000, fixed16Bytes(32767)}});

    auto kfs = husk::m2::resolveFBlockFixed16(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == doctest::Approx(0.0f));
    CHECK(kfs[1].second == doctest::Approx(1.0f));
}


TEST_CASE("resolveFBlockUint16: reads raw uint16_t keyframes (flipbook cell indices)") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {{0, rawIntBytes(3, 2)}, {100, rawIntBytes(7, 2)}});

    auto kfs = husk::m2::resolveFBlockUint16(blob, 50);
    REQUIRE(kfs.size() == 2);
    CHECK(kfs[0].second == 3);
    CHECK(kfs[1].second == 7);
}


TEST_CASE("resolveFBlockVec3: empty (zero keys) returns an empty vector, not an error") {
    std::vector<uint8_t> blob(100, 0);
    putFBlock(blob, 50, {});
    CHECK(husk::m2::resolveFBlockVec3(blob, 50).empty());
}


TEST_CASE("resolveFBlockVec3: claimed key range past the end of the blob throws") {
    std::vector<uint8_t> blob(20, 0);
    // Claims 5 C3Vector keys (60 bytes) at offset 0, but the blob is only 20
    // bytes -- same "trust the claimed count, bounds-check before reading"
    // discipline as checkInnerArrayFits.
    putArray(blob, 0, 5, 0);
    putArray(blob, 8, 5, 0);
    CHECK_THROWS_AS(husk::m2::resolveFBlockVec3(blob, 0), husk::m2::ParseError);
}


TEST_CASE("parseGlobalLoops: reads count M2Loop (bare uint32 timestamp) records in order") {
    std::vector<uint8_t> blob(100, 0);
    size_t off = 40;
    putU32(blob, off + 0, 2000);
    putU32(blob, off + 4, 5000);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto loops = husk::m2::parseGlobalLoops(blob, array);
    REQUIRE(loops.size() == 2);
    CHECK(loops[0] == 2000);
    CHECK(loops[1] == 5000);
}


TEST_CASE("parseGlobalLoops: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);
    husk::m2::Array array;
    array.count = 5;  // 20 bytes needed
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseGlobalLoops(blob, array), husk::m2::ParseError);
}


TEST_CASE("resolveVec3TrackSequence: interpolation_type 2 or 3 (bezier/hermite, M2SplineKey-only "
          "per wowdev.wiki) throws rather than silently misreading the values array at the wrong "
          "stride") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    uint16_t bezier = 2;
    std::memcpy(blob.data() + trackOff + 0x00, &bezier, 2);
    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);

    uint16_t hermite = 3;
    std::memcpy(blob.data() + trackOff + 0x00, &hermite, 2);
    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);
}


TEST_CASE("resolveVec3TrackSequence: interpolation_type 0 (step) or 1 (linear) are both accepted, "
          "the only two values a plain M2Track<T> should ever carry") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 2, 3})}}});

    uint16_t step = 0;
    std::memcpy(blob.data() + trackOff + 0x00, &step, 2);
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).size() == 1);

    uint16_t linear = 1;
    std::memcpy(blob.data() + trackOff + 0x00, &linear, 2);
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).size() == 1);
}


TEST_CASE("resolveVec3TrackSequence: a sequence index past the outer array's own count is empty, "
          "not an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {{{0, vec3Bytes({1, 1, 1})}}});

    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 5).empty());
}


TEST_CASE("resolveVec3TrackSequence: no sub-arrays at all is empty, not an error") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    putFullTrack(blob, trackOff, {});
    CHECK(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0).empty());
}


TEST_CASE("resolveQuatTrackSequence: decompresses the wiki's own worked identity-quaternion "
          "example (32767,32767,32767,65535) -> (0,0,0,1)") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // 65535 as a wire uint16 is -1 when reinterpreted as int16 -- that's
    // the actual on-disk encoding wowdev.wiki's example uses.
    putFullTrack(blob, trackOff,
                 {{{0, quatWireBytes(32767, 32767, 32767, static_cast<int16_t>(65535u))}}});

    auto seq0 = husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 1);
    CHECK(seq0[0].first == 0);
    CHECK(seq0[0].second.x == doctest::Approx(0.0f));
    CHECK(seq0[0].second.y == doctest::Approx(0.0f));
    CHECK(seq0[0].second.z == doctest::Approx(0.0f));
    CHECK(seq0[0].second.w == doctest::Approx(1.0f));
}


TEST_CASE("resolveQuatTrackSequence: decompresses a non-identity quaternion, proving per-component "
          "decode and x/y/z/w wire order") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    // Per src/m2.cpp's readCompQuat: decode(raw) = (raw<0 ? raw+32768 :
    // raw-32767) / 32767.0. Wire x=0 -> (0-32767)/32767 = -1.0; wire
    // y=32767 -> (32767-32767)/32767 = 0.0; wire z=65535 (-1 signed) ->
    // (-1+32768)/32767 = 1.0; wire w=0 -> same as x, -1.0. Four different
    // wire values, four different decoded results -- proves each component
    // is read from its own wire slot in x/y/z/w order, not e.g. all reading
    // the same offset.
    putFullTrack(blob, trackOff, {{{0, quatWireBytes(0, 32767, static_cast<int16_t>(65535u), 0)}}});

    auto seq0 = husk::m2::resolveQuatTrackSequence(blob, static_cast<uint32_t>(trackOff), 0);
    REQUIRE(seq0.size() == 1);
    CHECK(seq0[0].second.x == doctest::Approx(-1.0f));
    CHECK(seq0[0].second.y == doctest::Approx(0.0f));
    CHECK(seq0[0].second.z == doctest::Approx(1.0f));
    CHECK(seq0[0].second.w == doctest::Approx(-1.0f));
}


TEST_CASE("resolveVec3TrackSequence: keyframe data running past the end of the blob throws") {
    std::vector<uint8_t> blob(100, 0);
    size_t trackOff = 100;
    if (blob.size() < trackOff + 0x14) blob.resize(trackOff + 0x14, 0);
    putU16(blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    // Outer arrays both claim 1 sub-array; that sub-array claims 1000
    // keyframes at an offset that doesn't remotely fit in a 100-byte blob.
    size_t outerOff = blob.size();
    blob.resize(outerOff + 8, 0);
    putArray(blob, outerOff, 1000, 0);
    putArray(blob, trackOff + 0x04, 1, static_cast<uint32_t>(outerOff));
    putArray(blob, trackOff + 0x0C, 1, static_cast<uint32_t>(outerOff));

    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(blob, static_cast<uint32_t>(trackOff), 0),
                     husk::m2::ParseError);
}


TEST_CASE("resolveVec3TrackSequence: an externalDataBlob reads keyframe payload from that blob, "
          "descriptors still from the M2 blob") {
    // The M2 blob holds the track's descriptors (outer + one inner
    // M2Array) exactly as usual -- but the inner array's `offset` field is
    // deliberately small/plausible-looking *for the M2 blob* while
    // actually only being valid against a separate, unrelated "anim" blob
    // -- proving the payload really comes from externalDataBlob, not by
    // coincidence sharing layout with descriptorBlob.
    std::vector<uint8_t> m2Blob(100, 0);
    size_t trackOff = 100;
    m2Blob.resize(trackOff + 0x14, 0);
    putU16(m2Blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    // Two separate inner-array descriptors -- timestamps and values are
    // unrelated arrays, at unrelated offsets, even though both are
    // (deliberately) meaningless against m2Blob's own small buffer and
    // only valid against animBlob.
    size_t tsOuterOff = m2Blob.size();
    m2Blob.resize(tsOuterOff + 8, 0);
    putArray(m2Blob, tsOuterOff, 1, 40);  // 1 timestamp at animBlob offset 40
    size_t valOuterOff = m2Blob.size();
    m2Blob.resize(valOuterOff + 8, 0);
    putArray(m2Blob, valOuterOff, 1, 60);  // 1 C3Vector at animBlob offset 60
    putArray(m2Blob, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(m2Blob, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));

    std::vector<uint8_t> animBlob(100, 0);
    putU32(animBlob, 40, 12345);  // timestamp
    auto posBytes = vec3Bytes({7, 8, 9});
    std::memcpy(animBlob.data() + 60, posBytes.data(), posBytes.size());

    auto result = husk::m2::resolveVec3TrackSequence(m2Blob, static_cast<uint32_t>(trackOff), 0,
                                                       &animBlob);
    REQUIRE(result.size() == 1);
    CHECK(result[0].first == 12345);
    CHECK(result[0].second.x == doctest::Approx(7));
    CHECK(result[0].second.z == doctest::Approx(9));
}


TEST_CASE("resolveVec3TrackSequence: externalDataBlob too small for the claimed keyframe data "
          "throws, not a wild read") {
    std::vector<uint8_t> m2Blob(100, 0);
    size_t trackOff = 100;
    m2Blob.resize(trackOff + 0x14, 0);
    putU16(m2Blob, trackOff + 0x02, 0xFFFF);  // global_sequence: none (see putFullTrack)
    size_t tsOuterOff = m2Blob.size();
    m2Blob.resize(tsOuterOff + 8, 0);
    putArray(m2Blob, tsOuterOff, 1, 0);  // 1 timestamp at offset 0 -- fits tinyAnimBlob
    size_t valOuterOff = m2Blob.size();
    m2Blob.resize(valOuterOff + 8, 0);
    putArray(m2Blob, valOuterOff, 1, 40);  // 1 C3Vector at offset 40 -- doesn't fit
    putArray(m2Blob, trackOff + 0x04, 1, static_cast<uint32_t>(tsOuterOff));
    putArray(m2Blob, trackOff + 0x0C, 1, static_cast<uint32_t>(valOuterOff));

    std::vector<uint8_t> tinyAnimBlob(10, 0);  // far too small for offset 40 + 12 bytes

    CHECK_THROWS_AS(husk::m2::resolveVec3TrackSequence(m2Blob, static_cast<uint32_t>(trackOff), 0,
                                                         &tinyAnimBlob),
                     husk::m2::ParseError);
}


TEST_CASE("extractAnimBlob: chunked=false returns the file bytes verbatim") {
    std::vector<uint8_t> flat = {1, 2, 3, 4, 5};
    auto blob = husk::m2::extractAnimBlob(flat, /*chunked=*/false);
    CHECK(blob == flat);
}


TEST_CASE("extractAnimBlob: chunked=true finds and returns the AFM2 chunk's payload") {
    std::vector<uint8_t> file;
    appendChunk(file, "AFSA", {0xAA, 0xAA});  // unrelated chunk before AFM2
    appendChunk(file, "AFM2", {1, 2, 3, 4, 5, 6});
    appendChunk(file, "AFSB", {0xBB});  // unrelated chunk after AFM2

    auto blob = husk::m2::extractAnimBlob(file, /*chunked=*/true);
    std::vector<uint8_t> expected = {1, 2, 3, 4, 5, 6};
    CHECK(blob == expected);
}


TEST_CASE("extractAnimBlob: chunked=true with no AFM2 chunk throws, naming what it found") {
    std::vector<uint8_t> file;
    appendChunk(file, "AFSA", {0xAA});
    appendChunk(file, "AFSB", {0xBB});

    CHECK_THROWS_WITH_AS(husk::m2::extractAnimBlob(file, /*chunked=*/true),
                          doctest::Contains("AFSA"), husk::m2::ParseError);
}

