// writers::writeTerrainBundle -- canon::Terrain -> manifest.json +
// terrain.bin + liquid.bin. Built from synthetic tiles through the real
// parser and input module (tests/test_adt_fixtures.hpp), then read back with
// a real JSON parser and checked slice by slice against the canon values.

#include <doctest/doctest.h>

#include "bundle_schema.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <string>
#include <vector>

#include "../src/adt.hpp"
#include "../src/adt_canon_input.hpp"
#include "../src/writers/terrain_bundle_writer.hpp"
#include "test_adt_fixtures.hpp"

using namespace adtfx;
namespace adt = husk::adt;
namespace adtinput = husk::adtinput;
namespace canon = husk::canon;
namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readBytes(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

nlohmann::json readJsonFile(const fs::path& path) {
    std::vector<uint8_t> bytes = readBytes(path);
    auto parsed = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, /*allow_exceptions=*/false);
    REQUIRE_FALSE(parsed.is_discarded());
    return parsed;
}

// A placement_set document's instances, each with its referenced asset
// resolved into "asset"; the document is schema-checked on the way.
nlohmann::json readPlacements(const fs::path& setFile) {
    std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(setFile);
    std::string report;
    for (const husk::bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
    std::ifstream in(setFile);
    REQUIRE(in.good());
    nlohmann::json set = nlohmann::json::parse(in);
    nlohmann::json instances = set["instances"];
    for (auto& instance : instances) instance["asset"] = set["references"][instance["reference"].get<size_t>()]["asset"];
    return instances;
}

// The manifest, schema-checked (with its placement sets) on the way.
nlohmann::json readManifest(const fs::path& dir) {
    std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(dir / "manifest.json");
    std::string report;
    for (const husk::bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
    return readJsonFile(dir / "manifest.json");
}

void writeTile(canon::Terrain terrain, const fs::path& dir, const husk::writers::AssetUris& modelUris = {},
               const husk::writers::AssetUris& textureUris = {}) {
    terrain.subject = canon::Ref{canon::FileDataId{777001}, "azeroth_32_48", canon::NameSource::Listfile};
    husk::writers::Provenance provenance{"husk test", "adt", {{"obj0", "placements", true, ""}}};
    husk::writers::writeTerrainBundle(terrain, dir, provenance, modelUris, textureUris);
}

template <typename T>
std::vector<T> readSlice(const fs::path& dir, const nlohmann::json& slice) {
    std::vector<uint8_t> bin = readBytes(dir / slice["file"].get<std::string>());
    size_t offset = slice["byte_offset"].get<size_t>();
    size_t length = slice["byte_length"].get<size_t>();
    REQUIRE(offset + length <= bin.size());
    REQUIRE(length % sizeof(T) == 0);
    std::vector<T> out(length / sizeof(T));
    std::memcpy(out.data(), bin.data() + offset, length);
    return out;
}

// A tile exercising every manifest section: two textures (one with a DDS-ish
// payload, one unresolved), one alpha layer, one ground effect, two liquids,
// one model and one map-object placement.
canon::Terrain sampleTerrain() {
    RootSpec rootSpec;
    rootSpec.chunk = [](uint32_t ix, uint32_t iy) {
        ChunkSpec c;
        c.areaId = (ix + iy) % 2 == 0 ? 12 : 40;
        c.baseZ = 5.0f;
        c.height = [ix](int i) { return static_cast<float>(i + static_cast<int>(ix)); };
        if (ix == 3 && iy == 0) c.holesHighRes = {0, 0x10, 0, 0, 0, 0, 0, 0};
        return c;
    };
    std::vector<uint8_t> heights;
    for (int i = 0; i < 4; ++i) putF32(heights, 3.0f);
    for (int i = 0; i < 4; ++i) putU8(heights, 255);
    rootSpec.liquids = {LiquidSpec{0, 5, 100, 0, 0, 0, 0, 1, 1, {}, heights},
                        LiquidSpec{1, 2, 2, -1.0f, -1.0f, 0, 0, 8, 8, {}, {}}};

    ObjSpec objSpec;
    objSpec.doodads.push_back({555, 1, {kMapHalfExtent, 0, kMapHalfExtent}, {}, 1024, 0x40});
    objSpec.mapObjects.push_back({666, 2, {}, {}, 0x8, 1, 0, 1024});

    TexSpec texSpec;
    texSpec.diffuseIds = {1001, 1002};
    texSpec.layers = [](uint32_t c) {
        std::vector<LayerSpec> layers{LayerSpec{0, 0, 77, {}}};
        if (c == 2) layers.push_back(LayerSpec{1, 0x100, 0xFFFFFFFF, std::vector<uint8_t>(4096, 0x44)});
        return layers;
    };

    adt::RootFile root = adt::parseRoot(rootFile(rootSpec));
    std::optional<adt::ObjFile> obj = adt::parseObj(objFile(objSpec));
    std::optional<adt::TexFile> tex = adt::parseTex(texFile(texSpec));

    adtinput::TextureResolutions textures;
    canon::TextureRef grass;
    grass.state = canon::TextureRef::State::Resolved;
    grass.resolved = canon::Ref{canon::FileDataId{1001}, "grass", canon::NameSource::Listfile};
    grass.rawPayload = canon::TextureRef::Payload{{'D', 'D', 'S', ' ', 1, 2, 3}, canon::TextureEncoding::Bc1};
    textures.emplace(1001, grass);

    adtinput::GroundEffectDefinitions groundEffects;
    canon::GroundEffect effect;
    effect.ref = canon::Ref{canon::Db2Row{"GroundEffectTexture", 77}, "", canon::NameSource::None};
    effect.density = 9;
    effect.terrainType = 5;
    canon::GroundEffectDoodad doodad;
    doodad.doodad = canon::Ref{canon::Db2Row{"GroundEffectDoodad", 3}, "", canon::NameSource::None};
    doodad.model = canon::Ref{canon::FileDataId{888}, "flower", canon::NameSource::Listfile};
    doodad.weight = 2;
    effect.doodads.push_back(doodad);
    groundEffects.emplace(77, effect);

    adtinput::GamePathResolutions gamePaths;
    // Type 5 read (grass doubles as its one file frame), type 2 not.
    static const husk::liquiddb::Data liquids = [] {
        husk::liquiddb::Data d;
        d.materials[1] = {0, 1};
        d.types[5] = {"Slow Water", 0xf, 1, {{{1001}, -1}, {{0}, 1}}};
        d.objects[100] = {5, 180.0f, 1.0f, 1, 0};
        return d;
    }();
    static const husk::surfacedb::Data surfaces = [] {
        husk::surfacedb::Data d;
        d.types[5] = {5, "Grass", 12, 13, 6, "Grass", 0};
        return d;
    }();
    adtinput::TileSources sources{root, obj, tex, 0x4, "azeroth", 32, 48};
    return adtinput::buildCanonTerrain(sources, {textures, groundEffects, gamePaths, &liquids, &surfaces});
}

fs::path freshDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    return dir;
}

}  // namespace

TEST_CASE("writeTerrainBundle: a model document with the tile's subject, edge statement and constants") {
    fs::path dir = freshDir("husk-test-terrain-bundle-shape");
    writeTile(sampleTerrain(), dir);
    nlohmann::json m = readManifest(dir);

    CHECK(m["format"] == "husk-bundle");
    CHECK(m["kind"] == "model");
    CHECK(m["subject"]["name"] == "azeroth_32_48");
    CHECK(m["subject"]["id"]["value"] == 777001);
    CHECK(m["provenance"]["input_format"] == "adt");
    CHECK(m["definition"]["terrain"]["edge_vertices"] == "duplicated");
    CHECK_FALSE(m["resources"].contains("geometry"));
    const nlohmann::json& t = m["resources"]["terrain"];
    CHECK(t["chunks_per_side"] == 16);
    CHECK(t["quads_per_side"] == 8);
    CHECK(t["tile_size"].get<double>() == doctest::Approx(533.3333));
    CHECK(t["chunk_size"].get<double>() == doctest::Approx(33.3333));
    CHECK(t["quad_size"].get<double>() == doctest::Approx(4.16667));
    CHECK(t["row_axis"] == nlohmann::json::array({-1.0, 0.0, 0.0}));
    CHECK(t["column_axis"] == nlohmann::json::array({0.0, -1.0, 0.0}));
    CHECK(t["chunk_vertex_layout"] == "interleaved_9x9_corners_8x8_centres");
    CHECK(t["chunk_topology"] == "quad_centre_fan");
}

TEST_CASE("writeTerrainBundle: chunk slices hold exactly the canon chunk data") {
    canon::Terrain terrain = sampleTerrain();
    fs::path dir = freshDir("husk-test-terrain-bundle-chunks");
    writeTile(terrain, dir);
    nlohmann::json chunks = readManifest(dir)["resources"]["terrain"]["chunks"];

    CHECK(chunks["count"] == 256);
    CHECK(chunks["heights"]["component_count"] == 145);
    CHECK(chunks["normals"]["semantic"] == "NORMAL");

    std::vector<float> heights = readSlice<float>(dir, chunks["heights"]);
    REQUIRE(heights.size() == 256 * 145);
    CHECK(heights[145 * 3 + 10] == terrain.chunks[3].heights[10]);

    std::vector<float> origins = readSlice<float>(dir, chunks["origin"]);
    CHECK(origins[2 * 5] == terrain.chunks[5].origin.x);
    CHECK(origins[2 * 5 + 1] == terrain.chunks[5].origin.y);

    std::vector<float> normals = readSlice<float>(dir, chunks["normals"]);
    CHECK(normals.size() == 256 * 145 * 3);

    std::vector<uint8_t> holes = readSlice<uint8_t>(dir, chunks["hole_rows"]);
    CHECK(holes[8 * 3 + 1] == 0x10);

    // Two distinct AreaTable rows -> two `areas` entries, indexed per chunk.
    REQUIRE(chunks["areas"].size() == 2);
    std::vector<uint32_t> areaIndex = readSlice<uint32_t>(dir, chunks["area_index"]);
    CHECK(chunks["areas"][areaIndex[0]]["id"]["row"] == 12);
    CHECK(chunks["areas"][areaIndex[1]]["id"]["row"] == 40);

    REQUIRE(chunks["layers"].size() == 256);
    const nlohmann::json& layers = chunks["layers"][2];
    REQUIRE(layers.size() == 2);
    CHECK_FALSE(layers[0].contains("alpha"));
    CHECK(layers[0]["ground_effect_index"] == 0);
    std::vector<uint8_t> alpha = readSlice<uint8_t>(dir, layers[1]["alpha"]);
    CHECK(alpha == std::vector<uint8_t>(4096, 0x44));
}

TEST_CASE("writeTerrainBundle: textures embed their payload, or state why they are unresolved") {
    fs::path dir = freshDir("husk-test-terrain-bundle-textures");
    writeTile(sampleTerrain(), dir);
    nlohmann::json m = readManifest(dir);
    const nlohmann::json& textures = m["resources"]["terrain"]["textures"];
    const nlohmann::json& table = m["resources"]["textures"];
    REQUIRE(textures.size() == 2);

    const nlohmann::json& grass = textures[0];
    const nlohmann::json& grassDiffuse = table[grass["diffuse"].get<size_t>()];
    CHECK(grassDiffuse["state"] == "resolved");
    CHECK(grass["repeats_per_chunk"] == 8.0);
    // The writer embeds a payload under textures/ and points at it.
    CHECK(grassDiffuse["payload"]["uri"].get<std::string>().starts_with("textures/"));
    bool anyPayloadFile = false;
    for (const auto& entry : fs::directory_iterator(dir / "textures")) anyPayloadFile |= entry.is_regular_file();
    CHECK(anyPayloadFile);

    CHECK(table[textures[1]["diffuse"].get<size_t>()]["state"] == "known_unresolved");
}

TEST_CASE("writeTerrainBundle: a payload-less resolved texture points at the caller's shared uri") {
    canon::Terrain terrain = sampleTerrain();
    terrain.textures[0].diffuse.rawPayload.reset();
    terrain.liquidTypes[0].textures[0].frames[0].rawPayload.reset();
    fs::path dir = freshDir("husk-test-terrain-bundle-shared-texture");
    writeTile(terrain, dir, {}, {{1001, "../../textures/1001.dds"}});
    nlohmann::json m = readJsonFile(dir / "manifest.json");
    const nlohmann::json& table = m["resources"]["textures"];
    nlohmann::json grass = table[m["resources"]["terrain"]["textures"][0]["diffuse"].get<size_t>()];
    CHECK(grass["state"] == "resolved");
    CHECK(grass["payload"]["uri"] == "../../textures/1001.dds");
    CHECK(grass["payload"]["media_type"] == "image/vnd-ms.dds");
    // The water frame is the same shared texture: one table entry.
    CHECK(m["resources"]["liquid_types"][0]["textures"][0]["frames"][0] == m["resources"]["terrain"]["textures"][0]["diffuse"]);
    CHECK_FALSE(fs::exists(dir / "textures"));
}

TEST_CASE("writeTerrainBundle: ground effects, liquids and placement sets") {
    fs::path dir = freshDir("husk-test-terrain-bundle-objects");
    fs::path placedModel = dir.parent_path() / "models" / "555.canon.bundle" / "manifest.json";
    fs::create_directories(placedModel.parent_path());
    std::ofstream(placedModel) << "{}";
    writeTile(sampleTerrain(), dir, {{555, "../models/555.canon.bundle/manifest.json"}});
    nlohmann::json m = readManifest(dir);
    const nlohmann::json& t = m["resources"]["terrain"];

    REQUIRE(t["ground_effects"].size() == 1);
    const nlohmann::json& effect = t["ground_effects"][0];
    CHECK(effect["density"] == 9);
    CHECK(effect["doodads"][0]["model"]["id"]["value"] == 888);
    CHECK(effect["doodads"][0]["weight"] == 2);
    CHECK_FALSE(effect["doodads"][0]["model"].contains("uri"));
    const nlohmann::json& surface = m["resources"]["surface_types"][effect["surface_type"].get<size_t>()];
    CHECK(surface["ref"]["name"] == "Grass");
    CHECK(surface["ref"]["id"]["table"] == "TerrainType");
    CHECK(surface["sound_class"]["id"]["row"] == 6);
    CHECK(surface["footstep_spray_run"]["id"]["row"] == 12);

    REQUIRE(t["liquids"].size() == 2);
    const nlohmann::json& river = t["liquids"][0];
    CHECK(river["height_source"] == "stored");
    CHECK(river["liquid_object"]["id"]["row"] == 100);
    CHECK(readSlice<float>(dir, river["heights"]) == std::vector<float>(4, 3.0f));
    CHECK(readSlice<float>(dir, river["depth"]) == std::vector<float>(4, 1.0f));
    CHECK(river["liquid_type"]["name"] == "Slow Water");
    REQUIRE(t["liquid_objects"].size() == 1);
    CHECK(t["liquid_objects"][0]["identity"]["id"]["row"] == 100);
    CHECK(t["liquid_objects"][0]["flow_direction"].get<double>() == doctest::Approx(3.14159265));
    const nlohmann::json& types = m["resources"]["liquid_types"];
    REQUIRE(types.size() == 2);
    CHECK(types[0]["identity"]["name"] == "Slow Water");
    CHECK(types[0]["material"]["vertex_format"] == 0);
    CHECK(types[0]["source"]["flags"] == "0xf");
    REQUIRE(types[0]["textures"].size() == 2);
    const nlohmann::json& table = m["resources"]["textures"];
    CHECK(table[types[0]["textures"][0]["frames"][0].get<size_t>()]["state"] == "resolved");
    CHECK(types[0]["textures"][1]["source"]["kind"] == 1);
    CHECK(table[types[0]["textures"][1]["frames"][0].get<size_t>()]["state"] == "known_unresolved");
    CHECK(types[1]["identity"]["id"]["row"] == 2);
    CHECK_FALSE(types[1].contains("material"));
    const nlohmann::json& ocean = t["liquids"][1];
    CHECK(ocean["height_source"] == "min_height_level");
    CHECK_FALSE(ocean.contains("liquid_object"));
    CHECK(ocean["quad_rect"]["columns"] == 8);
    CHECK(readSlice<uint8_t>(dir, ocean["quad_exists"]) == std::vector<uint8_t>(64, 1));

    const nlohmann::json& owned = m["definition"]["placement_sets"];
    REQUIRE(owned.size() == 2);
    CHECK(owned[0]["always_on"] == true);
    CHECK(owned[0]["instance_count"] == 1);
    REQUIRE(m["references"].size() == 2);
    CHECK(m["references"][owned[0]["reference"].get<size_t>()]["asset"]["name"] == "doodads");
    CHECK(m["references"][0]["asset"]["uri"] == "sets/0.json");
    CHECK(m["references"][1]["asset"]["name"] == "map_objects");
    CHECK(m["references"][1]["asset"]["uri"] == "sets/1.json");

    nlohmann::json doodads = readPlacements(dir / "sets" / "0.json");
    REQUIRE(doodads.size() == 1);
    const nlohmann::json& model = doodads[0];
    CHECK(model["id"] == 1);
    // The bundle-relative uri, one level further up from sets/.
    CHECK(model["asset"]["uri"] == "../../models/555.canon.bundle/manifest.json");
    CHECK(model["translation"][0].get<double>() == doctest::Approx(-canon::terrainTileCorner(32, 48).x));
    CHECK(model["rotation"].size() == 4);
    CHECK(model["selection"]["placement_sets"].empty());

    nlohmann::json mapObjects = readPlacements(dir / "sets" / "1.json");
    REQUIRE(mapObjects.size() == 1);
    const nlohmann::json& building = mapObjects[0];
    CHECK(building["selection"]["placement_sets"] == nlohmann::json::array({1}));
    CHECK_FALSE(building["asset"].contains("uri"));
}

TEST_CASE("writeTerrainBundle: every slice stays inside its .bin, and the manifest is written last") {
    fs::path dir = freshDir("husk-test-terrain-bundle-slices");
    writeTile(sampleTerrain(), dir);
    nlohmann::json m = readManifest(dir);

    std::vector<nlohmann::json> slices;
    std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& n) {
        if (n.is_object() && n.contains("byte_offset")) slices.push_back(n);
        if (n.is_structured()) {
            for (const auto& child : n) walk(child);
        }
    };
    walk(m);
    REQUIRE(slices.size() > 10);
    for (const nlohmann::json& s : slices) {
        size_t binSize = fs::file_size(dir / s["file"].get<std::string>());
        CHECK(s["byte_offset"].get<size_t>() + s["byte_length"].get<size_t>() <= binSize);
    }
    // Resumable batch exports treat an existing manifest as "done", so it
    // must not be older than the payload files it describes.
    auto manifestTime = fs::last_write_time(dir / "manifest.json");
    CHECK(manifestTime >= fs::last_write_time(dir / "terrain.bin"));
    CHECK(manifestTime >= fs::last_write_time(dir / "liquid.bin"));
}
