// Tests for husk::m2's animation module (src/m2_animation.hpp/.cpp): the
// outer struct-array parsers -- parseColors/parseTextureWeights/
// parseTextureTransforms/parseSequences.
// Split out of the former tests/test_m2.cpp (FILE_SPLIT_TODO.md Item 5),
// then split further out of tests/test_m2_animation.cpp itself
// (FILE_SPLIT_TODO.md's post-completion audit -- that file was still over
// the 1000-line hard limit after Item 5's own split): this file keeps the
// "parse a whole M2Array of records" struct parsers; the per-track/per-
// curve keyframe *resolver* functions (resolveVec3TrackSequence and its
// Quat/Float/RawInt/GlobalSequence/FBlock siblings, TrackMeta) and
// extractAnimBlob moved to tests/test_m2_animation_tracks.cpp -- see that
// file's own doc comment.

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

