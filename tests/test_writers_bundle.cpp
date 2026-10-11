// Coverage for husk::writers::writeBundle (src/writers/bundle_writer.hpp) --
// the native glTF-free bundle format. Two tiers: synthetic small models for
// precise byte-level round-trip checks (built by hand, not via
// m2input::buildCanonModel, so the expected values are independent of that
// pipeline), and a real bloodelffemale.m2/.skin fixture (via
// m2input::buildCanonModel, same doctest::skip convention as
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

#include "bundle_schema.hpp"
#include "canon_model.hpp"
#include "m2_canon_input.hpp"
#include "m2.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"
#include "wmo_canon_input.hpp"
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

writers::Provenance testProvenance() { return writers::Provenance{"husk test", "test", {}}; }

// Every manifest a test reads is also checked against schema/, so the
// writer can't drift from the format's definition unnoticed.
nlohmann::json parseManifest(const fs::path& bundleDir) {
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(bundleDir);
    std::string report;
    for (const bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
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

canon::Joint makeJoint(int parent, canon::Vec3 pos, std::string name) {
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
    material.ref.id = canon::RecordIndex{0};
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
    model.primitiveMaterials.push_back(canon::Identity{canon::RecordIndex{0}});

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

    writers::writeBundle(model, dir, testProvenance());

    CHECK(fs::exists(dir / "manifest.json"));
    CHECK(fs::exists(dir / "mesh.bin"));
    CHECK(fs::exists(dir / "skeleton.bin"));
    CHECK(fs::exists(dir / "animation.bin"));

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["format"] == "husk-bundle");
    CHECK(manifest["kind"] == "model");
    CHECK(manifest["schema_version"] == "1.0.0");
    CHECK(manifest["space"]["length_unit"] == "yard");
    CHECK(manifest["provenance"]["producer"] == "husk test");
    CHECK(manifest["provenance"].contains("exported_at"));
    CHECK(manifest["definition"]["placement_sets"].empty());
    CHECK(manifest["selection"]["placement_sets"].empty());
    CHECK(manifest["references"].empty());
}

TEST_CASE("writeBundle: a Resolved TextureRef carrying a real payload writes a real "
          "textures/<name>.<ext> file and records a real uri (AUDIT.md §7.4); a Resolved "
          "TextureRef with no payload writes no uri and no file, and two layers sharing the "
          "same resolved name dedupe to one written file") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].layers.size() == 1);

    std::vector<uint8_t> pngBytes = {0x01, 0x02, 0x03, 0x04};  // arbitrary bytes -- this writer never decodes them, just stores them verbatim

    canon::TextureRef resolvedWithPayload;
    resolvedWithPayload.state = canon::TextureRef::State::Resolved;
    resolvedWithPayload.resolved.id = canon::FileDataId{99};
    resolvedWithPayload.resolved.name = "shared_texture";
    canon::TextureRef::Payload payload;
    payload.bytes = pngBytes;
    payload.encoding = canon::TextureEncoding::Png;
    resolvedWithPayload.payload = payload;
    model.materials[0].layers[0].texture = resolvedWithPayload;

    // A second layer on a second material, same resolved name -- exercises
    // the writtenTextures dedup (one file written, not two).
    canon::Material material2 = model.materials[0];
    material2.ref.id = canon::RecordIndex{1};
    model.materials.push_back(material2);
    model.primitiveMaterials.push_back(canon::Identity{canon::RecordIndex{1}});
    model.mesh.primitives.push_back(model.mesh.primitives[0]);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-texture-payload";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());

    fs::path textureFile = dir / "textures" / "shared_texture.png";
    REQUIRE(fs::exists(textureFile));
    std::vector<uint8_t> writtenBytes = readFile(textureFile.string());
    CHECK(writtenBytes == pngBytes);

    nlohmann::json manifest = parseManifest(dir);
    auto& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);
    for (auto& mat : materials) {
        auto& layer0 = mat["layers"][0];
        CHECK(layer0["texture"]["state"] == "resolved");
        CHECK(layer0["texture"]["payload"]["uri"] == "textures/shared_texture.png");
    }

    // Only one real file on disk despite two referencing layers.
    size_t textureFileCount = 0;
    for (const auto& entry : fs::directory_iterator(dir / "textures")) {
        (void)entry;
        ++textureFileCount;
    }
    CHECK(textureFileCount == 1);

    // Resolved but no payload -- no uri, no textures/ directory created at all.
    fs::path dirNoPayload = fs::temp_directory_path() / "husk-test-bundle-texture-no-payload";
    fs::remove_all(dirNoPayload);
    canon::Model modelNoPayload = buildSyntheticModel();
    canon::TextureRef resolvedNoPayload;
    resolvedNoPayload.state = canon::TextureRef::State::Resolved;
    resolvedNoPayload.resolved.id = canon::FileDataId{99};
    resolvedNoPayload.resolved.name = "unfetched_texture";
    modelNoPayload.materials[0].layers[0].texture = resolvedNoPayload;
    writers::writeBundle(modelNoPayload, dirNoPayload, testProvenance());

    nlohmann::json manifestNoPayload = parseManifest(dirNoPayload);
    auto& layer0NoPayload = manifestNoPayload["resources"]["materials"][0]["layers"][0];
    CHECK(layer0NoPayload["texture"]["state"] == "resolved");
    CHECK_FALSE(layer0NoPayload["texture"].contains("payload"));
    CHECK_FALSE(fs::exists(dirNoPayload / "textures"));
}

TEST_CASE("writeBundle: rawPayload (DDS-housed source blocks) wins over payload (PNG) when both are "
          "present, and a payload-only TextureRef still falls back to writing PNG (BUNDLE_FORMAT.md's "
          "'Texture encoding -- settled')") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].layers.size() == 1);

    // Deliberately not a real DDS byte layout -- this writer never parses
    // rawPayload's bytes, just stores them verbatim under a .dds name, same
    // "arbitrary bytes" convention the payload-only test above already uses.
    std::vector<uint8_t> ddsBytes = {0x44, 0x44, 0x53, 0x20, 0xAA, 0xBB};
    std::vector<uint8_t> pngBytes = {0x01, 0x02, 0x03, 0x04};

    canon::TextureRef bothPayloads;
    bothPayloads.state = canon::TextureRef::State::Resolved;
    bothPayloads.resolved.id = canon::FileDataId{101};
    bothPayloads.resolved.name = "both_variants";
    canon::TextureRef::Payload png;
    png.bytes = pngBytes;
    png.encoding = canon::TextureEncoding::Png;
    bothPayloads.payload = png;
    canon::TextureRef::Payload dds;
    dds.bytes = ddsBytes;
    dds.encoding = canon::TextureEncoding::Bc3;
    bothPayloads.rawPayload = dds;
    model.materials[0].layers[0].texture = bothPayloads;

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());

    // rawPayload wins: a real .dds file with rawPayload's own bytes, not
    // payload's PNG bytes, and no .png file written at all for this texture.
    fs::path ddsFile = dir / "textures" / "both_variants.dds";
    REQUIRE(fs::exists(ddsFile));
    CHECK(readFile(ddsFile.string()) == ddsBytes);
    CHECK_FALSE(fs::exists(dir / "textures" / "both_variants.png"));

    nlohmann::json manifest = parseManifest(dir);
    auto& layer0 = manifest["resources"]["materials"][0]["layers"][0];
    CHECK(layer0["texture"]["state"] == "resolved");
    CHECK(layer0["texture"]["payload"]["uri"] == "textures/both_variants.dds");

    // payload-only (no rawPayload) -- falls back to writing the .png
    // variant, same as before rawPayload existed at all (e.g. a source that
    // was never a compressed BLP, or one where DDS extraction declined).
    fs::path dirPngOnly = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload-fallback";
    fs::remove_all(dirPngOnly);
    canon::Model modelPngOnly = buildSyntheticModel();
    canon::TextureRef pngOnly;
    pngOnly.state = canon::TextureRef::State::Resolved;
    pngOnly.resolved.id = canon::FileDataId{102};
    pngOnly.resolved.name = "png_only";
    pngOnly.payload = png;
    modelPngOnly.materials[0].layers[0].texture = pngOnly;
    writers::writeBundle(modelPngOnly, dirPngOnly, testProvenance());

    fs::path pngFile = dirPngOnly / "textures" / "png_only.png";
    REQUIRE(fs::exists(pngFile));
    CHECK(readFile(pngFile.string()) == pngBytes);
    CHECK_FALSE(fs::exists(dirPngOnly / "textures" / "png_only.dds"));

    nlohmann::json manifestPngOnly = parseManifest(dirPngOnly);
    CHECK(manifestPngOnly["resources"]["materials"][0]["layers"][0]["texture"]["payload"]["uri"] ==
          "textures/png_only.png");

    // Neither present -- no uri, no textures/ directory, same as the
    // existing no-payload case above.
    fs::path dirNeither = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload-neither";
    fs::remove_all(dirNeither);
    canon::Model modelNeither = buildSyntheticModel();
    canon::TextureRef neither;
    neither.state = canon::TextureRef::State::Resolved;
    neither.resolved.id = canon::FileDataId{103};
    neither.resolved.name = "neither_variant";
    modelNeither.materials[0].layers[0].texture = neither;
    writers::writeBundle(modelNeither, dirNeither, testProvenance());

    nlohmann::json manifestNeither = parseManifest(dirNeither);
    CHECK_FALSE(manifestNeither["resources"]["materials"][0]["layers"][0]["texture"].contains("uri"));
    CHECK_FALSE(fs::exists(dirNeither / "textures"));
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

TEST_CASE("writeBundle: uv1/joints0/weights0 omitted for an unskinned, single-UV-set model") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-omissions";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];

    CHECK(!mesh.contains("uv1"));
    CHECK(!mesh.contains("joints0"));
    CHECK(!mesh.contains("weights0"));
}

TEST_CASE("writeBundle: uv1/joints0/weights0 present when the canon::Mesh has them") {
    canon::Model model = buildSyntheticModel();
    model.mesh.uv1 = std::vector<canon::Vec2>{{0, 1}, {1, 1}, {1, 0}};
    canon::Mesh::Skinning s;
    s.joints = {0, 1, 0, 0};
    s.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    model.mesh.skinning = {s, s, s};

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-skinned";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];

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

TEST_CASE("writeBundle: scene -- attachments, events, lights and emitters with inline and lifetime curves") {
    canon::Model model = buildSyntheticModel();
    canon::Attachment attachment;
    attachment.pointId = 11;
    attachment.bone = canon::boneRef(1);
    attachment.position = {1.0f, 2.0f, 3.0f};
    canon::ScalarCurve animate;
    animate.sequence = canon::SequenceRef::globalSequence(2);
    animate.interpolation = canon::Interpolation::Step;
    animate.keyframes = {{0.0f, 1.0f}};
    attachment.animateAttached.push_back(animate);
    model.scene.attachments.push_back(attachment);
    model.scene.events.push_back({"$DTH", 7, canon::boneRef(0), {}});
    model.scene.lights.push_back(canon::Light{});
    canon::ParticleEmitter particle;
    particle.bone = canon::boneRef(0);
    particle.textures.push_back(canon::TextureRef{});
    particle.headCell.keyframes = {{0, 4}, {32767, 9}};
    model.scene.particles.push_back(particle);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-scene";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& scene = manifest["resources"]["scene"];

    REQUIRE(scene["attachments"].size() == 1);
    const auto& a = scene["attachments"][0];
    CHECK(a["point_id"] == 11);
    CHECK(a["bone"]["id"]["value"] == 1);
    CHECK(a["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    REQUIRE(a["animate_attached"].size() == 1);
    CHECK(a["animate_attached"][0]["sequence_kind"] == "global_sequence");
    CHECK(a["animate_attached"][0]["sequence_index"] == 2);
    CHECK(a["animate_attached"][0]["interpolation"] == "step");

    CHECK(scene["events"][0]["identifier"] == "$DTH");
    CHECK_FALSE(scene["animated_lights"][0].contains("bone"));
    CHECK(scene["animated_lights"][0]["kind"] == "directional");
    CHECK(scene["animated_lights"][0]["source"]["type"] == 0);
    CHECK(scene["ribbon_emitters"].empty());
    const auto& p = scene["particle_emitters"][0];
    CHECK(p["textures"][0]["state"] == "known_unresolved");
    CHECK(p["head_cell"] == nlohmann::json::array({{0, 4}, {32767, 9}}));
    CHECK(p["emission_rate"].empty());
    CHECK_FALSE(p.contains("particle_model"));
}

TEST_CASE("writeBundle: local lights write their optional blocks only when present") {
    canon::Model model = buildSyntheticModel();
    canon::LocalLight spot;
    spot.ref = canon::Ref{canon::RecordIndex{0}, "mnld_0", canon::NameSource::Synthesized};
    spot.kind = canon::LocalLight::Kind::Spot;
    spot.position = {1, 2, 3};
    spot.eulerRotation = canon::Vec3{-3, 0, 0};
    spot.color = {1, 0.5f, 0};
    spot.intensity = 2;
    spot.attenuationEnd = 9;
    spot.cone = canon::LocalLight::Cone{5, 0.5f, 1.5f};
    spot.flicker = canon::LocalLight::Flicker{2, 0.25f, 3};
    spot.castsShadows = true;
    spot.cookie = canon::Ref{canon::FileDataId{9001}, "cookie_window", canon::NameSource::Listfile};
    spot.placementSet = canon::Ref{canon::RecordIndex{1}, "Set_B", canon::NameSource::WmoEmbedded};
    spot.sourceFlags = 0x3;
    canon::LocalLight omni;
    omni.ref = canon::Ref{canon::RecordIndex{0}, "molt_0", canon::NameSource::Synthesized};
    spot.origin = {"MNLD", 4};
    model.scene.localLights = {spot, omni};
    model.placementSets.resize(2);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-local-lights";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& lights = manifest["resources"]["scene"]["static_lights"];

    REQUIRE(lights.size() == 2);
    const auto& s = lights[0];
    CHECK(s["ref"]["name"] == "mnld_0");
    CHECK(s["kind"] == "spot");
    CHECK(s["euler_rotation"] == nlohmann::json::array({-3.0, 0.0, 0.0}));
    CHECK_FALSE(s.contains("rotation"));
    CHECK(s["color"] == nlohmann::json::array({1.0, 0.5, 0.0}));
    CHECK(s["cone"]["outer_angle"] == 1.5);
    CHECK(s["flicker"]["mode"] == 2);
    CHECK(s["casts_shadows"] == true);
    CHECK(s["cookie"]["id"]["value"] == 9001);
    CHECK(s["placement_set"] == 1);
    CHECK(s["source"]["flags"] == "0x3");
    CHECK(s["source"]["record"]["table"] == "MNLD");
    CHECK(s["source"]["record"]["index"] == 4);
    const auto& o = lights[1];
    CHECK(o["kind"] == "point");
    for (const char* key : {"rotation", "euler_rotation", "gradient", "cone", "flicker", "cookie", "placement_set"}) {
        CAPTURE(key);
        CHECK_FALSE(o.contains(key));
    }
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

TEST_CASE("writeBundle: a particulate instance writes its emitter images into the set, with one style block per emitter") {
    canon::Model model = buildSyntheticModel();
    wmo::ParticulateVolume v;
    v.position = {1, 2, 3};
    v.extent = {7, 8, 9};
    v.particleCount = 500;
    wmo::ParticulateEmitter dust;
    dust.textureFileDataId = 101;
    dust.blendMode = 3;
    dust.color = {1, 0.5f, 0, 1};
    dust.tilesX = 4;
    dust.tilesY = 4;
    dust.frames = 16;
    wmo::ParticulateEmitter odd = dust;
    odd.textureFileDataId = 555;
    odd.blendMode = 7;
    v.emitters = {dust, odd};
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    resolved.resolved = canon::Ref{canon::FileDataId{101}, "dust1", canon::NameSource::Listfile};
    resolved.payload = canon::TextureRef::Payload{{1, 2, 3}, canon::TextureEncoding::Png};
    canon::PlacedInstance instance = wmoinput::particulateInstance(v, {{101, resolved}}, {"MPVD", 0});
    instance.id = 9;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {instance};
    model.placementSets = {set};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-particulate";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);  // validates the set against the effect too
    CHECK_FALSE(manifest["resources"]["scene"].contains("ambient_particle_volumes"));
    CHECK_FALSE(fs::exists(dir / "particulate.bin"));

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    const auto& written = setDoc["instances"][0];
    CHECK(written["inputs"]["size"] == nlohmann::json::array({7.0, 8.0, 9.0}));
    const auto& texture = written["inputs"]["emitters"][0]["texture"];
    CHECK(texture["payload"]["uri"] == "../textures/dust1.png");
    CHECK(fs::exists(dir / "textures" / "dust1.png"));
    CHECK(written["inputs"]["emitters"][1]["texture"]["state"] == "known_unresolved");
    CHECK(written["style"]["volume"]["particulate"]["max_particles"] == 500);
    REQUIRE(written["style"]["emitter"].size() == 2);
    CHECK(written["style"]["emitter"][0]["draw"]["blend"] == "add");
    CHECK_FALSE(written["style"]["emitter"][1]["draw"].contains("blend"));
}

TEST_CASE("writeBundle: an ambient box instance writes list and record inputs and its colours as style") {
    canon::Model model = buildSyntheticModel();
    canon::PlacedInstance box;
    box.id = 3;
    box.asset = canon::Ref{canon::None{}, "classic_ambient_box", canon::NameSource::Synthesized};
    canon::EffectPlacement effect;
    std::vector<canon::InputValue> planes;
    for (int k = 0; k < 6; ++k) {
        planes.push_back({std::vector<canon::NamedInput>{{"normal", {canon::Vec3{0, 0, k % 2 ? 1.0f : -1.0f}}},
                                                         {"distance", {2.0}}}});
    }
    effect.inputs = {{"planes", {planes}}};
    canon::StyleBlock style;
    style.ambientColors = std::array<std::array<float, 4>, 3>{{{0.2f, 0.4f, 0.6f, 1.0f}, {}, {}}};
    effect.style = {{"ambient", {style}}};
    effect.unresolved = {{"end", canon::UnresolvedField::Kind::Scalar, 1.5, "region.edge"}};
    effect.origin = {"MBVD", 0};
    box.effect = effect;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {box};
    model.placementSets = {set};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-ambient";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    parseManifest(dir);  // validates the set's inputs against the effect too

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    const auto& instance = setDoc["instances"][0];
    REQUIRE(instance["inputs"]["planes"].size() == 6);
    CHECK(instance["inputs"]["planes"][1]["normal"] == nlohmann::json::array({0.0, 0.0, 1.0}));
    CHECK(instance["inputs"]["planes"][1]["distance"] == 2.0);
    CHECK(instance["style"]["ambient"]["ambient_color"]["colors"][0][1].get<double>() == doctest::Approx(0.4));
    CHECK(instance["unresolved"]["fields"][0]["candidate_role"] == "region.edge");
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

TEST_CASE("writeBundle: fog instances in an owned set, the effects they place, and the parts that name them") {
    canon::Model model = buildSyntheticModel();
    canon::PlacedInstance fog;
    fog.id = 7;
    fog.asset = canon::Ref{canon::None{}, "classic_local_fog", canon::NameSource::Synthesized};
    fog.translation = {1, 2, 3};
    fog.flags = 0x1000;
    canon::EffectPlacement effect;
    effect.inputs = {{"inner_radius", 5.0}, {"outer_radius", 20.0}};
    canon::StyleBlock style;
    style.distanceFog = canon::DistanceFog{canon::LinearFog{25, 100, {0.2f, 0.4f, 0.6f}}};
    style.underwater = canon::DistanceFog{canon::LinearFog{-25, 50, {0, 0, 1}}};
    effect.style = {{"fog", {style}}};
    effect.unresolved = {{"flags", canon::UnresolvedField::Kind::Bits, uint32_t{0x1000}, std::nullopt}};
    effect.origin = {"MFOG", 1};
    fog.effect = effect;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {fog};
    model.placementSets = {set};
    model.defaultFog = canon::InstanceRef{0, 7};
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{0}, "hall", canon::NameSource::WmoEmbedded}, 0, {}, {},
                                {canon::InstanceRef{0, 7}}});
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{1}, "", canon::NameSource::None}, 0, {}, {}, {}});
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-fogs";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    CHECK(manifest["resources"]["scene"]["default_fog"] == nlohmann::json({{"set", 0}, {"id", 7}}));
    const auto& parts = manifest["resources"]["geometry"]["mesh"]["parts"];
    CHECK(parts[0]["fog_volumes"] == nlohmann::json::array({{{"set", 0}, {"id", 7}}}));
    CHECK_FALSE(parts[1].contains("fog_volumes"));

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    CHECK(setDoc["references"][0]["kind"] == "effect");
    CHECK(setDoc["references"][0]["asset"]["uri"] == "../effects/classic_local_fog.json");
    const auto& instance = setDoc["instances"][0];
    CHECK(instance["translation"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(instance["inputs"]["outer_radius"] == 20.0);
    CHECK(instance["style"]["fog"]["distance_fog"]["linear"]["start"] == 25.0);
    CHECK(instance["style"]["fog"]["looks"]["underwater"]["linear"]["end"] == 50.0);
    CHECK(instance["unresolved"]["fields"][0]["value"] == "0x1000");
    CHECK(instance["source"]["record"]["table"] == "MFOG");
    CHECK_FALSE(instance.contains("selection"));
    CHECK(fs::exists(dir / "effects" / "classic_local_fog.json"));

    // parseManifest checked the documents; the placeholder effect is a warning.
    std::vector<bundle_schema::Problem> warnings;
    bundle_schema::validateDocument(dir, nullptr, &warnings);
    CHECK(warnings.size() == 1);
}

TEST_CASE("writeWorldBundle: tile grid, fog and particulate volumes, raw records sliced into world.bin") {
    canon::World world;
    world.subject = canon::Ref{canon::FileDataId{775971}, "azeroth", canon::NameSource::Listfile};
    world.cells.push_back({32, 49, canon::Ref{canon::FileDataId{778032}, "azeroth_32_49", canon::NameSource::Listfile},
                           {-533.3f, 0, -10}, {0, 533.3f, 90}});
    world.cells.push_back({33, 49, canon::Ref{canon::None{}, "azeroth_33_49", canon::NameSource::Listfile}, {}, {}});
    canon::FogVolume fog;
    fog.ref = canon::Ref{canon::RecordIndex{0}, "fog_1659", canon::NameSource::Synthesized};
    fog.origin = {"VFOG", 0};
    fog.id = 1659;
    fog.flags = 0x11;
    fog.position = {1, 2, 3};
    fog.animationPeriods = {3000, 0, 0, 0};
    fog.palette = canon::FogVolume::Palette{{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, 2.0f};
    fog.extra = canon::FogVolume::Extra{true, {600, 600, 200}, {0.4f, -15}};
    fog.sourceExtra.assign(96, 0);
    fog.sourceExtra2.assign(176, 0);
    fog.sourceExtra2[0x4C] = 0x7B;
    canon::FogVolume bare;
    bare.ref = canon::Ref{canon::RecordIndex{1}, "fog_1660", canon::NameSource::Synthesized};
    bare.origin = {"VFOG", 1};
    world.scene.fogs = {fog, bare};
    canon::MapParticulateVolume v;
    v.volume.ref = canon::Ref{canon::RecordIndex{0}, "particles_185", canon::NameSource::Synthesized};
    v.volume.origin = {"PVMI", 0};
    v.volume.sourceRecord.assign(4272, 0);
    v.id = 185;
    v.boundsMax = {5, 5, 10};
    v.points = {{canon::Vec2{1, 0}, -0.0f, 2717.7f}};
    v.boxes = {{{-1, -1, 0}, {1, 1, 1}, {}, 1}, {{-2, -2, 0}, {2, 2, 2}, {0}, 1}};
    world.scene.particulateVolumes = {v};

    fs::path root = fs::temp_directory_path() / "husk-test-world";
    fs::remove_all(root);
    fs::create_directories(root / "azeroth_32_49.bundle");
    std::ofstream(root / "azeroth_32_49.bundle" / "manifest.json") << "{}";
    fs::path dir = root / "azeroth.world.bundle";
    writers::Provenance provenance{"husk test", "wdt", {{"_fogs.wdt", "fog volumes", true, ""}}};
    writers::writeWorldBundle(world, dir, provenance, {"../azeroth_32_49.bundle/manifest.json", ""});
    nlohmann::json manifest = parseManifest(dir);

    CHECK(manifest["kind"] == "world");
    CHECK(manifest["subject"]["name"] == "azeroth");
    const auto& grid = manifest["grid"];
    CHECK(grid["cell_size"].get<double>() == doctest::Approx(533.3333));
    CHECK(grid["origin"][0].get<double>() == doctest::Approx(32 * 533.3333));
    CHECK(grid["axis_i"] == nlohmann::json::array({0.0, -1.0, 0.0}));
    CHECK(grid["axis_j"] == nlohmann::json::array({-1.0, 0.0, 0.0}));
    REQUIRE(grid["cells"].size() == 2);
    CHECK(grid["cells"][0]["i"] == 32);
    CHECK(grid["cells"][0]["j"] == 49);
    CHECK(grid["cells"][0]["bounds_max"][2] == 90.0);
    const auto& tile = manifest["references"][grid["cells"][0]["tile"].get<size_t>()];
    CHECK(tile["kind"] == "model");
    CHECK(tile["asset"]["uri"] == "../azeroth_32_49.bundle/manifest.json");
    CHECK_FALSE(manifest["references"][1]["asset"].contains("uri"));
    CHECK(manifest["placements"].empty());

    const auto& s = manifest["scene"];
    REQUIRE(s["fog_volumes"].size() == 2);
    const auto& f = s["fog_volumes"][0];
    CHECK(f["id"] == 1659);
    CHECK(f["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(f["animation_periods"] == nlohmann::json::array({3000, 0, 0, 0}));
    CHECK(f["shape"]["id"]["kind"] == "none");
    CHECK(f["source"]["flags"] == "0x11");
    CHECK(f["source"]["record"]["table"] == "VFOG");
    const auto& u = f["unresolved"];
    CHECK(u["palette"]["mode"] == 2.0);
    CHECK(u["extension"]["enabled"] == true);
    CHECK(u["extension"]["extent"] == nlohmann::json::array({600.0, 600.0, 200.0}));
    REQUIRE(u["extension"]["parameters"].size() == 13);
    CHECK(u["extension"]["parameters"][1] == -15.0);
    CHECK(u["extension_record"]["count"] == 96);
    CHECK(u["palette_record"]["file"] == "world.bin");
    for (const char* key : {"palette", "extension", "extension_record", "palette_record"}) {
        CAPTURE(key);
        CHECK_FALSE(s["fog_volumes"][1]["unresolved"].contains(key));
    }
    const auto& p = s["ambient_particle_volumes"][0];
    CHECK(p["volume"]["ref"]["name"] == "particles_185");
    CHECK(p["id"] == 185);
    CHECK(p["bounds_max"] == nlohmann::json::array({5.0, 5.0, 10.0}));
    CHECK(p["unresolved"]["points"][0]["direction"] == nlohmann::json::array({1.0, 0.0}));
    CHECK(p["boxes"][1]["children"] == nlohmann::json::array({0}));
    CHECK(p["boxes"][1]["source"]["complete"] == 1);

    std::vector<uint8_t> bin = readFile((dir / "world.bin").string());
    size_t at = u["palette_record"]["byte_offset"];
    CHECK(bin.at(at + 0x4C) == 0x7B);

    CHECK_THROWS(writers::writeWorldBundle(world, root / "mismatch.bundle", provenance, {""}));
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
    CHECK(material["layers"][0]["uv_scroll"] == nlohmann::json::array({0.5, 0.0}));

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
    // Real, expected dedup on this real fixture -- a genuinely FEWER
    // materials than primitives, not the old 1:1 assumption. If this ever
    // fails, either the fixture changed or dedup itself broke -- either
    // way worth knowing, not silently tolerated as "well it's <= now".
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

TEST_CASE("writeBundle: a material's framebuffer blend is written by name, and omitted when unknown") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    model.materials[0].framebufferBlend = canon::FramebufferBlend::AlphaKey;
    canon::Material unknown = model.materials[0];
    unknown.ref.id = canon::RecordIndex{1};
    unknown.framebufferBlend.reset();
    model.materials.push_back(unknown);
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-framebuffer-blend";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());

    nlohmann::json manifest = parseManifest(dir);
    const nlohmann::json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);
    CHECK(materials[0]["framebuffer_blend"] == "alpha_key");
    CHECK_FALSE(materials[1].contains("framebuffer_blend"));
}
