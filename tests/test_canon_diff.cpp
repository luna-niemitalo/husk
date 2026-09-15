// canon_diff.hpp's own comparators, exercised with small hand-built
// canon::/gltf:: structs -- the CLI-tier smoke test
// (tests/test_cli_compare_canon.cpp) covers the real-fixture end-to-end
// path; this file covers the comparison logic itself in isolation, both
// the matching case (no false positives on genuinely converged data) and
// the deviation case (a real, injected mismatch is actually caught).

#include <doctest/doctest.h>

#include "canon_diff.hpp"
#include "export_transform.hpp"

using namespace husk;

namespace {

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
    for (const auto& p : canonMesh.positions) legacy.positions.push_back(commands::toGltf(p));
    for (const auto& n : canonMesh.normals) legacy.normals.push_back(commands::toGltf(n));
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
    legacy.positions = {commands::toGltf(canonMesh.positions[0])};
    legacy.normals = {commands::toGltf(canonMesh.normals[0])};
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
    lj.globalPosition = commands::toGltf(j.globalPosition);
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

TEST_CASE("canon_diff::compareMaterialBlendModes: a real blend-mode mismatch is caught as a deviation") {
    canon::Model canonModel;
    canon::Material mat;
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Add;  // deliberately wrong for blendMode 0 below (expects Replace)
    mat.layers = {layer};
    canonModel.materials = {mat};

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
    canon::Model canonModel;
    canon::Material mat;
    canon::MaterialLayer layer;
    layer.blendIntoPrevious = canon::BlendOp::Replace;  // matches blendMode 0
    mat.layers = {layer};
    canonModel.materials = {mat};

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
