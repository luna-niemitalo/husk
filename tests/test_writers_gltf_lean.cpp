// husk::writers::writeLeanGlb (src/writers/gltf_lean.hpp) -- the lean,
// extras-free glTF projection off canon::Model. Two kinds of coverage here:
// synthetic small models for exact, hand-computed structural/numeric checks
// (no dependency on any real fixture being present), and the real
// bloodelffemale.m2/.skin fixture for the spec-conformance/Blender-import
// proofs this writer's whole point rests on (doctest::skip when unset, same
// convention as every other real-fixture test in this suite).

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "canon_model.hpp"
#include "m2.hpp"
#include "run_husk.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"
#include "writers/gltf_lean.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// A minimal, fully hand-authored canon::Model: a 2-joint skeleton (root +
// one child offset along raw M2's Z axis) skinning a single triangle, one
// PrimitiveGeoset, one Material. Every expected glTF value below is derived
// by hand from these raw inputs, independent of writeLeanGlb's own internal
// conversion calls.
canon::Model buildSyntheticModel() {
    canon::Model model;

    canon::Joint root;
    root.parent = -1;
    root.globalPosition = {0.0f, 0.0f, 0.0f};
    root.structuralLabel = "root";
    canon::Joint child;
    child.parent = 0;
    child.globalPosition = {0.0f, 0.0f, 5.0f};
    child.structuralLabel = "child";
    model.skeleton.joints = {root, child};

    model.mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    model.mesh.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    model.mesh.uv0 = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
    canon::Mesh::Skinning sk;
    sk.joints = {0, 0, 0, 0};
    sk.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    model.mesh.skinning = {sk, sk, sk};
    model.mesh.indices = {0, 1, 2};

    canon::PrimitiveGeoset prim;
    prim.geoset = canon::Geoset::fromRawId(0);
    prim.indexStart = 0;
    prim.indexCount = 3;
    model.mesh.primitives = {prim};

    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Modulate;
    canon::Material mat;
    mat.layers = {layer};
    model.materials = {mat};

    return model;
}

// Reads the .glb's own JSON chunk (the 12-byte GLB header, then one
// [length][type="JSON"][data] chunk before any BIN chunk) as a raw string
// -- independent of tinygltf's own parsed Value tree, since the "no extras
// anywhere" check needs to see the literal JSON text, not a tree that could
// normalize an empty object away.
std::string readGlbJsonChunk(const std::string& path) {
    std::vector<uint8_t> bytes = readFile(path);
    REQUIRE(bytes.size() >= 20);
    uint32_t jsonLength;
    std::memcpy(&jsonLength, bytes.data() + 12, 4);
    REQUIRE(bytes.size() >= 20 + jsonLength);
    return std::string(reinterpret_cast<const char*>(bytes.data()) + 20, jsonLength);
}

}  // namespace

TEST_CASE("writeLeanGlb: synthetic model -- node/skin/primitive/accessor counts, axis-converted "
          "positions round-trip") {
    canon::Model model = buildSyntheticModel();
    auto outPath = std::filesystem::temp_directory_path() / "husk-test-lean-synthetic.glb";
    std::filesystem::remove(outPath);

    writers::writeLeanGlb(model, outPath);

    tinygltf::TinyGLTF loader;
    tinygltf::Model gm;
    std::string err, warn;
    bool loaded = loader.LoadBinaryFromFile(&gm, &err, &warn, outPath.string());
    INFO("tinygltf error: ", err);
    REQUIRE(loaded);

    // 1 mesh node + 2 joint nodes.
    CHECK(gm.nodes.size() == 3);
    REQUIRE(gm.skins.size() == 1);
    CHECK(gm.skins[0].joints.size() == model.skeleton.joints.size());
    REQUIRE(gm.meshes.size() == 1);
    CHECK(gm.meshes[0].primitives.size() == model.mesh.primitives.size());

    const tinygltf::Primitive& prim = gm.meshes[0].primitives[0];
    auto posIt = prim.attributes.find("POSITION");
    REQUIRE(posIt != prim.attributes.end());
    const tinygltf::Accessor& posAcc = gm.accessors[posIt->second];
    CHECK(posAcc.count == model.mesh.positions.size());

    const tinygltf::Accessor& idxAcc = gm.accessors[prim.indices];
    CHECK(idxAcc.count == model.mesh.primitives[0].indexCount);

    // Independently-computed Z-up -> Y-up permutation (gltf::zUpToYUp's own
    // contract: x_gltf=x_m2, y_gltf=z_m2, z_gltf=-y_m2 -- verified directly
    // against gltf_math.cpp rather than assumed) applied by hand to each
    // raw position, compared against what the accessor actually decoded.
    const tinygltf::BufferView& posView = gm.bufferViews[posAcc.bufferView];
    const tinygltf::Buffer& buf = gm.buffers[posView.buffer];
    const float* decoded =
        reinterpret_cast<const float*>(buf.data.data() + posView.byteOffset + posAcc.byteOffset);
    for (size_t i = 0; i < model.mesh.positions.size(); ++i) {
        const m2::Vec3& raw = model.mesh.positions[i];
        CHECK(decoded[i * 3 + 0] == doctest::Approx(raw.x));
        CHECK(decoded[i * 3 + 1] == doctest::Approx(raw.z));
        CHECK(decoded[i * 3 + 2] == doctest::Approx(-raw.y));
    }

    std::filesystem::remove(outPath);
}

TEST_CASE("writeLeanGlb: synthetic model -- no extras object anywhere in the output JSON") {
    canon::Model model = buildSyntheticModel();
    auto outPath = std::filesystem::temp_directory_path() / "husk-test-lean-no-extras.glb";
    std::filesystem::remove(outPath);

    writers::writeLeanGlb(model, outPath);

    std::string json = readGlbJsonChunk(outPath.string());
    CHECK(json.find("extras") == std::string::npos);

    std::filesystem::remove(outPath);
}

TEST_CASE("writeLeanGlb: synthetic model -- joint hierarchy, bind translations, and material "
          "alphaMode") {
    canon::Model model = buildSyntheticModel();
    auto outPath = std::filesystem::temp_directory_path() / "husk-test-lean-hierarchy.glb";
    std::filesystem::remove(outPath);

    writers::writeLeanGlb(model, outPath);

    tinygltf::TinyGLTF loader;
    tinygltf::Model gm;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&gm, &err, &warn, outPath.string()));

    // node 0 is the mesh node; joint nodes follow, in skeleton order.
    REQUIRE(gm.nodes.size() == 3);
    const tinygltf::Node& rootNode = gm.nodes[1];
    const tinygltf::Node& childNode = gm.nodes[2];
    REQUIRE(rootNode.children.size() == 1);
    CHECK(rootNode.children[0] == 2);

    // root's own bind translation is its absolute position, unconverted
    // axis-wise except for the Z-up -> Y-up permutation: (0,0,0) -> (0,0,0).
    REQUIRE(rootNode.translation.size() == 3);
    CHECK(rootNode.translation[0] == doctest::Approx(0.0));
    CHECK(rootNode.translation[1] == doctest::Approx(0.0));
    CHECK(rootNode.translation[2] == doctest::Approx(0.0));

    // child is (0,0,5) - (0,0,0) = (0,0,5) in raw M2 space, parent-relative
    // -- zUpToYUp maps that to gltf (0,5,0).
    REQUIRE(childNode.translation.size() == 3);
    CHECK(childNode.translation[0] == doctest::Approx(0.0));
    CHECK(childNode.translation[1] == doctest::Approx(5.0));
    CHECK(childNode.translation[2] == doctest::Approx(0.0));

    REQUIRE(gm.materials.size() == 1);
    CHECK(gm.materials[0].alphaMode == "OPAQUE");
}

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("writeLeanGlb: a real fixture produces a glb the Khronos glTF-Validator accepts with "
          "zero errors" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);
    canon::Model canonModel = canon::assembleModel(model, batches, submeshes, triangleIndices);

    auto outPath = std::filesystem::temp_directory_path() / "husk-test-lean-validator.glb";
    std::filesystem::remove(outPath);
    writers::writeLeanGlb(canonModel, outPath);

    auto validation = husk::test::runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath.string() + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);
    // gltf_validator's own real summary line shape: "Errors: N, Warnings:
    // N, Infos: N, Hints: N" -- confirmed by running it directly against a
    // known-good .glb (the production writer's own output) before trusting
    // this substring, rather than guessing at a JSON key that turned out
    // not to exist in this tool's real -a output. Warnings aren't asserted
    // at zero: TEXCOORD_0/_1 "may be unused" is real and expected here --
    // this writer's own stated scope (canon::MaterialLayer::texture is
    // always KnownUnresolved, so no material ever references a UV set via
    // a real baseColorTexture) genuinely produces an unused-but-valid
    // accessor, not a defect.
    CHECK(validation.output.find("Errors: 0,") != std::string::npos);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("writeLeanGlb: a real fixture produces a glb the Khronos glTF-Validator accepts with "
          "zero errors" *
          doctest::skip(true)) {
    // see TEST_DESIGN.md#Conformance-gating
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("writeLeanGlb: Blender's own glTF importer reads a real fixture's lean output cleanly" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);
    canon::Model canonModel = canon::assembleModel(model, batches, submeshes, triangleIndices);

    auto outPath = std::filesystem::temp_directory_path() / "husk-test-lean-blender.glb";
    std::filesystem::remove(outPath);
    writers::writeLeanGlb(canonModel, outPath);

    auto blenderResult = husk::test::runCommand(
        std::string(HUSK_BLENDER) + " --background --factory-startup --python-exit-code 1 --python \"" +
        std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath.string() + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    auto parseProbeInt = [&](const std::string& key) {
        std::string marker = "HUSK_PROBE " + key + "=";
        auto pos = blenderResult.output.find(marker);
        REQUIRE(pos != std::string::npos);
        return std::stoi(blenderResult.output.substr(pos + marker.size()));
    };
    CHECK(parseProbeInt("armature_count") == 1);
    CHECK(parseProbeInt("bone_count") == static_cast<int>(canonModel.skeleton.joints.size()));
    CHECK(parseProbeInt("mesh_object_count") == 1);
    CHECK(static_cast<size_t>(parseProbeInt("total_vertex_count")) == canonModel.mesh.positions.size());

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("writeLeanGlb: Blender's own glTF importer reads a real fixture's lean output cleanly" *
          doctest::skip(true)) {
    // see TEST_DESIGN.md#Conformance-gating
}
#endif
