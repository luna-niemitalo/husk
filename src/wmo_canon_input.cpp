#include "wmo_canon_input.hpp"

#include <algorithm>
#include <array>
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

canon::AmbientVolume buildAmbientVolume(const WmoInputs& in, const wmo::AmbientVolume& v, const char* table,
                                        uint32_t index, canon::AmbientVolume::Shape shape) {
    canon::AmbientVolume out;
    out.origin = {table, index};
    out.shape = shape;
    out.position = toCanon(v.position);
    out.start = v.start;
    out.end = v.end;
    for (size_t k = 0; k < 3; ++k) out.colors[k] = rgbaOf(v.colors[k]);
    out.sourceFlags = v.flags;
    out.placementSet = ownedSetRef(in, v.doodadSet);
    return out;
}

canon::AmbientVolume buildAmbientBox(const WmoInputs& in, uint32_t index) {
    const wmo::AmbientBox& b = in.root->ambientBoxes[index];
    canon::AmbientVolume out;
    out.origin = {"MBVD", index};
    out.shape = canon::AmbientVolume::Shape::Box;
    out.end = b.end;
    for (const auto& [x, y, z, d] : b.planes) out.planes.push_back({canon::Vec3{x, y, z}, d});
    for (size_t k = 0; k < 3; ++k) out.colors[k] = rgbaOf(b.colors[k]);
    out.sourceFlags = b.flags;
    out.placementSet = ownedSetRef(in, b.doodadSet);
    return out;
}

constexpr uint32_t kFogInteriorExteriorBlend = 0x1;

canon::LocalFog buildFog(const WmoInputs& in, uint32_t index) {
    const wmo::Fog& f = in.root->fogs[index];
    canon::LocalFog out;
    out.origin = {"MFOG", index};
    out.position = toCanon(f.position);
    out.innerRadius = f.smallerRadius;
    out.outerRadius = f.largerRadius;
    out.interiorExteriorBlend = (f.flags & kFogInteriorExteriorBlend) != 0;
    out.fog = {f.fog.end, f.fog.startScalar, rgbaOf(f.fog.color)};
    out.underwater = {f.underwater.end, f.underwater.startScalar, rgbaOf(f.underwater.color)};
    out.sourceFlags = f.flags;
    if (f.doodadSet) out.placementSet = ownedSetRef(in, *f.doodadSet);
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
    const wmo::RootFile& root = *in.root;
    for (uint32_t i = 0; i < root.globalAmbientVolumes.size(); ++i) {
        scene.ambientVolumes.push_back(buildAmbientVolume(in, root.globalAmbientVolumes[i], "MAVG", i,
                                                          canon::AmbientVolume::Shape::Global));
    }
    for (uint32_t i = 0; i < root.ambientVolumes.size(); ++i) {
        scene.ambientVolumes.push_back(
            buildAmbientVolume(in, root.ambientVolumes[i], "MAVD", i, canon::AmbientVolume::Shape::Point));
    }
    for (uint32_t i = 0; i < root.ambientBoxes.size(); ++i) scene.ambientVolumes.push_back(buildAmbientBox(in, i));
    for (uint32_t i = 0; i < root.fogs.size(); ++i) scene.fogs.push_back(buildFog(in, i));
    for (uint32_t i = 0; i < in.root->particulateVolumes.size(); ++i) {
        canon::ParticulateVolume v = buildParticulateVolume(root.particulateVolumes[i], in.textures);
        v.origin = {"MPVD", i};
        scene.particulateVolumes.push_back(std::move(v));
    }
    for (uint32_t i = 0; i < in.root->newLights.size(); ++i) scene.localLights.push_back(buildNewLight(in, i));
    for (uint32_t i = 0; i < in.root->legacyLights.size(); ++i) scene.localLights.push_back(buildLegacyLight(in, i));
    nameByPosition(scene.ambientVolumes, "ambient");
    nameByPosition(scene.fogs, "fog");
    nameByPosition(scene.particulateVolumes, "particles");
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

canon::ParticulateVolume buildParticulateVolume(const wmo::ParticulateVolume& v, const TextureResolutions& textures) {
    canon::ParticulateVolume out;
    out.position = toCanon(v.position);
    out.extent = toCanon(v.extent);
    out.eulerRotation = toCanon(v.rotation);
    out.presetId = v.presetId;
    out.particleCount = v.particleCount;
    out.opacity = v.opacity;
    for (const auto& [value, value2, time] : v.envelope) out.envelope.push_back({time, value, value2});
    for (const wmo::ParticulateEmitter& e : v.emitters) {
        canon::ParticulateVolume::Emitter emitter;
        emitter.slot = e.slot;
        emitter.texture = textureById(textures, e.textureFileDataId);
        emitter.blend = framebufferBlendFor(e.blendMode);
        emitter.sourceBlend = e.blendMode;
        emitter.color = e.color;
        emitter.tilesX = e.tilesX;
        emitter.tilesY = e.tilesY;
        emitter.frames = e.frames;
        emitter.parameters = e.parameters;
        out.emitters.push_back(std::move(emitter));
    }
    out.sourceFlags = v.flags;
    out.sourceRecord = v.record;
    return out;
}

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
            meshPart.fogs.assign(g.fogIds.begin(), g.fogIds.end());
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
                             buildScene(in), buildPlacementSets(in), std::move(liquids));
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
