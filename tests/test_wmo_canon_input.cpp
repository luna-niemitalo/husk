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
    root.fogs.resize(1);  // every real root has an MFOG for its groups' fog id 0
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
    CHECK(s0.ref.name == "mnld_0");
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
    CHECK(a.ref.name == "molt_0");
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

TEST_CASE("buildCanonWmo: MPVD records become particulate volumes with resolved emitter textures") {
    wmo::RootFile root = syntheticRoot();
    wmo::ParticulateVolume v;
    v.flags = 21;
    v.extent = {7, 8, 9};
    v.presetId = 16386;
    v.envelope = {{0.5f, 1.0f, 0.0f}, {1.0f, 1.0f, 4.0f}};
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

    REQUIRE(model.scene.particulateVolumes.size() == 1);
    const canon::ParticulateVolume& out = model.scene.particulateVolumes[0];
    CHECK(out.ref.name == "mpvd_0");
    CHECK(out.extent.z == 9.0f);
    CHECK(out.presetId == 16386);
    CHECK(out.sourceFlags == 21);
    REQUIRE(out.envelope.size() == 2);
    CHECK(out.envelope[1].time == 4.0f);
    CHECK(out.envelope[1].value == 1.0f);
    CHECK(out.envelope[0].value == 0.5f);
    REQUIRE(out.emitters.size() == 2);
    CHECK(out.emitters[0].texture.resolved.name == "dust1");
    CHECK(out.emitters[0].blend == canon::FramebufferBlend::Add);
    CHECK(out.emitters[0].color[1] == 0.5f);
    CHECK(out.emitters[0].frames == 16);
    CHECK(out.emitters[1].slot == 1);
    CHECK(out.emitters[1].texture.unresolvedReason == "texture FileDataID 555 was not resolved");
    CHECK_FALSE(out.emitters[1].blend.has_value());
    CHECK(out.emitters[1].sourceBlend == 7);
    CHECK(out.sourceRecord.size() == 4272);
}

TEST_CASE("buildCanonWmo: MAVG, MAVD and MBVD become global, point and box ambient volumes") {
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

    REQUIRE(model.scene.ambientVolumes.size() == 3);
    const canon::AmbientVolume& g = model.scene.ambientVolumes[0];
    CHECK(g.ref.name == "mavg_0");
    CHECK(g.shape == canon::AmbientVolume::Shape::Global);
    // BGRA bytes 99 66 33 FF -> RGBA 0x33 0x66 0x99 0xFF.
    CHECK(g.colors[0][0] == doctest::Approx(0x33 / 255.0));
    CHECK(g.colors[0][2] == doctest::Approx(0x99 / 255.0));
    CHECK(g.colors[0][3] == 1.0f);
    CHECK(g.placementSet->name == "Set_$DefaultGlobal");
    const canon::AmbientVolume& p = model.scene.ambientVolumes[1];
    CHECK(p.ref.name == "mavd_0");
    CHECK(p.shape == canon::AmbientVolume::Shape::Point);
    CHECK(p.position.z == 3.0f);
    CHECK(p.end == 8.0f);
    CHECK(p.sourceFlags == 1);
    CHECK(p.placementSet->name == "Set_B");
    const canon::AmbientVolume& b = model.scene.ambientVolumes[2];
    CHECK(b.ref.name == "mbvd_0");
    CHECK(b.shape == canon::AmbientVolume::Shape::Box);
    REQUIRE(b.planes.size() == 6);
    CHECK(b.planes[1].normal.x == 1.0f);
    CHECK(b.planes[1].distance == 3.0f);
    CHECK(b.end == 1.5f);
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
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "part 1's fogs: expected indices < 1 scene fogs, got 3",
                         std::runtime_error);
}

TEST_CASE("buildCanonWmo: collision keeps WMO.md's collidable faces, with only the vertices they use") {
    wmo::RootFile root = syntheticRoot();
    wmo::GroupFile g = group(6, 1, 0, 4);
    g.indices = {0, 1, 2, 3, 4, 5, 1, 2, 3, 5, 4, 0};
    // Render face; detail face (no collision); collision-only face;
    // detail face also flagged collision.
    g.triangles = {{0x20, 0}, {0x24, 0}, {0x08, 0xFF}, {0x0C, 0}};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, g};

    canon::Model model = wmoinput::buildCanonWmo(in);

    const canon::CollisionMesh& c = model.collision;
    CHECK(c.triangleSourceFlags == std::vector<uint32_t>{0x20, 0x08, 0x0C});
    CHECK(c.triangleParts == std::vector<uint32_t>{1, 1, 1});
    // Group vertices 0,1,2 then 3 and 5 then 4: first use order.
    CHECK(c.indices == std::vector<uint32_t>{0, 1, 2, 1, 2, 3, 4, 5, 0});
    REQUIRE(c.positions.size() == 6);
    CHECK(c.positions[3].x == 3.0f);
    CHECK(c.positions[4].x == 5.0f);
    CHECK(c.triangleNormals.empty());
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

TEST_CASE("buildCanonWmo: MFOG fogs become local fogs; parts name theirs, MFED picks the doodad set") {
    wmo::RootFile root = syntheticRoot();
    wmo::Fog exitFog;
    exitFog.flags = 0x1001;
    exitFog.position = {1, 2, 3};
    exitFog.smallerRadius = 5;
    exitFog.largerRadius = 20;
    exitFog.fog = {100, 0.25f, 0xFF336699u};
    exitFog.underwater = {50, -0.5f, 0xFF0000FFu};
    exitFog.doodadSet = 1;
    root.fogs = {wmo::Fog{}, exitFog};
    wmo::GroupFile g = group(3, 1, 0, 1);
    g.fogIds = {1, 0, 0, 0};
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {g, std::nullopt};

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.scene.fogs.size() == 2);
    CHECK_FALSE(model.scene.fogs[0].placementSet.has_value());
    const canon::LocalFog& f = model.scene.fogs[1];
    CHECK(f.ref.name == "mfog_1");
    CHECK(f.position.z == 3.0f);
    CHECK(f.innerRadius == 5.0f);
    CHECK(f.outerRadius == 20.0f);
    CHECK(f.interiorExteriorBlend);
    CHECK(f.sourceFlags == 0x1001);
    CHECK(f.fog.end == 100.0f);
    CHECK(f.fog.startScalar == 0.25f);
    // BGRA bytes 99 66 33 FF -> RGBA 0x33 0x66 0x99 0xFF.
    CHECK(f.fog.color[0] == doctest::Approx(0x33 / 255.0));
    CHECK(f.underwater.startScalar == -0.5f);
    CHECK(f.underwater.color[2] == 1.0f);
    CHECK(f.placementSet->name == "Set_B");
    CHECK(model.mesh.parts[0].fogs == std::vector<uint32_t>{1, 0, 0, 0});
    CHECK(model.mesh.parts[1].fogs.empty());
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
