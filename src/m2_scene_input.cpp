#include "m2_scene_input.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "canon_physics.hpp"  // boneRef
#include "canon_policy.hpp"
#include "m2_animation.hpp"

namespace husk::m2input {

namespace {

constexpr uint32_t kMultiTextureParticleFlag = 0x10000000;

// Counterpart: m2_material_input.cpp's decodeFixed16 (anonymous there too).
float decodeFixed16(uint32_t bits) {
    auto b = static_cast<uint16_t>(bits);
    int16_t raw;
    std::memcpy(&raw, &b, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

canon::Vec3 toCanon(const m2::Vec3& v) { return {v.x, v.y, v.z}; }
canon::Vec2 toCanon(const m2::Vec2& v) { return {v.x, v.y}; }

// Where one model's M2Track data lives: `blob` for descriptors and inline
// keyframes, `external` for a non-inline sequence's keyframes.
struct TrackSource {
    const std::vector<uint8_t>& blob;
    uint32_t sequenceCount;
    const ExternalAnimBlobs& external;
};

template <typename T, typename Raw, typename ResolveSeq, typename ResolveGlobal, typename Convert>
canon::Animated<T> resolveTrack(const TrackSource& src, uint32_t trackOffset, ResolveSeq resolveSequence,
                                ResolveGlobal resolveGlobal, Convert convert) {
    m2::TrackMeta meta = m2::readTrackMeta(src.blob, trackOffset);
    canon::Interpolation interpolation =
        meta.interpolationType == 0 ? canon::Interpolation::Step : canon::Interpolation::Linear;
    auto toCurve = [&](canon::SequenceRef sequence, const std::vector<std::pair<uint32_t, Raw>>& raw) {
        canon::Curve<T> curve;
        curve.sequence = sequence;
        curve.interpolation = interpolation;
        curve.keyframes.reserve(raw.size());
        for (const auto& [ms, value] : raw) curve.keyframes.emplace_back(static_cast<float>(ms) / 1000.0f, convert(value));
        return curve;
    };

    canon::Animated<T> curves;
    if (meta.globalSequence != m2::TrackMeta::kNoGlobalSequence) {
        // A global-sequence track has no per-sequence .anim data to redirect to.
        auto raw = resolveGlobal(src.blob, trackOffset);
        if (!raw.empty()) curves.push_back(toCurve(canon::SequenceRef::globalSequence(meta.globalSequence), raw));
        return curves;
    }
    for (uint32_t si = 0; si < src.sequenceCount; ++si) {
        auto ext = src.external.find(si);
        auto raw = resolveSequence(src.blob, trackOffset, si, ext == src.external.end() ? nullptr : &ext->second);
        if (!raw.empty()) curves.push_back(toCurve(canon::SequenceRef::sequence(si), raw));
    }
    return curves;
}

using Blob = std::vector<uint8_t>;

canon::Animated<canon::Vec3> vec3Track(const TrackSource& src, uint32_t trackOffset) {
    return resolveTrack<canon::Vec3, m2::Vec3>(
        src, trackOffset,
        [](const Blob& b, uint32_t off, uint32_t si, const Blob* ext) {
            return m2::resolveVec3TrackSequence(b, off, si, ext);
        },
        [](const Blob& b, uint32_t off) { return m2::resolveVec3GlobalSequenceTrack(b, off); },
        [](const m2::Vec3& v) { return toCanon(v); });
}

canon::Animated<float> floatTrack(const TrackSource& src, uint32_t trackOffset) {
    return resolveTrack<float, float>(
        src, trackOffset,
        [](const Blob& b, uint32_t off, uint32_t si, const Blob* ext) {
            return m2::resolveFloatTrackSequence(b, off, si, ext);
        },
        [](const Blob& b, uint32_t off) { return m2::resolveFloatGlobalSequenceTrack(b, off); },
        [](float v) { return v; });
}

// An integer-valued track (a 0/1 flag or a small index), widened to float.
canon::Animated<float> intTrack(const TrackSource& src, uint32_t trackOffset, size_t elementSize) {
    return resolveTrack<float, uint32_t>(
        src, trackOffset,
        [elementSize](const Blob& b, uint32_t off, uint32_t si, const Blob* ext) {
            return m2::resolveRawIntTrackSequence(b, off, si, elementSize, ext);
        },
        [elementSize](const Blob& b, uint32_t off) { return m2::resolveRawIntGlobalSequenceTrack(b, off, elementSize); },
        [](uint32_t v) { return static_cast<float>(v); });
}

canon::Animated<float> fixed16Track(const TrackSource& src, uint32_t trackOffset) {
    return resolveTrack<float, uint32_t>(
        src, trackOffset,
        [](const Blob& b, uint32_t off, uint32_t si, const Blob* ext) {
            return m2::resolveRawIntTrackSequence(b, off, si, 2, ext);
        },
        [](const Blob& b, uint32_t off) { return m2::resolveRawIntGlobalSequenceTrack(b, off, 2); },
        decodeFixed16);
}

template <typename T, typename Raw, typename Convert>
canon::LifetimeCurve<T> lifetimeCurve(const std::vector<std::pair<uint16_t, Raw>>& raw, Convert convert) {
    canon::LifetimeCurve<T> curve;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, value] : raw) curve.keyframes.emplace_back(ts, convert(value));
    return curve;
}

canon::TextureRef textureFor(const m2::Model& model, const TextureResolutions& resolutions, uint16_t textureIndex,
                             const std::string& what) {
    if (textureIndex >= model.textures.size()) {
        throw std::runtime_error(what + " texture index: expected < " + std::to_string(model.textures.size()) +
                                  ", got " + std::to_string(textureIndex));
    }
    auto it = resolutions.find(textureIndex);
    if (it != resolutions.end()) return it->second;
    canon::TextureRef unresolved;
    unresolved.unresolvedReason = "texture " + std::to_string(textureIndex) + " was not resolved";
    return unresolved;
}

std::optional<canon::Ref> embeddedPath(const std::string& path) {
    if (path.empty()) return std::nullopt;
    return canon::Ref{canon::None{}, path, canon::NameSource::M2Embedded};
}

canon::Ref requiredBone(int64_t bone, const std::string& what) {
    if (bone < 0) {
        throw std::runtime_error(what + " bone: expected a bone index >= 0, got " + std::to_string(bone));
    }
    return canon::boneRef(static_cast<uint32_t>(bone));
}

std::vector<canon::Attachment> assembleAttachments(const std::vector<m2::Attachment>& attachments,
                                                   const TrackSource& src) {
    std::vector<canon::Attachment> out;
    out.reserve(attachments.size());
    for (size_t i = 0; i < attachments.size(); ++i) {
        const m2::Attachment& a = attachments[i];
        canon::Attachment attachment;
        const char* kind = m2::attachmentTypeName(a.id);
        attachment.ref = canon::Ref{canon::RecordIndex{static_cast<uint32_t>(i)}, kind ? kind : "",
                                    kind ? canon::NameSource::Synthesized : canon::NameSource::None};
        attachment.pointId = a.id;
        attachment.bone = requiredBone(a.bone, "attachment " + std::to_string(i));
        attachment.position = toCanon(a.position);
        attachment.animateAttached = intTrack(src, a.animateAttachedTrackOffset, 1);
        out.push_back(std::move(attachment));
    }
    return out;
}

canon::Light assembleLight(const m2::Light& l, const TrackSource& src) {
    canon::Light light;
    light.type = l.type;
    if (l.bone >= 0) light.bone = canon::boneRef(static_cast<uint32_t>(l.bone));
    light.position = toCanon(l.position);
    light.ambientColor = vec3Track(src, l.ambientColorTrackOffset);
    light.ambientIntensity = floatTrack(src, l.ambientIntensityTrackOffset);
    light.diffuseColor = vec3Track(src, l.diffuseColorTrackOffset);
    light.diffuseIntensity = floatTrack(src, l.diffuseIntensityTrackOffset);
    light.attenuationStart = floatTrack(src, l.attenuationStartTrackOffset);
    light.attenuationEnd = floatTrack(src, l.attenuationEndTrackOffset);
    light.visibility = intTrack(src, l.visibilityTrackOffset, 1);
    return light;
}

canon::RibbonEmitter assembleRibbon(const m2::Model& model, const m2::Ribbon& r, size_t index, const TrackSource& src,
                                    const TextureResolutions& resolutions) {
    std::string what = "ribbon emitter " + std::to_string(index);
    canon::RibbonEmitter ribbon;
    ribbon.ribbonId = r.ribbonId;
    ribbon.bone = canon::boneRef(r.boneIndex);
    ribbon.position = toCanon(r.position);
    for (uint16_t t : r.textureIndices) ribbon.textures.push_back(textureFor(model, resolutions, t, what));
    for (uint16_t m : r.materialIndices) {
        if (m >= model.materials.size()) {
            throw std::runtime_error(what + " material index: expected < " + std::to_string(model.materials.size()) +
                                      ", got " + std::to_string(m));
        }
        const m2::Material& material = model.materials[m];
        canon::RenderState state;
        state.flags = material.flags;
        if (material.blendMode <= static_cast<uint16_t>(canon::FramebufferBlend::BlendAdd)) {
            state.blend = static_cast<canon::FramebufferBlend>(material.blendMode);
        }
        ribbon.materials.push_back(state);
    }
    ribbon.color = vec3Track(src, r.colorTrackOffset);
    ribbon.alpha = fixed16Track(src, r.alphaTrackOffset);
    ribbon.heightAbove = floatTrack(src, r.heightAboveTrackOffset);
    ribbon.heightBelow = floatTrack(src, r.heightBelowTrackOffset);
    ribbon.textureSlot = intTrack(src, r.texSlotTrackOffset, 2);
    ribbon.visibility = intTrack(src, r.visibilityTrackOffset, 1);
    ribbon.edgesPerSecond = r.edgesPerSecond;
    ribbon.edgeLifetime = r.edgeLifetime;
    ribbon.gravity = r.gravity;
    ribbon.textureRows = r.textureRows;
    ribbon.textureColumns = r.textureCols;
    ribbon.priorityPlane = r.priorityPlane;
    ribbon.ribbonColorIndex = r.ribbonColorIndex;
    ribbon.textureTransformLookupIndex = r.textureTransformLookupIndex;
    return ribbon;
}

canon::ParticleEmitter assembleParticle(const m2::Model& model, const m2::ParticleEmitter& p, size_t index,
                                        const TrackSource& src, const TextureResolutions& resolutions) {
    const std::vector<uint8_t>& blob = model.blob;
    canon::ParticleEmitter particle;
    particle.particleId = p.particleId;
    particle.flags = p.flags;
    particle.bone = canon::boneRef(p.boneId);
    particle.position = toCanon(p.position);
    for (uint16_t t : particleTextureIndices(p)) {
        particle.textures.push_back(textureFor(model, resolutions, t, "particle emitter " + std::to_string(index)));
    }
    particle.particleModel = embeddedPath(p.particleModelFilename);
    particle.childEmittersModel = embeddedPath(p.childEmittersModelFilename);
    particle.blendingType = p.blendingType;
    particle.emitterType = p.emitterType;
    particle.particleColorIndex = p.particleColorIndex;
    std::copy(std::begin(p.multiTexScale), std::end(p.multiTexScale), particle.multiTextureScale);
    particle.priorityPlane = p.priorityPlane;
    particle.textureRows = p.rows;
    particle.textureColumns = p.columns;

    particle.emissionSpeed = floatTrack(src, p.emissionSpeedTrackOffset);
    particle.speedVariation = floatTrack(src, p.speedVariationTrackOffset);
    particle.verticalRange = floatTrack(src, p.verticalRangeTrackOffset);
    particle.horizontalRange = floatTrack(src, p.horizontalRangeTrackOffset);
    particle.gravity = floatTrack(src, p.gravityTrackOffset);
    particle.lifespan = floatTrack(src, p.lifespanTrackOffset);
    particle.emissionRate = floatTrack(src, p.emissionRateTrackOffset);
    particle.emissionAreaLength = floatTrack(src, p.emissionAreaLengthTrackOffset);
    particle.emissionAreaWidth = floatTrack(src, p.emissionAreaWidthTrackOffset);
    particle.zSource = floatTrack(src, p.zSourceTrackOffset);
    particle.enabledIn = intTrack(src, p.enabledInTrackOffset, 1);
    particle.lifespanVariation = p.lifespanVariation;
    particle.emissionRateVariation = p.emissionRateVariation;

    particle.color = lifetimeCurve<canon::Vec3>(m2::resolveFBlockVec3(blob, p.colorTrackBlockOffset),
                                                [](const m2::Vec3& v) { return toCanon(v); });
    particle.alpha = lifetimeCurve<float>(m2::resolveFBlockFixed16(blob, p.alphaTrackBlockOffset), [](float v) { return v; });
    particle.scale = lifetimeCurve<canon::Vec2>(m2::resolveFBlockVec2(blob, p.scaleTrackBlockOffset),
                                                [](const m2::Vec2& v) { return toCanon(v); });
    particle.headCell =
        lifetimeCurve<uint16_t>(m2::resolveFBlockUint16(blob, p.headUVAnimBlockOffset), [](uint16_t v) { return v; });
    particle.tailCell =
        lifetimeCurve<uint16_t>(m2::resolveFBlockUint16(blob, p.tailUVAnimBlockOffset), [](uint16_t v) { return v; });
    particle.scaleVariation = toCanon(p.scaleVary);

    particle.tailLength = p.tailLength;
    particle.twinkleSpeed = p.twinkleSpeed;
    particle.twinklePercent = p.twinklePercent;
    particle.twinkleScaleMin = p.twinkleScaleMin;
    particle.twinkleScaleMax = p.twinkleScaleMax;
    particle.inheritVelocityScale = p.inheritVelocityScale;
    particle.drag = p.drag;
    particle.baseSpin = p.baseSpin;
    particle.baseSpinVariation = p.baseSpinVariation;
    particle.spinSpeed = p.spinSpeed;
    particle.spinSpeedVariation = p.spinSpeedVariation;
    particle.tumbleMin = toCanon(p.tumbleMin);
    particle.tumbleMax = toCanon(p.tumbleMax);
    particle.windVector = toCanon(p.windVector);
    particle.windTime = p.windTime;
    particle.followSpeed1 = p.followSpeed1;
    particle.followScale1 = p.followScale1;
    particle.followSpeed2 = p.followSpeed2;
    particle.followScale2 = p.followScale2;
    for (const m2::Vec3& point : p.splinePoints) particle.splinePoints.push_back(toCanon(point));
    std::copy(std::begin(p.multiTexScrollMid), std::end(p.multiTexScrollMid), particle.multiTextureScrollMid);
    std::copy(std::begin(p.multiTexScrollRange), std::end(p.multiTexScrollRange), particle.multiTextureScrollRange);
    return particle;
}

}  // namespace

std::vector<uint16_t> particleTextureIndices(const m2::ParticleEmitter& emitter) {
    if ((emitter.flags & kMultiTextureParticleFlag) == 0) return {emitter.textureId};
    return {static_cast<uint16_t>(emitter.textureId & 0x1F), static_cast<uint16_t>((emitter.textureId >> 5) & 0x1F),
            static_cast<uint16_t>((emitter.textureId >> 10) & 0x1F)};
}

canon::Scene assembleScene(const m2::Model& model, uint32_t sequenceCount, const ExternalAnimBlobs& externalAnimBlobs,
                           const TextureResolutions& textureResolutions, const skel::Attachments* skelAttachments) {
    constexpr auto kStrict = canon::PartialFailurePolicy::Strict;
    const TrackSource modelTracks{model.blob, sequenceCount, externalAnimBlobs};
    canon::Scene scene;

    if (skelAttachments) {
        const ExternalAnimBlobs inlineOnly;
        scene.attachments =
            assembleAttachments(skelAttachments->attachments, TrackSource{skelAttachments->blob, sequenceCount, inlineOnly});
    } else {
        canon::enforcePartialFailurePolicy(model, "attachments", kStrict);
        scene.attachments = assembleAttachments(model.attachments, modelTracks);
    }

    canon::enforcePartialFailurePolicy(model, "events", kStrict);
    for (const m2::Event& e : model.events) {
        scene.events.push_back({e.identifier, e.data, canon::boneRef(e.bone), toCanon(e.position)});
    }

    canon::enforcePartialFailurePolicy(model, "lights", kStrict);
    for (const m2::Light& l : model.lights) scene.lights.push_back(assembleLight(l, modelTracks));

    canon::enforcePartialFailurePolicy(model, "ribbon_emitters", kStrict);
    for (size_t i = 0; i < model.ribbonEmitters.size(); ++i) {
        scene.ribbons.push_back(assembleRibbon(model, model.ribbonEmitters[i], i, modelTracks, textureResolutions));
    }

    // m2::loadModel leaves particleEmitters empty below kMinVerifiedParticleVersion,
    // so a pre-Cataclysm model simply has none here.
    canon::enforcePartialFailurePolicy(model, "particle_emitters", kStrict);
    for (size_t i = 0; i < model.particleEmitters.size(); ++i) {
        scene.particles.push_back(assembleParticle(model, model.particleEmitters[i], i, modelTracks, textureResolutions));
    }
    return scene;
}

}  // namespace husk::m2input
