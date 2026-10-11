// mapinput::buildMapEffects: parsed _fogs/_mpv records -> world placement
// sets of effect instances.

#include <doctest/doctest.h>

#include <algorithm>

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

const canon::UnresolvedField* field(const canon::PlacedInstance& i, const std::string& name) {
    const auto& fields = i.effect->unresolved;
    auto it = std::find_if(fields.begin(), fields.end(), [&](const canon::UnresolvedField& f) { return f.field == name; });
    return it == fields.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("buildMapEffects: fogs are volumetric fog instances by id, their model the shape, VFEX/VFE2 unresolved") {
    wdt::FogsFile fogs;
    fogs.fogs = {fog(1659, 0), fog(1660, 1728356), fog(1661, 4242)};
    wdt::Fog::Palette palette;
    palette.colors = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    palette.mode = 2.0f;
    fogs.fogs[1].palette = palette;
    wdt::Fog::Extra extra;
    extra.enabled = 1;
    extra.extent = {600, 600, 200};
    extra.parameters = {0.4f, -15};
    fogs.fogs[1].extraFields = extra;
    mapinput::MapSceneInputs in;
    in.map = "2656";
    in.fogs = &fogs;
    in.assetNames.emplace(1728356, "fogvolume_sphere");
    in.assetNames.emplace(mapinput::kFallbackFogModel, "errorcube");

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 1);
    CHECK(sets[0].ref.name == "fogs");
    REQUIRE(sets[0].instances.size() == 3);
    const canon::PlacedInstance& errorcube = sets[0].instances[0];
    CHECK(errorcube.id == 1659);
    CHECK(errorcube.asset.name == "classic_volumetric_fog");
    CHECK(errorcube.effect->origin.table == "VFOG");
    // Model 0: the client's fallback is the shape, and the 0 is kept.
    CHECK(std::get<canon::Ref>(errorcube.effect->inputs.at(0).value.value).name == "errorcube");
    CHECK(field(errorcube, "VFOG model (0: the client draws its fallback)"));
    CHECK_FALSE(field(errorcube, "VFE2+0x3C"));

    const canon::PlacedInstance& named = sets[0].instances[1];
    CHECK(named.translation.z == 300.0f);
    CHECK(named.rotation.w == 1.0f);
    const canon::Ref& shape = std::get<canon::Ref>(named.effect->inputs.at(0).value.value);
    CHECK(shape.name == "fogvolume_sphere");
    CHECK(shape.source == canon::NameSource::Listfile);
    const canon::StyleBlock& style = named.effect->style.at(0).blocks.at(0);
    CHECK(style.volumetricFog->color.x == 0.9f);
    CHECK(style.volumetricFog->minQuality == 1);
    CHECK(std::get<std::vector<double>>(field(named, "VFOG radius")->value)[2] == 2.5);
    CHECK(field(named, "VFEX+0x00")->candidateRole == "gate.enabled");
    CHECK(std::get<std::vector<double>>(field(named, "VFEX+0x04")->value)[2] == 200.0);
    CHECK(std::get<std::vector<double>>(field(named, "VFEX+0x10")->value)[1] == -15.0);
    CHECK(std::get<std::vector<double>>(field(named, "VFE2+0x2C")->value)[2] == 1.0);
    CHECK(std::get<double>(field(named, "VFE2+0x3C")->value) == 2.0);

    const canon::Ref& unlisted = std::get<canon::Ref>(sets[0].instances[2].effect->inputs.at(0).value.value);
    CHECK(std::get<canon::FileDataId>(unlisted.id).value == 4242);
    CHECK(unlisted.source == canon::NameSource::None);
}

TEST_CASE("buildMapEffects: particulate volumes reuse the WMO instance, with the PVBD tree as the region") {
    wdt::MpvFile mpv;
    wdt::MapParticulateVolume v;
    v.id = 185;
    v.volume.position = {1, 2, 3};
    v.volume.extent = {4, 4, 2};
    v.boundsMin = {-1, 0, 2};
    v.boundsMax = {3, 4, 4};
    wmo::ParticulateEmitter dust;
    dust.textureFileDataId = 77;
    dust.blendMode = 2;
    v.volume.emitters = {dust};
    v.points = {{{1, 0}, -0.0f, 2717.7f}};
    v.boxes = {{{-1, 0, 2}, {1, 4, 4}, {}, 1}, {{-1, 0, 2}, {3, 4, 4}, {0}, 0}};
    mpv.volumes = {v};
    mapinput::MapSceneInputs in;
    in.map = "2656";
    in.mpv = &mpv;
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    resolved.resolved = canon::Ref{canon::FileDataId{77}, "dust1", canon::NameSource::Listfile};
    in.textures.emplace(77, resolved);

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 1);
    CHECK(sets[0].ref.name == "particulate_volumes");
    REQUIRE(sets[0].instances.size() == 1);
    const canon::PlacedInstance& out = sets[0].instances[0];
    CHECK(out.id == 185);
    CHECK(out.asset.name == "classic_map_particulate_volume");
    CHECK(out.effect->origin.table == "PVMI");
    CHECK(out.translation.y == 2.0f);
    REQUIRE(out.effect->inputs.at(0).name == "pieces");
    const auto& pieces = std::get<std::vector<canon::InputValue>>(out.effect->inputs.at(0).value.value);
    REQUIRE(pieces.size() == 2);
    // Boxes move into the instance's frame: world min (-1, 0, 2) - position (1, 2, 3).
    const auto& root = std::get<std::vector<canon::NamedInput>>(pieces[1].value);
    CHECK(std::get<canon::Vec3>(root[0].value.value).x == -2.0f);
    CHECK(std::get<canon::Vec3>(root[0].value.value).z == -1.0f);
    CHECK(std::get<std::vector<canon::InputValue>>(root[2].value.value).size() == 1);
    CHECK_FALSE(std::get<bool>(root[3].value.value));
    // The root is the world box, and that box is position +- extent / 2: not carried.
    CHECK_FALSE(field(out, "PVMI bounds (min, max)"));
    CHECK(std::get<std::vector<double>>(field(out, "PVPD (x, y, +0x08, +0x0C)")->value)[3] == doctest::Approx(2717.7));
    const auto& emitters = std::get<std::vector<canon::InputValue>>(out.effect->inputs.at(1).value.value);
    const auto& texture = std::get<std::vector<canon::NamedInput>>(emitters[0].value)[0];
    CHECK(std::get<canon::TextureRef>(texture.value.value).resolved.name == "dust1");
    CHECK(out.effect->style.at(1).blocks.at(0).draw->blend == canon::FramebufferBlend::Alpha);
}

TEST_CASE("buildMapEffects: fog and particulate ids are separate spaces, so each gets its own set") {
    wdt::FogsFile fogs;
    fogs.fogs = {fog(185, 0)};
    wdt::MpvFile mpv;
    wdt::MapParticulateVolume v;
    v.id = 185;
    mpv.volumes = {v};
    mapinput::MapSceneInputs in;
    in.fogs = &fogs;
    in.mpv = &mpv;

    std::vector<canon::PlacementSet> sets = mapinput::buildMapEffects(in);

    REQUIRE(sets.size() == 2);
    CHECK(sets[0].instances[0].id == 185);
    CHECK(sets[1].instances[0].id == 185);
    // A volume without PVBD boxes has its world box as the one piece.
    const auto& pieces = std::get<std::vector<canon::InputValue>>(sets[1].instances[0].effect->inputs.at(0).value.value);
    CHECK(pieces.size() == 1);
}
