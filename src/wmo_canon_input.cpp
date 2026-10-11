#include "wmo_canon_input.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <string>

#include "liquid_canon_input.hpp"

namespace husk::wmoinput {

namespace {

constexpr uint32_t kFlagUnlit = 0x1;
constexpr uint32_t kFlagUnfogged = 0x2;
constexpr uint32_t kFlagUnculled = 0x4;

canon::Vec3 toCanon(const wmo::Vec3& v) { return {v[0], v[1], v[2]}; }

// MOMT.blendMode is an EGxBlend index (Rendering.md), not M2's remapped
// M2BLEND; only the modes canon::FramebufferBlend names are mapped.
std::optional<canon::FramebufferBlend> framebufferBlendFor(uint32_t egxBlend) {
    switch (egxBlend) {
        case 0: return canon::FramebufferBlend::Opaque;
        case 1: return canon::FramebufferBlend::AlphaKey;
        case 2: return canon::FramebufferBlend::Alpha;
        case 3: return canon::FramebufferBlend::Add;
        case 4: return canon::FramebufferBlend::Mod;
        case 5: return canon::FramebufferBlend::Mod2x;
        case 10: return canon::FramebufferBlend::NoAlphaAdd;
        case 13: return canon::FramebufferBlend::BlendAdd;
        default: return std::nullopt;
    }
}

// MOCV stores BGRA bytes; canon colour sets are RGBA.
std::array<uint8_t, 4> bgraToRgba(uint32_t bgra) {
    return {static_cast<uint8_t>(bgra >> 16), static_cast<uint8_t>(bgra >> 8), static_cast<uint8_t>(bgra),
            static_cast<uint8_t>(bgra >> 24)};
}

canon::TextureRef textureById(const TextureResolutions& textures, uint32_t fdid) {
    auto it = textures.find(fdid);
    if (it != textures.end()) return it->second;
    canon::TextureRef ref;
    ref.unresolvedReason = "texture FileDataID " + std::to_string(fdid) + " was not resolved";
    return ref;
}

canon::TextureRef textureFor(const WmoInputs& in, uint32_t slot) {
    const wmo::RootFile& root = *in.root;
    canon::TextureRef ref;
    if (!root.textureNames.empty()) {
        // MOTX mode: the slot is a byte offset into a path table, and husk
        // resolves textures by FileDataID only.
        ref.unresolvedReason = "MOTX path '" + wmo::stringAt(root.textureNames, slot) + "' has no FileDataID";
        return ref;
    }
    return textureById(in.textures, slot);
}

canon::Material buildMaterial(const WmoInputs& in, uint32_t index) {
    const wmo::Material& m = in.root->materials[index];
    canon::Material material;
    material.ref.id = canon::RecordIndex{index};
    material.framebufferBlend = framebufferBlendFor(m.blendMode);
    material.sourceFlags = m.flags;
    material.unlit = (m.flags & kFlagUnlit) != 0;
    material.unfogged = (m.flags & kFlagUnfogged) != 0;
    material.twoSided = (m.flags & kFlagUnculled) != 0;
    const char* shader = shaderName(m.shader);
    material.shader = canon::Ref{canon::RecordIndex{m.shader}, shader ? shader : "",
                                 shader ? canon::NameSource::Synthesized : canon::NameSource::None};

    for (uint32_t k = 0; k < m.textures.size(); ++k) {
        if (m.textures[k] == 0) continue;
        canon::MaterialLayer layer;
        layer.identity = canon::Ref{canon::RecordIndex{k}, "texture_" + std::to_string(k + 1),
                                    canon::NameSource::Synthesized};
        layer.texture = textureFor(in, m.textures[k]);
        // Assumed: texture k samples UV set k. The shader decides the real
        // mapping (WMO.md "Shader types"); not verified per shader.
        layer.uv = canon::UvSetIndex{k};
        layer.role = k == 0 ? canon::LayerRole{canon::KnownRole::Diffuse} : canon::LayerRole{canon::KnownRole::Unknown};
        if (k < 2 && !in.root->uvScrollSpeeds.empty()) {
            const wmo::Vec2& speed = in.root->uvScrollSpeeds[index][k];
            if (speed[0] != 0.0f || speed[1] != 0.0f) layer.uvScroll = canon::Vec2{speed[0], speed[1]};
        }
        if (k == 0) material.diffuseLayer = layer.identity;
        material.layers.push_back(std::move(layer));
    }
    return material;
}

canon::Ref fileRef(const WmoInputs& in, uint32_t fdid) {
    auto name = in.assetNames.find(fdid);
    if (name == in.assetNames.end()) return canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
    return canon::Ref{canon::FileDataId{fdid}, name->second, canon::NameSource::Listfile};
}

canon::Ref doodadAsset(const WmoInputs& in, const wmo::Doodad& d, size_t index) {
    const wmo::RootFile& root = *in.root;
    if (root.doodadFileDataIds.empty() && !root.doodadNames.empty()) {
        return canon::Ref{canon::None{}, wmo::stringAt(root.doodadNames, d.nameIndex), canon::NameSource::WmoEmbedded};
    }
    if (d.nameIndex >= root.doodadFileDataIds.size()) {
        throw std::runtime_error("MODD doodad " + std::to_string(index) + ": expected a MODI index < " +
                                  std::to_string(root.doodadFileDataIds.size()) + ", got " +
                                  std::to_string(d.nameIndex));
    }
    uint32_t fdid = root.doodadFileDataIds[d.nameIndex];
    if (fdid == 0) return canon::Ref{};  // an empty MODI slot: the doodad names no model
    return fileRef(in, fdid);
}

std::vector<canon::Model::OwnedSet> buildPlacementSets(const WmoInputs& in) {
    const wmo::RootFile& root = *in.root;
    std::vector<canon::Model::OwnedSet> sets;
    for (uint32_t s = 0; s < root.doodadSets.size(); ++s) {
        const wmo::DoodadSet& source = root.doodadSets[s];
        canon::Model::OwnedSet owned;
        owned.set.ref = canon::Ref{canon::RecordIndex{s}, source.name, canon::NameSource::WmoEmbedded};
        // WMO.md: the first set is additive and always shown.
        owned.alwaysOn = s == 0;
        for (uint32_t d = source.startIndex; d < source.startIndex + source.count; ++d) {
            const wmo::Doodad& doodad = root.doodads[d];
            canon::PlacedInstance instance;
            instance.id = d;  // the MODD index: stable and unique across the whole WMO
            instance.asset = doodadAsset(in, doodad, d);
            instance.translation = toCanon(doodad.position);
            instance.rotation = canon::Quat{doodad.rotation[0], doodad.rotation[1], doodad.rotation[2], doodad.rotation[3]};
            instance.scale = doodad.scale;
            canon::InstanceTint tint;
            tint.rgba = bgraToRgba(doodad.color);
            if (!root.doodadColorMultipliers.empty()) tint.multiplier = root.doodadColorMultipliers[d];
            instance.tint = tint;
            instance.flags = doodad.flags;
            owned.set.instances.push_back(std::move(instance));
        }
        sets.push_back(std::move(owned));
    }
    return sets;
}

constexpr int32_t kNewLightBlendsColors = 0x1;
constexpr int32_t kNewLightCastsShadows = 0x2;

canon::Vec3 colorOf(uint32_t bgra) {
    std::array<uint8_t, 4> rgba = bgraToRgba(bgra);
    return {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f};
}

// parseRoot already checked doodadSet against MODS.
canon::Ref ownedSetRef(const WmoInputs& in, uint32_t set) {
    return canon::Ref{canon::RecordIndex{set}, in.root->doodadSets[set].name, canon::NameSource::WmoEmbedded};
}

canon::LocalLight buildNewLight(const WmoInputs& in, uint32_t index) {
    const wmo::NewLight& l = in.root->newLights[index];
    canon::LocalLight out;
    out.origin = {"MNLD", index};
    switch (l.type) {
        case 0: out.kind = canon::LocalLight::Kind::Point; break;
        case 1: out.kind = canon::LocalLight::Kind::Spot; break;
        default:
            throw std::runtime_error("MNLD[" + std::to_string(index) + "].type: expected 0 (point) or 1 (spot), got " +
                                      std::to_string(l.type));
    }
    out.position = toCanon(l.position);
    out.eulerRotation = toCanon(l.rotation);
    out.color = colorOf(l.innerColor);
    out.intensity = l.intensity;
    out.intensityMultiplier = l.intensityMultiplier == 0.0f ? 1.0f : l.intensityMultiplier;
    out.attenuationStart = l.attenuationStart;
    out.attenuationEnd = l.attenuationEnd;
    if (l.flags & kNewLightBlendsColors) {
        out.gradient = canon::LocalLight::Gradient{colorOf(l.outerColor), l.blendStart, l.blendEnd};
    }
    if (out.kind == canon::LocalLight::Kind::Spot) {
        out.cone = canon::LocalLight::Cone{l.falloff, l.innerAngle, l.outerAngle};
    }
    if (l.flickerMode != 0) {
        out.flicker = canon::LocalLight::Flicker{static_cast<uint32_t>(l.flickerMode), l.flickerIntensity, l.flickerSpeed};
    }
    out.castsShadows = (l.flags & kNewLightCastsShadows) != 0;
    out.shadowScale = l.scale;
    if (l.cookieFileDataId != 0) out.cookie = fileRef(in, l.cookieFileDataId);
    out.placementSet = ownedSetRef(in, static_cast<uint32_t>(l.doodadSet));
    out.sourceFlags = static_cast<uint32_t>(l.flags);
    return out;
}

canon::LocalLight buildLegacyLight(const WmoInputs& in, uint32_t index) {
    const wmo::LegacyLight& l = in.root->legacyLights[index];
    canon::LocalLight out;
    out.origin = {"MOLT", index};
    switch (l.type) {
        case 0: out.kind = canon::LocalLight::Kind::Point; break;
        case 1: out.kind = canon::LocalLight::Kind::Spot; break;
        case 2: out.kind = canon::LocalLight::Kind::Directional; break;
        case 3: out.kind = canon::LocalLight::Kind::Ambient; break;
        default:
            throw std::runtime_error("MOLT[" + std::to_string(index) + "].type: expected 0..3, got " +
                                      std::to_string(l.type));
    }
    out.position = toCanon(l.position);
    if (out.kind == canon::LocalLight::Kind::Spot || out.kind == canon::LocalLight::Kind::Directional) {
        out.rotation = canon::Quat{l.rotation[0], l.rotation[1], l.rotation[2], l.rotation[3]};
    }
    out.color = colorOf(l.color);
    out.intensity = l.intensity;
    out.attenuated = l.useAttenuation != 0;
    out.attenuationStart = l.attenuationStart;
    out.attenuationEnd = l.attenuationEnd;
    out.sourceFlags = static_cast<uint32_t>(l.unknown[0]) | static_cast<uint32_t>(l.unknown[1]) << 8;
    return out;
}

std::array<float, 4> rgbaOf(uint32_t bgra) {
    std::array<uint8_t, 4> rgba = bgraToRgba(bgra);
    return {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f};
}

// What every ambient record shares as an effect instance: its three colours
// as style, its flags (all of them undocumented) as unresolved bits.
canon::PlacedInstance ambientInstance(const char* effect, const char* table, uint32_t index,
                                      const std::array<uint32_t, 3>& colors, uint32_t flags) {
    canon::PlacedInstance instance;
    instance.asset = canon::Ref{canon::None{}, effect, canon::NameSource::Synthesized};
    instance.flags = flags;
    canon::EffectPlacement placement;
    placement.origin = {table, index};
    canon::StyleBlock style;
    style.ambientColors.emplace();
    for (size_t k = 0; k < 3; ++k) (*style.ambientColors)[k] = rgbaOf(colors[k]);
    placement.style = {{"ambient", {style}}};
    if (flags != 0) placement.unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, flags, std::nullopt});
    instance.effect = std::move(placement);
    return instance;
}

// MAVG: the model-wide override. Its position and distances have no region
// to describe, so any set ones are carried unresolved.
canon::PlacedInstance globalAmbientInstance(const wmo::AmbientVolume& v, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_global", "MAVG", index, v.colors, v.flags);
    std::vector<canon::UnresolvedField>& unresolved = instance.effect->unresolved;
    if (v.position != wmo::Vec3{}) {
        unresolved.push_back({"position", canon::UnresolvedField::Kind::Scalars,
                              std::vector<double>(v.position.begin(), v.position.end()), std::nullopt});
    }
    if (v.start != 0) unresolved.push_back({"start", canon::UnresolvedField::Kind::Scalar, double{v.start}, std::nullopt});
    if (v.end != 0) unresolved.push_back({"end", canon::UnresolvedField::Kind::Scalar, double{v.end}, std::nullopt});
    return instance;
}

// MAVD: an override that fades out between two distances from a point.
canon::PlacedInstance pointAmbientInstance(const wmo::AmbientVolume& v, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_point", "MAVD", index, v.colors, v.flags);
    instance.translation = toCanon(v.position);
    instance.effect->inputs = {{"start", {double{v.start}}}, {"end", {double{v.end}}}};
    return instance;
}

// MBVD: an override inside six planes. Its `end` reads as a fade distance in
// from the faces (<= the smallest half-size in 52 of 54 boxes), not confirmed.
canon::PlacedInstance boxAmbientInstance(const wmo::AmbientBox& b, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_box", "MBVD", index, b.colors, b.flags);
    std::vector<canon::InputValue> planes;
    for (const auto& [x, y, z, d] : b.planes) {
        planes.push_back({std::vector<canon::NamedInput>{{"normal", {canon::Vec3{x, y, z}}}, {"distance", {double{d}}}}});
    }
    instance.effect->inputs = {{"planes", {std::move(planes)}}};
    instance.effect->unresolved.push_back({"end", canon::UnresolvedField::Kind::Scalar, double{b.end}, "region.edge"});
    return instance;
}

// The wiki's F_IEBLEND: radius ignored; not drawn, but scales the area fog
// down near the model's exits.
constexpr uint32_t kFogExitBlend = 0x1;

// The wiki's "empty fog entry", byte for byte (a -0.0 position included):
// what record 0 holds when the model has no default fog of its own. It is
// record 0 in 3,631 of 4,000 sampled roots and never a later record.
bool isEmptyFog(const wmo::Fog& f) {
    return f.flags == 0 && f.position[0] == 0 && f.position[1] == 0 && f.position[2] == 0 && f.smallerRadius == 0 &&
           f.largerRadius == 0 && f.fog.end == 444.4444580078125f && f.fog.startScalar == 0.25f &&
           f.fog.color == 0xFFFFFFFF && f.underwater.end == 222.22222900390625f && f.underwater.startScalar == -0.5f &&
           f.underwater.color == 0xFF0000FF;
}

void unresolvedFloat(const char* field, float value, std::vector<canon::UnresolvedField>& out) {
    if (std::isfinite(value)) {
        out.push_back({field, canon::UnresolvedField::Kind::Scalar, static_cast<double>(value), std::nullopt});
    } else {
        out.push_back({field, canon::UnresolvedField::Kind::Bits, std::bit_cast<uint32_t>(value), std::nullopt});
    }
}

// The wiki's MoP fog: thickens from end * startScalar to end. A layer whose
// numbers aren't finite is carried raw instead.
std::optional<canon::DistanceFog> distanceFog(const wmo::Fog::Layer& layer, const std::string& name,
                                              std::vector<canon::UnresolvedField>& unresolved) {
    if (std::isfinite(layer.end) && std::isfinite(layer.startScalar)) {
        return canon::DistanceFog{canon::LinearFog{layer.end * layer.startScalar, layer.end, colorOf(layer.color)}};
    }
    unresolvedFloat((name + ".end").c_str(), layer.end, unresolved);
    unresolvedFloat((name + ".start_scalar").c_str(), layer.startScalar, unresolved);
    return std::nullopt;
}

// Alpha is 0xFF in all 4,534 sampled records.
void requireOpaque(const wmo::Fog::Layer& layer, const char* field, std::vector<canon::UnresolvedField>& out) {
    uint32_t alpha = layer.color >> 24;
    if (alpha != 0xFF) out.push_back({field, canon::UnresolvedField::Kind::Scalar, static_cast<double>(alpha), std::nullopt});
}

// One MFOG record as an effect instance (REFACTOR/EFFECTS.md, literal tier).
canon::PlacedInstance buildFogInstance(const WmoInputs& in, uint32_t index) {
    const wmo::Fog& f = in.root->fogs[index];
    canon::PlacedInstance instance;
    instance.translation = toCanon(f.position);
    instance.flags = f.flags;
    canon::EffectPlacement effect;
    effect.origin = {"MFOG", index};
    canon::StyleBlock style;
    style.distanceFog = distanceFog(f.fog, "fog", effect.unresolved);
    style.underwater = distanceFog(f.underwater, "underwater", effect.unresolved);
    // Radii that aren't finite numbers occur (a Torghast record holds a NaN
    // and the integer 3): no radius is stated then, and the bits are kept.
    bool radiiFinite = std::isfinite(f.smallerRadius) && std::isfinite(f.largerRadius);
    if (f.flags & kFogExitBlend) {
        instance.asset = canon::Ref{canon::None{}, "classic_fog_exit_blend", canon::NameSource::Synthesized};
        style.exitBlend = true;
        effect.style = {{"exit_blend", {style}}};
        unresolvedFloat("smaller_radius", f.smallerRadius, effect.unresolved);
        unresolvedFloat("larger_radius", f.largerRadius, effect.unresolved);
    } else {
        instance.asset = canon::Ref{canon::None{}, "classic_local_fog", canon::NameSource::Synthesized};
        if (radiiFinite) {
            effect.inputs = {{"inner_radius", static_cast<double>(f.smallerRadius)},
                             {"outer_radius", static_cast<double>(f.largerRadius)}};
        } else {
            unresolvedFloat("smaller_radius", f.smallerRadius, effect.unresolved);
            unresolvedFloat("larger_radius", f.largerRadius, effect.unresolved);
        }
        effect.style = {{"fog", {style}}};
    }
    if (uint32_t unknown = f.flags & ~kFogExitBlend; unknown != 0) {
        effect.unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, unknown, std::nullopt});
    }
    requireOpaque(f.fog, "fog.color.alpha", effect.unresolved);
    requireOpaque(f.underwater, "underwater.color.alpha", effect.unresolved);
    if (f.doodadSetReserved) {
        effect.unresolved.push_back({"doodad_set.reserved", canon::UnresolvedField::Kind::Scalars,
                                     std::vector<double>(f.doodadSetReserved->begin(), f.doodadSetReserved->end()),
                                     std::nullopt});
    }
    instance.effect = std::move(effect);
    return instance;
}

// Puts effect instances into a WMO's owned sets. Ids follow the doodads'
// MODD indices, so they stay unique across the model's sets. A model
// without doodad sets gets an always-on "effects" set as set 0, which is
// where an instance with no set of its own goes otherwise.
struct EffectPlacer {
    std::vector<canon::Model::OwnedSet>& sets;
    uint32_t nextId;

    canon::InstanceRef place(uint32_t set, canon::PlacedInstance instance) {
        if (sets.empty()) {
            canon::Model::OwnedSet effects;
            effects.set.ref = canon::Ref{canon::RecordIndex{0}, "effects", canon::NameSource::Synthesized};
            effects.alwaysOn = true;
            sets.push_back(std::move(effects));
        }
        instance.id = nextId;
        sets[set].set.instances.push_back(std::move(instance));
        return {set, nextId++};
    }
};

// Every MFOG record but the empty entry, in the set it applies with (MFED),
// else set 0. Returns each record's instance, nullopt for the empty entry.
std::vector<std::optional<canon::InstanceRef>> placeFogs(const WmoInputs& in, EffectPlacer& placer) {
    const wmo::RootFile& root = *in.root;
    std::vector<std::optional<canon::InstanceRef>> out(root.fogs.size());
    for (uint32_t i = 0; i < root.fogs.size(); ++i) {
        if (isEmptyFog(root.fogs[i])) continue;
        out[i] = placer.place(root.fogs[i].doodadSet.value_or(0), buildFogInstance(in, i));
    }
    return out;
}

// MAVG/MAVD/MBVD, each in the set it applies with. Returns the MAVD and
// MBVD instances, which group refs name; nothing names an MAVG.
struct AmbientRefs {
    std::vector<canon::InstanceRef> points;
    std::vector<canon::InstanceRef> boxes;
};

AmbientRefs placeAmbient(const WmoInputs& in, EffectPlacer& placer) {
    const wmo::RootFile& root = *in.root;
    for (uint32_t i = 0; i < root.globalAmbientVolumes.size(); ++i) {
        const wmo::AmbientVolume& v = root.globalAmbientVolumes[i];
        placer.place(v.doodadSet, globalAmbientInstance(v, i));
    }
    AmbientRefs out;
    for (uint32_t i = 0; i < root.ambientVolumes.size(); ++i) {
        const wmo::AmbientVolume& v = root.ambientVolumes[i];
        out.points.push_back(placer.place(v.doodadSet, pointAmbientInstance(v, i)));
    }
    for (uint32_t i = 0; i < root.ambientBoxes.size(); ++i) {
        const wmo::AmbientBox& b = root.ambientBoxes[i];
        out.boxes.push_back(placer.place(b.doodadSet, boxAmbientInstance(b, i)));
    }
    return out;
}

// MPVD has no doodad set: every volume is in set 0.
std::vector<canon::InstanceRef> placeParticulate(const WmoInputs& in, EffectPlacer& placer);

// A group's refs into a list of instances: MAVR, MBVR, MPVR.
void appendRefs(uint32_t part, const char* tag, const std::vector<uint16_t>& refs,
                const std::vector<canon::InstanceRef>& instances, std::vector<canon::InstanceRef>& out) {
    for (uint16_t r : refs) {
        if (r >= instances.size()) {
            throw std::runtime_error("WMO group " + std::to_string(part) + "'s " + tag + ": expected refs < " +
                                     std::to_string(instances.size()) + " root records, got " + std::to_string(r));
        }
        out.push_back(instances[r]);
    }
}

// A group's fog list: MFVR, else MOGP fogIds. A 0 names the model's default
// fog, not a list entry: fogIds are left-packed (2,1,0,0) and MFVR never
// lists 0, in every sampled root.
std::vector<canon::InstanceRef> partFogs(uint32_t part, const wmo::GroupFile& g,
                                         const std::vector<std::optional<canon::InstanceRef>>& fogs) {
    std::vector<uint16_t> refs = g.fogRefs.empty() ? std::vector<uint16_t>(g.fogIds.begin(), g.fogIds.end()) : g.fogRefs;
    const char* tag = g.fogRefs.empty() ? "fogIds" : "MFVR";
    std::vector<canon::InstanceRef> out;
    for (uint16_t r : refs) {
        if (r >= fogs.size()) {
            throw std::runtime_error("WMO group " + std::to_string(part) + "'s " + tag + ": expected refs < " +
                                     std::to_string(fogs.size()) + " MFOG records, got " + std::to_string(r));
        }
        if (r != 0 && fogs[r]) out.push_back(*fogs[r]);
    }
    return out;
}

// Each entry's identity is its position in its canon list; where it came
// from stays in `origin`.
template <typename T>
void nameByPosition(std::vector<T>& entries, const std::string& prefix) {
    for (uint32_t i = 0; i < entries.size(); ++i) {
        entries[i].ref = canon::Ref{canon::RecordIndex{i}, prefix + "_" + std::to_string(i), canon::NameSource::Synthesized};
    }
}

canon::Scene buildScene(const WmoInputs& in) {
    canon::Scene scene;
    for (uint32_t i = 0; i < in.root->newLights.size(); ++i) scene.localLights.push_back(buildNewLight(in, i));
    for (uint32_t i = 0; i < in.root->legacyLights.size(); ++i) scene.localLights.push_back(buildLegacyLight(in, i));
    nameByPosition(scene.localLights, "light");
    return scene;
}

// WMO.md MLIQ "how to determine LiquidTypeRec to use", transcribed.
constexpr uint16_t kRootUsesLiquidTypeDbcId = 0x4;
constexpr uint32_t kGroupLiquidIsOcean = 0x80000;
constexpr uint32_t kLiquidGreenLava = 15;
constexpr uint32_t kLiquidEndBasic = 20;
constexpr uint32_t kLiquidFirstNonBasic = 21;

uint32_t toWmoLiquid(uint32_t basic, uint32_t groupFlags) {
    switch (basic & 3) {
        case 0: return (groupFlags & kGroupLiquidIsOcean) ? 14 : 13;  // WMO ocean / water
        case 1: return 14;
        case 2: return 19;  // WMO magma
        default: return 20;  // WMO slime
    }
}

// nullopt where the wiki's rules don't decide: groupLiquid 0 under the DBC
// flag (to_wmo_liquid(-1)), and legacy green lava, which the client
// resolves per tile by a rule the wiki marks unverified.
std::optional<uint32_t> liquidTypeId(uint16_t rootFlags, const wmo::GroupFile& g) {
    uint32_t groupLiquid = g.liquidType;
    if (rootFlags & kRootUsesLiquidTypeDbcId) {
        if (groupLiquid == 0) return std::nullopt;
        return groupLiquid < kLiquidFirstNonBasic ? toWmoLiquid(groupLiquid - 1, g.flags) : groupLiquid;
    }
    if (groupLiquid == kLiquidGreenLava) return std::nullopt;
    return groupLiquid < kLiquidEndBasic ? toWmoLiquid(groupLiquid, g.flags) : groupLiquid + 1;
}

canon::LiquidGrid buildLiquid(const WmoInputs& in, uint32_t part, const wmo::GroupFile& g) {
    const wmo::Liquid& l = *g.liquid;
    canon::LiquidGrid out;
    out.part = part;
    out.material = canon::RecordIndex{l.material};
    out.sourceLiquidType = g.liquidType;
    if (std::optional<uint32_t> id = liquidTypeId(in.root->header.flags, g)) {
        out.liquidType = canon::Ref{canon::Db2Row{"LiquidType", *id}, "", canon::NameSource::None};
    }
    out.corner = toCanon(l.corner);
    out.tileSize = wmo::kLiquidTileSize;
    out.vertsX = l.vertsX;
    out.vertsY = l.vertsY;
    out.heights = l.heights;
    out.vertexData = l.vertexData;
    out.tileFlags = l.tiles;
    // MCLQ's convention, which MLIQ shares: low nibble 0xF = no liquid. On 154
    // real groups, the tiles that pass lie over their group's footprint.
    for (uint8_t t : l.tiles) out.tileExists.push_back((t & 0x0F) != 0x0F ? 1 : 0);
    return out;
}

// Every group's MOPY/MPY2 entries as two face channels, in the order
// appendGroup appended the groups' triangles.
std::vector<canon::AttributeChannel> buildFaceAttributes(const WmoInputs& in) {
    std::vector<uint16_t> flags;
    std::vector<uint16_t> materials;
    for (uint32_t part = 0; part < in.groups.size(); ++part) {
        if (!in.groups[part]) continue;
        const wmo::GroupFile& g = *in.groups[part];
        if (g.triangles.size() != g.indices.size() / 3) {
            throw std::runtime_error("WMO group " + std::to_string(part) + ": expected one MOPY/MPY2 entry per triangle (" +
                                      std::to_string(g.indices.size() / 3) + "), got " +
                                      std::to_string(g.triangles.size()));
        }
        for (const wmo::TriangleInfo& t : g.triangles) {
            flags.push_back(t.flags);
            materials.push_back(t.material);
        }
    }
    return {{"wmo.poly_flags", canon::AttributeDomain::Face, 1, std::move(flags)},
            {"wmo.poly_material", canon::AttributeDomain::Face, 1, std::move(materials)}};
}

// Appends one group's vertices and batches to `mesh`. UV and colour sets a
// group lacks are zero-filled so every vertex has every set; the part's own
// flags say which sets the group really has.
void appendGroup(const WmoInputs& in, uint32_t part, const wmo::GroupFile& g, canon::Mesh& mesh,
                 std::vector<canon::Identity>& primitiveMaterials, size_t uvSetCount, size_t colorSetCount) {
    if (!g.positions.empty() && g.normals.size() != g.positions.size()) {
        throw std::runtime_error("WMO group " + std::to_string(part) + ": expected " +
                                  std::to_string(g.positions.size()) + " normals, got " + std::to_string(g.normals.size()));
    }
    auto vertexBase = static_cast<uint32_t>(mesh.positions.size());
    auto indexBase = static_cast<uint32_t>(mesh.indices.size());
    for (size_t v = 0; v < g.positions.size(); ++v) {
        mesh.positions.push_back(toCanon(g.positions[v]));
        mesh.normals.push_back(toCanon(g.normals[v]));
    }
    std::vector<std::vector<canon::Vec2>*> uvSets = {&mesh.uv0};
    if (uvSetCount > 1) uvSets.push_back(&*mesh.uv1);
    if (uvSetCount > 2) uvSets.push_back(&*mesh.uv2);
    for (size_t k = 0; k < uvSets.size(); ++k) {
        for (size_t v = 0; v < g.positions.size(); ++v) {
            uvSets[k]->push_back(k < g.uvSets.size() ? canon::Vec2{g.uvSets[k][v][0], g.uvSets[k][v][1]} : canon::Vec2{});
        }
    }
    for (size_t k = 0; k < colorSetCount; ++k) {
        for (size_t v = 0; v < g.positions.size(); ++v) {
            mesh.colorSets[k].push_back(k < g.colorSets.size() ? bgraToRgba(g.colorSets[k][v]) : std::array<uint8_t, 4>{});
        }
    }
    for (uint32_t index : g.indices) mesh.indices.push_back(vertexBase + index);

    for (size_t b = 0; b < g.batches.size(); ++b) {
        const wmo::Batch& batch = g.batches[b];
        if (batch.material >= in.root->materials.size()) {
            throw std::runtime_error("WMO group " + std::to_string(part) + " batch " + std::to_string(b) +
                                      ": expected a material < " + std::to_string(in.root->materials.size()) +
                                      ", got " + std::to_string(batch.material));
        }
        canon::PrimitiveGeoset primitive;  // geoset 0: always shown
        primitive.indexStart = indexBase + batch.startIndex;
        primitive.indexCount = batch.indexCount;
        primitive.part = part;
        mesh.primitives.push_back(primitive);
        primitiveMaterials.push_back(canon::Identity{canon::RecordIndex{batch.material}});
    }
}

}  // namespace

const char* shaderName(uint32_t id) {
    static constexpr const char* kNames[] = {
        "Diffuse",          "Specular",           "Metal",
        "Env",              "Opaque",             "EnvMetal",
        "TwoLayerDiffuse",  "TwoLayerEnvMetal",   "TwoLayerTerrain",
        "DiffuseEmissive",  "waterWindow",        "MaskedEnvMetal",
        "EnvMetalEmissive", "TwoLayerDiffuseOpaque", "submarineWindow",
        "TwoLayerDiffuseEmissive", "DiffuseTerrain", "AdditiveMaskedEnvMetal",
        "TwoLayerDiffuseMod2x", "TwoLayerDiffuseMod2xNA", "TwoLayerDiffuseAlpha",
        "Lod",              "Parallax",           "UnkDFShader",
    };
    return id < std::size(kNames) ? kNames[id] : nullptr;
}

namespace {

std::string fieldAt(const std::string& base, uint32_t offset) {
    char text[16];
    std::snprintf(text, sizeof(text), "+0x%X", offset);
    return base + text;
}

void unresolvedScalar(std::vector<canon::UnresolvedField>& out, std::string field, double value, bool keepZero = false) {
    if (value != 0 || keepZero) out.push_back({std::move(field), canon::UnresolvedField::Kind::Scalar, value, std::nullopt});
}

template <size_t N>
void unresolvedScalars(std::vector<canon::UnresolvedField>& out, std::string field, const std::array<float, N>& values) {
    if (std::all_of(values.begin(), values.end(), [](float v) { return v == 0; })) return;
    out.push_back({std::move(field), canon::UnresolvedField::Kind::Scalars, std::vector<double>(values.begin(), values.end()),
                   std::nullopt});
}

}  // namespace

canon::PlacedInstance particulateInstance(const wmo::ParticulateVolume& v, const TextureResolutions& textures,
                                          canon::SourceRecord origin) {
    canon::PlacedInstance instance;
    instance.asset = canon::Ref{canon::None{}, "classic_particulate_volume", canon::NameSource::Synthesized};
    // Every _mpv.wdt volume's world box is centred on its position and its
    // extent's size (619/619), so the extent is the full size.
    instance.translation = toCanon(v.position);
    instance.flags = v.flags;
    canon::EffectPlacement effect;
    effect.origin = std::move(origin);

    std::vector<canon::InputValue> emitters;
    canon::SourceStyle emitterStyle{"emitter", {}, true};
    std::vector<canon::UnresolvedField>& unresolved = effect.unresolved;
    for (size_t k = 0; k < v.emitters.size(); ++k) {
        const wmo::ParticulateEmitter& e = v.emitters[k];
        emitters.push_back({std::vector<canon::NamedInput>{
            {"texture", {textureById(textures, e.textureFileDataId)}},
            {"tiles_x", {int64_t{e.tilesX}}},
            {"tiles_y", {int64_t{e.tilesY}}},
            {"frames", {int64_t{e.frames}}}}});
        canon::StyleBlock block;
        block.draw = canon::StyleBlock::Draw{framebufferBlendFor(e.blendMode), e.color};
        emitterStyle.blocks.push_back(block);
        std::string base = "emitters[" + std::to_string(k) + "]";
        unresolvedScalars(unresolved, base + ".parameters", e.parameters);
        if (!block.draw->blend) {
            unresolved.push_back({base + ".blend", canon::UnresolvedField::Kind::Id, double(e.blendMode), std::nullopt});
        }
        unresolvedScalar(unresolved, fieldAt(base, 0x90), e.unknown90);
        unresolvedScalar(unresolved, fieldAt(base, 0xA8), e.unknownA8);
    }
    effect.inputs = {{"size", {toCanon(v.extent)}}, {"emitters", {std::move(emitters)}}};
    canon::StyleBlock volumeStyle;
    // The particle cap: the shader bounds its simulation threads by it.
    volumeStyle.maxParticles = v.particleCount;
    effect.style = {{"volume", {volumeStyle}, false}, std::move(emitterStyle)};

    // Neither the rotation's axis order nor its sign convention is settled
    // (2 of 193 records turn about two axes): carried, not applied.
    if (v.rotation != wmo::Vec3{}) {
        unresolved.push_back({"rotation", canon::UnresolvedField::Kind::Angle,
                              std::vector<double>(v.rotation.begin(), v.rotation.end()), std::nullopt});
    }
    if (v.flags != 0) unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, v.flags, std::nullopt});
    unresolvedScalar(unresolved, fieldAt("record", 0x04), v.unknown04);
    unresolvedScalar(unresolved, fieldAt("record", 0x2C), v.unknown2C);
    unresolved.push_back({"preset", canon::UnresolvedField::Kind::Id, double(v.presetId), std::nullopt});
    unresolvedScalar(unresolved, fieldAt("record", 0x38), v.unknown38);
    unresolvedScalar(unresolved, "opacity", v.opacity, true);
    if (!v.envelope.empty()) {
        std::vector<double> keys;
        for (const auto& [value, value2, time] : v.envelope) keys.insert(keys.end(), {time, value, value2});
        unresolved.push_back({"envelope (time, value, value2)", canon::UnresolvedField::Kind::Scalars, keys, std::nullopt});
    }
    unresolvedScalars(unresolved, fieldAt("record", 0x220), v.unknown220);
    unresolvedScalar(unresolved, fieldAt("record", 0x29C), v.unknown29C);
    for (const wmo::UnknownRange& range : v.unknownRanges) {
        unresolved.push_back({fieldAt("record", range.offset), canon::UnresolvedField::Kind::Words, range.words, std::nullopt});
    }
    instance.effect = std::move(effect);
    return instance;
}

namespace {

std::vector<canon::InstanceRef> placeParticulate(const WmoInputs& in, EffectPlacer& placer) {
    std::vector<canon::InstanceRef> out;
    for (uint32_t i = 0; i < in.root->particulateVolumes.size(); ++i) {
        out.push_back(placer.place(0, particulateInstance(in.root->particulateVolumes[i], in.textures, {"MPVD", i})));
    }
    return out;
}

}  // namespace


canon::Model buildCanonWmo(const WmoInputs& in) {
    const wmo::RootFile& root = *in.root;
    if (in.groups.size() != root.groups.size()) {
        throw std::runtime_error("WMO groups: expected one entry per MOGI group (" + std::to_string(root.groups.size()) +
                                  "), got " + std::to_string(in.groups.size()));
    }

    size_t uvSetCount = 1;
    size_t colorSetCount = 0;
    for (const auto& g : in.groups) {
        if (!g) continue;
        uvSetCount = std::max(uvSetCount, std::min<size_t>(g->uvSets.size(), 3));
        colorSetCount = std::max(colorSetCount, g->colorSets.size());
    }

    std::vector<canon::Model::OwnedSet> sets = buildPlacementSets(in);
    EffectPlacer placer{sets, static_cast<uint32_t>(root.doodads.size())};
    std::vector<std::optional<canon::InstanceRef>> fogs = placeFogs(in, placer);
    AmbientRefs ambient = placeAmbient(in, placer);
    std::vector<canon::InstanceRef> particulate = placeParticulate(in, placer);

    canon::Mesh mesh;
    if (uvSetCount > 1) mesh.uv1.emplace();
    if (uvSetCount > 2) mesh.uv2.emplace();
    mesh.colorSets.resize(colorSetCount);
    std::vector<canon::Identity> primitiveMaterials;
    for (uint32_t part = 0; part < root.groups.size(); ++part) {
        const wmo::GroupInfo& info = root.groups[part];
        canon::MeshPart meshPart;
        std::string name = info.nameOffset >= 0 ? wmo::stringAt(root.groupNames, static_cast<uint32_t>(info.nameOffset)) : "";
        meshPart.ref = canon::Ref{canon::RecordIndex{part}, name,
                                  name.empty() ? canon::NameSource::None : canon::NameSource::WmoEmbedded};
        meshPart.flags = info.flags;
        meshPart.boundsMin = toCanon(info.bounds.min);
        meshPart.boundsMax = toCanon(info.bounds.max);
        if (in.groups[part]) {
            const wmo::GroupFile& g = *in.groups[part];
            meshPart.fogs = partFogs(part, g, fogs);
            appendRefs(part, "MPVR", g.particulateVolumeRefs, particulate, meshPart.particulateVolumes);
            appendRefs(part, "MAVR", g.ambientVolumeRefs, ambient.points, meshPart.ambientVolumes);
            appendRefs(part, "MBVR", g.ambientBoxRefs, ambient.boxes, meshPart.ambientVolumes);
            // A group without portals may store start 0xFFFF with count 0.
            if (g.portalCount != 0 && g.portalStart + g.portalCount > root.portalRefs.size()) {
                throw std::runtime_error("WMO group " + std::to_string(part) + ": expected MOPR refs [" +
                                          std::to_string(g.portalStart) + ", " +
                                          std::to_string(g.portalStart + g.portalCount) + ") within " +
                                          std::to_string(root.portalRefs.size()));
            }
            for (size_t r = g.portalStart; r < size_t{g.portalStart} + g.portalCount; ++r) {
                const wmo::PortalRef& ref = root.portalRefs[r];
                meshPart.portals.push_back({ref.portal, ref.group, ref.side});
            }
        }
        mesh.parts.push_back(meshPart);
        if (in.groups[part]) appendGroup(in, part, *in.groups[part], mesh, primitiveMaterials, uvSetCount, colorSetCount);
    }
    for (size_t i = 0; i < root.portals.size(); ++i) {
        const wmo::Portal& p = root.portals[i];
        canon::Portal portal;
        for (size_t v = p.startVertex; v < p.startVertex + p.count; ++v) {
            portal.vertices.push_back(toCanon(root.portalVertices[v]));
        }
        portal.normal = toCanon(p.normal);
        portal.distance = p.distance;
        if (!root.portalExtras.empty()) portal.sourceExtra.assign(root.portalExtras[i].begin(), root.portalExtras[i].end());
        mesh.portals.push_back(std::move(portal));
    }
    mesh.attributes = buildFaceAttributes(in);
    std::vector<canon::LiquidGrid> liquids;
    for (uint32_t part = 0; part < in.groups.size(); ++part) {
        if (in.groups[part] && in.groups[part]->liquid) liquids.push_back(buildLiquid(in, part, *in.groups[part]));
    }

    std::vector<canon::LiquidType> liquidTypes;
    for (uint32_t row : liquidTypeRows(in)) liquidTypes.push_back(liquidinput::buildLiquidType(row, in.liquids, in.textures));
    for (canon::LiquidGrid& l : liquids) {
        auto id = std::get_if<canon::Db2Row>(&l.liquidType.id);
        if (!id) continue;
        l.liquidType = std::find_if(liquidTypes.begin(), liquidTypes.end(), [&](const canon::LiquidType& t) {
                           return std::get<canon::Db2Row>(t.ref.id).row == id->row;
                       })->ref;
    }

    std::vector<canon::Material> materials;
    for (uint32_t i = 0; i < root.materials.size(); ++i) materials.push_back(buildMaterial(in, i));

    canon::Model model =
        canon::assembleModel(canon::Skeleton{}, std::move(mesh), std::move(materials), std::move(primitiveMaterials), {},
                             buildScene(in), std::move(sets), std::move(liquids), {},
                             fogs.empty() ? std::nullopt : fogs[0]);
    model.liquidTypes = std::move(liquidTypes);
    return model;
}

std::vector<uint32_t> liquidTypeRows(const WmoInputs& in) {
    std::vector<uint32_t> out;
    for (const std::optional<wmo::GroupFile>& g : in.groups) {
        if (!g || !g->liquid) continue;
        std::optional<uint32_t> id = liquidTypeId(in.root->header.flags, *g);
        if (id && std::find(out.begin(), out.end(), *id) == out.end()) out.push_back(*id);
    }
    return out;
}

}  // namespace husk::wmoinput
