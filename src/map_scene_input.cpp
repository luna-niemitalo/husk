#include "map_scene_input.hpp"

#include <string>

namespace husk::mapinput {

namespace {

canon::Vec3 toCanon(const wdt::Vec3& v) { return {v[0], v[1], v[2]}; }

canon::Ref modelRef(const MapSceneInputs& in, uint32_t fdid) {
    if (fdid == 0) return canon::Ref{};
    auto name = in.assetNames.find(fdid);
    if (name == in.assetNames.end()) return canon::Ref{canon::FileDataId{fdid}, "", canon::NameSource::None};
    return canon::Ref{canon::FileDataId{fdid}, name->second, canon::NameSource::Listfile};
}

canon::FogVolume buildFog(const MapSceneInputs& in, const wdt::Fog& f, uint32_t index) {
    canon::FogVolume out;
    out.ref = canon::Ref{canon::RecordIndex{index}, "vfog_" + std::to_string(f.id), canon::NameSource::Synthesized};
    out.id = f.id;
    out.color = toCanon(f.color);
    out.intensity = f.intensity;
    out.unknown18 = f.unknown18;
    out.position = toCanon(f.position);
    out.rotation = canon::Quat{f.rotation[0], f.rotation[1], f.rotation[2], f.rotation[3]};
    out.radius = f.radius;
    out.animationPeriods = f.animationPeriods;
    out.flags = f.flags;
    out.model = modelRef(in, f.modelFileDataId);
    out.fogLevel = f.fogLevel;
    if (f.palette) {
        canon::FogVolume::Palette palette;
        for (size_t k = 0; k < 3; ++k) palette.colors[k] = toCanon(f.palette->colors[k]);
        palette.mode = f.palette->mode;
        out.palette = palette;
    }
    out.sourceExtra = f.extra;
    out.sourceExtra2 = f.extra2;
    return out;
}

canon::MapParticulateVolume buildParticulate(const MapSceneInputs& in, const wdt::MapParticulateVolume& v,
                                             uint32_t index) {
    canon::MapParticulateVolume out;
    out.volume = wmoinput::buildParticulateVolume(v.volume, in.textures);
    out.volume.ref = canon::Ref{canon::RecordIndex{index}, "pvmi_" + std::to_string(v.id), canon::NameSource::Synthesized};
    out.id = v.id;
    out.boundsMin = toCanon(v.boundsMin);
    out.boundsMax = toCanon(v.boundsMax);
    for (const wdt::ParticulatePoint& p : v.points) {
        out.points.push_back({canon::Vec2{p.direction[0], p.direction[1]}, p.unknown08, p.unknown0C});
    }
    for (const wdt::ParticulateBox& b : v.boxes) {
        out.boxes.push_back({toCanon(b.min), toCanon(b.max), b.children, b.complete});
    }
    return out;
}

}  // namespace

canon::MapScene buildMapScene(const MapSceneInputs& in) {
    canon::MapScene out;
    out.map = canon::Ref{canon::None{}, in.map, canon::NameSource::Listfile};
    if (in.fogs) {
        for (uint32_t i = 0; i < in.fogs->fogs.size(); ++i) out.fogs.push_back(buildFog(in, in.fogs->fogs[i], i));
    }
    if (in.mpv) {
        for (uint32_t i = 0; i < in.mpv->volumes.size(); ++i) {
            out.particulateVolumes.push_back(buildParticulate(in, in.mpv->volumes[i], i));
        }
    }
    return out;
}

}  // namespace husk::mapinput
