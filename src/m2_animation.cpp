#include "m2_animation.hpp"

#include <algorithm>
#include <cstring>

#include "chunk.hpp"

namespace husk::m2 {

namespace {

// Locates an M2Track<T>'s single value, *only* when the track is
// unambiguously constant -- exactly one animation sub-array (outer.count
// == 1) with exactly one keyframe in it (inner.count == 1). Anything else
// is real per-sequence or globally-looped keyframe animation (roadmap
// stage 6, not this parser's job), and returns nullopt rather than
// guessing. Requiring both counts to be exactly 1 is what
// `interpolation_ranges`/"Blocks that use global sequences also only have
// one track" (wowdev.wiki M2#Interpolation) actually describes as a
// non-animated block; anything looser risks silently reading an unrelated
// keyframe (e.g. sequence 0's first, possibly-transparent alpha value) as
// if it were a sensible default. Returns the byte offset of the single
// value on success, checked the same bounds-checked way any other M2Array
// element access in this file is.
// TODO: Remove: an earlier version of this code took element [0][0]
// unconditionally, which for a real bloodelffemale.m2 batch would have
// silently rendered the entire model invisible -- fixed by this check.
std::optional<size_t> constantTrackValueOffset(const uint8_t* data, size_t blobSize,
                                                size_t trackOffset) {
    Array outer = readArray(data, blobSize, trackOffset + 0x0C);
    if (outer.count != 1) {
        return std::nullopt;
    }
    Array inner = readArray(data, blobSize, outer.offset);
    if (inner.count != 1) {
        return std::nullopt;
    }
    return inner.offset;
}

// True when an M2Track<T> carries real keyframe data beyond the
// single-constant-value case constantTrackValueOffset resolves --
// per-M2Sequence animation (outer.count > 1), or a global-sequence-driven
// loop (outer.count == 1 but with more than one keyframe in its single
// sub-array). Both constantTrackValueOffset and this function agree on
// "empty" (outer.count == 0): neither an animated nor a constant track,
// just genuinely no data. This is what lets a caller distinguish "this
// M2Color/M2TextureWeight track is nullopt because it's genuinely empty"
// from "...because it's animated and husk has no way to represent that in
// a static baseColorFactor" -- see Color::colorAnimated/alphaAnimated and
// TextureWeight::weightAnimated.
bool trackHasAnimatedData(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    Array outer = readArray(data, blobSize, trackOffset + 0x0C);
    if (outer.count == 0) {
        return false;
    }
    if (outer.count > 1) {
        return true;
    }
    Array inner = readArray(data, blobSize, outer.offset);
    return inner.count > 1;
}

std::optional<float> readFixed16TrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    // fixed16: a 16-bit fixed-point fraction, 0 (0.0) .. 0x7FFF (1.0) --
    // wowdev.wiki M2#Colors_and_transparency's own "0 - transparent,
    // 0x7FFF - opaque" note for M2Color::alpha; M2TextureWeight::weight
    // uses the same scale.
    int16_t raw;
    uint16_t bits = readU16(data, blobSize, *valueOffset);
    std::memcpy(&raw, &bits, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

std::optional<Vec3> readVec3TrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    return readVec3(data, blobSize, *valueOffset);
}

// Same shape as readVec3TrackValue, but for an M2Track<C4Quaternion> --
// M2TextureTransform::rotation's own type (wowdev.wiki Common_Types
// C4Quaternion: 4 raw floats x/y/z/w, *not* the compressed M2CompQuat
// bones use -- see readCompQuat below for that unrelated format).
std::optional<Quat> readQuatFloatTrackValue(const uint8_t* data, size_t blobSize, size_t trackOffset) {
    auto valueOffset = constantTrackValueOffset(data, blobSize, trackOffset);
    if (!valueOffset) {
        return std::nullopt;
    }
    Quat q;
    q.x = readF32(data, blobSize, *valueOffset + 0);
    q.y = readF32(data, blobSize, *valueOffset + 4);
    q.z = readF32(data, blobSize, *valueOffset + 8);
    q.w = readF32(data, blobSize, *valueOffset + 12);
    return q;
}

// Unpacks one M2CompQuat (4x int16, x/y/z/w order) into a Quat, per
// wowdev.wiki "Quaternion values and 2.x" -- fetched 2026-07-25, since the
// main M2 page only links to this formula rather than inlining it:
//   float(v < 0 ? v + 32768 : v - 32767) / 32767.0
// applied independently to each component. The identity quaternion
// (0,0,0,1) is wire value (32767,32767,32767,65535) -- 65535 read as a
// signed int16 is -1, and (-1 + 32768)/32767 == 1.0, matching the wiki's
// own worked example exactly (checked in tests/test_m2.cpp).
Quat readCompQuat(const uint8_t* data, size_t blobSize, size_t off) {
    auto decode = [](int16_t raw) {
        int v = raw < 0 ? raw + 32768 : raw - 32767;
        return static_cast<float>(v) / 32767.0f;
    };
    auto readI16 = [&](size_t o) {
        uint16_t bits = readU16(data, blobSize, o);
        int16_t v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    };
    Quat q;
    q.x = decode(readI16(off + 0));
    q.y = decode(readI16(off + 2));
    q.z = decode(readI16(off + 4));
    q.w = decode(readI16(off + 6));
    return q;
}

// Reads one raw C4Quaternion keyframe value (4x float32, x/y/z/w order, no
// M2CompQuat decompression) -- TextureTransform::rotation's own wire format,
// see readQuatFloatTrackValue above for the constant-case equivalent this
// mirrors for the animated case.
Quat readRawQuat(const uint8_t* data, size_t blobSize, size_t off) {
    Quat q;
    q.x = readF32(data, blobSize, off + 0);
    q.y = readF32(data, blobSize, off + 4);
    q.z = readF32(data, blobSize, off + 8);
    q.w = readF32(data, blobSize, off + 12);
    return q;
}

// Resolves one M2Track<T>'s keyframe sub-array for a specific sequence
// index -- the general case constantTrackValueOffset (above) deliberately
// refuses to handle. Returns the (outer-array-relative) timestamps/values
// inner-array descriptors, or nullopt if `sequenceIndex` is out of range
// for either of the track's own outer arrays (never an error -- a sequence
// with no inline data for this track is the ordinary case for anything
// husk doesn't have the .anim keyframes for, see Sequence::flags).
std::optional<std::pair<Array, Array>> trackSequenceInnerArrays(const uint8_t* data,
                                                                  size_t blobSize,
                                                                  size_t trackOffset,
                                                                  uint32_t sequenceIndex) {
    Array timestampsOuter = readArray(data, blobSize, trackOffset + 0x04);
    Array valuesOuter = readArray(data, blobSize, trackOffset + 0x0C);
    if (sequenceIndex >= timestampsOuter.count || sequenceIndex >= valuesOuter.count) {
        return std::nullopt;
    }
    Array timestampsInner =
        readArray(data, blobSize, timestampsOuter.offset + static_cast<size_t>(sequenceIndex) * 8);
    Array valuesInner =
        readArray(data, blobSize, valuesOuter.offset + static_cast<size_t>(sequenceIndex) * 8);
    return std::make_pair(timestampsInner, valuesInner);
}

}  // namespace

std::vector<Color> parseColors(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Color> colors;
    if (array.count == 0) {
        return colors;
    }

    // M2Color: M2Track<C3Vector> color (0x14) + M2Track<fixed16> alpha
    // (0x14), 0x28 = 40 bytes total.
    constexpr size_t kColorSize = 0x28;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kColorSize) {
        throw ParseError("colors array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kColorSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    colors.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kColorSize;
        Color c;
        c.color = readVec3TrackValue(data, blobSize, off + 0x00);
        c.alpha = readFixed16TrackValue(data, blobSize, off + 0x14);
        c.colorAnimated = !c.color && trackHasAnimatedData(data, blobSize, off + 0x00);
        c.alphaAnimated = !c.alpha && trackHasAnimatedData(data, blobSize, off + 0x14);
        c.colorTrackOffset = static_cast<uint32_t>(off + 0x00);
        c.alphaTrackOffset = static_cast<uint32_t>(off + 0x14);
        colors.push_back(c);
    }

    return colors;
}

std::vector<TextureWeight> parseTextureWeights(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<TextureWeight> weights;
    if (array.count == 0) {
        return weights;
    }

    constexpr size_t kWeightSize = 0x14;  // M2Track<fixed16>, 20 bytes
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kWeightSize) {
        throw ParseError("textureWeights array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kWeightSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    weights.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kWeightSize;
        TextureWeight w;
        w.weight = readFixed16TrackValue(data, blobSize, off + 0x00);
        w.weightAnimated = !w.weight && trackHasAnimatedData(data, blobSize, off + 0x00);
        w.weightTrackOffset = static_cast<uint32_t>(off + 0x00);
        weights.push_back(w);
    }

    return weights;
}

std::vector<TextureTransform> parseTextureTransforms(const std::vector<uint8_t>& blob,
                                                       const Array& array) {
    std::vector<TextureTransform> transforms;
    if (array.count == 0) {
        return transforms;
    }

    // M2TextureTransform: M2Track<C3Vector> translation (0x14) +
    // M2Track<C4Quaternion> rotation (0x14) + M2Track<C3Vector> scaling
    // (0x14), 0x3C = 60 bytes total (wowdev.wiki M2#Texture_Transforms).
    constexpr size_t kTransformSize = 0x3C;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kTransformSize) {
        throw ParseError("textureTransforms array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kTransformSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    transforms.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kTransformSize;
        TextureTransform t;
        t.translation = readVec3TrackValue(data, blobSize, off + 0x00);
        t.rotation = readQuatFloatTrackValue(data, blobSize, off + 0x14);
        t.scaling = readVec3TrackValue(data, blobSize, off + 0x28);
        t.translationAnimated = !t.translation && trackHasAnimatedData(data, blobSize, off + 0x00);
        t.rotationAnimated = !t.rotation && trackHasAnimatedData(data, blobSize, off + 0x14);
        t.scalingAnimated = !t.scaling && trackHasAnimatedData(data, blobSize, off + 0x28);
        t.translationTrackOffset = static_cast<uint32_t>(off + 0x00);
        t.rotationTrackOffset = static_cast<uint32_t>(off + 0x14);
        t.scalingTrackOffset = static_cast<uint32_t>(off + 0x28);
        transforms.push_back(t);
    }

    return transforms;
}

std::vector<Sequence> parseSequences(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Sequence> sequences;
    if (array.count == 0) {
        return sequences;
    }

    // M2Sequence, wowdev.wiki M2#Animation_sequences -- 0x40 (64) bytes,
    // see Sequence's doc comment in m2_animation.hpp for why (an
    // easy-to-miss 28-byte M2Bounds field, real-data-verified).
    constexpr size_t kSequenceSize = 0x40;
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kSequenceSize) {
        throw ParseError("sequences array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kSequenceSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    sequences.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kSequenceSize;
        Sequence s;
        s.id = readU16(data, blobSize, off + 0x00);
        s.variationIndex = readU16(data, blobSize, off + 0x02);
        s.duration = readU32(data, blobSize, off + 0x04);
        s.movespeed = readF32(data, blobSize, off + 0x08);
        s.flags = readU32(data, blobSize, off + 0x0C);
        s.frequency = static_cast<int16_t>(readU16(data, blobSize, off + 0x10));
        s.replay.minimum = readU32(data, blobSize, off + 0x14);
        s.replay.maximum = readU32(data, blobSize, off + 0x18);
        s.blendTimeIn = readU16(data, blobSize, off + 0x1C);
        s.blendTimeOut = readU16(data, blobSize, off + 0x1E);
        s.bounds.min = {readF32(data, blobSize, off + 0x20), readF32(data, blobSize, off + 0x24),
                         readF32(data, blobSize, off + 0x28)};
        s.bounds.max = {readF32(data, blobSize, off + 0x2C), readF32(data, blobSize, off + 0x30),
                         readF32(data, blobSize, off + 0x34)};
        s.boundsRadius = readF32(data, blobSize, off + 0x38);
        s.variationNext = static_cast<int16_t>(readU16(data, blobSize, off + 0x3C));
        s.aliasNext = readU16(data, blobSize, off + 0x3E);
        sequences.push_back(s);
    }

    return sequences;
}

TrackMeta readTrackMeta(const std::vector<uint8_t>& blob, uint32_t trackOffset) {
    TrackMeta meta;
    meta.interpolationType = readU16(blob.data(), blob.size(), trackOffset + 0x00);
    meta.globalSequence = readU16(blob.data(), blob.size(), trackOffset + 0x02);
    return meta;
}

namespace {

// Shared bounds-checked "read n fixed-size records at a foreign-data
// offset" guard -- same up-front-validate-before-reserve discipline as
// every other parse* function in this file, factored out here since
// resolveVec3TrackSequence/resolveQuatTrackSequence both need it for data
// that isn't a top-level M2Array (it's an inner M2Track array instead),
// which is otherwise the one shape parseVertices/parseBones/etc. don't
// already cover.
// TODO: Remove: FAILURES.md #2.
void checkInnerArrayFits(const Array& inner, size_t elementSize, size_t blobSize,
                          const char* what) {
    if (inner.offset > blobSize || inner.count > (blobSize - inner.offset) / elementSize) {
        throw ParseError(std::string("track claims ") + std::to_string(inner.count) + " " + what +
                          " (" + std::to_string(elementSize) + " bytes each) at offset " +
                          std::to_string(inner.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
}

}  // namespace

std::vector<std::pair<uint32_t, Vec3>> resolveVec3TrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    // A global-sequence track loops continuously, independent of any
    // M2Sequence -- resolving it by `sequenceIndex` at all would silently
    // attribute its data to whichever M2Sequence happens to occupy the
    // track's outer-array position `sequenceIndex` (see TrackMeta's doc
    // comment). husk doesn't have global_sequences durations to build a
    // real independent clip yet, so this returns empty rather than guessing
    // -- strictly more correct than the old behavior, even though it means
    // no animation comes out for this track today.
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    // interpolation_type 2/3 (cubic bezier/hermite) is only valid for
    // M2SplineKey<T> tracks per wowdev.wiki -- bone translation/scale,
    // M2Color, and M2TextureWeight are all plain M2Track<T>, so a real file
    // reporting 2/3 here means either a version this parser doesn't
    // understand or a corrupted read, not a case husk can silently keep
    // reading at the wrong (3x) stride (see TrackMeta's doc comment).
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 12, dataSize, "C3Vector values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Vec3>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Vec3 v = readVec3(data, dataSize, valuesInner.offset + i * 12);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveQuatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    // See resolveVec3TrackSequence's identical checks for why these two
    // conditions are handled before touching the timestamps/values arrays
    // at all.
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 8, dataSize, "M2CompQuat values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readCompQuat(data, dataSize, valuesInner.offset + i * 8);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, Vec3>> resolveVec3GlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    // A global-sequence track's outer M2Array<M2Array<T>> holds exactly one
    // sub-array (see this function's doc comment) -- index 0 is always the
    // right (and only) one to resolve, the same way trackSequenceInnerArrays
    // already resolves a specific M2Sequence's sub-array by index.
    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 12, dataSize, "C3Vector values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Vec3>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Vec3 v = readVec3(data, dataSize, valuesInner.offset + i * 12);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveQuatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 8, dataSize, "M2CompQuat values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readCompQuat(data, dataSize, valuesInner.offset + i * 8);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveRawQuatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    // See resolveVec3TrackSequence's identical checks for why these two
    // conditions are handled before touching the timestamps/values arrays.
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 16, dataSize, "C4Quaternion values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readRawQuat(data, dataSize, valuesInner.offset + i * 16);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, Quat>> resolveRawQuatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 16, dataSize, "C4Quaternion values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, Quat>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        Quat q = readRawQuat(data, dataSize, valuesInner.offset + i * 16);
        out.emplace_back(ts, q);
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> resolveFloatTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 4, dataSize, "float values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, float>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        float v = readF32(data, dataSize, valuesInner.offset + i * 4);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> resolveFloatGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, 4, dataSize, "float values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, float>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        float v = readF32(data, dataSize, valuesInner.offset + i * 4);
        out.emplace_back(ts, v);
    }
    return out;
}

namespace {
uint32_t readRawIntElement(const uint8_t* data, size_t dataSize, size_t off, size_t elementSize) {
    if (elementSize == 1) {
        return readU8(data, dataSize, off);
    }
    if (elementSize == 2) {
        return readU16(data, dataSize, off);
    }
    throw ParseError("resolveRawIntTrackSequence: unsupported element size " +
                      std::to_string(elementSize) + " (only 1 or 2 are valid)");
}
}  // namespace

std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntTrackSequence(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceIndex,
    size_t elementSize, const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence != TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, sequenceIndex);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, elementSize, dataSize, "raw int values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        uint32_t v = readRawIntElement(data, dataSize, valuesInner.offset + i * elementSize, elementSize);
        out.emplace_back(ts, v);
    }
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> resolveRawIntGlobalSequenceTrack(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t elementSize,
    const std::vector<uint8_t>* externalDataBlob) {
    TrackMeta meta = readTrackMeta(blob, trackOffset);
    if (meta.globalSequence == TrackMeta::kNoGlobalSequence) {
        return {};
    }
    if (meta.interpolationType > 1) {
        throw ParseError("track at offset " + std::to_string(trackOffset) +
                          " has interpolation_type " + std::to_string(meta.interpolationType) +
                          ", but this track kind is never M2SplineKey-based -- unexpected file "
                          "version or corrupted read?");
    }

    auto inner = trackSequenceInnerArrays(blob.data(), blob.size(), trackOffset, /*sequenceIndex=*/0);
    if (!inner) {
        return {};
    }
    const Array& timestampsInner = inner->first;
    const Array& valuesInner = inner->second;

    const uint8_t* data = externalDataBlob ? externalDataBlob->data() : blob.data();
    size_t dataSize = externalDataBlob ? externalDataBlob->size() : blob.size();

    checkInnerArrayFits(timestampsInner, 4, dataSize, "timestamps");
    checkInnerArrayFits(valuesInner, elementSize, dataSize, "raw int values");
    size_t n = std::min<size_t>(timestampsInner.count, valuesInner.count);

    std::vector<std::pair<uint32_t, uint32_t>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t ts = readU32(data, dataSize, timestampsInner.offset + i * 4);
        uint32_t v = readRawIntElement(data, dataSize, valuesInner.offset + i * elementSize, elementSize);
        out.emplace_back(ts, v);
    }
    return out;
}

namespace {
// `structureLabel` defaults to "FBlock" for every existing caller
// (resolveFBlockGeneric below); resolveFixed16PartTrack passes
// "M2PartTrack" instead, since it shares this exact two-Array byte layout
// but isn't actually an FBlock (no interpolation_type/global_sequence
// header) -- see resolveFixed16PartTrack's own doc comment in
// m2_animation.hpp.
void checkFBlockArrayFits(const Array& a, size_t elementSize, size_t blobSize, const char* what,
                           const char* structureLabel = "FBlock") {
    if (a.offset > blobSize || a.count > (blobSize - a.offset) / elementSize) {
        throw ParseError(std::string(structureLabel) + " claims " + std::to_string(a.count) + " " +
                          what + " (" + std::to_string(elementSize) + " bytes each) at offset " +
                          std::to_string(a.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
}

// Shared by resolveFBlockFixed16 (FBlock<fixed16>, e.g. M2Particle's
// alphaTrack) and resolveFixed16PartTrack (M2PartTrack<fixed16>, EXP2's
// alphaCutoff) -- same 0x0000..0x7FFF -> 0.0..1.0 scaling
// readFixed16TrackValue's own raw bit read uses, just against a flat
// element offset instead of an M2Track's inner sub-array.
float decodeFixed16Element(const uint8_t* d, size_t s, size_t o) {
    int16_t raw;
    uint16_t bits = readU16(d, s, o);
    std::memcpy(&raw, &bits, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}
}  // namespace

FBlockMeta readFBlockMeta(const std::vector<uint8_t>& blob, uint32_t blockOffset) {
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    FBlockMeta meta;
    meta.timestamps = readArray(data, blobSize, blockOffset + 0x00);
    meta.keys = readArray(data, blobSize, blockOffset + 0x08);
    return meta;
}

namespace {
// Shared "FBlock timestamps (uint16_t each) + N-byte keys" walk -- every
// resolveFBlockXxx below differs only in the key element size and decode,
// same elementSize-as-parameter style resolveRawIntTrackSequence uses
// instead of a template.
template <typename T, typename Decode>
std::vector<std::pair<uint16_t, T>> resolveFBlockGeneric(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset, size_t keyElementSize,
                                                           Decode decode) {
    FBlockMeta meta = readFBlockMeta(blob, blockOffset);
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    checkFBlockArrayFits(meta.timestamps, 2, blobSize, "timestamps");
    checkFBlockArrayFits(meta.keys, keyElementSize, blobSize, "keys");
    size_t n = std::min<size_t>(meta.timestamps.count, meta.keys.count);

    std::vector<std::pair<uint16_t, T>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        uint16_t ts = readU16(data, blobSize, meta.timestamps.offset + i * 2);
        T v = decode(data, blobSize, meta.keys.offset + i * keyElementSize);
        out.emplace_back(ts, v);
    }
    return out;
}
}  // namespace

std::vector<std::pair<uint16_t, Vec3>> resolveFBlockVec3(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset) {
    return resolveFBlockGeneric<Vec3>(blob, blockOffset, 12,
                                       [](const uint8_t* d, size_t s, size_t o) { return readVec3(d, s, o); });
}

std::vector<std::pair<uint16_t, Vec2>> resolveFBlockVec2(const std::vector<uint8_t>& blob,
                                                           uint32_t blockOffset) {
    return resolveFBlockGeneric<Vec2>(blob, blockOffset, 8, [](const uint8_t* d, size_t s, size_t o) {
        Vec2 v;
        v.x = readF32(d, s, o);
        v.y = readF32(d, s, o + 4);
        return v;
    });
}

std::vector<std::pair<uint16_t, float>> resolveFBlockFixed16(const std::vector<uint8_t>& blob,
                                                               uint32_t blockOffset) {
    return resolveFBlockGeneric<float>(blob, blockOffset, 2, decodeFixed16Element);
}

std::vector<std::pair<uint16_t, uint16_t>> resolveFBlockUint16(const std::vector<uint8_t>& blob,
                                                                 uint32_t blockOffset) {
    return resolveFBlockGeneric<uint16_t>(blob, blockOffset, 2,
                                           [](const uint8_t* d, size_t s, size_t o) { return readU16(d, s, o); });
}

std::vector<std::pair<float, float>> resolveFixed16PartTrack(const std::vector<uint8_t>& blob,
                                                                uint32_t blockOffset) {
    // M2PartTrack<fixed16> is byte-for-byte the same {Array; Array;}
    // 16-byte header FBlockMeta already models -- reused directly rather
    // than duplicating the 8/8-byte offset reads.
    FBlockMeta meta = readFBlockMeta(blob, blockOffset);
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();
    checkFBlockArrayFits(meta.timestamps, 2, blobSize, "times", "M2PartTrack");
    checkFBlockArrayFits(meta.keys, 2, blobSize, "values", "M2PartTrack");
    size_t n = std::min<size_t>(meta.timestamps.count, meta.keys.count);

    std::vector<std::pair<float, float>> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        float t = decodeFixed16Element(data, blobSize, meta.timestamps.offset + i * 2);
        float v = decodeFixed16Element(data, blobSize, meta.keys.offset + i * 2);
        out.emplace_back(t, v);
    }
    return out;
}

std::vector<uint32_t> parseGlobalLoops(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<uint32_t> loops;
    if (array.count == 0) {
        return loops;
    }

    constexpr size_t kLoopSize = 4;  // M2Loop: a bare uint32_t timestamp
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kLoopSize) {
        throw ParseError("globalLoops array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kLoopSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    loops.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        loops.push_back(readU32(data, blobSize, array.offset + static_cast<size_t>(i) * kLoopSize));
    }

    return loops;
}

std::vector<uint8_t> extractAnimBlob(const std::vector<uint8_t>& animFileBytes, bool chunked) {
    if (!chunked) {
        return animFileBytes;
    }
    auto chunks = readChunks(animFileBytes.data(), animFileBytes.size());
    auto afm2 = findChunk(chunks, "AFM2");
    if (!afm2) {
        std::string found;
        for (const auto& c : chunks) {
            if (!found.empty()) found += ", ";
            found += c.tag;
        }
        throw ParseError("chunked .anim file has no AFM2 chunk; chunks found: [" + found + "]");
    }
    return std::vector<uint8_t>(afm2->data, afm2->data + afm2->size);
}

}  // namespace husk::m2
