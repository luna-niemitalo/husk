// Coverage for husk::writers::writeBundle (src/writers/bundle_writer.hpp) --
// the native glTF-free bundle format. Two tiers: synthetic small models for
// precise byte-level round-trip checks (built by hand, not via
// canon::assembleModel, so the expected values are independent of that
// pipeline), and a real bloodelffemale.m2/.skin fixture (via
// canon::assembleModel, same doctest::skip convention as
// tests/test_canon_model.cpp) for the structural checks that only make
// sense against real, non-trivial data.
//
// JSON validity is checked with a real parser (nlohmann, already on the
// include path via tinygltf -- see tests/test_cli_info_json.cpp's own doc
// comment for why a real parser is used instead of a hand-rolled balance
// check), not a substring/balance check.

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "canon_model.hpp"
#include "m2.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"
#include "writers/bundle_writer.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string readTextFile(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

nlohmann::json parseManifest(const fs::path& bundleDir) {
    std::string text = readTextFile(bundleDir / "manifest.json");
    auto parsed = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    REQUIRE(!parsed.is_discarded());
    return parsed;
}

// Every object anywhere in the manifest that looks like a BufferSlice
// (carries "file"/"byte_offset"/"byte_length" together) -- collected by a
// generic recursive walk so this doesn't need to know the manifest's exact
// shape to check the one structural invariant every slice must satisfy.
void collectBufferSlices(const nlohmann::json& node, std::vector<nlohmann::json>& out) {
    if (node.is_object()) {
        if (node.contains("file") && node.contains("byte_offset") && node.contains("byte_length")) {
            out.push_back(node);
        }
        for (auto& [key, value] : node.items()) {
            collectBufferSlices(value, out);
        }
    } else if (node.is_array()) {
        for (const auto& item : node) {
            collectBufferSlices(item, out);
        }
    }
}

canon::Joint makeJoint(int parent, m2::Vec3 pos, std::string name) {
    canon::Joint j;
    j.parent = parent;
    j.globalPosition = pos;
    j.ref.name = std::move(name);
    j.ref.id = canon::RecordIndex{0};
    j.ref.source = canon::NameSource::Synthesized;
    j.billboard = canon::BillboardMode::None;
    j.structuralLabel = "label";
    return j;
}

// A small, fully hand-built canon::Model: 2 joints, 3 vertices, no
// skinning, no uv1, one geoset primitive, one material with one layer, and
// one animation clip where only joint 0 has curve data (joint 1 is nullopt
// -- the sparse case).
canon::Model buildSyntheticModel() {
    canon::Model model;

    model.skeleton.joints.push_back(makeJoint(-1, {0.0f, 0.0f, 0.0f}, "root"));
    model.skeleton.joints.push_back(makeJoint(0, {1.0f, 2.0f, 3.0f}, "child"));

    model.mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    model.mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    model.mesh.uv0 = {{0, 0}, {1, 0}, {0, 1}};
    // uv1 left as nullopt; skinning left empty -- the two omission cases.
    model.mesh.indices = {0, 1, 2};

    canon::PrimitiveGeoset prim;
    prim.geoset = canon::Geoset::fromRawId(203);  // group 2, variant 3
    prim.indexStart = 0;
    prim.indexCount = 3;
    model.mesh.primitives.push_back(prim);

    canon::Material material;
    canon::MaterialLayer layer;
    layer.identity.name = "layer0";
    layer.identity.source = canon::NameSource::Synthesized;
    layer.role = canon::KnownRole::Diffuse;
    layer.uv = canon::UvSetIndex{0};
    layer.texture.state = canon::TextureRef::State::KnownUnresolved;
    layer.texture.unresolvedReason = "no bytes available";
    layer.blendIntoPrevious = canon::BlendOp::Modulate;
    material.layers.push_back(layer);
    model.materials.push_back(material);

    canon::AnimationClip clip;
    clip.sequence = canon::SequenceRef::sequence(5);
    canon::BoneAnimationCurves curves0;
    curves0.translation.sequence = clip.sequence;
    curves0.translation.interpolation = canon::Interpolation::Linear;
    curves0.translation.keyframes = {{0.0f, {0.0f, 0.0f, 0.0f}}, {1.0f, {1.0f, 0.0f, 0.0f}}};
    curves0.rotation.sequence = clip.sequence;
    curves0.rotation.keyframes = {{0.0f, {0.0f, 0.0f, 0.0f, 1.0f}}};
    curves0.scale.sequence = clip.sequence;
    curves0.scale.keyframes = {{0.0f, {1.0f, 1.0f, 1.0f}}};
    clip.boneCurves = {curves0, std::nullopt};
    model.animations.push_back(clip);

    return model;
}

}  // namespace

TEST_CASE("writeBundle: synthetic model -- manifest.json is valid JSON, files exist") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-basic";
    fs::remove_all(dir);

    writers::writeBundle(model, dir);

    CHECK(fs::exists(dir / "manifest.json"));
    CHECK(fs::exists(dir / "mesh.bin"));
    CHECK(fs::exists(dir / "skeleton.bin"));
    CHECK(fs::exists(dir / "animation.bin"));

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["schema_version"] == "0.1.0");
    CHECK(manifest["endianness"] == "little");
    CHECK(manifest.contains("exported_at"));
    CHECK(manifest.contains("producer"));
}

TEST_CASE("writeBundle: every BufferSlice's byte range fits inside its named file") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-slices";
    fs::remove_all(dir);

    writers::writeBundle(model, dir);
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

    writers::writeBundle(model, dir);
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

    const auto& positionsSlice = manifest["resources"]["mesh"]["positions"];
    CHECK(positionsSlice["component_type"] == "f32");
    CHECK(positionsSlice["component_count"] == 3);
    CHECK(positionsSlice["count"] == 3);
    CHECK(positionsSlice["semantic"] == "POSITION");

    std::vector<uint8_t> posBytes = readSlice(positionsSlice);
    REQUIRE(posBytes.size() == model.mesh.positions.size() * sizeof(m2::Vec3));
    std::vector<m2::Vec3> readBack(model.mesh.positions.size());
    std::memcpy(readBack.data(), posBytes.data(), posBytes.size());
    for (size_t i = 0; i < readBack.size(); ++i) {
        CHECK(readBack[i].x == doctest::Approx(model.mesh.positions[i].x));
        CHECK(readBack[i].y == doctest::Approx(model.mesh.positions[i].y));
        CHECK(readBack[i].z == doctest::Approx(model.mesh.positions[i].z));
    }

    const auto& indicesSlice = manifest["resources"]["mesh"]["indices"];
    CHECK(indicesSlice["component_type"] == "u32");
    std::vector<uint8_t> idxBytes = readSlice(indicesSlice);
    REQUIRE(idxBytes.size() == model.mesh.indices.size() * sizeof(uint32_t));
    std::vector<uint32_t> readIndices(model.mesh.indices.size());
    std::memcpy(readIndices.data(), idxBytes.data(), idxBytes.size());
    CHECK(readIndices == model.mesh.indices);

    // bind_translation is PARENT-RELATIVE (writers::localBindTranslation),
    // not raw globalPosition -- joint 1's expected value is (1,2,3) - (0,0,0).
    const auto& bindSlice = manifest["resources"]["skeleton"]["bind_translation"];
    std::vector<uint8_t> bindBytes = readSlice(bindSlice);
    std::vector<m2::Vec3> bindTranslations(2);
    std::memcpy(bindTranslations.data(), bindBytes.data(), bindBytes.size());
    CHECK(bindTranslations[0].x == doctest::Approx(0.0f));
    CHECK(bindTranslations[1].x == doctest::Approx(1.0f));
    CHECK(bindTranslations[1].y == doctest::Approx(2.0f));
    CHECK(bindTranslations[1].z == doctest::Approx(3.0f));

    const auto& parentsSlice = manifest["resources"]["skeleton"]["parents"];
    CHECK(parentsSlice["component_type"] == "i32");
    std::vector<uint8_t> parentBytes = readSlice(parentsSlice);
    std::vector<int32_t> parents(2);
    std::memcpy(parents.data(), parentBytes.data(), parentBytes.size());
    CHECK(parents[0] == -1);
    CHECK(parents[1] == 0);
}

TEST_CASE("writeBundle: uv1/joints0/weights0 omitted for an unskinned, single-UV-set model") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-omissions";
    fs::remove_all(dir);

    writers::writeBundle(model, dir);
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["mesh"];

    CHECK(!mesh.contains("uv1"));
    CHECK(!mesh.contains("joints0"));
    CHECK(!mesh.contains("weights0"));
}

TEST_CASE("writeBundle: uv1/joints0/weights0 present when the canon::Mesh has them") {
    canon::Model model = buildSyntheticModel();
    model.mesh.uv1 = std::vector<m2::Vec2>{{0, 1}, {1, 1}, {1, 0}};
    canon::Mesh::Skinning s;
    s.joints = {0, 1, 0, 0};
    s.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    model.mesh.skinning = {s, s, s};

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-skinned";
    fs::remove_all(dir);
    writers::writeBundle(model, dir);
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["mesh"];

    REQUIRE(mesh.contains("uv1"));
    CHECK(mesh["uv1"]["semantic"] == "TEXCOORD_1");
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

    writers::writeBundle(model, dir);
    nlohmann::json manifest = parseManifest(dir);

    const auto& animation = manifest["resources"]["animation"];
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

TEST_CASE(
    "writeBundle: real fixture -- primitives[i].material_index == i for every primitive" *
    doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model m2model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    canon::Model model = canon::assembleModel(m2model, batches, submeshes, triangleIndices);
    REQUIRE(!model.mesh.primitives.empty());
    REQUIRE(model.materials.size() == model.mesh.primitives.size());

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-real-fixture";
    fs::remove_all(dir);
    writers::writeBundle(model, dir);
    nlohmann::json manifest = parseManifest(dir);

    const auto& primitives = manifest["resources"]["mesh"]["primitives"];
    REQUIRE(primitives.size() == model.mesh.primitives.size());
    for (size_t i = 0; i < primitives.size(); ++i) {
        CHECK(primitives[i]["material_index"] == i);
    }

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
