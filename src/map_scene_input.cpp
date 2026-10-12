#include "map_scene_input.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <string>
#include <unordered_map>

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
    // fog_level_to_priority: the lowest fog quality the classic client drew
    // it at, as what a renderer may thin first.
    if (f.fogLevel <= 2) {
        effect.priority = std::array{canon::Priority::Essential, canon::Priority::Ambient,
                                     canon::Priority::Decorative}[f.fogLevel];
        effect.derived.push_back({"priority", "fog_level_to_priority"});
    }

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

// Packed 0x00RRGGBB as stored -> RGB 0..1; a nonzero high byte (1, 3 or
// 0xFF, unverified: alpha or flags) goes to `unresolved` under `column`.
canon::Vec3 colorOf(uint32_t packed, const char* column, Unresolved& unresolved) {
    if (uint32_t high = packed >> 24) {
        unresolved.push_back({std::string("LightData ") + column + " high byte", canon::UnresolvedField::Kind::Scalar,
                              double(high), std::nullopt});
    }
    return {static_cast<float>((packed >> 16) & 0xFF) / 255.0f, static_cast<float>((packed >> 8) & 0xFF) / 255.0f,
            static_cast<float>(packed & 0xFF) / 255.0f};
}

std::optional<canon::TextureRef> gradingTexture(const MapSceneInputs& in, uint32_t fdid) {
    if (fdid == 0) return std::nullopt;
    auto resolved = in.textures.find(fdid);
    if (resolved != in.textures.end()) return resolved->second;
    canon::TextureRef unresolved;
    unresolved.state = canon::TextureRef::State::KnownUnresolved;
    unresolved.resolved = canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
    return unresolved;
}

canon::LightKey lightKey(const MapSceneInputs& in, const lightdb::Key& k) {
    canon::LightKey out;
    Unresolved& u = out.unresolved;
    out.time = static_cast<float>(k.time) * 30.0f;  // half-minutes
    out.directColor = colorOf(k.directColor, "DirectColor", u);
    out.ambientColor = colorOf(k.ambientColor, "AmbientColor", u);
    out.horizonAmbientColor = colorOf(k.horizonAmbientColor, "HorizonAmbientColor", u);
    out.groundAmbientColor = colorOf(k.groundAmbientColor, "GroundAmbientColor", u);
    out.skyTopColor = colorOf(k.skyTopColor, "SkyTopColor", u);
    out.skyMiddleColor = colorOf(k.skyMiddleColor, "SkyMiddleColor", u);
    out.skyBand1Color = colorOf(k.skyBand1Color, "SkyBand1Color", u);
    out.skyBand2Color = colorOf(k.skyBand2Color, "SkyBand2Color", u);
    out.skySmogColor = colorOf(k.skySmogColor, "SkySmogColor", u);
    out.sunColor = colorOf(k.sunColor, "SunColor", u);
    out.cloudSunColor = colorOf(k.cloudSunColor, "CloudSunColor", u);
    out.cloudEmissiveColor = colorOf(k.cloudEmissiveColor, "CloudEmissiveColor", u);
    out.cloudLayer1AmbientColor = colorOf(k.cloudLayer1AmbientColor, "CloudLayer1AmbientColor", u);
    out.cloudLayer2AmbientColor = colorOf(k.cloudLayer2AmbientColor, "CloudLayer2AmbientColor", u);
    out.cloudDensity = k.cloudDensity;
    out.oceanCloseColor = colorOf(k.oceanCloseColor, "OceanCloseColor", u);
    out.oceanFarColor = colorOf(k.oceanFarColor, "OceanFarColor", u);
    out.riverCloseColor = colorOf(k.riverCloseColor, "RiverCloseColor", u);
    out.riverFarColor = colorOf(k.riverFarColor, "RiverFarColor", u);
    out.shadowOpacity = colorOf(k.shadowOpacity, "ShadowOpacity", u);
    out.colorGrading = gradingTexture(in, k.colorGradingFileDataId);
    out.darkerColorGrading = gradingTexture(in, k.darkerColorGradingFileDataId);

    canon::ZoneFogValues& f = out.fog;
    f.end = k.fogEnd;
    f.scaler = k.fogScaler;
    f.density = k.fogDensity;
    f.startOffset = k.fogStartOffset;
    f.height = k.fogHeight;
    f.heightScaler = k.fogHeightScaler;
    f.heightDensity = k.fogHeightDensity;
    f.zScalar = k.fogZScalar;
    f.curveStart = k.mainFogStartDist;
    f.curveEnd = k.mainFogEndDist;
    f.sunAngle = k.sunFogAngle;
    f.sunStrength = k.sunFogStrength;
    f.endColorDistance = k.endFogColorDistance;
    f.skyColor = colorOf(k.skyFogColor, "SkyFogColor", u);
    f.endColor = colorOf(k.endFogColor, "EndFogColor", u);
    f.sunColor = colorOf(k.sunFogColor, "SunFogColor", u);
    f.heightColor = colorOf(k.fogHeightColor, "FogHeightColor", u);
    f.endHeightColor = colorOf(k.endFogHeightColor, "EndFogHeightColor", u);
    f.heightCoefficients = k.fogHeightCoefficients;
    f.mainCoefficients = k.mainFogCoefficients;
    f.heightDensityCoefficients = k.heightDensityFogCoefficients;

    // Only where they differ from 12.1.0's common value (0 on 17,423 of
    // 19,866 rows, 1 on 19,348): every key would carry them otherwise.
    if (k.field_10_0_0_44649_042 != 0) {
        u.push_back({"LightData Field_10_0_0_44649_042", canon::UnresolvedField::Kind::Scalar,
                     double(k.field_10_0_0_44649_042), std::nullopt});
    }
    if (k.field_12_0_0_63854_043 != 1.0f) {
        u.push_back({"LightData Field_12_0_0_63854_043", canon::UnresolvedField::Kind::Scalar,
                     double{k.field_12_0_0_63854_043}, std::nullopt});
    }
    return out;
}

std::optional<canon::Vec3> nonzero(const std::array<float, 3>& v) {
    if (v == std::array<float, 3>{}) return std::nullopt;
    return canon::Vec3{v[0], v[1], v[2]};
}

canon::LightSettings lightSettings(const MapSceneInputs& in, const lightdb::Params& p) {
    canon::LightSettings out;
    out.highlightSky = p.highlightSky;
    if (p.lightSkyboxId != 0) {
        canon::Skybox skybox;
        skybox.ref = canon::Ref{canon::Db2Row{"LightSkybox", p.lightSkyboxId}, "", canon::NameSource::None};
        auto row = in.lights->skyboxes.find(p.lightSkyboxId);
        if (row != in.lights->skyboxes.end()) {
            skybox.model = modelRef(in, row->second.skyboxFileDataId);
            if (row->second.celestialSkyboxFileDataId != 0) skybox.celestialModel = modelRef(in, row->second.celestialSkyboxFileDataId);
            skybox.flags = row->second.flags;
            out.skybox = std::move(skybox);
        } else {
            out.unresolved.push_back({"LightParams LightSkyboxID (no LightSkybox row)", canon::UnresolvedField::Kind::Id,
                                      double(p.lightSkyboxId), std::nullopt});
        }
    }
    out.cloudType = p.cloudTypeId;
    out.glow = p.glow;
    out.riverShallowAlpha = p.waterShallowAlpha;
    out.riverDeepAlpha = p.waterDeepAlpha;
    out.oceanShallowAlpha = p.oceanShallowAlpha;
    out.oceanDeepAlpha = p.oceanDeepAlpha;
    out.flags = p.flags;
    out.ssaoSettings = p.ssaoSettingsId;
    out.sunPolar = p.sunPolar * std::numbers::pi_v<float> / 180.0f;
    out.sunAzimuth = p.sunAzimuth * std::numbers::pi_v<float> / 180.0f;
    out.sunAttenuationStart = p.sunAttenuationStart;
    out.sunAttenuationEnd = p.sunAttenuationEnd;
    out.celestialSphereOverride = nonzero(p.overrideCelestialSphere);
    out.sunPositionOverride = nonzero(p.overrideSunPosition);
    // Field_12_0_0_63534_016 .. _029, then Field_12_0_1_65617_030 .. _031,
    // each only where it differs from the value at least 1,669 of 12.1.0's
    // 1,674 rows hold. _027 is the one integer column.
    static constexpr std::array<float, 16> kCommon = {1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0.35f, 1, 0.7f};
    for (size_t i = 0; i < p.unnamed.size(); ++i) {
        double value = i == 11 ? double(p.unnamed[i]) : double{std::bit_cast<float>(p.unnamed[i])};
        if (value == double{kCommon[i]}) continue;
        std::string field = (i < 14 ? "LightParams Field_12_0_0_63534_0" : "LightParams Field_12_0_1_65617_0") +
                            std::to_string(16 + i);
        out.unresolved.push_back({field, canon::UnresolvedField::Kind::Scalar, value, std::nullopt});
    }
    return out;
}

// The map's parameter sets, each once, in first-use order; `index` maps a
// LightParams id to its position.
struct ParamsTable {
    std::vector<canon::LightParams> entries;
    std::unordered_map<uint16_t, uint32_t> index;

    uint32_t add(const MapSceneInputs& in, uint16_t id, const std::vector<lightdb::Key>& keys) {
        auto [it, added] = index.emplace(id, static_cast<uint32_t>(entries.size()));
        if (!added) return it->second;
        canon::LightParams params;
        params.ref = canon::Ref{canon::Db2Row{"LightParams", id}, "", canon::NameSource::None};
        auto row = in.lights->params.find(id);
        if (row != in.lights->params.end()) params.settings = lightSettings(in, row->second);
        for (const lightdb::Key& k : keys) params.keys.push_back(lightKey(in, k));
        entries.push_back(std::move(params));
        return it->second;
    }
};

// One Light zone as an instance: the map-wide one (position and radii 0,
// wowdev.wiki DB/Light) everywhere, a local one a sphere fading between its
// falloff radii. Its first five LightParams slots are the style's
// conditions, indexing `table`; a slot naming params LightData has no keys
// for, and slots 5..7, stay unresolved.
canon::PlacedInstance zoneInstance(const MapSceneInputs& in, const lightdb::Zone& zone, ParamsTable& table) {
    canon::PlacedInstance instance;
    instance.id = zone.id;
    canon::EffectPlacement effect;
    effect.origin = {"Light", zone.id};
    bool global = zone.position == std::array<float, 3>{} && zone.falloffStart == 0 && zone.falloffEnd == 0;
    if (global) {
        instance.asset = canon::Ref{canon::None{}, "classic_zone_light_global", canon::NameSource::Synthesized};
    } else {
        instance.asset = canon::Ref{canon::None{}, "classic_zone_light", canon::NameSource::Synthesized};
        instance.translation = {zone.position[0], zone.position[1], zone.position[2]};
        effect.inputs = {{"inner_radius", {double{zone.falloffStart}}}, {"outer_radius", {double{zone.falloffEnd}}}};
    }
    canon::ZoneLight light;
    for (size_t slot = 0; slot < zone.params.size(); ++slot) {
        uint16_t params = zone.params[slot];
        if (params == 0) continue;
        auto keys = in.lights->keysByParams.find(params);
        if (slot < canon::ZoneLight::SlotCount && keys != in.lights->keysByParams.end()) {
            light.params[slot] = table.add(in, params, keys->second);
            continue;
        }
        effect.unresolved.push_back({"Light params slot " + std::to_string(slot), canon::UnresolvedField::Kind::Id,
                                     double(params), std::nullopt});
    }
    canon::StyleBlock style;
    style.zoneLight = light;
    effect.style = {{"light", {std::move(style)}}};
    instance.effect = std::move(effect);
    return instance;
}

std::vector<const lightdb::Zone*> mapZones(const MapSceneInputs& in) {
    std::vector<const lightdb::Zone*> out;
    if (!in.lights) return out;
    std::string dir = in.map;
    std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) { return std::tolower(c); });
    auto mapId = in.lights->mapIds.find(dir);
    if (mapId == in.lights->mapIds.end()) return out;
    for (const lightdb::Zone& zone : in.lights->zones) {
        if (zone.map == mapId->second) out.push_back(&zone);
    }
    return out;
}

}  // namespace

ZoneLightAssets zoneLightAssets(const MapSceneInputs& in) {
    ZoneLightAssets out;
    for (const lightdb::Zone* zone : mapZones(in)) {
        for (size_t slot = 0; slot < canon::ZoneLight::SlotCount; ++slot) {
            uint16_t id = zone->params[slot];
            if (auto keys = in.lights->keysByParams.find(id); keys != in.lights->keysByParams.end()) {
                for (const lightdb::Key& k : keys->second) {
                    for (uint32_t fdid : {k.colorGradingFileDataId, k.darkerColorGradingFileDataId})
                        if (fdid != 0) out.textures.insert(fdid);
                }
            }
            auto params = in.lights->params.find(id);
            if (params == in.lights->params.end()) continue;
            auto skybox = in.lights->skyboxes.find(params->second.lightSkyboxId);
            if (skybox == in.lights->skyboxes.end()) continue;
            out.models.insert(skybox->second.skyboxFileDataId);
            if (skybox->second.celestialSkyboxFileDataId != 0) out.models.insert(skybox->second.celestialSkyboxFileDataId);
        }
    }
    return out;
}

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
    canon::PlacementSet zones;
    zones.ref = canon::Ref{canon::None{}, "zone_lights", canon::NameSource::Synthesized};
    ParamsTable table;
    for (const lightdb::Zone* zone : mapZones(in)) zones.instances.push_back(zoneInstance(in, *zone, table));
    zones.lightParams = std::move(table.entries);
    std::vector<canon::PlacementSet> out;
    for (canon::PlacementSet* set : {&fogs, &particulate, &zones}) {
        if (!set->instances.empty()) out.push_back(std::move(*set));
    }
    return out;
}

}  // namespace husk::mapinput
