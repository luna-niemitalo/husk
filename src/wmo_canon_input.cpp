#include "wmo_canon_input.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>
#include <string>

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

canon::TextureRef textureFor(const WmoInputs& in, uint32_t slot) {
    const wmo::RootFile& root = *in.root;
    canon::TextureRef ref;
    if (!root.textureNames.empty()) {
        // MOTX mode: the slot is a byte offset into a path table, and husk
        // resolves textures by FileDataID only.
        ref.unresolvedReason = "MOTX path '" + wmo::stringAt(root.textureNames, slot) + "' has no FileDataID";
        return ref;
    }
    auto it = in.textures.find(slot);
    if (it != in.textures.end()) return it->second;
    ref.unresolvedReason = "texture FileDataID " + std::to_string(slot) + " was not resolved";
    return ref;
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
    auto name = in.assetNames.find(fdid);
    if (name == in.assetNames.end()) return canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
    return canon::Ref{canon::FileDataId{fdid}, name->second, canon::NameSource::Listfile};
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
        mesh.parts.push_back(meshPart);
        if (in.groups[part]) appendGroup(in, part, *in.groups[part], mesh, primitiveMaterials, uvSetCount, colorSetCount);
    }

    std::vector<canon::Material> materials;
    for (uint32_t i = 0; i < root.materials.size(); ++i) materials.push_back(buildMaterial(in, i));

    return canon::assembleModel(canon::Skeleton{}, std::move(mesh), std::move(materials), std::move(primitiveMaterials),
                                {}, {}, buildPlacementSets(in));
}

}  // namespace husk::wmoinput
