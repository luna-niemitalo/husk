#include "map_scene_input.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace husk::mapinput {

namespace {

canon::Vec3 toCanon(const wdt::Vec3& v) { return {v[0], v[1], v[2]}; }

canon::Ref modelRef(const MapSceneInputs& in, uint32_t fdid) {
    auto name = in.assetNames.find(fdid);
    if (name == in.assetNames.end()) return canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
    return canon::Ref{canon::FileDataId{fdid}, name->second, canon::NameSource::Listfile};
}

using Unresolved = std::vector<canon::UnresolvedField>;

template <typename Array>
void scalars(Unresolved& out, std::string field, const Array& values,
             std::optional<std::string> candidate = std::nullopt) {
    out.push_back({std::move(field), canon::UnresolvedField::Kind::Scalars,
                   std::vector<double>(values.begin(), values.end()), std::move(candidate)});
}

void words(Unresolved& out, const std::string& table, const std::vector<wmo::UnknownRange>& ranges) {
    for (const wmo::UnknownRange& r : ranges) {
        char offset[16];
        std::snprintf(offset, sizeof(offset), "+0x%X", r.offset);
        out.push_back({table + offset, canon::UnresolvedField::Kind::Words, r.words, std::nullopt});
    }
}

// One VFOG with its VFEX and VFE2 as a classic_volumetric_fog instance, at
// the fog's position and rotation. Only the shape model, the colour and the
// quality level have a stated role; the rest is unresolved.
canon::PlacedInstance fogInstance(const MapSceneInputs& in, const wdt::Fog& f, uint32_t index) {
    canon::PlacedInstance instance;
    instance.id = f.id;
    instance.asset = canon::Ref{canon::None{}, "classic_volumetric_fog", canon::NameSource::Synthesized};
    instance.translation = toCanon(f.position);
    instance.rotation = canon::Quat{f.rotation[0], f.rotation[1], f.rotation[2], f.rotation[3]};
    instance.flags = f.flags;
    canon::EffectPlacement effect;
    effect.origin = {"VFOG", index};
    effect.inputs = {{"shape", {modelRef(in, f.modelFileDataId ? f.modelFileDataId : kFallbackFogModel)}}};
    canon::StyleBlock style;
    style.volumetricFog = canon::StyleBlock::VolumetricFog{toCanon(f.color), f.fogLevel};
    effect.style = {{"fog", {style}}};

    Unresolved& u = effect.unresolved;
    if (f.modelFileDataId == 0) {
        u.push_back({"VFOG model (0: the client draws its fallback)", canon::UnresolvedField::Kind::Id, 0.0, std::nullopt});
    }
    scalars(u, "VFOG intensity", f.intensity);
    u.push_back({"VFOG+0x18", canon::UnresolvedField::Kind::Scalar, double{f.unknown18}, std::nullopt});
    scalars(u, "VFOG radius", f.radius);
    scalars(u, "VFOG animation periods", f.animationPeriods);
    if (f.flags != 0) u.push_back({"VFOG flags", canon::UnresolvedField::Kind::Bits, f.flags, std::nullopt});
    if (f.extraFields) {
        const wdt::Fog::Extra& x = *f.extraFields;
        // 0 in 3 of 1,542 records: reads as an enable switch.
        u.push_back({"VFEX+0x00", canon::UnresolvedField::Kind::Scalar, double(x.enabled), "gate.enabled"});
        scalars(u, "VFEX+0x04", x.extent);
        scalars(u, "VFEX+0x10", x.parameters);
        words(u, "VFEX", x.unknownRanges);
    }
    if (f.palette) {
        const wdt::Fog::Palette& p = *f.palette;
        for (auto [offset, color] : {std::pair{"VFE2+0x00", &p.colors[0]}, std::pair{"VFE2+0x20", &p.colors[1]},
                                     std::pair{"VFE2+0x2C", &p.colors[2]}}) {
            u.push_back({offset, canon::UnresolvedField::Kind::Color, std::vector<double>(color->begin(), color->end()),
                         std::nullopt});
        }
        scalars(u, "VFE2+0x0C", p.scalars0C);
        u.push_back({"VFE2+0x38", canon::UnresolvedField::Kind::Scalar, double{p.scalar38}, std::nullopt});
        u.push_back({"VFE2+0x3C", canon::UnresolvedField::Kind::Scalar, double{p.mode}, std::nullopt});
        scalars(u, "VFE2+0x40", p.scalars40);
        scalars(u, "VFE2+0x50", p.scalars50);
        words(u, "VFE2", p.unknownRanges);
    }
    instance.effect = std::move(effect);
    return instance;
}

canon::Vec3 minus(const canon::Vec3& a, const canon::Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

bool same(const canon::Vec3& a, const canon::Vec3& b) {
    return std::abs(a.x - b.x) < 1e-2f && std::abs(a.y - b.y) < 1e-2f && std::abs(a.z - b.z) < 1e-2f;
}

// One PVMI group as a classic_map_particulate_volume instance at the
// volume's position: the WMO record's instance, with the PVBD box tree,
// in the instance's frame, as its region. In all 619 real groups the
// tree's one root is the PVMI world box, which is position ± extent / 2;
// a group that breaks that keeps its box unresolved.
canon::PlacedInstance particulateInstance(const MapSceneInputs& in, const wdt::MapParticulateVolume& v, uint32_t index) {
    canon::PlacedInstance instance = wmoinput::particulateInstance(v.volume, in.textures, {"PVMI", index});
    instance.id = v.id;
    instance.asset = canon::Ref{canon::None{}, "classic_map_particulate_volume", canon::NameSource::Synthesized};
    canon::Vec3 origin = instance.translation;
    canon::Vec3 boundsMin = toCanon(v.boundsMin);
    canon::Vec3 boundsMax = toCanon(v.boundsMax);

    std::vector<wdt::ParticulateBox> boxes = v.boxes;
    if (boxes.empty()) boxes.push_back({v.boundsMin, v.boundsMax, {}, 1});
    std::set<uint32_t> children;
    for (const wdt::ParticulateBox& b : boxes) children.insert(b.children.begin(), b.children.end());
    std::vector<canon::InputValue> pieces;
    bool rootIsBounds = true;
    for (uint32_t i = 0; i < boxes.size(); ++i) {
        const wdt::ParticulateBox& b = boxes[i];
        if (!children.count(i)) rootIsBounds = rootIsBounds && same(toCanon(b.min), boundsMin) && same(toCanon(b.max), boundsMax);
        std::vector<canon::InputValue> childIndices;
        for (uint32_t c : b.children) childIndices.push_back({int64_t{c}});
        pieces.push_back({std::vector<canon::NamedInput>{{"min", {minus(toCanon(b.min), origin)}},
                                                         {"max", {minus(toCanon(b.max), origin)}},
                                                         {"children", {std::move(childIndices)}},
                                                         {"complete", {b.complete != 0}}}});
    }
    canon::EffectPlacement& effect = *instance.effect;
    auto size = std::find_if(effect.inputs.begin(), effect.inputs.end(), [](const canon::NamedInput& i) { return i.name == "size"; });
    *size = {"pieces", {std::move(pieces)}};
    canon::Vec3 extent = toCanon(v.volume.extent);
    canon::Vec3 half{extent.x / 2, extent.y / 2, extent.z / 2};
    bool boundsAreBox = same(boundsMin, minus(origin, half)) && same(boundsMax, minus(origin, {-half.x, -half.y, -half.z}));
    if (!rootIsBounds || !boundsAreBox) {
        scalars(effect.unresolved, "PVMI bounds (min, max)",
                std::array<float, 6>{boundsMin.x, boundsMin.y, boundsMin.z, boundsMax.x, boundsMax.y, boundsMax.z});
    }
    if (!v.points.empty()) {
        std::vector<double> points;
        for (const wdt::ParticulatePoint& p : v.points) points.insert(points.end(), {p.direction[0], p.direction[1], p.unknown08, p.unknown0C});
        effect.unresolved.push_back({"PVPD (x, y, +0x08, +0x0C)", canon::UnresolvedField::Kind::Scalars, points, std::nullopt});
    }
    return instance;
}

}  // namespace

std::vector<canon::PlacementSet> buildMapEffects(const MapSceneInputs& in) {
    canon::PlacementSet fogs;
    fogs.ref = canon::Ref{canon::None{}, "fogs", canon::NameSource::Synthesized};
    if (in.fogs) {
        for (uint32_t i = 0; i < in.fogs->fogs.size(); ++i) fogs.instances.push_back(fogInstance(in, in.fogs->fogs[i], i));
    }
    canon::PlacementSet particulate;
    particulate.ref = canon::Ref{canon::None{}, "particulate_volumes", canon::NameSource::Synthesized};
    if (in.mpv) {
        for (uint32_t i = 0; i < in.mpv->volumes.size(); ++i) {
            particulate.instances.push_back(particulateInstance(in, in.mpv->volumes[i], i));
        }
    }
    std::vector<canon::PlacementSet> out;
    for (canon::PlacementSet* set : {&fogs, &particulate}) {
        if (!set->instances.empty()) out.push_back(std::move(*set));
    }
    return out;
}

}  // namespace husk::mapinput
