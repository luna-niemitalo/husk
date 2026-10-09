// canon_diff.hpp's own comparators, exercised with small hand-built
// canon::/gltf:: structs -- the CLI-tier smoke test
// (tests/test_cli_export_canon.cpp) covers the real-fixture end-to-end
// path; this file covers the comparison logic itself in isolation, both
// the matching case (no false positives on genuinely converged data) and
// the deviation case (a real, injected mismatch is actually caught).

#include <doctest/doctest.h>

#include "canon_diff.hpp"

using namespace husk;

namespace {

// commands::toGltf only overloads m2::Vec3, not canon::Vec3 (a distinct
// type, canon_primitives.hpp) -- same reasoning canon_diff.cpp's own local
// toGltf gives: wraps the same real, shared gltf_math.hpp conversion
// rather than adding a canon::-accepting overload to the legacy header.
gltf::Vec3 toGltf(const canon::Vec3& v) { return gltf::zUpToYUp({v.x, v.y, v.z}); }

// A tiny two-vertex, one-primitive canon::Mesh plus its exact legacy
// (Y-up, toGltf-converted) counterpart -- the smallest fixture that
// exercises positions/normals/uv0/skinning/primitives all at once.
canon::Mesh makeCanonMesh() {
    canon::Mesh m;
    m.positions = {{1, 2, 3}, {4, 5, 6}};
    m.normals = {{0, 0, 1}, {0, 1, 0}};
    m.uv0 = {{0.1f, 0.2f}, {0.3f, 0.4f}};
    m.skinning = {{{0, 1, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}}, {{1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}}};
    m.indices = {0, 1, 0};
    canon::PrimitiveGeoset pg;
    pg.geoset = canon::Geoset::fromRawId(102);
    pg.indexStart = 0;
    pg.indexCount = 3;
    m.primitives = {pg};
    return m;
}

gltf::Mesh makeMatchingLegacyMesh(const canon::Mesh& canonMesh) {
    gltf::Mesh legacy;
    for (const auto& p : canonMesh.positions) legacy.positions.push_back(toGltf(p));
    for (const auto& n : canonMesh.normals) legacy.normals.push_back(toGltf(n));
    for (const auto& uv : canonMesh.uv0) legacy.texCoords.push_back({uv.x, uv.y});
    legacy.texCoords2 = {{0, 0}, {0, 0}};  // canon leaves uv1 absent for this all-origin fixture
    for (const auto& sk : canonMesh.skinning) {
        gltf::JointWeights jw;
        for (int i = 0; i < 4; ++i) {
            jw.joints[i] = sk.joints[static_cast<size_t>(i)];
            jw.weights[i] = sk.weights[static_cast<size_t>(i)];
        }
        legacy.skinning.push_back(jw);
    }
    gltf::Primitive prim;
    prim.indices = {canonMesh.indices[0], canonMesh.indices[1], canonMesh.indices[2]};
    prim.skinSectionId = 102;
    legacy.primitives = {prim};
    return legacy;
}

}  // namespace

TEST_CASE("canon_diff::compareMesh: a genuinely converged mesh reports zero deviations") {
    canon::Mesh canonMesh = makeCanonMesh();
    gltf::Mesh legacy = makeMatchingLegacyMesh(canonMesh);

    canon_diff::Report r = canon_diff::compareMesh(canonMesh, legacy);
    INFO("first deviation (if any): ", r.deviations.empty() ? "<none>" : r.deviations.front());
    CHECK(r.ok());
    // The documented uv1 divergence always surfaces as a note here (this
    // fixture's uv1 is all-origin) -- never silently dropped.
    CHECK(r.notes.size() >= 1);
}

TEST_CASE("canon_diff::compareMesh: an injected position mismatch is caught as a deviation") {
    canon::Mesh canonMesh = makeCanonMesh();
    gltf::Mesh legacy = makeMatchingLegacyMesh(canonMesh);
    legacy.positions[1].x += 5.0f;  // real, injected divergence

    canon_diff::Report r = canon_diff::compareMesh(canonMesh, legacy);
    CHECK_FALSE(r.ok());
    bool foundPositionDeviation = false;
    for (const auto& d : r.deviations) {
        if (d.find("position mismatch") != std::string::npos) foundPositionDeviation = true;
    }
    CHECK(foundPositionDeviation);
}

TEST_CASE("canon_diff::compareMesh: a vertex-count mismatch is caught and stops further comparison") {
    canon::Mesh canonMesh = makeCanonMesh();
    gltf::Mesh legacy = makeMatchingLegacyMesh(canonMesh);
    legacy.positions.pop_back();
    legacy.normals.pop_back();
    legacy.texCoords.pop_back();

    canon_diff::Report r = canon_diff::compareMesh(canonMesh, legacy);
    REQUIRE(r.deviations.size() == 1);
    CHECK(r.deviations.front().find("vertex count differs") != std::string::npos);
}

TEST_CASE("canon_diff::compareMesh: canon's own uv1/skinning divergences are notes, not deviations, "
          "when legacy's own extra data is exactly the documented all-zero fabrication") {
    canon::Mesh canonMesh;
    canonMesh.positions = {{0, 0, 0}};
    canonMesh.normals = {{0, 0, 1}};
    canonMesh.uv0 = {{0, 0}};
    // canonMesh.skinning left empty -- unskinned model.

    gltf::Mesh legacy;
    legacy.positions = {toGltf(canonMesh.positions[0])};
    legacy.normals = {toGltf(canonMesh.normals[0])};
    legacy.texCoords = {{0, 0}};
    legacy.texCoords2 = {{0, 0}};
    // legacy.skinning intentionally left empty too (an unskinned model has
    // no Skeleton to build boneCount-many zero entries against at all --
    // see cmd_export.cpp's `if (!bones.empty())` guard).

    canon_diff::Report r = canon_diff::compareMesh(canonMesh, legacy);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareSkeleton: matching joints report zero deviations") {
    canon::Skeleton canonSkel;
    canon::Joint j;
    j.parent = -1;
    j.globalPosition = {1, 2, 3};
    j.billboard = canon::BillboardMode::Spherical;
    canonSkel.joints = {j};

    gltf::Skeleton legacy;
    gltf::Skeleton::Joint lj;
    lj.parent = -1;
    lj.globalPosition = toGltf(j.globalPosition);
    lj.billboardMode = "spherical";
    legacy.joints = {lj};

    canon_diff::Report r = canon_diff::compareSkeleton(canonSkel, legacy);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareSkeleton: an injected parent mismatch is caught as a deviation") {
    canon::Skeleton canonSkel;
    canon::Joint root;
    root.parent = -1;
    canon::Joint child;
    child.parent = 0;
    canonSkel.joints = {root, child};

    gltf::Skeleton legacy;
    gltf::Skeleton::Joint lroot;
    lroot.parent = -1;
    gltf::Skeleton::Joint lchild;
    lchild.parent = -1;  // wrong -- should be 0
    legacy.joints = {lroot, lchild};

    canon_diff::Report r = canon_diff::compareSkeleton(canonSkel, legacy);
    CHECK_FALSE(r.ok());
    bool foundParentDeviation = false;
    for (const auto& d : r.deviations) {
        if (d.find("parent mismatch") != std::string::npos) foundParentDeviation = true;
    }
    CHECK(foundParentDeviation);
}

namespace {

// Every compareMaterialBlendModes test below builds one canon::Model with
// exactly one material and one primitive -- this sets up the now-required
// canon::Model::primitiveMaterials indirection (canon_model.hpp's own doc
// comment) so a test doesn't have to repeat it inline six times.
canon::Model oneMaterialOnePrimitiveModel(canon::MaterialLayer layer) {
    canon::Model canonModel;
    canon::Material mat;
    mat.ref.id = canon::RecordIndex{0};
    mat.layers = {std::move(layer)};
    mat.framebufferBlend = canon::FramebufferBlend::Opaque;  // every test below uses M2 blend mode 0
    canonModel.materials = {mat};
    canonModel.primitiveMaterials = {canon::Identity{canon::RecordIndex{0}}};
    return canonModel;
}

// Same shape, multiple layers -- for the additional-texture-layer
// (layers[1..]) tests below, which need more than one MaterialLayer.
canon::Model oneMaterialOnePrimitiveModel(std::vector<canon::MaterialLayer> layers) {
    canon::Model canonModel;
    canon::Material mat;
    mat.ref.id = canon::RecordIndex{0};
    mat.layers = std::move(layers);
    mat.framebufferBlend = canon::FramebufferBlend::Opaque;
    canonModel.materials = {mat};
    canonModel.primitiveMaterials = {canon::Identity{canon::RecordIndex{0}}};
    return canonModel;
}

}  // namespace

TEST_CASE("canon_diff::compareMaterialBlendModes: a real blend-mode mismatch is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Add;  // deliberately wrong for blendMode 0 below (expects Replace)
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;  // expectedBlendOp(0) == Replace

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials);
    CHECK_FALSE(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a matching blend mode reports zero deviations") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;  // matches blendMode 0
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a framebuffer blend that disagrees with the M2 blend mode "
          "is a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;  // ALPHA_KEY's combine op too, so only the new field differs
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);
    canonModel.materials[0].framebufferBlend = canon::FramebufferBlend::Opaque;

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 1;  // ALPHA_KEY

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials);
    REQUIRE(r.deviations.size() == 1);
    CHECK(r.deviations[0] == "framebuffer blend mismatch at primitive 0: expected M2 blend mode 1");

    canonModel.materials[0].framebufferBlend = canon::FramebufferBlend::AlphaKey;
    CHECK(canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials).ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: an undocumented M2 blend mode must leave framebufferBlend unset") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Fade;  // the unknown-mode combine fallback
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);
    canonModel.materials[0].framebufferBlend.reset();

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 9;

    CHECK(canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials).ok());
    canonModel.materials[0].framebufferBlend = canon::FramebufferBlend::Alpha;
    CHECK_FALSE(canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials).ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: legacyMaterials/legacyPrimitives omitted (default) skips "
          "the texture-identity check entirely, even with a mismatching fdid") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.texture.state = canon::TextureRef::State::Resolved;
    layer.texture.resolved.id = canon::FileDataId{111};
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    // No legacyMaterials/legacyPrimitives arguments at all -- default empty.
    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a real resolved-texture FileDataID mismatch against "
          "legacyMaterials (resolved via legacyPrimitives' own materialIndex) is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.texture.state = canon::TextureRef::State::Resolved;
    layer.texture.resolved.id = canon::FileDataId{111};
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].baseColorTextureFileDataId = 222;  // deliberately different from canon's 111
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a matching resolved-texture FileDataID against "
          "legacyMaterials (resolved via legacyPrimitives' own materialIndex) reports zero deviations") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.texture.state = canon::TextureRef::State::Resolved;
    layer.texture.resolved.id = canon::FileDataId{111};
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].baseColorTextureFileDataId = 111;  // matches canon's resolved fdid
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a KnownUnresolved canon texture state matches legacy when "
          "legacy's own baseColorImagePng is also empty, even if legacy resolved an unrelated fdid") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    // Default TextureRef::State::KnownUnresolved -- e.g. no TextureResolutions
    // was supplied to assembleModel at all.
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    // A resolved fdid with no embedded bytes is still "couldn't get bytes"
    // (baseColorImagePng's own doc comment) -- this field alone isn't what
    // the KnownUnresolved check looks at.
    legacyMaterials[0].baseColorTextureFileDataId = 999;
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a KnownUnresolved canon texture state is caught as a "
          "deviation when legacy actually embedded real bytes for the same slot") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    // Default TextureRef::State::KnownUnresolved.
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].baseColorImagePng = {1, 2, 3, 4};  // legacy resolved real bytes canon couldn't
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("KnownUnresolved") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("canon_diff::compareMaterialBlendModes: an Ambiguous canon texture state matches legacy when "
          "legacy also reports real alternateTextureCandidates") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.texture.state = canon::TextureRef::State::Ambiguous;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].alternateTextureCandidates.resize(2);  // legacy found the same real ambiguity
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: an Ambiguous canon texture state is caught as a deviation "
          "when legacy found no candidates at all") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.texture.state = canon::TextureRef::State::Ambiguous;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);  // alternateTextureCandidates left empty
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("Ambiguous") != std::string::npos) found = true;
    }
    CHECK(found);
}

namespace {

// A one-keyframe canon::VecCurve plus its exact legacy AnimatedColorCurve
// counterpart -- raw (non-axis-converted) values, matching this project's
// own finding that tint/UV-transform curves are colors/texture-space
// values, not spatial positions (canon_diff.cpp's own doc comment).
canon::VecCurve makeVecCurve(uint32_t sequenceIndex, float t, float x, float y, float z) {
    canon::VecCurve c;
    c.sequence = canon::SequenceRef::sequence(sequenceIndex);
    c.keyframes = {{t, canon::Vec3{x, y, z}}};
    return c;
}

gltf::Material::AnimatedColorCurve makeLegacyColorCurve(int sequenceIndex, float t, float x, float y, float z) {
    gltf::Material::AnimatedColorCurve c;
    c.sequenceIndex = sequenceIndex;
    c.keyframes = {{t, gltf::Vec3{x, y, z}}};
    return c;
}

}  // namespace

TEST_CASE("canon_diff::compareMaterialBlendModes: a matching tint curve against legacy's own tintAnimation "
          "entry for the same sequence reports zero deviations") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.tint = makeVecCurve(0, 1.0f, 0.5f, 0.25f, 0.1f);
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].tintAnimation = {makeLegacyColorCurve(0, 1.0f, 0.5f, 0.25f, 0.1f)};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    INFO("first deviation (if any): ", r.deviations.empty() ? "<none>" : r.deviations.front());
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a genuine tint curve value mismatch against legacy's "
          "tintAnimation is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.tint = makeVecCurve(0, 1.0f, 0.5f, 0.25f, 0.1f);
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    // Deliberately wrong red channel (0.9 vs canon's 0.5).
    legacyMaterials[0].tintAnimation = {makeLegacyColorCurve(0, 1.0f, 0.9f, 0.25f, 0.1f)};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("tint") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a tint curve with no matching-sequence legacy "
          "tintAnimation entry at all is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    layer.tint = makeVecCurve(0, 1.0f, 0.5f, 0.25f, 0.1f);
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);  // tintAnimation left empty
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: canon's single alphaFade curve matches legacy's separate "
          "weightFadeAnimation vector when it wasn't sourced from color at all") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    canon::ScalarCurve fade;
    fade.sequence = canon::SequenceRef::sequence(0);
    fade.keyframes = {{2.0f, 0.75f}};
    layer.alphaFade = fade;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    // alphaFadeAnimation left empty; only weightFadeAnimation has the
    // matching-sequence entry -- m2_material_input.hpp's own doc comment
    // for why canon's one alphaFade slot can come from either source.
    gltf::Material::AnimatedScalarCurve weightCurve;
    weightCurve.sequenceIndex = 0;
    weightCurve.keyframes = {{2.0f, 0.75f}};
    legacyMaterials[0].weightFadeAnimation = {weightCurve};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    INFO("first deviation (if any): ", r.deviations.empty() ? "<none>" : r.deviations.front());
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: an alphaFade curve matching neither legacy "
          "alphaFadeAnimation nor weightFadeAnimation is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    canon::ScalarCurve fade;
    fade.sequence = canon::SequenceRef::sequence(0);
    fade.keyframes = {{2.0f, 0.75f}};
    layer.alphaFade = fade;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);  // both curve vectors left empty
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("alphaFade") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a uvAnimation translation curve mismatch against legacy's "
          "textureTransformTranslationAnimation is caught as a deviation") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    canon::MaterialLayer::TextureTransformCurves xf;
    xf.translation = makeVecCurve(0, 0.5f, 1.0f, 0.0f, 0.0f);
    layer.uvAnimation = xf;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    // Deliberately wrong x translation (2.0 vs canon's 1.0).
    legacyMaterials[0].textureTransformTranslationAnimation = {makeLegacyColorCurve(0, 0.5f, 2.0f, 0.0f, 0.0f)};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("uv translation") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a matching uvAnimation translation curve against legacy's "
          "textureTransformTranslationAnimation reports zero deviations") {
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;
    canon::MaterialLayer::TextureTransformCurves xf;
    xf.translation = makeVecCurve(0, 0.5f, 1.0f, 0.0f, 0.0f);
    layer.uvAnimation = xf;
    canon::Model canonModel = oneMaterialOnePrimitiveModel(layer);

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].textureTransformTranslationAnimation = {makeLegacyColorCurve(0, 0.5f, 1.0f, 0.0f, 0.0f)};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    INFO("first deviation (if any): ", r.deviations.empty() ? "<none>" : r.deviations.front());
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: matching additional (layers[1..]) texture layers against "
          "legacy's additionalTextureLayers report zero deviations") {
    canon::MaterialLayer primary;
    primary.blendIntoPrevious = canon::BlendOp::Replace;
    primary.texture.state = canon::TextureRef::State::Resolved;
    primary.texture.resolved.id = canon::FileDataId{111};

    canon::MaterialLayer secondary;
    secondary.texture.state = canon::TextureRef::State::Resolved;
    secondary.texture.resolved.id = canon::FileDataId{222};

    canon::Model canonModel = oneMaterialOnePrimitiveModel(std::vector<canon::MaterialLayer>{primary, secondary});

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    legacyMaterials[0].baseColorTextureFileDataId = 111;
    gltf::Material::AdditionalTextureLayer al;
    al.fileDataId = 222;
    legacyMaterials[0].additionalTextureLayers = {al};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    INFO("first deviation (if any): ", r.deviations.empty() ? "<none>" : r.deviations.front());
    CHECK(r.ok());
}

TEST_CASE("canon_diff::compareMaterialBlendModes: a genuine additional-layer texture FileDataID mismatch "
          "against legacy's additionalTextureLayers is caught as a deviation") {
    canon::MaterialLayer primary;
    primary.blendIntoPrevious = canon::BlendOp::Replace;

    canon::MaterialLayer secondary;
    secondary.texture.state = canon::TextureRef::State::Resolved;
    secondary.texture.resolved.id = canon::FileDataId{222};

    canon::Model canonModel = oneMaterialOnePrimitiveModel(std::vector<canon::MaterialLayer>{primary, secondary});

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    gltf::Material::AdditionalTextureLayer al;
    al.fileDataId = 333;  // deliberately different from canon's 222
    legacyMaterials[0].additionalTextureLayers = {al};
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("additional layer") != std::string::npos) found = true;
    }
    CHECK(found);
}

TEST_CASE("canon_diff::compareMaterialBlendModes: an additional-layer count mismatch (canon's layers[1..] vs "
          "legacy's additionalTextureLayers) is caught as a deviation") {
    canon::MaterialLayer primary;
    primary.blendIntoPrevious = canon::BlendOp::Replace;

    canon::MaterialLayer secondary;
    secondary.texture.state = canon::TextureRef::State::Resolved;
    secondary.texture.resolved.id = canon::FileDataId{222};

    canon::MaterialLayer tertiary;
    tertiary.texture.state = canon::TextureRef::State::Resolved;
    tertiary.texture.resolved.id = canon::FileDataId{333};

    // canon has 2 additional layers (layers[1], layers[2]); legacy only has 1.
    canon::Model canonModel =
        oneMaterialOnePrimitiveModel(std::vector<canon::MaterialLayer>{primary, secondary, tertiary});

    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;
    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;
    batches[0].materialIndex = 0;

    std::vector<m2::Material> materials(1);
    materials[0].blendMode = 0;

    std::vector<gltf::Material> legacyMaterials(1);
    gltf::Material::AdditionalTextureLayer al;
    al.fileDataId = 222;
    legacyMaterials[0].additionalTextureLayers = {al};  // only 1, canon expects 2
    std::vector<gltf::Primitive> legacyPrimitives(1);
    legacyPrimitives[0].materialIndex = 0;

    canon_diff::Report r = canon_diff::compareMaterialBlendModes(canonModel, batches, submeshes, materials,
                                                                  legacyMaterials, legacyPrimitives);
    CHECK_FALSE(r.ok());
    bool found = false;
    for (const auto& d : r.deviations) {
        if (d.find("additional layer(s)") != std::string::npos) found = true;
    }
    CHECK(found);
}
