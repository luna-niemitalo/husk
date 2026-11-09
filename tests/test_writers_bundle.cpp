// Coverage for husk::writers::writeBundle (src/writers/bundle_writer.hpp) --
// the native glTF-free bundle format. Two tiers: synthetic small models for
// precise byte-level round-trip checks (built by hand, not via
// m2input::buildCanonModel, so the expected values are independent of that
// pipeline), and a real bloodelffemale.m2/.skin fixture (via
// m2input::buildCanonModel, same doctest::skip convention as
// tests/test_canon_model.cpp) for the structural checks that only make
// sense against real, non-trivial data. This file: manifest structure,
// mesh, rig, liquids and collision; scene/effect output is in
// test_writers_bundle_scene.cpp, appearance in _materials.cpp.
//
// JSON validity is checked with a real parser (nlohmann, already on the
// include path via tinygltf -- see tests/test_cli_info_json.cpp's own doc
// comment for why a real parser is used instead of a hand-rolled balance
// check), not a substring/balance check.

#include <doctest/doctest.h>

#include "test_bundle_fixtures.hpp"

TEST_CASE("writeBundle: synthetic model -- manifest.json is valid JSON, files exist") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-basic";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());

    CHECK(fs::exists(dir / "manifest.json"));
    CHECK(fs::exists(dir / "mesh.bin"));
    CHECK(fs::exists(dir / "skeleton.bin"));
    CHECK(fs::exists(dir / "animation.bin"));

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["format"] == "husk-bundle");
    CHECK(manifest["kind"] == "model");
    CHECK(manifest["schema_version"] == "0.3.0");
    CHECK(manifest["space"]["length_unit"] == "yard");
    CHECK(manifest["provenance"]["producer"] == "husk test");
    CHECK_FALSE(manifest["provenance"].contains("exported_at"));
    CHECK(manifest["definition"]["placement_sets"].empty());
    CHECK(manifest["selection"]["placement_sets"].empty());
    CHECK(manifest["references"].empty());
}

TEST_CASE("writeBundle: every BufferSlice's byte range fits inside its named file") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-slices";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    std::vector<nlohmann::json> slices;
    collectBufferSlices(manifest, slices);
    REQUIRE(!slices.empty());

    // One assertion covering every slice, not one CHECK per slice -- a real
    // model's animation data alone can carry thousands of slices (one
    // times/values pair per joint per sequence per channel), and asserting
    // per-slice multiplies the file's real assertion count by that corpus
    // size for no added coverage (every slice is still checked; only the
    // number of doctest assertion *reports* changes). Same "aggregate, then
    // point at the specific failure" discipline test_canon_animation_
    // convergence.cpp's own findBestInlinePick establishes for the analogous
    // per-keyframe case. On failure, CAPTURE names the first offending slice.
    std::optional<std::string> firstFailure;
    for (const auto& slice : slices) {
        std::string file = slice["file"].get<std::string>();
        size_t byteOffset = slice["byte_offset"].get<size_t>();
        size_t byteLength = slice["byte_length"].get<size_t>();
        size_t actualSize = fs::file_size(dir / file);
        if (byteOffset + byteLength > actualSize && !firstFailure) {
            firstFailure = file + " offset=" + std::to_string(byteOffset) + " length=" +
                           std::to_string(byteLength) + " actualSize=" + std::to_string(actualSize);
        }
    }
    INFO("first offending slice (if any): ", firstFailure.value_or("none"));
    CHECK(!firstFailure.has_value());
}

TEST_CASE("writeBundle: synthetic model round-trips exact byte values via raw file reads") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-roundtrip";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    auto readSlice = [&](const nlohmann::json& slice) -> std::vector<uint8_t> {
        std::string file = slice["file"].get<std::string>();
        size_t byteOffset = slice["byte_offset"].get<size_t>();
        size_t byteLength = slice["byte_length"].get<size_t>();
        std::ifstream in(dir / file, std::ios::binary);
        REQUIRE(in.good());
        in.seekg(static_cast<std::streamoff>(byteOffset));
        std::vector<uint8_t> bytes(byteLength);
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteLength));
        REQUIRE(in.good());
        return bytes;
    };

    const auto& positionsSlice = manifest["resources"]["geometry"]["mesh"]["positions"];
    CHECK(positionsSlice["component_type"] == "f32");
    CHECK(positionsSlice["component_count"] == 3);
    CHECK(positionsSlice["count"] == 3);
    CHECK(positionsSlice["semantic"] == "POSITION");

    std::vector<uint8_t> posBytes = readSlice(positionsSlice);
    REQUIRE(posBytes.size() == model.mesh.positions.size() * sizeof(canon::Vec3));
    std::vector<canon::Vec3> readBack(model.mesh.positions.size());
    std::memcpy(readBack.data(), posBytes.data(), posBytes.size());
    for (size_t i = 0; i < readBack.size(); ++i) {
        CHECK(readBack[i].x == doctest::Approx(model.mesh.positions[i].x));
        CHECK(readBack[i].y == doctest::Approx(model.mesh.positions[i].y));
        CHECK(readBack[i].z == doctest::Approx(model.mesh.positions[i].z));
    }

    const auto& indicesSlice = manifest["resources"]["geometry"]["mesh"]["indices"];
    CHECK(indicesSlice["component_type"] == "u32");
    std::vector<uint8_t> idxBytes = readSlice(indicesSlice);
    REQUIRE(idxBytes.size() == model.mesh.indices.size() * sizeof(uint32_t));
    std::vector<uint32_t> readIndices(model.mesh.indices.size());
    std::memcpy(readIndices.data(), idxBytes.data(), idxBytes.size());
    CHECK(readIndices == model.mesh.indices);

    // bind_translation is PARENT-RELATIVE (writers::localBindTranslation),
    // not raw globalPosition -- joint 1's expected value is (1,2,3) - (0,0,0).
    const auto& bindSlice = manifest["resources"]["rig"]["skeleton"]["bind_translation"];
    std::vector<uint8_t> bindBytes = readSlice(bindSlice);
    std::vector<canon::Vec3> bindTranslations(2);
    std::memcpy(bindTranslations.data(), bindBytes.data(), bindBytes.size());
    CHECK(bindTranslations[0].x == doctest::Approx(0.0f));
    CHECK(bindTranslations[1].x == doctest::Approx(1.0f));
    CHECK(bindTranslations[1].y == doctest::Approx(2.0f));
    CHECK(bindTranslations[1].z == doctest::Approx(3.0f));

    const auto& parentsSlice = manifest["resources"]["rig"]["skeleton"]["parents"];
    CHECK(parentsSlice["component_type"] == "i32");
    std::vector<uint8_t> parentBytes = readSlice(parentsSlice);
    std::vector<int32_t> parents(2);
    std::memcpy(parents.data(), parentBytes.data(), parentBytes.size());
    CHECK(parents[0] == -1);
    CHECK(parents[1] == 0);
}

TEST_CASE("writeBundle: one uv set, no joints0/weights0 for an unskinned, single-UV-set model") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-omissions";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];

    CHECK(mesh["uv_sets"].size() == 1);
    CHECK(!mesh.contains("joints0"));
    CHECK(!mesh.contains("weights0"));
}

TEST_CASE("writeBundle: a second uv set and joints0/weights0 when the canon::Mesh has them") {
    canon::Model model = buildSyntheticModel();
    model.mesh.uvSets.push_back({{0, 1}, {1, 1}, {1, 0}});
    canon::Mesh::Skinning s;
    s.joints = {0, 1, 0, 0};
    s.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    model.mesh.skinning = {s, s, s};

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-skinned";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];

    REQUIRE(mesh["uv_sets"].size() == 2);
    CHECK(mesh["uv_sets"][1]["semantic"] == "TEXCOORD_1");
    REQUIRE(mesh.contains("joints0"));
    CHECK(mesh["joints0"]["component_type"] == "u8");
    CHECK(mesh["joints0"]["component_count"] == 4);
    REQUIRE(mesh.contains("weights0"));
    CHECK(mesh["weights0"]["component_type"] == "f32");
    CHECK(mesh["weights0"]["component_count"] == 4);
}

TEST_CASE("writeBundle: sparse animation -- only joints with real curve data get an entry") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-sparse-anim";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    const auto& animation = manifest["resources"]["rig"]["animation"];
    REQUIRE(animation.size() == 1);
    CHECK(animation[0]["sequence_index"] == 5);
    CHECK(animation[0]["sequence_kind"] == "sequence");

    // model.animations[0].boneCurves has 2 entries (joint 0 real, joint 1
    // nullopt) -- only joint 0 should appear.
    const auto& joints = animation[0]["joints"];
    REQUIRE(joints.size() == 1);
    CHECK(joints[0]["joint_index"] == 0);
    CHECK(joints[0]["translation"]["values"]["count"] == 2);
}

TEST_CASE("writeBundle: liquids slice into liquid.bin, which only exists when there are liquids") {
    canon::Model model = buildSyntheticModel();
    fs::path dry = fs::temp_directory_path() / "husk-test-bundle-no-liquid";
    fs::remove_all(dry);
    writers::writeBundle(model, dry, testProvenance());
    CHECK(parseManifest(dry)["resources"]["geometry"]["liquids"].empty());
    CHECK_FALSE(fs::exists(dry / "liquid.bin"));

    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{0}, "pool", canon::NameSource::WmoEmbedded}, 0, {}, {}});
    canon::LiquidGrid l;
    l.material = canon::RecordIndex{0};
    l.liquidType = canon::Ref{canon::Db2Row{"LiquidType", 13}, "", canon::NameSource::None};
    l.sourceLiquidType = 1;
    l.corner = {1, 2, 3};
    l.tileSize = 25.0f / 6.0f;
    l.vertsX = 3;
    l.vertsY = 2;
    l.heights = {0, 1, 2, 3, 4, 5};
    l.vertexData.assign(6, {7, 0, 0, 0});
    l.tileExists = {1, 0};
    l.tileFlags = {0x44, 0x0F};
    model.liquids.push_back(l);
    fs::path wet = fs::temp_directory_path() / "husk-test-bundle-liquid";
    fs::remove_all(wet);
    writers::writeBundle(model, wet, testProvenance());
    nlohmann::json manifest = parseManifest(wet);
    const auto& written = manifest["resources"]["geometry"]["liquids"][0];

    CHECK(written["part"] == 0);
    CHECK(written["liquid_type"]["id"]["row"] == 13);
    CHECK(written["verts_x"] == 3);
    CHECK(written["heights"]["file"] == "liquid.bin");
    CHECK(written["heights"]["count"] == 6);
    CHECK(written["source"]["vertex_data"]["component_count"] == 4);
    CHECK(written["source"]["liquid_type"] == 1);
    CHECK(written["tile_exists"]["count"] == 2);
    std::vector<uint8_t> bin = readFile((wet / "liquid.bin").string());
    size_t flagsAt = written["source"]["tile_flags"]["byte_offset"];
    CHECK(bin.at(flagsAt) == 0x44);
    CHECK(bin.at(flagsAt + 1) == 0x0F);
}

TEST_CASE("writeBundle: collision slices into collision.bin, and both are omitted without collision") {
    canon::Model model = buildSyntheticModel();
    fs::path bare = fs::temp_directory_path() / "husk-test-bundle-no-collision";
    fs::remove_all(bare);
    writers::writeBundle(model, bare, testProvenance());
    CHECK_FALSE(parseManifest(bare)["resources"]["geometry"].contains("collision"));
    CHECK_FALSE(fs::exists(bare / "collision.bin"));

    model.collision.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    model.collision.indices = {0, 1, 2};
    model.collision.triangleNormals = {{0, 0, 1}};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-collision";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& c = manifest["resources"]["geometry"]["collision"];

    CHECK(c["positions"]["file"] == "collision.bin");
    CHECK(c["positions"]["count"] == 3);
    CHECK(c["indices"]["count"] == 3);
    CHECK(c["triangle_normals"]["count"] == 1);
}

TEST_CASE("writeBundle: attribute channels slice into mesh.bin with their domain and element count") {
    canon::Model model = buildSyntheticModel();
    model.mesh.attributes = {{"wmo.poly_flags", canon::AttributeDomain::Face, 1, std::vector<uint16_t>{0x20, 0x48}},
                             {"test.rgb", canon::AttributeDomain::Vertex, 3, std::vector<float>{1, 2, 3, 4, 5, 6}}};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-attributes";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& attributes = manifest["resources"]["geometry"]["mesh"]["attributes"];

    REQUIRE(attributes.size() == 2);
    CHECK(attributes[0]["name"] == "wmo.poly_flags");
    CHECK(attributes[0]["domain"] == "face");
    CHECK(attributes[0]["data"]["file"] == "mesh.bin");
    CHECK(attributes[0]["data"]["component_type"] == "u16");
    CHECK(attributes[0]["data"]["count"] == 2);
    CHECK(attributes[1]["domain"] == "vertex");
    CHECK(attributes[1]["data"]["component_type"] == "f32");
    CHECK(attributes[1]["data"]["component_count"] == 3);
    CHECK(attributes[1]["data"]["count"] == 2);
    std::vector<uint8_t> bin = readFile((dir / "mesh.bin").string());
    size_t at = attributes[0]["data"]["byte_offset"];
    CHECK(bin.at(at + 2) == 0x48);
}

TEST_CASE("writeBundle: portals on the mesh, links on their parts") {
    canon::Model model = buildSyntheticModel();
    canon::Portal portal;
    portal.vertices = {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}};
    portal.normal = {1, 0, 0};
    portal.distance = -2.5f;
    model.mesh.portals = {portal, portal};
    model.mesh.portals[1].sourceExtra.assign(16, 7);
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{0}, "hall", canon::NameSource::WmoEmbedded}, 0, {}, {},
                                {}, {{1, 1, -1}}});
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{1}, "room", canon::NameSource::WmoEmbedded}, 0, {}, {},
                                {}, {{1, 0, 1}}});
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-portals";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];

    REQUIRE(mesh["portals"].size() == 2);
    CHECK(mesh["portals"][0]["vertices"][2] == nlohmann::json::array({0.0, 1.0, 1.0}));
    CHECK(mesh["portals"][0]["normal"] == nlohmann::json::array({1.0, 0.0, 0.0}));
    CHECK(mesh["portals"][0]["distance"] == -2.5);
    CHECK_FALSE(mesh["portals"][0].contains("unresolved"));
    CHECK(mesh["portals"][1]["unresolved"]["extra_record"].size() == 16);
    const auto& link = mesh["parts"][0]["portals"][0];
    CHECK(link["portal"] == 1);
    CHECK(link["to_part"] == 1);
    CHECK(link["unresolved"]["side"] == -1);
}

TEST_CASE("writeBundle: surface types, materials' surface_type and the per-face channel") {
    canon::Model model = buildSyntheticModel();
    canon::SurfaceType stone;
    stone.ref = canon::Ref{canon::Db2Row{"TerrainType", 2}, "Stone", canon::NameSource::Db2};
    stone.soundClass = canon::Ref{canon::Db2Row{"TerrainTypeSounds", 3}, "Stone", canon::NameSource::Db2};
    canon::SurfaceType unknown;
    unknown.ref = canon::Ref{canon::Db2Row{"TerrainType", 99}, "", canon::NameSource::None};
    model.surfaceTypes = {stone, unknown};
    model.materials[0].surfaceType = 0;
    model.mesh.attributes.push_back({"surface_type", canon::AttributeDomain::Face, 1,
                                     std::vector<uint16_t>(model.mesh.indices.size() / 3, 1)});

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-surfaces";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, writers::Provenance{"husk test", "wmo", {}});
    nlohmann::json manifest = parseManifest(dir);

    const auto& surfaces = manifest["resources"]["surface_types"];
    REQUIRE(surfaces.size() == 2);
    CHECK(surfaces[0]["ref"]["name"] == "Stone");
    CHECK(surfaces[0]["sound_class"]["id"]["row"] == 3);
    CHECK_FALSE(surfaces[1].contains("sound_class"));
    CHECK(manifest["resources"]["materials"][0]["surface_type"] == 0);
    const auto& channels = manifest["resources"]["geometry"]["mesh"]["attributes"];
    CHECK(std::any_of(channels.begin(), channels.end(), [](const nlohmann::json& c) { return c["name"] == "surface_type"; }));
}

TEST_CASE("writeBundle: parts, colour sets, material flags and placement-set files") {
    canon::Model model = buildSyntheticModel();
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{0}, "hall", canon::NameSource::WmoEmbedded}, 0x8, {}, {}});
    model.mesh.primitives[0].part = 0;
    model.mesh.colorSets = {{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}}};
    model.materials[0].twoSided = true;
    model.materials[0].shader = canon::Ref{canon::RecordIndex{6}, "TwoLayerDiffuse", canon::NameSource::Synthesized};
    model.materials[0].layers[0].uvScroll = canon::Vec2{0.5f, 0.0f};
    canon::Model::OwnedSet owned;
    owned.alwaysOn = true;
    owned.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    canon::PlacedInstance instance;
    instance.id = 7;
    instance.asset = canon::Ref{canon::FileDataId{7001}, "barrel01", canon::NameSource::Listfile};
    instance.translation = {1, 2, 3};
    instance.scale = 2.0f;
    owned.set.instances.push_back(instance);
    model.placementSets.push_back(owned);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-wmo";
    fs::remove_all(dir);
    fs::create_directories(dir / "models" / "7001.canon.bundle");
    std::ofstream(dir / "models" / "7001.canon.bundle" / "manifest.json") << "{}";
    writers::writeBundle(model, dir, testProvenance(), {{7001, "models/7001.canon.bundle/manifest.json"}});
    nlohmann::json manifest = parseManifest(dir);
    const auto& resources = manifest["resources"];
    const auto& mesh = resources["geometry"]["mesh"];

    CHECK(mesh["parts"][0]["ref"]["name"] == "hall");
    CHECK(mesh["parts"][0]["ref"]["name_source"] == "wmo_embedded");
    CHECK(mesh["parts"][0]["source"]["flags"] == "0x8");
    CHECK(mesh["primitives"][0]["part"] == 0);
    CHECK(mesh["colors"][0]["component_type"] == "u8");
    CHECK(mesh["colors"][0]["semantic"] == "COLOR_0");
    const auto& material = resources["materials"][0];
    CHECK(material["two_sided"] == true);
    CHECK(material["shader"]["name"] == "TwoLayerDiffuse");
    const auto& uvMap = resources["uv_maps"][material["layers"][0]["uv_map"].get<size_t>()];
    CHECK(uvMap["scroll"] == nlohmann::json::array({0.5, 0.0}));

    const auto& set = manifest["definition"]["placement_sets"][0];
    CHECK(set["always_on"] == true);
    CHECK(set["instance_count"] == 1);
    const auto& reference = manifest["references"][set["reference"].get<size_t>()];
    CHECK(reference["kind"] == "placement_set");
    CHECK(reference["asset"]["uri"] == "sets/0.json");
    CHECK(reference["asset"]["name"] == "Set_$DefaultGlobal");
    nlohmann::json setFile = nlohmann::json::parse(readTextFile(dir / "sets" / "0.json"));
    CHECK(setFile["kind"] == "placement_set");
    CHECK(setFile["subject"]["name"] == "Set_$DefaultGlobal");
    const auto& written = setFile["instances"][0];
    CHECK(written["id"] == 7);
    const auto& asset = setFile["references"][written["reference"].get<size_t>()]["asset"];
    CHECK(asset["uri"] == "../models/7001.canon.bundle/manifest.json");
    CHECK(asset["name"] == "barrel01");
    CHECK(written["scale"] == 2.0);
    CHECK(written["selection"]["placement_sets"].empty());
    CHECK_FALSE(written.contains("tint"));
}

TEST_CASE(
    "writeBundle: real fixture -- primitives[i].material_index resolves via canon::resolveMaterialIndex, "
    "not positionally" *
    doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model m2model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    canon::Model model = m2input::buildCanonModel(m2model, batches, submeshes, triangleIndices);
    REQUIRE(!model.mesh.primitives.empty());
    REQUIRE(model.primitiveMaterials.size() == model.mesh.primitives.size());
    // Real, expected dedup on this real fixture: strictly fewer materials
    // than primitives. If this fails, either the fixture changed or dedup
    // itself broke.
    REQUIRE(model.materials.size() < model.mesh.primitives.size());

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-real-fixture";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    const auto& primitives = manifest["resources"]["geometry"]["mesh"]["primitives"];
    REQUIRE(primitives.size() == model.mesh.primitives.size());
    std::optional<size_t> firstMismatch;
    for (size_t i = 0; i < primitives.size(); ++i) {
        auto expected = canon::resolveMaterialIndex(model, i);
        REQUIRE(expected.has_value());
        if (primitives[i]["material_index"] != *expected && !firstMismatch) {
            firstMismatch = i;
        }
    }
    CHECK_FALSE(firstMismatch.has_value());

    // Every BufferSlice's byte range must still fit its file on real,
    // non-trivial data (not just the synthetic tiny case above) -- one
    // aggregated assertion, not one per slice: a real model's animation data
    // alone carries thousands of slices (one times/values pair per joint per
    // sequence per channel), and CHECKing each individually multiplies this
    // file's assertion count by that corpus size for no added coverage
    // (every slice is still checked; only the assertion *count* changes).
    std::vector<nlohmann::json> slices;
    collectBufferSlices(manifest, slices);
    REQUIRE(!slices.empty());
    std::optional<std::string> firstFailure;
    for (const auto& slice : slices) {
        std::string file = slice["file"].get<std::string>();
        size_t byteOffset = slice["byte_offset"].get<size_t>();
        size_t byteLength = slice["byte_length"].get<size_t>();
        size_t actualSize = fs::file_size(dir / file);
        if (byteOffset + byteLength > actualSize && !firstFailure) {
            firstFailure = file + " offset=" + std::to_string(byteOffset) + " length=" +
                           std::to_string(byteLength) + " actualSize=" + std::to_string(actualSize);
        }
    }
    INFO("first offending slice (if any): ", firstFailure.value_or("none"));
    CHECK(!firstFailure.has_value());
}
