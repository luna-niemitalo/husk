// wmoinput::buildCanonWmo (wmo_canon_input.hpp): a WMO as an ordinary
// canon::Model -- groups as mesh parts, MOMT as materials, MODS as owned
// placement sets (REFACTOR/PLACEMENT_SETS.md).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "test_data_paths.hpp"
#include "wmo.hpp"
#include "wmo_canon_input.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The wiki's "empty fog entry": record 0 of most real roots.
wmo::Fog emptyFog() {
    wmo::Fog f;
    f.fog = {444.4444580078125f, 0.25f, 0xFFFFFFFFu};
    f.underwater = {222.22222900390625f, -0.5f, 0xFF0000FFu};
    return f;
}

// Two groups: group 0 has two triangles and two UV sets, group 1 one
// triangle and one UV set. Two materials, two doodad sets over three doodads.
wmo::RootFile syntheticRoot() {
    wmo::RootFile root;
    root.header.groupCount = 2;
    root.groups.resize(2);
    root.groupNames = std::string("hall\0", 5);
    root.groups[0].nameOffset = 0;
    root.groups[0].flags = 0x8;
    wmo::Material diffuse;
    diffuse.shader = 0;
    diffuse.blendMode = 1;
    diffuse.flags = 0x5;  // unlit, two-sided
    diffuse.textures = {101, 0, 0};
    wmo::Material twoLayer;
    twoLayer.shader = 6;
    twoLayer.blendMode = 7;  // GxBlend_InvSrcAlphaAdd: no canon name
    twoLayer.textures = {102, 103, 0};
    root.materials = {diffuse, twoLayer};
    root.uvScrollSpeeds = {{{{0, 0}, {0, 0}}}, {{{0.5f, 0}, {0, 0.25f}}}};
    root.doodadFileDataIds = {7001, 0};
    for (uint32_t i = 0; i < 3; ++i) {
        wmo::Doodad d;
        d.nameIndex = i == 2 ? 1 : 0;
        d.position = {float(i), 0, 0};
        d.color = 0x80102030u;  // BGRA bytes 30 20 10 80 -> RGBA 10 20 30 80
        root.doodads.push_back(d);
    }
    root.doodadColorMultipliers = {1.0f, 2.0f, 3.0f};
    root.doodadSets = {{"Set_$DefaultGlobal", 0, 1}, {"Set_B", 1, 2}};
    root.fogs = {emptyFog()};  // every real root has an MFOG for its groups' fog id 0
    return root;
}

wmo::GroupFile group(uint32_t vertexCount, uint32_t uvSets, uint32_t material, uint32_t triangles) {
    wmo::GroupFile g;
    for (uint32_t v = 0; v < vertexCount; ++v) {
        g.positions.push_back({float(v), 0, 0});
        g.normals.push_back({0, 0, 1});
    }
    for (uint32_t s = 0; s < uvSets; ++s) g.uvSets.emplace_back(vertexCount, wmo::Vec2{float(s + 1), 0});
    g.colorSets.emplace_back(vertexCount, 0xFF000000u);
    for (uint32_t t = 0; t < triangles * 3; ++t) g.indices.push_back(t % vertexCount);
    g.triangles.assign(triangles, wmo::TriangleInfo{0x20, static_cast<uint16_t>(material)});
    wmo::Batch b;
    b.indexCount = triangles * 3;
    b.material = material;
    g.batches.push_back(b);
    return g;
}

}  // namespace

TEST_CASE("buildCanonWmo: groups become parts sharing one vertex buffer, batches become primitives") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {group(4, 2, 1, 2), group(3, 1, 0, 1)};

    canon::Model model = wmoinput::buildCanonWmo(in);

    CHECK(model.skeleton.joints.empty());
    REQUIRE(model.mesh.parts.size() == 2);
    CHECK(model.mesh.parts[0].ref.name == "hall");
    CHECK(model.mesh.parts[0].ref.source == canon::NameSource::WmoEmbedded);
    CHECK(model.mesh.parts[0].flags == 0x8);
    CHECK(model.mesh.parts[1].ref.source == canon::NameSource::None);
    CHECK(model.mesh.positions.size() == 7);
    REQUIRE(model.mesh.uv1.has_value());
    CHECK((*model.mesh.uv1)[0].x == 2.0f);
    CHECK((*model.mesh.uv1)[4].x == 0.0f);  // group 1 has no second set: zero-filled
    REQUIRE(model.mesh.primitives.size() == 2);
    CHECK(model.mesh.primitives[1].indexStart == 6);
    CHECK(model.mesh.primitives[1].part == 1u);
    CHECK(model.mesh.indices[6] == 4);  // group 1's vertex 0, offset past group 0's 4 vertices
    CHECK(canon::resolveMaterialIndex(model, 0) == 1u);
}

TEST_CASE("buildCanonWmo: materials carry blend, shared flags, shader name, textures and UV scroll") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    in.textures.emplace(101, resolved);

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.materials.size() == 2);
    const canon::Material& diffuse = model.materials[0];
    CHECK(diffuse.framebufferBlend == canon::FramebufferBlend::AlphaKey);
    CHECK(diffuse.unlit);
    CHECK(diffuse.twoSided);
    CHECK_FALSE(diffuse.unfogged);
    REQUIRE(diffuse.shader.has_value());
    CHECK(diffuse.shader->name == "Diffuse");
    REQUIRE(diffuse.layers.size() == 1);
    CHECK(diffuse.layers[0].texture.state == canon::TextureRef::State::Resolved);

    const canon::Material& twoLayer = model.materials[1];
    CHECK_FALSE(twoLayer.framebufferBlend.has_value());
    CHECK(twoLayer.shader->name == "TwoLayerDiffuse");
    REQUIRE(twoLayer.layers.size() == 2);
    CHECK(twoLayer.layers[1].texture.unresolvedReason == "texture FileDataID 103 was not resolved");
    REQUIRE(twoLayer.layers[0].uvScroll.has_value());
    CHECK(twoLayer.layers[0].uvScroll->x == 0.5f);
    CHECK(twoLayer.layers[1].uvScroll->y == 0.25f);
    CHECK(model.mesh.primitives.empty());  // both group files missing
}

TEST_CASE("buildCanonWmo: doodad sets become owned placement sets, set 0 always on") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    in.assetNames.emplace(7001, "barrel01");

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.placementSets.size() == 2);
    CHECK(model.placementSets[0].alwaysOn);
    CHECK_FALSE(model.placementSets[1].alwaysOn);
    CHECK(model.placementSets[1].set.ref.name == "Set_B");
    const auto& instances = model.placementSets[1].set.instances;
    REQUIRE(instances.size() == 2);
    CHECK(instances[0].id == 1);
    CHECK(std::get<canon::FileDataId>(instances[0].asset.id).value == 7001);
    CHECK(instances[0].asset.name == "barrel01");
    CHECK(instances[0].translation.x == 1.0f);
    REQUIRE(instances[0].tint.has_value());
    CHECK(instances[0].tint->rgba == std::array<uint8_t, 4>{0x10, 0x20, 0x30, 0x80});
    CHECK(instances[0].tint->multiplier == 2.0f);
    CHECK(std::holds_alternative<canon::None>(instances[1].asset.id));  // MODI slot 1 is empty
}

TEST_CASE("buildCanonWmo: MNLD lights become local lights that switch with their doodad set") {
    wmo::RootFile root = syntheticRoot();
    wmo::NewLight spot;
    spot.type = 1;
    spot.flags = 0x1 | 0x2;
    spot.doodadSet = 1;
    spot.innerColor = 0xFFFF8000u;  // BGRA bytes 00 80 FF FF -> RGB 255 128 0
    spot.outerColor = 0xFF0000FFu;
    spot.position = {1, 2, 3};
    spot.rotation = {-3.0f, 0, 0};
    spot.blendStart = 1;
    spot.blendEnd = 5;
    spot.flickerMode = 2;
    spot.flickerSpeed = 3;
    spot.cookieFileDataId = 9001;
    spot.innerAngle = 0.5f;
    spot.outerAngle = 1.5f;
    spot.scale = 0.5f;
    spot.intensityMultiplier = 0;
    wmo::NewLight point;
    point.innerColor = 0xFFFFFFFFu;
    point.intensityMultiplier = 2;
    root.newLights = {spot, point};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    in.assetNames.emplace(9001, "cookie_window");

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.scene.localLights.size() == 2);
    const canon::LocalLight& s0 = model.scene.localLights[0];
    CHECK(s0.ref.name == "light_0");
    CHECK(s0.origin.table == "MNLD");
    CHECK(s0.origin.index == 0);
    CHECK(s0.kind == canon::LocalLight::Kind::Spot);
    CHECK(s0.position.z == 3.0f);
    CHECK_FALSE(s0.rotation.has_value());
    CHECK(s0.eulerRotation->x == -3.0f);
    CHECK(s0.color.x == 1.0f);
    CHECK(s0.color.y == doctest::Approx(128 / 255.0));
    CHECK(s0.color.z == 0.0f);
    CHECK(s0.intensityMultiplier == 1.0f);  // a stored 0 reads as 1
    REQUIRE(s0.gradient.has_value());
    CHECK(s0.gradient->outerColor.z == 1.0f);
    CHECK(s0.gradient->blendEnd == 5.0f);
    REQUIRE(s0.cone.has_value());
    CHECK(s0.cone->outerAngle == 1.5f);
    REQUIRE(s0.flicker.has_value());
    CHECK(s0.flicker->mode == 2);
    CHECK(s0.castsShadows);
    CHECK(s0.shadowScale == 0.5f);
    CHECK(s0.cookie->name == "cookie_window");
    CHECK(std::get<canon::RecordIndex>(s0.placementSet->id).value == 1);
    CHECK(s0.placementSet->name == "Set_B");

    const canon::LocalLight& p1 = model.scene.localLights[1];
    CHECK(p1.kind == canon::LocalLight::Kind::Point);
    CHECK(p1.intensityMultiplier == 2.0f);
    CHECK_FALSE(p1.gradient.has_value());
    CHECK_FALSE(p1.cone.has_value());
    CHECK_FALSE(p1.flicker.has_value());
    CHECK_FALSE(p1.castsShadows);
    CHECK_FALSE(p1.cookie.has_value());
    CHECK(std::get<canon::RecordIndex>(p1.placementSet->id).value == 0);
}

TEST_CASE("buildCanonWmo: MOLT lights follow MNLD, always on, rotation only for spot and directional") {
    wmo::RootFile root = syntheticRoot();
    root.newLights.resize(1);
    wmo::LegacyLight omni;
    omni.useAttenuation = 1;
    omni.rotation = {0, 0, 0, 1};
    wmo::LegacyLight direct;
    direct.type = 2;
    direct.unknown = {1, 1};
    direct.rotation = {0, 0, 1, 0};
    root.legacyLights = {omni, direct};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.scene.localLights.size() == 3);
    const canon::LocalLight& a = model.scene.localLights[1];
    // Numbered after the MNLD lights; the source record stays in origin.
    CHECK(a.ref.name == "light_1");
    CHECK(std::get<canon::RecordIndex>(a.ref.id).value == 1);
    CHECK(a.origin.table == "MOLT");
    CHECK(a.origin.index == 0);
    CHECK(a.kind == canon::LocalLight::Kind::Point);
    CHECK(a.attenuated);
    CHECK_FALSE(a.rotation.has_value());
    CHECK_FALSE(a.placementSet.has_value());
    const canon::LocalLight& b = model.scene.localLights[2];
    CHECK(b.kind == canon::LocalLight::Kind::Directional);
    CHECK_FALSE(b.attenuated);
    CHECK(b.rotation->z == 1.0f);
    CHECK(b.sourceFlags == 0x0101);
}

namespace {

wmo::Liquid liquid2x2(uint16_t material) {
    wmo::Liquid l;
    l.vertsX = 3;
    l.vertsY = 2;
    l.corner = {10, 20, -5};
    l.material = material;
    l.heights = {0, 1, 2, 3, 4, 5};
    l.vertexData.assign(6, {});
    l.tiles = {0x44, 0x0F};
    return l;
}

// The LiquidType row a group with `groupLiquid`/`groupFlags` resolves to
// under root flags `rootFlags`; 0 when unresolved.
uint32_t resolvedType(uint16_t rootFlags, uint32_t groupLiquid, uint32_t groupFlags = 0) {
    wmo::RootFile root = syntheticRoot();
    root.header.flags = rootFlags;
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.liquidType = groupLiquid;
    g.flags = groupFlags;
    g.liquid = liquid2x2(1);
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {g, std::nullopt};
    canon::Model model = wmoinput::buildCanonWmo(in);
    REQUIRE(model.liquids.size() == 1);
    const auto* row = std::get_if<canon::Db2Row>(&model.liquids[0].liquidType.id);
    CHECK(model.liquids[0].sourceLiquidType == groupLiquid);
    return row ? row->row : 0;
}

}  // namespace

TEST_CASE("buildCanonWmo: a group's MLIQ becomes a liquid grid on that part") {
    wmo::RootFile root = syntheticRoot();
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.liquid = liquid2x2(1);
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, g};

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.liquids.size() == 1);
    const canon::LiquidGrid& l = model.liquids[0];
    CHECK(l.part == 1);
    CHECK(std::get<canon::RecordIndex>(l.material).value == 1);
    CHECK(l.corner.y == 20.0f);
    CHECK(l.tileSize == doctest::Approx(25.0 / 6.0));
    CHECK(l.vertsX == 3);
    CHECK(l.heights.size() == 6);
    CHECK(l.tileFlags == std::vector<uint8_t>{0x44, 0x0F});
    CHECK(l.tileExists == std::vector<uint8_t>{1, 0});
}

TEST_CASE("buildCanonWmo: each liquid type the groups name is listed once, named from the liquid DB2s") {
    wmo::RootFile root = syntheticRoot();
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.liquid = liquid2x2(1);
    husk::liquiddb::Data liquids;
    liquids.types[13] = {"WMO Water", 0x1, 1, {}};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {g, g};
    in.liquids = &liquids;
    REQUIRE(wmoinput::liquidTypeRows(in) == std::vector<uint32_t>{13});

    canon::Model model = wmoinput::buildCanonWmo(in);
    REQUIRE(model.liquidTypes.size() == 1);
    CHECK(model.liquidTypes[0].ref.name == "WMO Water");
    CHECK(model.liquidTypes[0].flags == 0x1u);
    REQUIRE(model.liquids.size() == 2);
    CHECK(model.liquids[1].liquidType.name == "WMO Water");
}

TEST_CASE("buildCanonWmo: liquid type follows WMO.md's rules, with and without the DBC-id root flag") {
    // With MOHD flag 0x4: basic types go through to_wmo_liquid(groupLiquid - 1).
    CHECK(resolvedType(0x4, 1) == 13);            // water
    CHECK(resolvedType(0x4, 1, 0x80000) == 14);   // water in an "ocean" group
    CHECK(resolvedType(0x4, 2) == 14);            // ocean
    CHECK(resolvedType(0x4, 15) == 19);           // basic 14 & 3 = magma
    CHECK(resolvedType(0x4, 4) == 20);            // slime
    CHECK(resolvedType(0x4, 1206) == 1206);       // non-basic: the LiquidType row itself
    CHECK(resolvedType(0x4, 0) == 0);             // to_wmo_liquid(-1): undecided
    // Without it: to_wmo_liquid(groupLiquid), non-basic + 1, green lava undecided.
    CHECK(resolvedType(0, 0) == 13);
    CHECK(resolvedType(0, 2) == 19);
    CHECK(resolvedType(0, 40) == 41);
    CHECK(resolvedType(0, 15) == 0);
}

TEST_CASE("buildCanonWmo: MPVD records become particulate effect instances with resolved emitter textures") {
    wmo::RootFile root = syntheticRoot();
    wmo::ParticulateVolume v;
    v.flags = 21;
    v.position = {1, 2, 3};
    v.extent = {7, 8, 9};
    v.rotation = {0, 0, 0.5f};
    v.presetId = 16386;
    v.particleCount = 500;
    v.envelope = {{0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 4.0f}};
    v.unknownRanges = {{0x270, {0, 7}}};
    wmo::ParticulateEmitter dust;
    dust.slot = 0;
    dust.textureFileDataId = 101;
    dust.blendMode = 3;
    dust.color = {1, 0.5f, 0, 1};
    dust.tilesX = 4;
    dust.frames = 16;
    wmo::ParticulateEmitter odd = dust;
    odd.slot = 1;
    odd.textureFileDataId = 555;
    odd.blendMode = 7;
    v.emitters = {dust, odd};
    v.record.assign(4272, 0);
    root.particulateVolumes = {v};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    resolved.resolved = canon::Ref{canon::FileDataId{101}, "dust1", canon::NameSource::Listfile};
    in.textures.emplace(101, resolved);

    canon::Model model = wmoinput::buildCanonWmo(in);

    // Set 0: doodad 0, then the volume.
    REQUIRE(model.placementSets[0].set.instances.size() == 2);
    const canon::PlacedInstance& out = model.placementSets[0].set.instances[1];
    CHECK(out.asset.name == "classic_particulate_volume");
    CHECK(out.translation.z == 3.0f);
    CHECK(out.flags == 21);
    const canon::EffectPlacement& effect = *out.effect;
    CHECK(effect.origin.table == "MPVD");
    CHECK(std::get<canon::Vec3>(effect.inputs.at(0).value.value).z == 9.0f);
    const auto& emitters = std::get<std::vector<canon::InputValue>>(effect.inputs.at(1).value.value);
    REQUIRE(emitters.size() == 2);
    const auto& dustInput = std::get<std::vector<canon::NamedInput>>(emitters[0].value);
    CHECK(std::get<canon::TextureRef>(dustInput[0].value.value).resolved.name == "dust1");
    CHECK(std::get<int64_t>(dustInput[3].value.value) == 16);
    const auto& oddInput = std::get<std::vector<canon::NamedInput>>(emitters[1].value);
    CHECK(std::get<canon::TextureRef>(oddInput[0].value.value).unresolvedReason == "texture FileDataID 555 was not resolved");

    REQUIRE(effect.style.size() == 2);
    CHECK(effect.style[0].blocks.at(0).maxParticles == 500u);
    CHECK(effect.style[1].perElement);
    REQUIRE(effect.style[1].blocks.size() == 2);
    CHECK(effect.style[1].blocks[0].draw->blend == canon::FramebufferBlend::Add);
    CHECK(effect.style[1].blocks[0].draw->color[1] == 0.5f);
    CHECK_FALSE(effect.style[1].blocks[1].draw->blend.has_value());

    auto field = [&](const std::string& name) -> const canon::UnresolvedField* {
        auto it = std::find_if(effect.unresolved.begin(), effect.unresolved.end(),
                               [&](const canon::UnresolvedField& f) { return f.field == name; });
        return it == effect.unresolved.end() ? nullptr : &*it;
    };
    REQUIRE(field("emitters[1].blend"));
    CHECK(std::get<double>(field("emitters[1].blend")->value) == 7);
    REQUIRE(field("rotation"));
    CHECK(field("rotation")->kind == canon::UnresolvedField::Kind::Angle);
    CHECK(std::get<double>(field("preset")->value) == 16386);
    REQUIRE(field("envelope (time, value, value2)"));
    CHECK(std::get<std::vector<double>>(field("envelope (time, value, value2)")->value).size() == 6);
    REQUIRE(field("record+0x270"));
    CHECK(std::get<std::vector<uint32_t>>(field("record+0x270")->value)[1] == 7);
    CHECK_FALSE(field("record+0x4"));  // zero fields aren't carried
}

TEST_CASE("buildCanonWmo: a group's MAVR/MBVR/MPVR name the scene volumes its part uses; a ref past its table fails") {
    wmo::RootFile root = syntheticRoot();
    root.globalAmbientVolumes = {wmo::AmbientVolume{}};
    root.ambientVolumes = {wmo::AmbientVolume{}, wmo::AmbientVolume{}};
    root.ambientBoxes = {wmo::AmbientBox{}};
    root.particulateVolumes = {wmo::ParticulateVolume{}};
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.ambientVolumeRefs = {1};
    g.ambientBoxRefs = {0};
    g.particulateVolumeRefs = {0};
    g.fogRefs = {0};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, g};

    canon::Model model = wmoinput::buildCanonWmo(in);
    // Instances in set 0, ids after the 3 doodads: MAVG 3, MAVD 4-5, MBVD 6.
    REQUIRE(model.mesh.parts[1].ambientVolumes.size() == 2);
    CHECK(model.mesh.parts[1].ambientVolumes[0].id == 5);
    CHECK(model.mesh.parts[1].ambientVolumes[1].id == 6);
    // The MPVD volume follows them, id 7.
    REQUIRE(model.mesh.parts[1].particulateVolumes.size() == 1);
    CHECK(model.mesh.parts[1].particulateVolumes[0].id == 7);
    // MFVR replaces the four fogIds slots; 0 is the default fog, here the
    // empty entry, so no list entry.
    CHECK(model.mesh.parts[1].fogs.empty());
    CHECK(model.mesh.parts[0].ambientVolumes.empty());

    g.ambientBoxRefs = {1};
    in.groups = {std::nullopt, g};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 1's MBVR: expected refs < 1 root records, got 1",
                         std::runtime_error);
}

TEST_CASE("buildCanonWmo: MAVG, MAVD and MBVD become global, point and box ambient instances in their sets") {
    wmo::RootFile root = syntheticRoot();
    wmo::AmbientVolume global;
    global.colors = {0xFF336699u, 0, 0};
    wmo::AmbientVolume point;
    point.position = {1, 2, 3};
    point.start = 4;
    point.end = 8;
    point.doodadSet = 1;
    point.flags = 1;
    wmo::AmbientBox box;
    box.planes = {{{-1, 0, 0, 2}, {1, 0, 0, 3}, {0, -1, 0, 4}, {0, 1, 0, 5}, {0, 0, -1, 6}, {0, 0, 1, 7}}};
    box.end = 1.5f;
    root.globalAmbientVolumes = {global};
    root.ambientVolumes = {point};
    root.ambientBoxes = {box};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};

    canon::Model model = wmoinput::buildCanonWmo(in);

    // Set 0 holds doodad 0, then the global and the box; Set_B the point.
    const auto& set0 = model.placementSets[0].set.instances;
    REQUIRE(set0.size() == 3);
    const canon::PlacedInstance& g = set0[1];
    CHECK(g.asset.name == "classic_ambient_global");
    CHECK(g.effect->origin.table == "MAVG");
    const auto& colors = *g.effect->style.at(0).blocks.at(0).ambientColors;
    // BGRA bytes 99 66 33 FF -> RGBA 0x33 0x66 0x99 0xFF.
    CHECK(colors[0][0] == doctest::Approx(0x33 / 255.0));
    CHECK(colors[0][2] == doctest::Approx(0x99 / 255.0));
    CHECK(colors[0][3] == 1.0f);
    CHECK(g.effect->unresolved.empty());

    const canon::PlacedInstance& b = set0[2];
    CHECK(b.asset.name == "classic_ambient_box");
    CHECK(b.effect->origin.table == "MBVD");
    const auto& planes = std::get<std::vector<canon::InputValue>>(b.effect->inputs.at(0).value.value);
    REQUIRE(planes.size() == 6);
    const auto& face = std::get<std::vector<canon::NamedInput>>(planes[1].value);
    CHECK(std::get<canon::Vec3>(face[0].value.value).x == 1.0f);
    CHECK(std::get<double>(face[1].value.value) == 3.0);
    // The fade distance is a candidate, not a typed edge.
    REQUIRE(b.effect->unresolved.size() == 1);
    CHECK(b.effect->unresolved[0].field == "end");
    CHECK(b.effect->unresolved[0].candidateRole == "region.edge");

    const auto& setB = model.placementSets[1].set.instances;
    REQUIRE(setB.size() == 3);
    const canon::PlacedInstance& p = setB[2];
    CHECK(p.asset.name == "classic_ambient_point");
    CHECK(p.effect->origin.index == 0);
    CHECK(p.translation.z == 3.0f);
    CHECK(p.effect->inputs.at(1).name == "end");
    CHECK(std::get<double>(p.effect->inputs.at(1).value.value) == 8.0);
    CHECK(p.flags == 1);
    CHECK(std::get<uint32_t>(p.effect->unresolved.at(0).value) == 1);
}

TEST_CASE("buildCanonWmo: inconsistent inputs throw with expected and actual") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO groups: expected one entry per MOGI group (2), got 1",
                         std::runtime_error);

    in.groups = {group(3, 1, 5, 1), std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 0 batch 0: expected a material < 2, got 5",
                         std::runtime_error);

    root.doodads[0].nameIndex = 9;
    in.groups = {std::nullopt, std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "MODD doodad 0: expected a MODI index < 2, got 9",
                         std::runtime_error);

    root.doodads[0].nameIndex = 0;
    root.newLights.resize(1);
    root.newLights[0].type = 4;
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "MNLD[0].type: expected 0 (point) or 1 (spot), got 4",
                         std::runtime_error);
    root.newLights.clear();
    root.legacyLights.resize(1);
    root.legacyLights[0].type = 7;
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "MOLT[0].type: expected 0..3, got 7", std::runtime_error);

    root.legacyLights.clear();
    wmo::GroupFile wet = group(3, 1, 0, 1);
    wet.liquid = liquid2x2(9);
    in.groups = {wet, std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "liquid 0's material: expected an index < 2 materials, got 9",
                         std::runtime_error);

    wmo::GroupFile foggy = group(3, 1, 0, 1);
    foggy.fogIds = {0, 3, 0, 0};
    in.groups = {std::nullopt, foggy};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 1's fogIds: expected refs < 1 MFOG records, got 3",
                         std::runtime_error);
}

TEST_CASE("buildCanonWmo: every face's MOPY/MPY2 flags and material are face channels, groups in order") {
    wmo::RootFile root = syntheticRoot();
    wmo::GroupFile first = group(3, 1, 0, 2);
    first.triangles = {{0x20, 0}, {0x24, 1}};
    wmo::GroupFile second = group(3, 1, 0, 1);
    second.triangles = {{0x08, 0xFF}};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {first, second};

    canon::Model model = wmoinput::buildCanonWmo(in);

    CHECK(model.collision.indices.empty());
    REQUIRE(model.mesh.attributes.size() == 2);
    const canon::AttributeChannel& flags = model.mesh.attributes[0];
    CHECK(flags.name == "wmo.poly_flags");
    CHECK(flags.domain == canon::AttributeDomain::Face);
    CHECK(std::get<std::vector<uint16_t>>(flags.values) == std::vector<uint16_t>{0x20, 0x24, 0x08});
    CHECK(model.mesh.attributes[1].name == "wmo.poly_material");
    CHECK(std::get<std::vector<uint16_t>>(model.mesh.attributes[1].values) == std::vector<uint16_t>{0, 1, 0xFF});

    second.triangles.clear();
    in.groups = {first, second};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 1: expected one MOPY/MPY2 entry per triangle (1), got 0",
                         std::runtime_error);
}

TEST_CASE("buildCanonWmo: MOPT portals go on the mesh, each group's MOPR range on its part") {
    wmo::RootFile root = syntheticRoot();
    root.portalVertices = {{9, 9, 9}, {0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}};
    root.portals = {{1, 4, {1, 0, 0}, -2.5f}};
    root.portalRefs = {{0, 1, 1}, {0, 0, -1}};
    root.portalExtras = {{}};
    root.portalExtras[0][0] = 0x41;
    wmo::GroupFile hall = group(3, 1, 0, 1);
    hall.portalStart = 0;
    hall.portalCount = 1;
    wmo::GroupFile room = group(3, 1, 0, 1);
    room.portalStart = 1;
    room.portalCount = 1;
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {hall, room};

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.mesh.portals.size() == 1);
    const canon::Portal& p = model.mesh.portals[0];
    REQUIRE(p.vertices.size() == 4);
    CHECK(p.vertices[0].x == 0.0f);
    CHECK(p.vertices[2].z == 1.0f);
    CHECK(p.normal.x == 1.0f);
    CHECK(p.distance == -2.5f);
    REQUIRE(p.sourceExtra.size() == 16);
    CHECK(p.sourceExtra[0] == 0x41);
    REQUIRE(model.mesh.parts[0].portals.size() == 1);
    CHECK(model.mesh.parts[0].portals[0].toPart == 1);
    CHECK(model.mesh.parts[0].portals[0].side == 1);
    REQUIRE(model.mesh.parts[1].portals.size() == 1);
    CHECK(model.mesh.parts[1].portals[0].toPart == 0);
    CHECK(model.mesh.parts[1].portals[0].side == -1);

    room.portalStart = 0xFFFF;
    room.portalCount = 0;
    in.groups = {hall, room};
    CHECK(wmoinput::buildCanonWmo(in).mesh.parts[1].portals.empty());
    room.portalStart = 1;
    room.portalCount = 2;
    in.groups = {hall, room};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 1: expected MOPR refs [1, 3) within 2",
                         std::runtime_error);
}

namespace {

const canon::PlacedInstance* instanceById(const canon::Model& model, canon::InstanceRef ref) {
    const auto& instances = model.placementSets.at(ref.set).set.instances;
    auto it = std::find_if(instances.begin(), instances.end(), [&](const canon::PlacedInstance& i) { return i.id == ref.id; });
    return it == instances.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("buildCanonWmo: MFOG fogs become effect instances in the set they apply with; parts name theirs") {
    wmo::RootFile root = syntheticRoot();
    wmo::Fog exitFog;
    exitFog.flags = 0x1001;
    exitFog.position = {1, 2, 3};
    exitFog.smallerRadius = 5;
    exitFog.largerRadius = 20;
    exitFog.fog = {100, 0.25f, 0xFF336699u};
    exitFog.underwater = {50, -0.5f, 0xFF0000FFu};
    exitFog.doodadSet = 1;
    wmo::Fog sphere;
    sphere.position = {4, 5, 6};
    sphere.smallerRadius = 2;
    sphere.largerRadius = 8;
    sphere.fog = {40, 0.5f, 0x7F336699u};
    sphere.underwater = {10, -1, 0xFF000000u};
    root.fogs = {emptyFog(), exitFog, sphere};
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.fogIds = {2, 1, 0, 0};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {g, std::nullopt};

    canon::Model model = wmoinput::buildCanonWmo(in);

    // The empty entry is no instance and no default fog; ids follow the 3 doodads.
    CHECK_FALSE(model.defaultFog.has_value());
    REQUIRE(model.mesh.parts[0].fogs.size() == 2);
    CHECK(model.mesh.parts[0].fogs[0].set == 0);
    CHECK(model.mesh.parts[0].fogs[0].id == 4);
    CHECK(model.mesh.parts[0].fogs[1].set == 1);
    CHECK(model.mesh.parts[0].fogs[1].id == 3);
    CHECK(model.mesh.parts[1].fogs.empty());

    const canon::PlacedInstance* exit = instanceById(model, model.mesh.parts[0].fogs[1]);
    REQUIRE(exit);
    CHECK(exit->asset.name == "classic_fog_exit_blend");
    CHECK(exit->translation.z == 3.0f);
    CHECK(exit->flags == 0x1001);
    REQUIRE(exit->effect.has_value());
    CHECK(exit->effect->origin.table == "MFOG");
    CHECK(exit->effect->origin.index == 1);
    CHECK(exit->effect->inputs.empty());
    REQUIRE(exit->effect->style.size() == 1);
    CHECK(exit->effect->style[0].source == "exit_blend");
    CHECK(exit->effect->style[0].blocks.at(0).exitBlend);
    REQUIRE(exit->effect->unresolved.size() == 3);
    CHECK(exit->effect->unresolved[0].field == "smaller_radius");
    CHECK(exit->effect->unresolved[2].field == "flags");
    CHECK(std::get<uint32_t>(exit->effect->unresolved[2].value) == 0x1000);

    const canon::PlacedInstance* local = instanceById(model, model.mesh.parts[0].fogs[0]);
    REQUIRE(local);
    CHECK(local->asset.name == "classic_local_fog");
    REQUIRE(local->effect->inputs.size() == 2);
    CHECK(local->effect->inputs[0].name == "inner_radius");
    CHECK(std::get<double>(local->effect->inputs[1].value.value) == 8.0);
    const canon::StyleBlock& style = local->effect->style.at(0).blocks.at(0);
    // start = end * startScalar; BGRA bytes 99 66 33 7F -> RGB 0x33 0x66 0x99.
    CHECK(style.distanceFog->linear->start == 20.0f);
    CHECK(style.distanceFog->linear->end == 40.0f);
    CHECK(style.distanceFog->linear->color.x == doctest::Approx(0x33 / 255.0));
    CHECK(style.underwater->linear->start == -10.0f);
    // A colour alpha other than 0xFF is carried, not dropped.
    REQUIRE(local->effect->unresolved.size() == 1);
    CHECK(local->effect->unresolved[0].field == "fog.color.alpha");
    CHECK(std::get<double>(local->effect->unresolved[0].value) == 0x7F);
}

TEST_CASE("buildCanonWmo: a real MFOG record 0 is the model's default fog; a model without sets gets an effects set") {
    wmo::RootFile root = syntheticRoot();
    root.doodadSets.clear();
    root.fogs[0].smallerRadius = 3;
    root.fogs[0].largerRadius = 9;
    wmo::GroupFile g = group(3, 1, 0, 1);
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {g, std::nullopt};

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.defaultFog.has_value());
    REQUIRE(model.placementSets.size() == 1);
    CHECK(model.placementSets[0].set.ref.name == "effects");
    CHECK(model.placementSets[0].alwaysOn);
    CHECK(instanceById(model, *model.defaultFog)->asset.name == "classic_local_fog");
    // A fogIds slot of 0 names the default fog, not a list entry.
    CHECK(model.mesh.parts[0].fogs.empty());
}

TEST_CASE("buildCanonWmo: real guardtower -- every batch in range, every set and doodad carried" *
          doctest::skip(test::testWmoGuardTower().empty())) {
    std::filesystem::path rootPath = test::testWmoGuardTower();
    wmo::RootFile root = wmo::parseRoot(readFile(rootPath.string()));
    wmoinput::WmoInputs in;
    in.root = &root;
    size_t batches = 0;
    for (int g = 0; g < 2; ++g) {
        auto path = rootPath.parent_path() / ("guardtower_00" + std::to_string(g) + ".wmo");
        in.groups.emplace_back(wmo::parseGroup(readFile(path.string())));
        batches += in.groups.back()->batches.size();
    }

    canon::Model model = wmoinput::buildCanonWmo(in);

    CHECK(model.mesh.primitives.size() == batches);
    CHECK(model.materials.size() == 20);
    REQUIRE(model.placementSets.size() == 3);
    size_t instances = 0;
    for (const auto& owned : model.placementSets) instances += owned.set.instances.size();
    CHECK(instances == 4 + 58 + 108);
    CHECK(model.mesh.colorSets.size() == 1);
}
