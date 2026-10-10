// mapinput::buildMapScene: parsed _fogs/_mpv records -> canon::MapScene.

#include <doctest/doctest.h>

#include "map_scene_input.hpp"

using namespace husk;

namespace {

wdt::Fog fog(uint32_t id, uint32_t model) {
    wdt::Fog f;
    f.id = id;
    f.color = {0.9f, 0.45f, 0.07f};
    f.position = {100, 200, 300};
    f.rotation = {0, 0, 0, 1};
    f.radius = {950, 200, 2.5f};
    f.modelFileDataId = model;
    f.fogLevel = 1;
    return f;
}

}  // namespace

TEST_CASE("buildMapScene: fogs carry their id, model identity and VFE2 palette; raw extras pass through") {
    wdt::FogsFile fogs;
    fogs.fogs = {fog(1659, 0), fog(1660, 1728356), fog(1661, 4242)};
    fogs.fogs[1].palette = wdt::Fog::Palette{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, 2.0f};
    fogs.fogs[1].extraFields = wdt::Fog::Extra{1, {600, 600, 200}, {0.4f, -15}};
    fogs.fogs[1].extra.assign(96, 7);
    fogs.fogs[1].extra2.assign(176, 8);
    mapinput::MapSceneInputs in;
    in.map = "2656";
    in.fogs = &fogs;
    in.assetNames.emplace(1728356, "fogvolume_sphere");

    canon::MapScene scene = mapinput::buildMapScene(in);

    CHECK(scene.map.name == "2656");
    CHECK(scene.particulateVolumes.empty());
    REQUIRE(scene.fogs.size() == 3);
    const canon::FogVolume& errorcube = scene.fogs[0];
    CHECK(errorcube.ref.name == "vfog_1659");
    CHECK(std::holds_alternative<canon::None>(errorcube.model.id));
    CHECK_FALSE(errorcube.palette.has_value());
    const canon::FogVolume& named = scene.fogs[1];
    CHECK(std::get<canon::RecordIndex>(named.ref.id).value == 1);
    CHECK(named.id == 1660);
    CHECK(named.position.z == 300.0f);
    CHECK(named.rotation.w == 1.0f);
    CHECK(named.radius[2] == 2.5f);
    CHECK(named.model.name == "fogvolume_sphere");
    CHECK(named.model.source == canon::NameSource::Listfile);
    REQUIRE(named.palette.has_value());
    CHECK(named.palette->colors[2].z == 1.0f);
    CHECK(named.palette->mode == 2.0f);
    REQUIRE(named.extra.has_value());
    CHECK(named.extra->enabled);
    CHECK(named.extra->extent.z == 200.0f);
    CHECK(named.extra->parameters[1] == -15.0f);
    CHECK_FALSE(errorcube.extra.has_value());
    CHECK(named.sourceExtra.size() == 96);
    CHECK(named.sourceExtra2.size() == 176);
    const canon::FogVolume& unlisted = scene.fogs[2];
    CHECK(std::get<canon::FileDataId>(unlisted.model.id).value == 4242);
    CHECK(unlisted.model.source == canon::NameSource::None);
}

TEST_CASE("buildMapScene: particulate volumes reuse the WMO record mapping, plus bounds, points and box tree") {
    wdt::MpvFile mpv;
    wdt::MapParticulateVolume v;
    v.id = 185;
    v.boundsMin = {-5, -5, 0};
    v.boundsMax = {5, 5, 10};
    v.volume.position = {1, 2, 3};
    wmo::ParticulateEmitter dust;
    dust.textureFileDataId = 77;
    dust.blendMode = 2;
    v.volume.emitters = {dust};
    v.volume.record.assign(4272, 0);
    v.points = {{{1, 0}, -0.0f, 2717.7f}};
    v.boxes = {{{-1, -1, 0}, {1, 1, 1}, {}, 1}, {{-2, -2, 0}, {2, 2, 2}, {0}, 0}};
    mpv.volumes = {v};
    mapinput::MapSceneInputs in;
    in.map = "2656";
    in.mpv = &mpv;
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    resolved.resolved = canon::Ref{canon::FileDataId{77}, "dust1", canon::NameSource::Listfile};
    in.textures.emplace(77, resolved);

    canon::MapScene scene = mapinput::buildMapScene(in);

    CHECK(scene.fogs.empty());
    REQUIRE(scene.particulateVolumes.size() == 1);
    const canon::MapParticulateVolume& out = scene.particulateVolumes[0];
    CHECK(out.volume.ref.name == "pvmi_185");
    CHECK(out.id == 185);
    CHECK(out.volume.position.y == 2.0f);
    REQUIRE(out.volume.emitters.size() == 1);
    CHECK(out.volume.emitters[0].texture.resolved.name == "dust1");
    CHECK(out.volume.emitters[0].blend == canon::FramebufferBlend::Alpha);
    CHECK(out.boundsMax.z == 10.0f);
    REQUIRE(out.points.size() == 1);
    CHECK(out.points[0].direction.x == 1.0f);
    CHECK(out.points[0].unknown0C == 2717.7f);
    REQUIRE(out.boxes.size() == 2);
    CHECK(out.boxes[1].children == std::vector<uint32_t>{0});
    CHECK(out.boxes[1].complete == 0);
    CHECK(out.volume.sourceRecord.size() == 4272);
}
