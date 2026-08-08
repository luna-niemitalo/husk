#include "m2_scene.hpp"

#include <cstring>

#include "m2_skeleton.hpp"

namespace husk::m2 {

std::vector<Attachment> parseAttachments(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Attachment> attachments;
    if (array.count == 0) {
        return attachments;
    }

    constexpr size_t kAttachmentSize = 0x28;  // M2Attachment, wowdev.wiki M2#Attachments
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kAttachmentSize) {
        throw ParseError("attachments array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kAttachmentSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    attachments.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kAttachmentSize;
        Attachment a;
        a.id = readU32(data, blobSize, off + 0x00);
        uint16_t boneBits = readU16(data, blobSize, off + 0x04);
        std::memcpy(&a.bone, &boneBits, sizeof(a.bone));
        a.position = readVec3(data, blobSize, off + 0x08);
        a.animateAttachedTrackOffset = static_cast<uint32_t>(off + 0x14);
        attachments.push_back(a);
    }

    return attachments;
}

std::vector<Event> parseEvents(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Event> events;
    if (array.count == 0) {
        return events;
    }

    constexpr size_t kEventSize = 0x24;  // M2Event, wowdev.wiki M2#Events
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kEventSize) {
        throw ParseError("events array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kEventSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    events.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kEventSize;
        Event e;
        // identifier is 4 raw ASCII bytes, file order (not reversed, same
        // as chunk tags -- see chunk.hpp), trimmed of a trailing NUL the
        // same way readName handles the `name` field.
        char idBytes[5] = {};
        std::memcpy(idBytes, data + off + 0x00, 4);
        e.identifier = std::string(idBytes, 4);
        while (!e.identifier.empty() && e.identifier.back() == '\0') e.identifier.pop_back();
        e.data = readU32(data, blobSize, off + 0x04);
        e.bone = readU32(data, blobSize, off + 0x08);
        e.position = readVec3(data, blobSize, off + 0x0C);
        events.push_back(e);
    }

    return events;
}

std::vector<Light> parseLights(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Light> lights;
    if (array.count == 0) {
        return lights;
    }

    constexpr size_t kLightSize = 0x9C;  // M2Light, wowdev.wiki M2#Lights
    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kLightSize) {
        throw ParseError("lights array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kLightSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    lights.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kLightSize;
        Light l;
        l.type = readU16(data, blobSize, off + 0x00);
        uint16_t boneBits = readU16(data, blobSize, off + 0x02);
        std::memcpy(&l.bone, &boneBits, sizeof(l.bone));
        l.position = readVec3(data, blobSize, off + 0x04);
        // wowdev.wiki M2#Lights field offsets: ambient_color (0x10),
        // ambient_intensity (0x24), diffuse_color (0x38), diffuse_intensity
        // (0x4C), attenuation_start (0x60), attenuation_end (0x74),
        // visibility (0x88) -- offsets of the M2Track structs themselves,
        // not their resolved values.
        l.ambientColorTrackOffset = static_cast<uint32_t>(off + 0x10);
        l.ambientIntensityTrackOffset = static_cast<uint32_t>(off + 0x24);
        l.diffuseColorTrackOffset = static_cast<uint32_t>(off + 0x38);
        l.diffuseIntensityTrackOffset = static_cast<uint32_t>(off + 0x4C);
        l.attenuationStartTrackOffset = static_cast<uint32_t>(off + 0x60);
        l.attenuationEndTrackOffset = static_cast<uint32_t>(off + 0x74);
        l.visibilityTrackOffset = static_cast<uint32_t>(off + 0x88);
        lights.push_back(l);
    }

    return lights;
}

std::vector<Ribbon> parseRibbons(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<Ribbon> ribbons;
    if (array.count == 0) {
        return ribbons;
    }

    // M2Ribbon, >= Wrath shape (wowdev.wiki M2#Ribbon_emitters -- see
    // Ribbon's doc comment in m2_scene.hpp for the full offset derivation).
    // TODO: Remove: WIKI_FINDINGS.md for the real-file cross-check that
    // confirmed it.
    constexpr size_t kRibbonSize = 0xB0;
    constexpr size_t kRibbonIdOffset = 0x00;
    constexpr size_t kBoneIndexOffset = 0x04;
    constexpr size_t kPositionOffset = 0x08;
    constexpr size_t kTextureIndicesOffset = 0x14;
    constexpr size_t kMaterialIndicesOffset = 0x1C;
    constexpr size_t kColorTrackOffset = 0x24;
    constexpr size_t kAlphaTrackOffset = 0x38;
    constexpr size_t kHeightAboveTrackOffset = 0x4C;
    constexpr size_t kHeightBelowTrackOffset = 0x60;
    constexpr size_t kEdgesPerSecondOffset = 0x74;
    constexpr size_t kEdgeLifetimeOffset = 0x78;
    constexpr size_t kGravityOffset = 0x7C;
    constexpr size_t kTextureRowsOffset = 0x80;
    constexpr size_t kTextureColsOffset = 0x82;
    constexpr size_t kTexSlotTrackOffset = 0x84;
    constexpr size_t kVisibilityTrackOffset = 0x98;
    constexpr size_t kPriorityPlaneOffset = 0xAC;
    constexpr size_t kRibbonColorIndexOffset = 0xAE;
    constexpr size_t kTextureTransformLookupIndexOffset = 0xAF;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kRibbonSize) {
        throw ParseError("ribbonEmitters array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kRibbonSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    ribbons.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kRibbonSize;
        Ribbon r;
        r.ribbonId = readU32(data, blobSize, off + kRibbonIdOffset);
        r.boneIndex = readU32(data, blobSize, off + kBoneIndexOffset);
        r.position = readVec3(data, blobSize, off + kPositionOffset);
        r.textureIndices =
            parseUint16Array(blob, readArray(data, blobSize, off + kTextureIndicesOffset));
        r.materialIndices =
            parseUint16Array(blob, readArray(data, blobSize, off + kMaterialIndicesOffset));
        r.colorTrackOffset = static_cast<uint32_t>(off + kColorTrackOffset);
        r.alphaTrackOffset = static_cast<uint32_t>(off + kAlphaTrackOffset);
        r.heightAboveTrackOffset = static_cast<uint32_t>(off + kHeightAboveTrackOffset);
        r.heightBelowTrackOffset = static_cast<uint32_t>(off + kHeightBelowTrackOffset);
        r.edgesPerSecond = readF32(data, blobSize, off + kEdgesPerSecondOffset);
        r.edgeLifetime = readF32(data, blobSize, off + kEdgeLifetimeOffset);
        r.gravity = readF32(data, blobSize, off + kGravityOffset);
        r.textureRows = readU16(data, blobSize, off + kTextureRowsOffset);
        r.textureCols = readU16(data, blobSize, off + kTextureColsOffset);
        r.texSlotTrackOffset = static_cast<uint32_t>(off + kTexSlotTrackOffset);
        r.visibilityTrackOffset = static_cast<uint32_t>(off + kVisibilityTrackOffset);
        r.priorityPlane = static_cast<int16_t>(readU16(data, blobSize, off + kPriorityPlaneOffset));
        r.ribbonColorIndex = static_cast<int8_t>(readU8(data, blobSize, off + kRibbonColorIndexOffset));
        r.textureTransformLookupIndex =
            static_cast<int8_t>(readU8(data, blobSize, off + kTextureTransformLookupIndexOffset));
        ribbons.push_back(r);
    }

    return ribbons;
}

std::vector<ParticleEmitter> parseParticles(const std::vector<uint8_t>& blob, const Array& array) {
    std::vector<ParticleEmitter> particles;
    if (array.count == 0) {
        return particles;
    }

    // M2Particle, Cata+ shape (wowdev.wiki M2#Particle_emitters -- see
    // ParticleEmitter's doc comment in m2_scene.hpp for the full offset
    // derivation and its real-file cross-check).
    constexpr size_t kParticleSize = 0x1EC;
    constexpr size_t kParticleIdOffset = 0x00;
    constexpr size_t kFlagsOffset = 0x04;
    constexpr size_t kPositionOffset = 0x08;
    constexpr size_t kBoneIdOffset = 0x14;
    constexpr size_t kTextureIdOffset = 0x16;
    constexpr size_t kParticleModelFilenameOffset = 0x18;
    constexpr size_t kChildEmittersModelFilenameOffset = 0x20;
    constexpr size_t kBlendingTypeOffset = 0x28;
    constexpr size_t kEmitterTypeOffset = 0x29;
    constexpr size_t kParticleColorIndexOffset = 0x2A;
    constexpr size_t kMultiTexScaleOffset = 0x2C;
    constexpr size_t kPriorityPlaneOffset = 0x2E;
    constexpr size_t kRowsOffset = 0x30;
    constexpr size_t kColumnsOffset = 0x32;
    constexpr size_t kEmissionSpeedTrackOffset = 0x34;
    constexpr size_t kSpeedVariationTrackOffset = 0x48;
    constexpr size_t kVerticalRangeTrackOffset = 0x5C;
    constexpr size_t kHorizontalRangeTrackOffset = 0x70;
    constexpr size_t kGravityTrackOffset = 0x84;
    constexpr size_t kLifespanTrackOffset = 0x98;
    constexpr size_t kLifespanVariationOffset = 0xAC;
    constexpr size_t kEmissionRateTrackOffset = 0xB0;
    constexpr size_t kEmissionRateVariationOffset = 0xC4;
    constexpr size_t kEmissionAreaWidthTrackOffset = 0xC8;
    constexpr size_t kEmissionAreaLengthTrackOffset = 0xDC;
    constexpr size_t kZSourceTrackOffset = 0xF0;
    constexpr size_t kColorTrackBlockOffset = 0x104;
    constexpr size_t kAlphaTrackBlockOffset = 0x114;
    constexpr size_t kScaleTrackBlockOffset = 0x124;
    constexpr size_t kScaleVaryOffset = 0x134;
    constexpr size_t kHeadUVAnimBlockOffset = 0x13C;
    constexpr size_t kTailUVAnimBlockOffset = 0x14C;
    constexpr size_t kTailLengthOffset = 0x15C;
    constexpr size_t kTwinkleSpeedOffset = 0x160;
    constexpr size_t kTwinklePercentOffset = 0x164;
    constexpr size_t kTwinkleScaleOffset = 0x168;
    constexpr size_t kInheritVelocityScaleOffset = 0x170;
    constexpr size_t kDragOffset = 0x174;
    constexpr size_t kBaseSpinOffset = 0x178;
    constexpr size_t kBaseSpinVariationOffset = 0x17C;
    constexpr size_t kSpinSpeedOffset = 0x180;
    constexpr size_t kSpinSpeedVariationOffset = 0x184;
    constexpr size_t kTumbleOffset = 0x188;
    constexpr size_t kWindVectorOffset = 0x1A0;
    constexpr size_t kWindTimeOffset = 0x1AC;
    constexpr size_t kFollowSpeed1Offset = 0x1B0;
    constexpr size_t kFollowScale1Offset = 0x1B4;
    constexpr size_t kFollowSpeed2Offset = 0x1B8;
    constexpr size_t kFollowScale2Offset = 0x1BC;
    constexpr size_t kSplinePointsOffset = 0x1C0;
    constexpr size_t kEnabledInTrackOffset = 0x1C8;
    constexpr size_t kMultiTexScrollMidOffset = 0x1DC;
    constexpr size_t kMultiTexScrollRangeOffset = 0x1E4;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kParticleSize) {
        throw ParseError("particleEmitters array claims " + std::to_string(array.count) +
                          " records (" + std::to_string(kParticleSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    particles.reserve(array.count);

    // fixed_point<int8_t,2,5>: 2 integer bits + 5 fraction bits -> raw/32.
    auto readFixed8_2_5 = [&](size_t o) {
        int8_t raw;
        uint8_t bits = readU8(data, blobSize, o);
        std::memcpy(&raw, &bits, sizeof(raw));
        return static_cast<float>(raw) / 32.0f;
    };
    // fixed_point<uint16_t,6,9>: 6 integer bits + 9 fraction bits -> raw/512.
    auto readFixed16_6_9 = [&](size_t o) {
        return static_cast<float>(readU16(data, blobSize, o)) / 512.0f;
    };

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kParticleSize;
        ParticleEmitter p;
        p.particleId = readU32(data, blobSize, off + kParticleIdOffset);
        p.flags = readU32(data, blobSize, off + kFlagsOffset);
        p.position = readVec3(data, blobSize, off + kPositionOffset);
        p.boneId = readU16(data, blobSize, off + kBoneIdOffset);
        p.textureId = readU16(data, blobSize, off + kTextureIdOffset);
        p.particleModelFilename =
            readName(data, blobSize, readArray(data, blobSize, off + kParticleModelFilenameOffset));
        p.childEmittersModelFilename = readName(
            data, blobSize, readArray(data, blobSize, off + kChildEmittersModelFilenameOffset));
        p.blendingType = readU8(data, blobSize, off + kBlendingTypeOffset);
        p.emitterType = readU8(data, blobSize, off + kEmitterTypeOffset);
        p.particleColorIndex = readU16(data, blobSize, off + kParticleColorIndexOffset);
        p.multiTexScale[0] = readFixed8_2_5(off + kMultiTexScaleOffset);
        p.multiTexScale[1] = readFixed8_2_5(off + kMultiTexScaleOffset + 1);
        p.priorityPlane = static_cast<int16_t>(readU16(data, blobSize, off + kPriorityPlaneOffset));
        p.rows = readU16(data, blobSize, off + kRowsOffset);
        p.columns = readU16(data, blobSize, off + kColumnsOffset);
        p.emissionSpeedTrackOffset = static_cast<uint32_t>(off + kEmissionSpeedTrackOffset);
        p.speedVariationTrackOffset = static_cast<uint32_t>(off + kSpeedVariationTrackOffset);
        p.verticalRangeTrackOffset = static_cast<uint32_t>(off + kVerticalRangeTrackOffset);
        p.horizontalRangeTrackOffset = static_cast<uint32_t>(off + kHorizontalRangeTrackOffset);
        p.gravityTrackOffset = static_cast<uint32_t>(off + kGravityTrackOffset);
        p.lifespanTrackOffset = static_cast<uint32_t>(off + kLifespanTrackOffset);
        p.lifespanVariation = readF32(data, blobSize, off + kLifespanVariationOffset);
        p.emissionRateTrackOffset = static_cast<uint32_t>(off + kEmissionRateTrackOffset);
        p.emissionRateVariation = readF32(data, blobSize, off + kEmissionRateVariationOffset);
        p.emissionAreaWidthTrackOffset = static_cast<uint32_t>(off + kEmissionAreaWidthTrackOffset);
        p.emissionAreaLengthTrackOffset = static_cast<uint32_t>(off + kEmissionAreaLengthTrackOffset);
        p.zSourceTrackOffset = static_cast<uint32_t>(off + kZSourceTrackOffset);
        p.colorTrackBlockOffset = static_cast<uint32_t>(off + kColorTrackBlockOffset);
        p.alphaTrackBlockOffset = static_cast<uint32_t>(off + kAlphaTrackBlockOffset);
        p.scaleTrackBlockOffset = static_cast<uint32_t>(off + kScaleTrackBlockOffset);
        p.scaleVary.x = readF32(data, blobSize, off + kScaleVaryOffset);
        p.scaleVary.y = readF32(data, blobSize, off + kScaleVaryOffset + 4);
        p.headUVAnimBlockOffset = static_cast<uint32_t>(off + kHeadUVAnimBlockOffset);
        p.tailUVAnimBlockOffset = static_cast<uint32_t>(off + kTailUVAnimBlockOffset);
        p.tailLength = readF32(data, blobSize, off + kTailLengthOffset);
        p.twinkleSpeed = readF32(data, blobSize, off + kTwinkleSpeedOffset);
        p.twinklePercent = readF32(data, blobSize, off + kTwinklePercentOffset);
        p.twinkleScaleMin = readF32(data, blobSize, off + kTwinkleScaleOffset);
        p.twinkleScaleMax = readF32(data, blobSize, off + kTwinkleScaleOffset + 4);
        p.inheritVelocityScale = readF32(data, blobSize, off + kInheritVelocityScaleOffset);
        p.drag = readF32(data, blobSize, off + kDragOffset);
        p.baseSpin = readF32(data, blobSize, off + kBaseSpinOffset);
        p.baseSpinVariation = readF32(data, blobSize, off + kBaseSpinVariationOffset);
        p.spinSpeed = readF32(data, blobSize, off + kSpinSpeedOffset);
        p.spinSpeedVariation = readF32(data, blobSize, off + kSpinSpeedVariationOffset);
        p.tumbleMin = readVec3(data, blobSize, off + kTumbleOffset);
        p.tumbleMax = readVec3(data, blobSize, off + kTumbleOffset + 12);
        p.windVector = readVec3(data, blobSize, off + kWindVectorOffset);
        p.windTime = readF32(data, blobSize, off + kWindTimeOffset);
        p.followSpeed1 = readF32(data, blobSize, off + kFollowSpeed1Offset);
        p.followScale1 = readF32(data, blobSize, off + kFollowScale1Offset);
        p.followSpeed2 = readF32(data, blobSize, off + kFollowSpeed2Offset);
        p.followScale2 = readF32(data, blobSize, off + kFollowScale2Offset);
        p.splinePoints =
            parseVec3Array(blob, readArray(data, blobSize, off + kSplinePointsOffset));
        p.enabledInTrackOffset = static_cast<uint32_t>(off + kEnabledInTrackOffset);
        p.multiTexScrollMid[0] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 0);
        p.multiTexScrollMid[1] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 2);
        p.multiTexScrollMid[2] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 4);
        p.multiTexScrollMid[3] = readFixed16_6_9(off + kMultiTexScrollMidOffset + 6);
        p.multiTexScrollRange[0] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 0);
        p.multiTexScrollRange[1] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 2);
        p.multiTexScrollRange[2] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 4);
        p.multiTexScrollRange[3] = readFixed16_6_9(off + kMultiTexScrollRangeOffset + 6);
        particles.push_back(p);
    }

    return particles;
}

std::vector<ExtendedParticle> parseExtendedParticles(const std::vector<uint8_t>& blob,
                                                       const Array& array) {
    std::vector<ExtendedParticle> out;
    if (array.count == 0) {
        return out;
    }

    // M2ExtendedParticle, wowdev.wiki M2#EXP2 -- see ExtendedParticle's doc
    // comment in m2_scene.hpp for the full offset derivation (unverified
    // against real data, structurally unambiguous either way).
    constexpr size_t kSize = 0x1C;
    constexpr size_t kZSourceOffset = 0x00;
    constexpr size_t kColorMultOffset = 0x04;
    constexpr size_t kAlphaMultOffset = 0x08;
    constexpr size_t kAlphaCutoffOffset = 0x0C;

    const uint8_t* data = blob.data();
    size_t blobSize = blob.size();

    if (array.offset > blobSize || array.count > (blobSize - array.offset) / kSize) {
        throw ParseError("EXP2 content array claims " + std::to_string(array.count) + " records (" +
                          std::to_string(kSize) + " bytes each) at offset " +
                          std::to_string(array.offset) + ", which needs more room than the blob's " +
                          std::to_string(blobSize) + " bytes");
    }
    out.reserve(array.count);

    for (uint32_t i = 0; i < array.count; ++i) {
        size_t off = static_cast<size_t>(array.offset) + static_cast<size_t>(i) * kSize;
        ExtendedParticle p;
        p.zSource = readF32(data, blobSize, off + kZSourceOffset);
        p.colorMult = readF32(data, blobSize, off + kColorMultOffset);
        p.alphaMult = readF32(data, blobSize, off + kAlphaMultOffset);
        p.alphaCutoffOffset = static_cast<uint32_t>(off + kAlphaCutoffOffset);
        out.push_back(p);
    }

    return out;
}

}  // namespace husk::m2
