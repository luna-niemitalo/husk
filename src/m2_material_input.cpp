#include "m2_material_input.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace husk::m2input {

namespace {

// M2BLEND_* (wowdev.wiki M2/Rendering#M2BLEND): 0 OPAQUE, 1 ALPHA_KEY,
// 2 ALPHA, 3 NO_ALPHA_ADD, 4 ADD, 5 MOD, 6 MOD2X -- the same numeric table
// export_texture_resolution.cpp's own alphaModeForBlend doc comment cites
// ("0 (OPAQUE) maps directly, 1 (ALPHA_KEY) maps to MASK, and everything
// else... maps to BLEND"). No blendModeToGxTexOp-shaped conversion exists
// anywhere else in this codebase (confirmed by a full repo search before
// writing this) -- BlendOp's own EGxTexOp vocabulary names a texture
// *combiner* stage (how one texture unit folds into the running stack
// within a single draw), while M2Material::blendMode is that draw's final
// *framebuffer* blend against whatever's already on screen -- a different
// real GPU stage. MaterialLayer::blendIntoPrevious has no separate
// framebuffer-blend field to hold blendMode's actual meaning, so this maps
// one onto the other because BlendOp's six named modes happen to name-match
// MOD/MOD2X/ADD closely enough to be more useful than leaving every layer
// at the struct's bare default. This is genuinely new code, not a mirrored
// fact from elsewhere in the pipeline -- flagged in this task's own report
// for review, not treated as settled.
canon::BlendOp blendModeToBlendOp(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return canon::BlendOp::Replace;    // OPAQUE: full replace, no blend
        case 1: return canon::BlendOp::Replace;    // ALPHA_KEY: alpha-tested, still a full replace once it passes
        case 2: return canon::BlendOp::Fade;       // ALPHA: real alpha blend, closest named fit
        case 3: return canon::BlendOp::Add;        // NO_ALPHA_ADD
        case 4: return canon::BlendOp::Add;        // ADD
        case 5: return canon::BlendOp::Modulate;   // MOD
        case 6: return canon::BlendOp::Modulate2x; // MOD2X
        default: return canon::BlendOp::Fade;      // unknown mode: same "closest BLEND approximation" fallback alphaModeForBlend uses
    }
}

// Same decode export_texture_resolution.cpp's own decodeFixed16 uses
// (anonymous-namespace there, not exported for reuse across the
// commands/m2input boundary) -- wowdev.wiki M2#Colors_and_transparency's
// "0 - transparent, 0x7FFF - opaque" scale. Duplicated, not shared: see
// that file's own decodeFixed16 for the counterpart.
float decodeFixed16(uint32_t bits) {
    auto b = static_cast<uint16_t>(bits);
    int16_t raw;
    std::memcpy(&raw, &b, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

canon::Interpolation toInterpolation(const std::vector<uint8_t>& blob, uint32_t trackOffset) {
    return m2::readTrackMeta(blob, trackOffset).interpolationType == 0 ? canon::Interpolation::Step
                                                                        : canon::Interpolation::Linear;
}

canon::VecCurve toVecCurve(std::vector<std::pair<uint32_t, m2::Vec3>> raw, canon::Interpolation interpolation,
                            uint32_t sequenceIndex) {
    canon::VecCurve curve;
    curve.sequence = canon::SequenceRef::sequence(sequenceIndex);
    curve.interpolation = interpolation;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, v] : raw) curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
    return curve;
}

canon::QuatCurve toQuatCurve(std::vector<std::pair<uint32_t, m2::Quat>> raw, canon::Interpolation interpolation,
                              uint32_t sequenceIndex) {
    canon::QuatCurve curve;
    curve.sequence = canon::SequenceRef::sequence(sequenceIndex);
    curve.interpolation = interpolation;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, v] : raw) curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, v);
    return curve;
}

canon::ScalarCurve toScalarCurve(std::vector<std::pair<uint32_t, uint32_t>> raw, canon::Interpolation interpolation,
                                  uint32_t sequenceIndex) {
    canon::ScalarCurve curve;
    curve.sequence = canon::SequenceRef::sequence(sequenceIndex);
    curve.interpolation = interpolation;
    curve.keyframes.reserve(raw.size());
    for (const auto& [ts, bits] : raw)
        curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, decodeFixed16(bits));
    return curve;
}

// Layer 0's UV lookup (the batch's own textureCoordComboIndex, unoffset) --
// throws on an out-of-range index into a non-empty textureCoordCombos
// table, same required strictness export_materials.cpp applies to this
// exact field (that function's own throw, before its per-layer loop even
// starts). An empty table means the model doesn't use this pre-Cataclysm
// feature at all -- every layer is UV set 0, batch.textureCoordComboIndex
// never dereferenced, same convention M2MaterialInputs::textureCoordCombos
// documents.
canon::UvRef resolveUvStrict(uint16_t coordComboIndex, const M2MaterialInputs& m2, size_t batchIndex) {
    if (m2.textureCoordCombos.empty()) return canon::UvSetIndex{0};
    if (coordComboIndex >= m2.textureCoordCombos.size()) {
        throw std::runtime_error("batch " + std::to_string(batchIndex) + "'s textureCoordComboIndex (" +
                                  std::to_string(coordComboIndex) + ") is out of range for " +
                                  std::to_string(m2.textureCoordCombos.size()) +
                                  " textureCoordCombos entries");
    }
    uint16_t mapping = m2.textureCoordCombos[coordComboIndex];
    if (mapping == 0xFFFF) return canon::EnvironmentMapped{};
    return canon::UvSetIndex{static_cast<uint32_t>(mapping == 1 ? 1 : 0)};
}

// A later layer's (offset-adjusted) UV lookup -- best-effort, same
// tolerance export_materials.cpp's own additionalTextureLayers loop applies
// to this field: an out-of-range offset (or an empty table) just falls back
// to UV set 0 rather than throwing, since this is supplementary
// multi-texture metadata, not required for a usable layer.
canon::UvRef resolveUvBestEffort(size_t coordComboIdx, const M2MaterialInputs& m2) {
    if (m2.textureCoordCombos.empty() || coordComboIdx >= m2.textureCoordCombos.size()) {
        return canon::UvSetIndex{0};
    }
    uint16_t mapping = m2.textureCoordCombos[coordComboIdx];
    if (mapping == 0xFFFF) return canon::EnvironmentMapped{};
    return canon::UvSetIndex{static_cast<uint32_t>(mapping == 1 ? 1 : 0)};
}

// A layer's role: EnvironmentMapped is a real, unambiguous fact (a runtime-
// computed reflection vector has no other honest name -- KnownRole::Env
// exists for exactly this). Otherwise, M2Texture::type names a real slot
// (m2::textureTypeName, e.g. "skin"/"char_hair") whenever one is known --
// that string IS the honest role, not invented terminology, so it's used
// as-is via LayerRole's open-string alternative. Only when neither of
// those applies (type 0, an ordinary embedded/FileDataID-driven texture)
// does this fall back to a KnownRole: Diffuse for the primary (index-0)
// layer, since that's the one layer glTF's own baseColorTexture slot
// actually corresponds to; Unknown for any later layer, since assigning it
// e.g. Detail/Specular/Emission would be guessing at a shading role real
// M2 batch data doesn't itself assert -- that inference needs the
// shaderId's real Combiners_* formula decoded (ShadingFunction/Function,
// deliberately not built by this function, see m2_material_input.hpp's own
// doc comment).
canon::LayerRole resolveRole(const canon::UvRef& uv, uint32_t textureType, bool isPrimaryLayer) {
    if (std::holds_alternative<canon::EnvironmentMapped>(uv)) return canon::KnownRole::Env;
    if (const char* typeName = m2::textureTypeName(textureType)) return std::string(typeName);
    return isPrimaryLayer ? canon::LayerRole(canon::KnownRole::Diffuse) : canon::LayerRole(canon::KnownRole::Unknown);
}

// A layer's stable identity: the M2 texture-array index (RecordIndex), not
// a raw position in canon::Material::layers -- same "identity a named slot
// points at" reasoning MaterialLayer::identity's own doc comment states.
// Name preference: a real embedded M2 filename (NameSource::M2Embedded, a
// genuine per-file string) when the texture actually carries one, else the
// wiki-derived type name (NameSource::Synthesized -- husk derives this
// string algorithmically from a constant, the same justification
// ShadingFunction::identity's own doc comment gives for that source), else
// no name at all.
canon::Ref layerIdentity(uint16_t textureIndex, const m2::Texture& tex) {
    canon::Ref ref;
    ref.id = canon::RecordIndex{textureIndex};
    if (!tex.filename.empty()) {
        ref.name = tex.filename;
        ref.source = canon::NameSource::M2Embedded;
    } else if (const char* typeName = m2::textureTypeName(tex.type)) {
        ref.name = typeName;
        ref.source = canon::NameSource::Synthesized;
    }
    return ref;
}

// Deliberately never resolved here -- see m2_material_input.hpp's own doc
// comment for why performing texture resolution is out of this function's
// scope. The honest default when the caller hasn't supplied anything for
// this layer's key via `textureResolutions`.
canon::TextureRef unresolvedTextureRef() {
    canon::TextureRef t;
    t.state = canon::TextureRef::State::KnownUnresolved;
    t.unresolvedReason = "texture resolution out of scope for canon assembly";
    return t;
}

// Looks up `textureIndex` (the same M2 texture array index
// `layerIdentity` below keys `Ref::id`'s `RecordIndex` on) in the caller's
// already-resolved map; falls back to `unresolvedTextureRef()` when absent
// -- see `TextureResolutions`'s own doc comment (m2_material_input.hpp)
// for why this is the one place resolution data enters this input module
// at all.
canon::TextureRef resolveTextureRef(uint16_t textureIndex, const TextureResolutions& textureResolutions) {
    auto it = textureResolutions.find(static_cast<uint32_t>(textureIndex));
    return it != textureResolutions.end() ? it->second : unresolvedTextureRef();
}

std::optional<canon::VecCurve> resolveColorTint(const m2::Color& color, const M2MaterialInputs& m2,
                                                 uint32_t sequenceIndex) {
    if (!color.colorAnimated || !m2.blob) return std::nullopt;
    auto raw = m2::resolveVec3TrackSequence(*m2.blob, color.colorTrackOffset, sequenceIndex);
    if (raw.empty()) return std::nullopt;
    return toVecCurve(std::move(raw), toInterpolation(*m2.blob, color.colorTrackOffset), sequenceIndex);
}

// M2Color::alpha is an M2Track<fixed16> -- same raw-int-then-scale shape
// export_materials.cpp's resolveAnimatedFixed16Curve reads via
// m2::resolveRawIntTrackSequence(..., elementSize=2, ...), reused directly
// here rather than re-derived.
std::optional<canon::ScalarCurve> resolveColorAlphaFade(const m2::Color& color, const M2MaterialInputs& m2,
                                                          uint32_t sequenceIndex) {
    if (!color.alphaAnimated || !m2.blob) return std::nullopt;
    auto raw = m2::resolveRawIntTrackSequence(*m2.blob, color.alphaTrackOffset, sequenceIndex,
                                               /*elementSize=*/2);
    if (raw.empty()) return std::nullopt;
    return toScalarCurve(std::move(raw), toInterpolation(*m2.blob, color.alphaTrackOffset), sequenceIndex);
}

std::optional<canon::ScalarCurve> resolveWeightFade(const m2::TextureWeight& weight, const M2MaterialInputs& m2,
                                                      uint32_t sequenceIndex) {
    if (!weight.weightAnimated || !m2.blob) return std::nullopt;
    auto raw = m2::resolveRawIntTrackSequence(*m2.blob, weight.weightTrackOffset, sequenceIndex,
                                               /*elementSize=*/2);
    if (raw.empty()) return std::nullopt;
    return toScalarCurve(std::move(raw), toInterpolation(*m2.blob, weight.weightTrackOffset), sequenceIndex);
}

std::optional<canon::MaterialLayer::TextureTransformCurves> resolveUvAnimation(const m2::TextureTransform& xf,
                                                                                 const M2MaterialInputs& m2,
                                                                                 uint32_t sequenceIndex) {
    if (!m2.blob) return std::nullopt;
    canon::MaterialLayer::TextureTransformCurves curves;
    bool any = false;

    if (xf.translationAnimated) {
        auto raw = m2::resolveVec3TrackSequence(*m2.blob, xf.translationTrackOffset, sequenceIndex);
        if (!raw.empty()) {
            curves.translation =
                toVecCurve(std::move(raw), toInterpolation(*m2.blob, xf.translationTrackOffset), sequenceIndex);
            any = true;
        }
    }
    // TextureTransform::rotation is a raw C4Quaternion (4 floats, no
    // M2CompQuat decompression) -- the same distinction
    // m2_animation_input.hpp's own doc comment draws for bone rotation
    // tracks, so this uses resolveRawQuatTrackSequence, not
    // resolveQuatTrackSequence.
    if (xf.rotationAnimated) {
        auto raw = m2::resolveRawQuatTrackSequence(*m2.blob, xf.rotationTrackOffset, sequenceIndex);
        if (!raw.empty()) {
            curves.rotation =
                toQuatCurve(std::move(raw), toInterpolation(*m2.blob, xf.rotationTrackOffset), sequenceIndex);
            any = true;
        }
    }
    if (xf.scalingAnimated) {
        auto raw = m2::resolveVec3TrackSequence(*m2.blob, xf.scalingTrackOffset, sequenceIndex);
        if (!raw.empty()) {
            curves.scaling =
                toVecCurve(std::move(raw), toInterpolation(*m2.blob, xf.scalingTrackOffset), sequenceIndex);
            any = true;
        }
    }
    return any ? std::optional<canon::MaterialLayer::TextureTransformCurves>(std::move(curves)) : std::nullopt;
}

// The required, layer-0 (primary) texture unit -- every check here throws
// on the same corruption cases export_materials.cpp's own batch loop does
// for its base texture, tint, and transparency-fade fields.
canon::MaterialLayer assemblePrimaryLayer(const skin::Batch& batch, size_t batchIndex, const M2MaterialInputs& m2,
                                           uint32_t sequenceIndex, canon::BlendOp blendOp,
                                           const TextureResolutions& textureResolutions) {
    if (batch.textureComboIndex >= m2.textureCombos.size()) {
        throw std::runtime_error("batch " + std::to_string(batchIndex) + "'s textureComboIndex (" +
                                  std::to_string(batch.textureComboIndex) + ") is out of range for " +
                                  std::to_string(m2.textureCombos.size()) + " textureCombos entries");
    }
    uint16_t textureIndex = m2.textureCombos[batch.textureComboIndex];
    if (textureIndex >= m2.textures.size()) {
        throw std::runtime_error("batch " + std::to_string(batchIndex) + "'s texture (index " +
                                  std::to_string(textureIndex) + " via textureCombos[" +
                                  std::to_string(batch.textureComboIndex) + "]) is out of range for " +
                                  std::to_string(m2.textures.size()) + " textures");
    }
    const auto& tex = m2.textures[textureIndex];

    canon::MaterialLayer layer;
    layer.uv = resolveUvStrict(batch.textureCoordComboIndex, m2, batchIndex);
    layer.identity = layerIdentity(textureIndex, tex);
    layer.texture = resolveTextureRef(textureIndex, textureResolutions);
    layer.role = resolveRole(layer.uv, tex.type, /*isPrimaryLayer=*/true);
    layer.blendIntoPrevious = blendOp;

    // colorIndex is genuinely optional (0xFFFF/"none" is common and
    // expected); once present it's a required, bounds-checked lookup, same
    // strictness export_materials.cpp applies.
    if (batch.colorIndex != 0xFFFF) {
        if (batch.colorIndex >= m2.colors.size()) {
            throw std::runtime_error("batch " + std::to_string(batchIndex) + "'s colorIndex (" +
                                      std::to_string(batch.colorIndex) + ") is out of range for " +
                                      std::to_string(m2.colors.size()) + " colors");
        }
        const auto& color = m2.colors[batch.colorIndex];
        layer.tint = resolveColorTint(color, m2, sequenceIndex);
        layer.alphaFade = resolveColorAlphaFade(color, m2, sequenceIndex);
    }

    // textureWeightComboIndex is documented as not nullable (every real
    // batch export_materials.cpp was tested against has a valid one), so
    // it's resolved unconditionally like any other required index, exactly
    // matching that function's own two-level bounds check (combo -> weight).
    // A real M2Color and M2TextureWeight can independently animate at the
    // same time ("I assume these are multiplied together" per
    // m2::TextureWeight's own doc comment) -- canon::MaterialLayer has only
    // one `alphaFade` slot to canon_material.hpp's own current design, not
    // two, so a color-alpha curve (set just above) wins over a weight-fade
    // curve when both are genuinely animated on the same batch. Flagged in
    // this task's own report as a real representational gap, not silently
    // glossed over.
    if (!m2.textureWeightCombos.empty()) {
        if (batch.textureWeightComboIndex >= m2.textureWeightCombos.size()) {
            throw std::runtime_error(
                "batch " + std::to_string(batchIndex) + "'s textureWeightComboIndex (" +
                std::to_string(batch.textureWeightComboIndex) + ") is out of range for " +
                std::to_string(m2.textureWeightCombos.size()) + " textureWeightCombos entries");
        }
        uint16_t weightIndex = m2.textureWeightCombos[batch.textureWeightComboIndex];
        if (weightIndex >= m2.textureWeights.size()) {
            throw std::runtime_error(
                "batch " + std::to_string(batchIndex) + "'s texture weight (index " +
                std::to_string(weightIndex) + " via textureWeightCombos[" +
                std::to_string(batch.textureWeightComboIndex) + "]) is out of range for " +
                std::to_string(m2.textureWeights.size()) + " textureWeights entries");
        }
        if (!layer.alphaFade) {
            layer.alphaFade = resolveWeightFade(m2.textureWeights[weightIndex], m2, sequenceIndex);
        }
    }

    // textureTransformComboIndex is best-effort even when present (an
    // out-of-range combo/transform index is skipped, not a failure), same
    // tolerance export_materials.cpp applies to this field.
    if (batch.textureTransformComboIndex != 0xFFFF &&
        batch.textureTransformComboIndex < m2.textureTransformCombos.size()) {
        uint16_t transformIndex = m2.textureTransformCombos[batch.textureTransformComboIndex];
        if (transformIndex < m2.textureTransforms.size()) {
            layer.uvAnimation = resolveUvAnimation(m2.textureTransforms[transformIndex], m2, sequenceIndex);
        }
    }

    return layer;
}

}  // namespace

canon::Material assembleMaterial(const skin::Batch& batch, size_t batchIndex, const M2MaterialInputs& m2,
                                  uint32_t sequenceIndex, const TextureResolutions& textureResolutions) {
    canon::Material result;

    // export_materials.cpp checks materialIndex unconditionally, before its
    // own `if (b.textureCount > 0)` gate -- a real gltf::Material is built
    // from `mat` (alphaMode/blendMode/doubleSided/unlit) even for a
    // textureless batch, so a corrupt materialIndex is a real foreign-data
    // problem regardless of textureCount. Mirrored here with the same
    // ordering even though canon::Material has no batch-wide equivalent of
    // those fields to actually populate when there ends up being no layer
    // to carry blendOp on.
    if (batch.materialIndex >= m2.materials.size()) {
        throw std::runtime_error("batch " + std::to_string(batchIndex) + "'s materialIndex (" +
                                  std::to_string(batch.materialIndex) + ") is out of range for " +
                                  std::to_string(m2.materials.size()) + " materials");
    }
    // blendMode is shared by every layer of this batch's material -- there
    // is only one M2Material per batch, not one per texture unit, so every
    // layer in this stack blends the same way.
    canon::BlendOp blendOp = blendModeToBlendOp(m2.materials[batch.materialIndex].blendMode);

    // M2Batch::textureCount == 0: no real M2 texture unit for this batch at
    // all -- Material::layers documents "one entry per real M2 texture
    // unit", so zero is the honest count, not a placeholder layer.
    if (batch.textureCount == 0) {
        return result;
    }

    result.layers.push_back(
        assemblePrimaryLayer(batch, batchIndex, m2, sequenceIndex, blendOp, textureResolutions));

    // Additional texture layers (textureCount > 1): per wowdev.wiki
    // M2/.skin#Texture_units, layer i's real combo index is
    // textureComboIndex + i. Mirrors export_materials.cpp's own
    // additionalTextureLayers loop exactly, including its two distinct
    // failure responses: an out-of-range *combo* index stops the loop
    // outright (further offsets only grow further out of range), while an
    // out-of-range *texture* index for one specific offset just skips that
    // one layer and keeps trying later ones.
    for (uint16_t layerOffset = 1; layerOffset < batch.textureCount; ++layerOffset) {
        size_t comboIdx = static_cast<size_t>(batch.textureComboIndex) + layerOffset;
        if (comboIdx >= m2.textureCombos.size()) break;
        uint16_t textureIndex = m2.textureCombos[comboIdx];
        if (textureIndex >= m2.textures.size()) continue;
        const auto& tex = m2.textures[textureIndex];

        canon::MaterialLayer layer;
        layer.uv = resolveUvBestEffort(static_cast<size_t>(batch.textureCoordComboIndex) + layerOffset, m2);
        layer.identity = layerIdentity(textureIndex, tex);
        layer.texture = resolveTextureRef(textureIndex, textureResolutions);
        layer.role = resolveRole(layer.uv, tex.type, /*isPrimaryLayer=*/false);
        layer.blendIntoPrevious = blendOp;
        result.layers.push_back(std::move(layer));
    }

    if (std::holds_alternative<canon::KnownRole>(result.layers.front().role) &&
        std::get<canon::KnownRole>(result.layers.front().role) == canon::KnownRole::Diffuse) {
        result.diffuseLayer = result.layers.front().identity;
    }

    return result;
}

}  // namespace husk::m2input
