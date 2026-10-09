// husk::adtinput -- ADT -> canon::Terrain, against synthetic tiles
// (tests/test_adt_fixtures.hpp). Every convention this module owns (world
// frame, normal decode, hole masks, liquid height provenance, placement
// transforms, name-table resolution, alpha/layer assembly) gets its own case;
// the real-data evidence for each convention is WIKI_FINDINGS/WORLD.md.

#include <cmath>
#include <doctest/doctest.h>

#include "../src/adt.hpp"
#include "../src/adt_canon_input.hpp"
#include "test_adt_fixtures.hpp"

using namespace adtfx;
namespace adt = husk::adt;
namespace adtinput = husk::adtinput;
namespace canon = husk::canon;

namespace {

struct Inputs {
    adtinput::TextureResolutions textures;
    adtinput::GroundEffectDefinitions groundEffects;
    adtinput::GamePathResolutions gamePaths;
};

canon::Terrain build(const RootSpec& rootSpec, const std::optional<ObjSpec>& objSpec = std::nullopt,
                     const std::optional<TexSpec>& texSpec = std::nullopt, const Inputs& inputs = {},
                     std::optional<uint32_t> wdtFlags = 0x4) {
    adt::RootFile root = adt::parseRoot(rootFile(rootSpec));
    std::optional<adt::ObjFile> obj;
    if (objSpec) obj = adt::parseObj(objFile(*objSpec));
    std::optional<adt::TexFile> tex;
    if (texSpec) tex = adt::parseTex(texFile(*texSpec));
    adtinput::TileSources sources{root, obj, tex, wdtFlags, "azeroth", rootSpec.tileX, rootSpec.tileY};
    return adtinput::buildCanonTerrain(sources, {inputs.textures, inputs.groundEffects, inputs.gamePaths});
}

canon::Vec3 rotate(const canon::Quat& q, const canon::Vec3& v) {
    // v' = v + 2w (u x v) + 2 u x (u x v), u = (x, y, z)
    canon::Vec3 u{q.x, q.y, q.z};
    auto cross = [](const canon::Vec3& a, const canon::Vec3& b) {
        return canon::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    canon::Vec3 t = cross(u, v);
    t = {2 * t.x, 2 * t.y, 2 * t.z};
    canon::Vec3 ut = cross(u, t);
    return {v.x + q.w * t.x + ut.x, v.y + q.w * t.y + ut.y, v.z + q.w * t.z + ut.z};
}

float dot(const canon::Vec3& a, const canon::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float degrees(float radians) { return radians * 180.0f / 3.14159265f; }

canon::Placement onlyPlacement(const ObjSpec& obj, const Inputs& inputs = {}) {
    canon::Terrain t = build(RootSpec{}, obj, std::nullopt, inputs);
    REQUIRE(t.placements.size() == 1);
    return t.placements[0];
}

canon::Placement placedAt(std::array<float, 3> rotation) {
    ObjSpec obj;
    obj.doodads.push_back({5, 1, {}, rotation, 1024, 0x40});
    return onlyPlacement(obj);
}

}  // namespace

TEST_CASE("adtinput: chunks land in the world frame with absolute heights and their grid index") {
    RootSpec spec;
    spec.chunk = [](uint32_t ix, uint32_t iy) {
        ChunkSpec c;
        c.baseZ = 40.0f + static_cast<float>(iy);
        c.height = [](int i) { return static_cast<float>(i) * 0.25f; };
        c.areaId = 9 + ix;
        return c;
    };
    canon::Terrain t = build(spec);

    CHECK(t.tileX == 32);
    CHECK(t.tileY == 48);
    CHECK(t.map.name == "azeroth");
    REQUIRE(t.chunks.size() == 256);
    const canon::TerrainChunk& c = t.chunks[16 * 2 + 7];
    CHECK(c.gridX == 7);
    CHECK(c.gridY == 2);
    CHECK(c.origin.x == doctest::Approx(chunkOrigin(32, 48, 7, 2)[0]));
    CHECK(c.origin.y == doctest::Approx(chunkOrigin(32, 48, 7, 2)[1]));
    CHECK(c.heights[0] == 42.0f);
    CHECK(c.heights[144] == 42.0f + 36.0f);
    auto area = std::get<canon::Db2Row>(c.area.id);
    CHECK(area.table == "AreaTable");
    CHECK(area.row == 16);
    // No _tex0: no layers, no ground-cover data.
    CHECK(c.layers.empty());
    CHECK(t.textures.empty());
}

TEST_CASE("adtinput: tile coordinates from the file name must match MCNK #0's world position") {
    RootSpec spec;
    spec.chunk = [](uint32_t, uint32_t) {
        ChunkSpec c;
        c.overridePosition = true;
        c.positionOverride = chunkOrigin(31, 48, 0, 0);
        return c;
    };
    CHECK_THROWS_WITH(build(spec), doctest::Contains("tile (32,48) from file name: expected MCNK #0 at world"));
}

TEST_CASE("adtinput: MCNR normals decode X, Y, Z and are normalized; a zero normal falls back to up") {
    RootSpec spec;
    spec.chunk = [](uint32_t ix, uint32_t) {
        ChunkSpec c;
        c.normal = ix == 0 ? std::array<int8_t, 3>{0, 0, 0} : std::array<int8_t, 3>{127, 0, 127};
        return c;
    };
    canon::Terrain t = build(spec);
    canon::Vec3 up = t.chunks[0].normals[0];
    CHECK(up.x == 0.0f);
    CHECK(up.y == 0.0f);
    CHECK(up.z == 1.0f);
    canon::Vec3 n = t.chunks[1].normals[5];
    CHECK(n.x == doctest::Approx(std::sqrt(0.5f)));
    CHECK(n.y == 0.0f);
    CHECK(n.z == doctest::Approx(std::sqrt(0.5f)));
}

TEST_CASE("adtinput: holes come from the 64-bit map under high_res_holes, else the 16-bit map expanded 2x2") {
    RootSpec spec;
    spec.chunk = [](uint32_t ix, uint32_t) {
        ChunkSpec c;
        c.holesHighRes = {0x81, 0, 0, 0, 0, 0, 0, 0x01};
        c.holesLowRes = (1 << 0) | (1 << 5);  // block (0,0) and block (1,1)
        if (ix == 1) c.flags = 0;
        return c;
    };
    canon::Terrain t = build(spec);
    CHECK(t.chunks[0].holeRows == std::array<uint8_t, 8>{0x81, 0, 0, 0, 0, 0, 0, 0x01});
    CHECK(t.chunks[1].holeRows == std::array<uint8_t, 8>{0x03, 0x03, 0x0C, 0x0C, 0, 0, 0, 0});
}

TEST_CASE("adtinput: liquid surfaces keep their rect and record where their heights came from") {
    std::vector<uint8_t> heightDepth;
    for (int i = 0; i < 4; ++i) putF32(heightDepth, 10.0f + static_cast<float>(i));
    for (int i = 0; i < 4; ++i) putU8(heightDepth, static_cast<uint8_t>(i * 85));
    std::vector<uint8_t> heightUv;
    for (int i = 0; i < 4; ++i) putF32(heightUv, 20.0f);
    for (int i = 0; i < 4; ++i) {
        putU16(heightUv, 16);
        putU16(heightUv, 8);
    }

    RootSpec spec;
    spec.liquids = {
        LiquidSpec{17, 5, 100, 0, 0, 3, 4, 1, 1, {}, heightDepth},
        LiquidSpec{18, 2, 2, 7.5f, 7.5f, 0, 0, 2, 2, {0x09}, {}},
        LiquidSpec{19, 2, 0, 0, 0, 0, 0, 1, 1, {}, std::vector<uint8_t>(4, 200)},
        LiquidSpec{20, 1, 0, 0, 0, 0, 0, 1, 1, {}, heightUv},
    };
    canon::Terrain t = build(spec);
    REQUIRE(t.liquids.size() == 4);

    const canon::LiquidSurface& river = t.liquids[0];
    CHECK(river.chunkGridX == 1);
    CHECK(river.chunkGridY == 1);
    CHECK(std::get<canon::Db2Row>(river.liquidType.id).row == 5);
    CHECK(std::get<canon::Db2Row>(river.liquidObject.id).row == 100);
    CHECK(river.quadX == 3);
    CHECK(river.quadY == 4);
    CHECK(river.heightSource == canon::LiquidHeightSource::Heightmap);
    CHECK(river.heights == std::vector<float>{10, 11, 12, 13});
    REQUIRE(river.depth.has_value());
    CHECK((*river.depth)[3] == doctest::Approx(1.0f));
    CHECK(river.quadExists == std::vector<uint8_t>{1});

    const canon::LiquidSurface& ocean = t.liquids[1];
    CHECK(ocean.heightSource == canon::LiquidHeightSource::MinHeightLevel);
    CHECK(ocean.heights == std::vector<float>(9, 7.5f));
    // A liquid_object_or_lvf below 42 is a vertex format number, not a LiquidObject row.
    CHECK(std::holds_alternative<canon::None>(ocean.liquidObject.id));
    // Exists bitmap is LSB-first, row-major over the rect: 0x09 = quads 0 and 3.
    CHECK(ocean.quadExists == std::vector<uint8_t>{1, 0, 0, 1});
    CHECK_FALSE(ocean.depth.has_value());

    const canon::LiquidSurface& depthOnly = t.liquids[2];
    CHECK(depthOnly.heightSource == canon::LiquidHeightSource::Zero);
    CHECK(depthOnly.heights == std::vector<float>(4, 0.0f));

    const canon::LiquidSurface& textured = t.liquids[3];
    REQUIRE(textured.uv.has_value());
    CHECK((*textured.uv)[0].x == 2.0f);
    CHECK((*textured.uv)[0].y == 1.0f);
}

TEST_CASE("adtinput: MDDF position maps (map-corner-relative, height, ...) onto world X north, Y west, Z up") {
    ObjSpec obj;
    obj.doodads.push_back({777, 42, {kMapHalfExtent - 100.0f, 50.0f, kMapHalfExtent - 200.0f}, {}, 2048, 0x40});
    canon::Placement p = onlyPlacement(obj);
    CHECK(p.kind == canon::Placement::Kind::Model);
    CHECK(std::get<canon::FileDataId>(p.asset.id).value == 777);
    CHECK(p.uniqueId == 42);
    CHECK(p.position.x == doctest::Approx(200.0f));
    CHECK(p.position.y == doctest::Approx(100.0f));
    CHECK(p.position.z == 50.0f);
    CHECK(p.scale == 2.0f);
    CHECK(p.flags == 0x40);
    CHECK_FALSE(p.doodadSet.has_value());
}

TEST_CASE("adtinput: an untilted placement keeps model up as world up, and yaw is a pure rotation about Z") {
    for (float yaw : {0.0f, 37.0f, 90.0f, 215.0f}) {
        CAPTURE(yaw);
        canon::Placement p = placedAt({0, yaw, 0});
        canon::Vec3 up = rotate(p.rotation, {0, 0, 1});
        CHECK(up.z == doctest::Approx(1.0f).epsilon(1e-5));
        CHECK(p.rotation.x == doctest::Approx(0.0f).epsilon(1e-5));
        CHECK(p.rotation.y == doctest::Approx(0.0f).epsilon(1e-5));
    }
    // Two yaws differ by exactly their difference about Z (the sign is
    // wiki-only; the magnitude is what this checks).
    canon::Vec3 a = rotate(placedAt({0, 10, 0}).rotation, {1, 0, 0});
    canon::Vec3 b = rotate(placedAt({0, 70, 0}).rotation, {1, 0, 0});
    CHECK(degrees(std::acos(dot(a, b))) == doctest::Approx(60.0f).epsilon(1e-3));
}

TEST_CASE("adtinput: tilt about either horizontal placement axis tilts model up by that angle") {
    for (std::array<float, 3> r : {std::array<float, 3>{12, 0, 0}, std::array<float, 3>{0, 0, 12},
                                   std::array<float, 3>{12, 145, 0}}) {
        CAPTURE(r[0]);
        CAPTURE(r[1]);
        CAPTURE(r[2]);
        canon::Vec3 up = rotate(placedAt(r).rotation, {0, 0, 1});
        CHECK(degrees(std::acos(up.z)) == doctest::Approx(12.0f).epsilon(1e-3));
    }
}

TEST_CASE("adtinput: MODF placements carry their doodad set, and scale only when flagged") {
    ObjSpec obj;
    obj.mapObjects.push_back({900, 1, {}, {}, 0x8, 2, 0, 2048});
    obj.mapObjects.push_back({901, 2, {}, {}, 0x8 | 0x4, 0, 0, 512});
    canon::Terrain t = build(RootSpec{}, obj);
    REQUIRE(t.placements.size() == 2);
    CHECK(t.placements[0].kind == canon::Placement::Kind::MapObject);
    CHECK(t.placements[0].doodadSet == 2);
    CHECK(t.placements[0].scale == 1.0f);
    CHECK(t.placements[1].scale == 0.5f);
}

TEST_CASE("adtinput: name-table placements resolve through the listfile, else keep the raw path as their name") {
    ObjSpec obj;
    obj.doodadNames = {"World\\Generic\\Tree.mdx", "World\\Unlisted\\Rock.mdx"};
    obj.mapObjectNames = {"World\\wmo\\Inn.wmo"};
    obj.doodads.push_back({0, 1, {}, {}, 1024, 0});
    obj.doodads.push_back({1, 2, {}, {}, 1024, 0});
    obj.mapObjects.push_back({0, 3, {}, {}, 0, 0, 0, 1024});
    Inputs inputs;
    inputs.gamePaths = {{"World\\Generic\\Tree.mdx", 4242}, {"World\\wmo\\Inn.wmo", 5353}};
    canon::Terrain t = build(RootSpec{}, obj, std::nullopt, inputs);
    REQUIRE(t.placements.size() == 3);

    CHECK(std::get<canon::FileDataId>(t.placements[0].asset.id).value == 4242);

    const canon::Ref& unlisted = t.placements[1].asset;
    CHECK(std::holds_alternative<canon::None>(unlisted.id));
    CHECK(unlisted.name == "World\\Unlisted\\Rock.mdx");
    CHECK(unlisted.source == canon::NameSource::AdtEmbedded);

    CHECK(std::get<canon::FileDataId>(t.placements[2].asset.id).value == 5353);
}

namespace {

canon::TextureRef resolvedTexture(uint32_t fdid) {
    canon::TextureRef ref;
    ref.state = canon::TextureRef::State::Resolved;
    ref.resolved = canon::Ref{canon::FileDataId{fdid}, "tex" + std::to_string(fdid), canon::NameSource::Listfile};
    return ref;
}

}  // namespace

TEST_CASE("adtinput: textures come from the caller's resolutions, unresolved ones say why") {
    TexSpec tex;
    tex.diffuseIds = {1001, 1002};
    tex.heightIds = {2001, 0};
    tex.textureParams = {0x10, 0};
    Inputs inputs;
    inputs.textures = {{1001, resolvedTexture(1001)}, {2001, resolvedTexture(2001)}};
    canon::Terrain t = build(RootSpec{}, std::nullopt, tex, inputs);

    REQUIRE(t.textures.size() == 2);
    CHECK(t.textures[0].diffuse.state == canon::TextureRef::State::Resolved);
    REQUIRE(t.textures[0].height.has_value());
    CHECK(std::get<canon::FileDataId>(t.textures[0].height->resolved.id).value == 2001);
    // texture_scale 1 (flags bits 4..7) halves the 8x-per-chunk base.
    CHECK(t.textures[0].repeatsPerChunk == 4.0f);
    CHECK(t.textures[0].heightScale == 0.5f);
    CHECK(t.textures[0].heightOffset == 2.0f);

    CHECK(t.textures[1].diffuse.state == canon::TextureRef::State::KnownUnresolved);
    CHECK(t.textures[1].diffuse.unresolvedReason.find("1002") != std::string::npos);
    CHECK_FALSE(t.textures[1].height.has_value());
    CHECK(t.textures[1].repeatsPerChunk == 8.0f);
}

TEST_CASE("adtinput: MTEX filename textures resolve through the listfile like name-table placements") {
    TexSpec tex;
    tex.textureNames = {"Tileset\\Elwynn\\Grass.blp", "Tileset\\Unlisted\\Dirt.blp"};
    Inputs inputs;
    inputs.gamePaths = {{"Tileset\\Elwynn\\Grass.blp", 3001}};
    inputs.textures = {{3001, resolvedTexture(3001)}};
    canon::Terrain t = build(RootSpec{}, std::nullopt, tex, inputs);
    REQUIRE(t.textures.size() == 2);
    CHECK(std::get<canon::FileDataId>(t.textures[0].diffuse.resolved.id).value == 3001);
    CHECK(t.textures[1].diffuse.state == canon::TextureRef::State::KnownUnresolved);
    CHECK(t.textures[1].diffuse.unresolvedReason == "MTEX path not in listfile: Tileset\\Unlisted\\Dirt.blp");
}

TEST_CASE("adtinput: layers carry texture index, alpha, flags, and deduplicated ground effects") {
    TexSpec tex;
    tex.diffuseIds = {1, 2, 3};
    tex.layers = [](uint32_t c) {
        std::vector<LayerSpec> layers{LayerSpec{0, 0x80, 77, {}}};
        if (c == 4) {
            layers.push_back(LayerSpec{1, 0x100 | 0x40 | 2 | (3 << 3), 77, std::vector<uint8_t>(4096, 0x30)});
            layers.push_back(LayerSpec{2, 0x100, 88, std::vector<uint8_t>(4096, 0x90)});
        }
        return layers;
    };
    RootSpec root;
    root.chunk = [](uint32_t ix, uint32_t iy) {
        ChunkSpec c;
        if (ix == 4 && iy == 0) {
            c.predominantTexture[0] = 0xE4;  // quads 0..3 -> layers 0, 1, 2, 3
            c.noEffectDoodad[2] = 0x05;
        }
        return c;
    };
    Inputs inputs;
    canon::GroundEffect grass;
    grass.ref = canon::Ref{canon::Db2Row{"GroundEffectTexture", 77}, "", canon::NameSource::None};
    grass.density = 12;
    inputs.groundEffects = {{77, grass}};
    canon::Terrain t = build(root, std::nullopt, tex, inputs);

    const canon::TerrainChunk& c = t.chunks[4];
    REQUIRE(c.layers.size() == 3);
    CHECK(c.layers[0].textureIndex == 0);
    CHECK(c.layers[0].overbright);
    CHECK(c.layers[0].alpha.empty());
    CHECK(c.layers[1].textureIndex == 1);
    REQUIRE(c.layers[1].alpha.size() == 4096);
    CHECK(c.layers[1].alpha[100] == 0x30);
    REQUIRE(c.layers[1].animation.has_value());
    CHECK(c.layers[1].animation->directionDegrees == 90.0f);
    CHECK(c.layers[1].animation->speed == 3);
    CHECK_FALSE(c.layers[2].animation.has_value());

    // Effect 77 is defined and used twice: one entry. Effect 88 is not defined: a bare Ref.
    REQUIRE(t.groundEffects.size() == 2);
    CHECK(c.layers[0].groundEffectIndex == c.layers[1].groundEffectIndex);
    CHECK(t.groundEffects[*c.layers[0].groundEffectIndex].density == 12u);
    const canon::GroundEffect& bare = t.groundEffects[*c.layers[2].groundEffectIndex];
    CHECK(std::get<canon::Db2Row>(bare.ref.id).row == 88);
    CHECK_FALSE(bare.density.has_value());

    CHECK(c.dominantLayer[0] == 0);
    CHECK(c.dominantLayer[1] == 1);
    CHECK(c.dominantLayer[2] == 2);
    CHECK(c.dominantLayer[3] == 3);
    CHECK(c.groundEffectSuppressedRows[2] == 0x05);
}

TEST_CASE("adtinput: a chunk with no layers stays untextured, not an error") {
    TexSpec tex;
    tex.layers = [](uint32_t c) { return c == 0 ? std::vector<LayerSpec>{} : std::vector<LayerSpec>{LayerSpec{}}; };
    canon::Terrain t = build(RootSpec{}, std::nullopt, tex);
    CHECK(t.chunks[0].layers.empty());
    CHECK(t.chunks[1].layers.size() == 1);
}

TEST_CASE("adtinput: a layer after the first without use_alpha_map is an error") {
    TexSpec tex;
    tex.layers = [](uint32_t c) {
        std::vector<LayerSpec> layers{LayerSpec{}};
        if (c == 3) layers.push_back(LayerSpec{0, 0, 0xFFFFFFFF, {}});
        return layers;
    };
    CHECK_THROWS_WITH(build(RootSpec{}, std::nullopt, tex),
                      doctest::Contains("MCLY layer 1 of MCNK (3,0): expected use_alpha_map (0x100)"));
}

TEST_CASE("adtinput: texture layers need the WDT flags that pick the alpha format") {
    CHECK_THROWS_WITH(build(RootSpec{}, std::nullopt, TexSpec{}, {}, std::nullopt),
                      doctest::Contains("need the map's WDT MPHD flags"));
}

TEST_CASE("adtinput: the WDT's alpha-format bits select 8-bit or 4-bit alpha decoding") {
    TexSpec tex;
    tex.layers = [](uint32_t c) {
        std::vector<LayerSpec> layers{LayerSpec{}};
        if (c == 0) layers.push_back(LayerSpec{0, 0x100, 0xFFFFFFFF, std::vector<uint8_t>(4096, 0x21)});
        return layers;
    };
    // 8-bit (MPHD 0x4): bytes straight through.
    CHECK(build(RootSpec{}, std::nullopt, tex, {}, 0x4).chunks[0].layers[1].alpha[0] == 0x21);
    // 0x80 (height texturing) also implies 8-bit.
    CHECK(build(RootSpec{}, std::nullopt, tex, {}, 0x80).chunks[0].layers[1].alpha[0] == 0x21);
    // Neither: 4-bit, low nibble first.
    CHECK(build(RootSpec{}, std::nullopt, tex, {}, 0).chunks[0].layers[1].alpha[0] == 0x11);
}

TEST_CASE("adtinput: textureFileDataIds and gamePaths list what a caller must resolve") {
    TexSpec texSpec;
    texSpec.textureNames = {"a.blp", "b.blp", "a.blp"};
    adt::TexFile tex = adt::parseTex(texFile(texSpec));
    CHECK(adtinput::textureFileDataIds(tex, {{"a.blp", 7}}) == std::vector<uint32_t>{7});

    TexSpec idSpec;
    idSpec.diffuseIds = {5, 6, 5};
    idSpec.heightIds = {0, 9, 6};
    CHECK(adtinput::textureFileDataIds(adt::parseTex(texFile(idSpec)), {}) == std::vector<uint32_t>{5, 6, 9});

    ObjSpec objSpec;
    objSpec.doodadNames = {"m.m2"};
    objSpec.mapObjectNames = {"w.wmo"};
    std::optional<adt::ObjFile> obj = adt::parseObj(objFile(objSpec));
    std::optional<adt::TexFile> texOpt = tex;
    CHECK(adtinput::gamePaths(obj, texOpt) == std::vector<std::string>{"m.m2", "w.wmo", "a.blp", "b.blp", "a.blp"});
    CHECK(adtinput::gamePaths(std::nullopt, std::nullopt).empty());
}

TEST_CASE("adtinput::groundEffectDefinitions joins texture rows to doodad models") {
    husk::groundeffect::Data data;
    data.textures.push_back({77, 20u, {{1, 3}, {2, 1}}});
    data.doodads[1] = {12345, 0x4};
    auto defs = adtinput::groundEffectDefinitions(data);
    REQUIRE(defs.count(77) == 1);
    const canon::GroundEffect& e = defs.at(77);
    CHECK(e.density == 20u);
    REQUIRE(e.doodads.size() == 2);
    CHECK(std::get<canon::FileDataId>(e.doodads[0].model.id).value == 12345);
    CHECK(e.doodads[0].weight == 3);
    CHECK(e.doodads[0].flags == 0x4);
    // Doodad row 2 is missing from GroundEffectDoodad: identity kept, no model.
    CHECK(std::get<canon::Db2Row>(e.doodads[1].doodad.id).row == 2);
    CHECK(std::holds_alternative<canon::None>(e.doodads[1].model.id));
}
