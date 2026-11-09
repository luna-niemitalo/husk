// Shared helpers for tests/test_writers_bundle*.cpp: reading back what a
// bundle writer wrote (schema-checked) and a small hand-built canon::Model.
// Anonymous namespace: each including TU gets its own private copy.
#pragma once

#include <doctest/doctest.h>

#include <bit>
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
#include "map_scene_input.hpp"
#include "uuid.hpp"
#include "wmo_canon_input.hpp"
#include "writers/bundle_writer.hpp"
#include "writers/texture_store.hpp"

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

// A texture a document uses by index, from its resources.textures.
const nlohmann::json& textureAt(const nlohmann::json& document, const nlohmann::json& index) {
    return document["resources"]["textures"][index.get<size_t>()];
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
    model.mesh.uvSets[0] = {{0, 0}, {1, 0}, {0, 1}};
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
