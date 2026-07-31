// writeGlb() delegates the actual glTF/.glb framing to tinygltf (see
// nix/flake.nix, src/gltf.cpp) rather than reimplementing the format here.
// So instead of re-deriving glTF's binary chunk layout by hand (the
// test_m2.cpp/test_skin.cpp style), these tests round-trip writeGlb()'s
// output back through tinygltf's own loader and check the data survived --
// that exercises real glTF-spec compliance (tinygltf's loader enforces the
// spec independently of how husk built the Model), not just "husk agrees
// with itself."

#include <cstring>
#include <doctest/doctest.h>
#include <tiny_gltf.h>

#include "../src/gltf.hpp"

namespace {

husk::gltf::Mesh buildTriangleMesh() {
    husk::gltf::Mesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh.texCoords = {{0, 0}, {1, 0}, {0, 1}};
    husk::gltf::Primitive prim;
    prim.indices = {0, 1, 2};
    mesh.primitives = {prim};
    return mesh;
}

tinygltf::Model loadBack(const std::vector<uint8_t>& glb) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    bool ok = loader.LoadBinaryFromMemory(&model, &err, &warn, glb.data(),
                                           static_cast<unsigned int>(glb.size()));
    INFO("tinygltf error: ", err);
    REQUIRE(ok);
    return model;
}

}  // namespace

TEST_CASE("zUpToYUp: (X, Y, Z) becomes (X, -Z, Y), per wowdev.wiki M2#Vertices") {
    husk::gltf::Vec3 in{1, 2, 3};
    auto out = husk::gltf::zUpToYUp(in);
    CHECK(out.x == doctest::Approx(1));
    CHECK(out.y == doctest::Approx(-3));
    CHECK(out.z == doctest::Approx(2));
}

TEST_CASE("writeGlb: output starts with the glTF binary magic and version 2") {
    auto glb = husk::gltf::writeGlb(buildTriangleMesh());
    REQUIRE(glb.size() >= 12);
    CHECK(glb[0] == 'g');
    CHECK(glb[1] == 'l');
    CHECK(glb[2] == 'T');
    CHECK(glb[3] == 'F');
    uint32_t version;
    std::memcpy(&version, glb.data() + 4, 4);
    CHECK(version == 2);
}

TEST_CASE("writeGlb: round-trips positions/normals/texCoords/indices through tinygltf's own loader") {
    auto mesh = buildTriangleMesh();
    auto glb = husk::gltf::writeGlb(mesh);
    auto model = loadBack(glb);

    REQUIRE(model.meshes.size() == 1);
    REQUIRE(model.meshes[0].primitives.size() == 1);
    const auto& prim = model.meshes[0].primitives[0];
    CHECK(prim.mode == TINYGLTF_MODE_TRIANGLES);

    REQUIRE(prim.attributes.count("POSITION") == 1);
    REQUIRE(prim.attributes.count("NORMAL") == 1);
    REQUIRE(prim.attributes.count("TEXCOORD_0") == 1);
    REQUIRE(prim.indices >= 0);

    auto readVec3Accessor = [&](int accessorIdx) {
        const auto& acc = model.accessors[accessorIdx];
        const auto& view = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[view.buffer];
        std::vector<husk::gltf::Vec3> out(acc.count);
        std::memcpy(out.data(), buf.data.data() + view.byteOffset + acc.byteOffset,
                    acc.count * sizeof(husk::gltf::Vec3));
        return out;
    };
    auto readVec2Accessor = [&](int accessorIdx) {
        const auto& acc = model.accessors[accessorIdx];
        const auto& view = model.bufferViews[acc.bufferView];
        const auto& buf = model.buffers[view.buffer];
        std::vector<husk::gltf::Vec2> out(acc.count);
        std::memcpy(out.data(), buf.data.data() + view.byteOffset + acc.byteOffset,
                    acc.count * sizeof(husk::gltf::Vec2));
        return out;
    };

    int posIdx = prim.attributes.at("POSITION");
    REQUIRE(model.accessors[posIdx].count == 3);
    auto positions = readVec3Accessor(posIdx);
    CHECK(positions[1].x == doctest::Approx(1));
    CHECK(positions[2].y == doctest::Approx(1));
    // glTF requires POSITION accessors to carry min/max.
    REQUIRE(model.accessors[posIdx].minValues.size() == 3);
    REQUIRE(model.accessors[posIdx].maxValues.size() == 3);
    CHECK(model.accessors[posIdx].maxValues[0] == doctest::Approx(1));

    int normIdx = prim.attributes.at("NORMAL");
    auto normals = readVec3Accessor(normIdx);
    CHECK(normals[0].z == doctest::Approx(1));

    int uvIdx = prim.attributes.at("TEXCOORD_0");
    auto uvs = readVec2Accessor(uvIdx);
    CHECK(uvs[1].x == doctest::Approx(1));
    CHECK(uvs[2].y == doctest::Approx(1));

    const auto& idxAcc = model.accessors[prim.indices];
    REQUIRE(idxAcc.count == 3);
    const auto& idxView = model.bufferViews[idxAcc.bufferView];
    const auto& idxBuf = model.buffers[idxView.buffer];
    std::vector<uint32_t> indices(3);
    std::memcpy(indices.data(), idxBuf.data.data() + idxView.byteOffset + idxAcc.byteOffset,
                3 * sizeof(uint32_t));
    CHECK(indices[0] == 0);
    CHECK(indices[1] == 1);
    CHECK(indices[2] == 2);
}

TEST_CASE("writeGlb: mismatched attribute array lengths throws") {
    auto mesh = buildTriangleMesh();
    mesh.normals.pop_back();  // now 2 normals but 3 positions
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: no primitives at all throws") {
    auto mesh = buildTriangleMesh();
    mesh.primitives.clear();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: empty indices throws") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].indices.clear();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: indices count not a multiple of 3 throws") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].indices = {0, 1};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: an index referencing a nonexistent position throws") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].indices = {0, 1, 99};  // only 3 positions, index 2 max
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: a primitive's materialIndex out of range for `materials` throws") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;  // but no materials were given
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

namespace {

// A 3-joint chain: root (0) -> mid (1) -> tip (2), at global positions
// (0,0,0), (0,2,0), (0,2,3). Every triangle vertex is fully weighted to a
// different joint, so the round-trip test can tell them apart.
husk::gltf::Skeleton buildChainSkeleton() {
    husk::gltf::Skeleton skel;
    skel.joints.push_back({-1, {0, 0, 0}, {0, 0, 0}});
    skel.joints.push_back({0, {0, 2, 0}, {0, 2, 0}});
    skel.joints.push_back({1, {0, 0, 3}, {0, 2, 3}});
    return skel;
}

husk::gltf::Mesh buildSkinnedTriangleMesh() {
    auto mesh = buildTriangleMesh();
    husk::gltf::JointWeights w0, w1, w2;
    w0.joints[0] = 0;
    w0.weights[0] = 1.0f;
    w1.joints[0] = 1;
    w1.weights[0] = 1.0f;
    w2.joints[0] = 2;
    w2.weights[0] = 1.0f;
    mesh.skinning = {w0, w1, w2};
    return mesh;
}

}  // namespace

TEST_CASE("writeGlb: skinned mesh round-trips joints, weights, and the joint hierarchy") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.skins.size() == 1);
    const auto& skin = model.skins[0];
    REQUIRE(skin.joints.size() == 3);

    // Node hierarchy: joint 0 is root.parent-less, joint 1 is its child,
    // joint 2 is joint 1's child. Translations are local-to-parent.
    int rootNode = skin.joints[0];
    int midNode = skin.joints[1];
    int tipNode = skin.joints[2];
    REQUIRE(model.nodes[rootNode].children.size() == 1);
    CHECK(model.nodes[rootNode].children[0] == midNode);
    REQUIRE(model.nodes[midNode].children.size() == 1);
    CHECK(model.nodes[midNode].children[0] == tipNode);

    REQUIRE(model.nodes[midNode].translation.size() == 3);
    CHECK(model.nodes[midNode].translation[1] == doctest::Approx(2));
    REQUIRE(model.nodes[tipNode].translation.size() == 3);
    CHECK(model.nodes[tipNode].translation[2] == doctest::Approx(3));

    // Inverse bind matrices: pure translation by -globalPosition, column-major.
    REQUIRE(skin.inverseBindMatrices >= 0);
    const auto& ibmAcc = model.accessors[skin.inverseBindMatrices];
    REQUIRE(ibmAcc.count == 3);
    const auto& ibmView = model.bufferViews[ibmAcc.bufferView];
    const auto& ibmBuf = model.buffers[ibmView.buffer];
    std::vector<float> ibm(16 * 3);
    std::memcpy(ibm.data(), ibmBuf.data.data() + ibmView.byteOffset + ibmAcc.byteOffset,
                ibm.size() * sizeof(float));
    // Joint 2 (tip)'s global position is (0,2,3) -> translation column is
    // (-0,-2,-3), the last 4 floats of its 16-float column-major matrix.
    const float* tipMat = ibm.data() + 16 * 2;
    CHECK(tipMat[12] == doctest::Approx(0));
    CHECK(tipMat[13] == doctest::Approx(-2));
    CHECK(tipMat[14] == doctest::Approx(-3));
    CHECK(tipMat[15] == doctest::Approx(1));

    // Mesh node references the skin; primitive carries JOINTS_0/WEIGHTS_0.
    REQUIRE(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);
    const auto& prim = model.meshes[0].primitives[0];
    REQUIRE(prim.attributes.count("JOINTS_0") == 1);
    REQUIRE(prim.attributes.count("WEIGHTS_0") == 1);

    const auto& jAcc = model.accessors[prim.attributes.at("JOINTS_0")];
    REQUIRE(jAcc.count == 3);
    const auto& jView = model.bufferViews[jAcc.bufferView];
    const auto& jBuf = model.buffers[jView.buffer];
    const uint8_t* jData = jBuf.data.data() + jView.byteOffset + jAcc.byteOffset;
    CHECK(jData[0 * 4 + 0] == 0);  // vertex 0 -> joint 0
    CHECK(jData[1 * 4 + 0] == 1);  // vertex 1 -> joint 1
    CHECK(jData[2 * 4 + 0] == 2);  // vertex 2 -> joint 2

    const auto& wAcc = model.accessors[prim.attributes.at("WEIGHTS_0")];
    const auto& wView = model.bufferViews[wAcc.bufferView];
    const auto& wBuf = model.buffers[wView.buffer];
    std::vector<float> weights(4 * 3);
    std::memcpy(weights.data(), wBuf.data.data() + wView.byteOffset + wAcc.byteOffset,
                weights.size() * sizeof(float));
    CHECK(weights[0 * 4 + 0] == doctest::Approx(1));
    CHECK(weights[2 * 4 + 0] == doctest::Approx(1));
}

TEST_CASE("writeGlb: a joint's billboardMode becomes a \"billboard\" key in its node's extras") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[1].billboardMode = "spherical";
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    int midNode = model.skins[0].joints[1];
    REQUIRE(model.nodes[midNode].extras.Has("billboard"));
    CHECK(model.nodes[midNode].extras.Get("billboard").Get<std::string>() == "spherical");

    // Joints without a billboardMode get no extras at all.
    int rootNode = model.skins[0].joints[0];
    CHECK_FALSE(model.nodes[rootNode].extras.Has("billboard"));
}

TEST_CASE("writeGlb: a skeleton's correctionSets round-trip as bone_correction_sets on the "
          "skin's extras (WIKI_FINDINGS.md §4/TODO_correctness.md #3)") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 1103216;
    husk::gltf::Skeleton::CorrectionSet::Correction c;
    c.joint = 1;
    c.matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.01f, 0.02f, 0.03f, 1};
    set.corrections = {c};
    skel.correctionSets = {set};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& sets = extras.Get("bone_correction_sets");
    REQUIRE(sets.IsArray());
    REQUIRE(sets.ArrayLen() == 1);

    const auto& set0 = sets.Get(0);
    CHECK(set0.Get("file_data_id").GetNumberAsInt() == 1103216);
    const auto& corrections = set0.Get("corrections");
    REQUIRE(corrections.IsArray());
    REQUIRE(corrections.ArrayLen() == 1);
    const auto& c0 = corrections.Get(0);
    CHECK(c0.Get("joint").GetNumberAsInt() == 1);
    const auto& matrix = c0.Get("matrix");
    REQUIRE(matrix.IsArray());
    REQUIRE(matrix.ArrayLen() == 16);
    CHECK(matrix.Get(12).GetNumberAsDouble() == doctest::Approx(0.01));
    CHECK(matrix.Get(13).GetNumberAsDouble() == doctest::Approx(0.02));
    CHECK(matrix.Get(14).GetNumberAsDouble() == doctest::Approx(0.03));
    CHECK(matrix.Get(15).GetNumberAsDouble() == doctest::Approx(1));
}

TEST_CASE("writeGlb: a skeleton with no correctionSets gets no bone_correction_sets extras key") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a correctionSet entry with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 1;
    husk::gltf::Skeleton::CorrectionSet::Correction c;
    c.joint = 99;
    set.corrections = {c};
    skel.correctionSets = {set};

    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: a skeleton's ribbonAnchors/particleAnchors round-trip as ribbon_emitters/"
          "particle_emitters on the skin's extras -- minimal placement data only") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.ribbonAnchors = {{0xFFFFFFFFu, 1, {0.1f, 0.2f, 0.3f}}};
    skel.particleAnchors = {{42, 0, {1.0f, -1.0f, 0.5f}}, {43, 1, {0, 0, 0}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());

    const auto& ribbons = extras.Get("ribbon_emitters");
    REQUIRE(ribbons.IsArray());
    REQUIRE(ribbons.ArrayLen() == 1);
    CHECK(ribbons.Get(0).Get("id").GetNumberAsInt() == -1);
    CHECK(ribbons.Get(0).Get("joint").GetNumberAsInt() == 1);
    const auto& rPos = ribbons.Get(0).Get("position");
    CHECK(rPos.Get("x").GetNumberAsDouble() == doctest::Approx(0.1));
    CHECK(rPos.Get("z").GetNumberAsDouble() == doctest::Approx(0.3));

    const auto& particles = extras.Get("particle_emitters");
    REQUIRE(particles.IsArray());
    REQUIRE(particles.ArrayLen() == 2);
    CHECK(particles.Get(0).Get("id").GetNumberAsInt() == 42);
    CHECK(particles.Get(1).Get("joint").GetNumberAsInt() == 1);
}

TEST_CASE("writeGlb: a skeleton with no ribbon/particle anchors gets no such extras keys") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a ribbon anchor with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.ribbonAnchors = {{0, 99, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: a particle anchor with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.particleAnchors = {{0, -1, {0, 0, 0}}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: ribbon/particle anchors coexist with bone_correction_sets without "
          "clobbering each other") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 7;
    skel.correctionSets = {set};
    skel.ribbonAnchors = {{0, 0, {0, 0, 0}}};
    skel.particleAnchors = {{0, 0, {0, 0, 0}}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("bone_correction_sets").IsArray());
    CHECK(extras.Get("ribbon_emitters").IsArray());
    CHECK(extras.Get("particle_emitters").IsArray());
}

TEST_CASE("writeGlb: a skeleton's physicsBodies round-trip as physics_bodies on the skin's "
          "extras -- minimal placement data only (DESIGN.md's anchor/dump-chunks split)") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.physicsBodies = {{0, 1, {0.1f, 0.2f, 0.3f}, 0}, {1, 0, {1.0f, -1.0f, 0.5f}, 2}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& bodies = extras.Get("physics_bodies");
    REQUIRE(bodies.IsArray());
    REQUIRE(bodies.ArrayLen() == 2);
    CHECK(bodies.Get(0).Get("id").GetNumberAsInt() == 0);
    CHECK(bodies.Get(0).Get("joint").GetNumberAsInt() == 1);
    CHECK(bodies.Get(0).Get("body_type").GetNumberAsInt() == 0);
    const auto& pos = bodies.Get(0).Get("position");
    CHECK(pos.Get("x").GetNumberAsDouble() == doctest::Approx(0.1));
    CHECK(pos.Get("z").GetNumberAsDouble() == doctest::Approx(0.3));
    CHECK(bodies.Get(1).Get("id").GetNumberAsInt() == 1);
    CHECK(bodies.Get(1).Get("body_type").GetNumberAsInt() == 2);
}

TEST_CASE("writeGlb: a skeleton with no physics bodies gets no physics_bodies extras key") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    CHECK_FALSE(model.skins[0].extras.IsObject());
}

TEST_CASE("writeGlb: a physics body with an out-of-range joint throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.physicsBodies = {{0, 99, {0, 0, 0}, 0}};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: physics bodies coexist with bone_correction_sets/ribbon_emitters/"
          "particle_emitters without clobbering each other") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::Skeleton::CorrectionSet set;
    set.fileDataId = 7;
    skel.correctionSets = {set};
    skel.ribbonAnchors = {{0, 0, {0, 0, 0}}};
    skel.particleAnchors = {{0, 0, {0, 0, 0}}};
    skel.physicsBodies = {{0, 0, {0, 0, 0}, 0}};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);

    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("bone_correction_sets").IsArray());
    CHECK(extras.Get("ribbon_emitters").IsArray());
    CHECK(extras.Get("particle_emitters").IsArray());
    CHECK(extras.Get("physics_bodies").IsArray());
}

// A single writeGlb mesh may legitimately share a skeleton without being
// skinned by it (empty mesh.skinning opts out -- see writeGlbMulti's doc
// comment); the still-real error case is skinning data that's *present*
// but the wrong length.
TEST_CASE("writeGlb: skeleton given with no mesh.skinning succeeds, mesh node gets no skin") {
    auto mesh = buildTriangleMesh();  // no skinning data
    auto skel = buildChainSkeleton();
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel);
    auto model = loadBack(glb);
    REQUIRE(!model.nodes.empty());
    CHECK(model.nodes[0].skin == -1);
}

TEST_CASE("writeGlb: skeleton given with mesh.skinning present but the wrong length throws") {
    auto mesh = buildTriangleMesh();  // 3 positions
    mesh.skinning = {husk::gltf::JointWeights{}};  // 1 entry
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: mesh.skinning given without a skeleton throws") {
    auto mesh = buildSkinnedTriangleMesh();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, nullptr), husk::gltf::Error);
}

TEST_CASE("writeGlb: mesh.skinning length mismatched with positions throws") {
    auto mesh = buildSkinnedTriangleMesh();
    mesh.skinning.pop_back();
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: joint parent index out of range throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[2].parent = 99;
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

TEST_CASE("writeGlb: joint that is its own parent throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    skel.joints[1].parent = 1;
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel), husk::gltf::Error);
}

namespace {

// A 2-triangle quad (0,1,2 and 1,3,2), each triangle its own primitive so a
// test can prove they get independent index buffers and materials -- roadmap
// stage 5 (see README.md), where one M2 submesh/batch becomes one primitive.
husk::gltf::Mesh buildTwoPrimitiveQuad() {
    husk::gltf::Mesh mesh;
    mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    mesh.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh.texCoords = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    husk::gltf::Primitive p0;
    p0.indices = {0, 1, 2};
    p0.materialIndex = 0;
    husk::gltf::Primitive p1;
    p1.indices = {1, 3, 2};
    p1.materialIndex = 1;
    mesh.primitives = {p0, p1};
    return mesh;
}

}  // namespace

TEST_CASE("writeGlb: each primitive keeps its own indices, attributes are shared") {
    auto mesh = buildTwoPrimitiveQuad();
    std::vector<husk::gltf::Material> materials(2);
    materials[0].name = "opaque_mat";
    materials[0].alphaMode = husk::gltf::Material::AlphaMode::Opaque;
    materials[1].name = "blend_mat";
    materials[1].alphaMode = husk::gltf::Material::AlphaMode::Blend;
    materials[1].doubleSided = true;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.materials.size() == 2);
    CHECK(model.materials[0].name == "opaque_mat");
    CHECK(model.materials[0].alphaMode == "OPAQUE");
    CHECK_FALSE(model.materials[0].doubleSided);
    CHECK(model.materials[1].name == "blend_mat");
    CHECK(model.materials[1].alphaMode == "BLEND");
    CHECK(model.materials[1].doubleSided);

    REQUIRE(model.meshes[0].primitives.size() == 2);
    const auto& prim0 = model.meshes[0].primitives[0];
    const auto& prim1 = model.meshes[0].primitives[1];
    CHECK(prim0.material == 0);
    CHECK(prim1.material == 1);

    // Both primitives share the same POSITION accessor (one shared vertex
    // buffer)...
    CHECK(prim0.attributes.at("POSITION") == prim1.attributes.at("POSITION"));
    // ...but each has its own index accessor/buffer view, with the right
    // 3-entry slice.
    CHECK(prim0.indices != prim1.indices);
    REQUIRE(model.accessors[prim1.indices].count == 3);
    const auto& idxAcc = model.accessors[prim1.indices];
    const auto& idxView = model.bufferViews[idxAcc.bufferView];
    const auto& idxBuf = model.buffers[idxView.buffer];
    std::vector<uint32_t> idx1(3);
    std::memcpy(idx1.data(), idxBuf.data.data() + idxView.byteOffset + idxAcc.byteOffset,
                3 * sizeof(uint32_t));
    CHECK(idx1[0] == 1);
    CHECK(idx1[1] == 3);
    CHECK(idx1[2] == 2);
}

TEST_CASE("writeGlb: a material with unlit=true gets the KHR_materials_unlit extension") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].unlit = true;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.materials.size() == 1);
    CHECK(model.materials[0].extensions.count("KHR_materials_unlit") == 1);
    REQUIRE(model.extensionsUsed.size() == 1);
    CHECK(model.extensionsUsed[0] == "KHR_materials_unlit");
}

TEST_CASE("writeGlb: a material with unlit=false (the default) gets no unlit extension, and "
          "extensionsUsed stays empty") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.materials.size() == 1);
    CHECK(model.materials[0].extensions.count("KHR_materials_unlit") == 0);
    CHECK(model.extensionsUsed.empty());
}

TEST_CASE("writeGlb: a primitive with materialIndex -1 gets no material") {
    auto mesh = buildTriangleMesh();  // materialIndex defaults to -1
    std::vector<husk::gltf::Material> materials(1);
    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);
    CHECK(model.meshes[0].primitives[0].material == -1);
}

TEST_CASE("writeGlb: a material's baseColorImagePng is embedded as a real glTF image+texture") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    // A real, valid 1x1 PNG (RGBA, generated via Pillow) -- has to actually
    // decode, since this test round-trips through tinygltf's own loader
    // (which decodes embedded images via stb_image), same rationale as
    // every other test in this file.
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPng;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].mimeType == "image/png");
    // tinygltf's loader decoded it -- confirms it's not just bytes that
    // happen to round-trip, but an image a real glTF consumer can read.
    CHECK(model.images[0].width == 1);
    CHECK(model.images[0].height == 1);

    REQUIRE(model.textures.size() == 1);
    CHECK(model.textures[0].source == 0);
    REQUIRE(model.materials[0].pbrMetallicRoughness.baseColorTexture.index == 0);
}

// Regression tests for FAILURES2.md #1/#6: geoset (skinSectionId) and
// multi-texture-layer metadata are exposed as inert glTF `extras` -- husk
// doesn't filter geosets or fake WoW's texture-combiner math, but a custom
// renderer or Blender script (mesh mask / geometry nodes / driven material)
// can use this to implement its own selection, the same "tag it, don't
// guess at semantics" treatment `billboardMode` already gets (see
// gltf.hpp's Primitive::skinSectionId / Material::AdditionalTextureLayer
// doc comments).
TEST_CASE("writeGlb: a primitive's skinSectionId round-trips as geoset_id/group/variant extras") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].skinSectionId = 401;  // group 4, variant 1

    auto glb = husk::gltf::writeGlb(mesh);
    auto model = loadBack(glb);

    REQUIRE(model.meshes[0].primitives.size() == 1);
    const auto& extras = model.meshes[0].primitives[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("geoset_id").GetNumberAsInt() == 401);
    CHECK(extras.Get("geoset_group").GetNumberAsInt() == 4);
    CHECK(extras.Get("geoset_variant").GetNumberAsInt() == 1);
}

TEST_CASE("writeGlb: a primitive with no skinSectionId (the batches.empty() fallback shape) gets "
          "no geoset extras") {
    auto mesh = buildTriangleMesh();  // skinSectionId left at its default (-1)

    auto glb = husk::gltf::writeGlb(mesh);
    auto model = loadBack(glb);

    REQUIRE(model.meshes[0].primitives.size() == 1);
    CHECK_FALSE(model.meshes[0].primitives[0].extras.IsObject());
}

TEST_CASE("writeGlb: a material's additionalTextureLayers round-trip as extras, with an embedded "
          "auxiliary image/texture when imagePng is given") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    std::vector<husk::gltf::Material> materials(1);
    husk::gltf::Material::AdditionalTextureLayer withImage;
    withImage.fileDataId = 555;
    withImage.texCoord = 1;
    withImage.imagePng = onePixelPng;
    husk::gltf::Material::AdditionalTextureLayer withoutImage;
    withoutImage.fileDataId = 777;
    materials[0].additionalTextureLayers = {withImage, withoutImage};

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    const auto& layers = extras.Get("additional_textures");
    REQUIRE(layers.IsArray());
    REQUIRE(layers.ArrayLen() == 2);

    const auto& layer0 = layers.Get(0);
    CHECK(layer0.Get("file_data_id").GetNumberAsInt() == 555);
    CHECK(layer0.Get("tex_coord").GetNumberAsInt() == 1);
    int texIdx = layer0.Get("texture_index").GetNumberAsInt();
    REQUIRE(texIdx >= 0);
    REQUIRE(static_cast<size_t>(texIdx) < model.textures.size());
    CHECK(model.images[model.textures[texIdx].source].width == 1);

    const auto& layer1 = layers.Get(1);
    CHECK(layer1.Get("file_data_id").GetNumberAsInt() == 777);
    // No imagePng given for this one -- no texture_index key at all.
    CHECK_FALSE(layer1.Get("texture_index").IsInt());
}

TEST_CASE("writeGlb: a material with no additionalTextureLayers gets no such extras") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

TEST_CASE("writeGlb: a material's textureTransform round-trips as extras (FINDINGS.md §3.1)") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);
    husk::gltf::Material::TextureTransform xf;
    xf.constant = true;
    xf.translation = {0.1f, 0.2f, 0.0f};
    xf.rotation[0] = 0;
    xf.rotation[1] = 0;
    xf.rotation[2] = 0.7071f;
    xf.rotation[3] = 0.7071f;
    xf.scaling = {2.0f, 3.0f, 1.0f};
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    const auto& tf = extras.Get("texture_transform");
    REQUIRE(tf.IsObject());
    CHECK(tf.Get("constant").Get<bool>() == true);
    CHECK(tf.Get("translation").Get(0).GetNumberAsDouble() == doctest::Approx(0.1));
    CHECK(tf.Get("translation").Get(1).GetNumberAsDouble() == doctest::Approx(0.2));
    CHECK(tf.Get("rotation").Get(2).GetNumberAsDouble() == doctest::Approx(0.7071));
    CHECK(tf.Get("rotation").Get(3).GetNumberAsDouble() == doctest::Approx(0.7071));
    CHECK(tf.Get("scaling").Get(0).GetNumberAsDouble() == doctest::Approx(2.0));
    CHECK(tf.Get("scaling").Get(1).GetNumberAsDouble() == doctest::Approx(3.0));
}

TEST_CASE("writeGlb: a material with no textureTransform gets no such extras key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

TEST_CASE("writeGlb: additionalTextureLayers and textureTransform extras coexist on the same "
          "material without one clobbering the other") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);
    husk::gltf::Material::AdditionalTextureLayer layer;
    layer.fileDataId = 42;
    materials[0].additionalTextureLayers = {layer};
    husk::gltf::Material::TextureTransform xf;
    xf.constant = false;
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    REQUIRE(extras.Get("additional_textures").IsArray());
    CHECK(extras.Get("additional_textures").Get(0).Get("file_data_id").GetNumberAsInt() == 42);
    REQUIRE(extras.Get("texture_transform").IsObject());
    CHECK(extras.Get("texture_transform").Get("constant").Get<bool>() == false);
}

// Regression test for FAILURES2.md #2: glTF 2.0 requires every accessor's
// total byte offset to be a multiple of its component type's size (4 bytes
// for the FLOAT/UNSIGNED_INT accessors husk emits) -- the
// ACCESSOR_TOTAL_OFFSET_ALIGNMENT rule the Khronos glTF-Validator enforces.
// Before the fix, appendBufferView never padded the shared buffer, so an
// embedded image of a byte length that wasn't itself a multiple of 4 (true
// of essentially every real PNG, and true of this fixture's own 70-byte
// onePixelPng, reused from the test above) silently misaligned every
// bufferView appended after it -- concretely, the very next primitive's
// index accessor. Checked generically (every bufferView in the whole
// document, not just the one known-affected pair) so this also guards the
// inverse-bind-matrix/animation-sampler buffer views against the same class
// of regression.
TEST_CASE("writeGlb: every bufferView stays 4-byte aligned even after an odd-length embedded image") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    REQUIRE(onePixelPng.size() % 4 != 0);  // confirms this fixture actually exercises the bug

    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPng;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(!model.bufferViews.empty());
    for (size_t i = 0; i < model.bufferViews.size(); ++i) {
        INFO("bufferView ", i, " byteOffset=", model.bufferViews[i].byteOffset);
        CHECK(model.bufferViews[i].byteOffset % 4 == 0);
    }
}

TEST_CASE("writeGlb: a material without baseColorImagePng gets no image/texture") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);
    CHECK(model.images.empty());
    CHECK(model.textures.empty());
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorTexture.index == -1);
}

TEST_CASE("writeGlb: a material's baseColorFactor round-trips through tinygltf's own loader") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorFactor[0] = 1.0f;
    materials[0].baseColorFactor[1] = 0.5f;
    materials[0].baseColorFactor[2] = 0.25f;
    materials[0].baseColorFactor[3] = 0.75f;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.materials[0].pbrMetallicRoughness.baseColorFactor.size() == 4);
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorFactor[0] == doctest::Approx(1.0));
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorFactor[1] == doctest::Approx(0.5));
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorFactor[2] == doctest::Approx(0.25));
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorFactor[3] == doctest::Approx(0.75));
}

TEST_CASE("writeGlb: mesh.texCoords2 adds a TEXCOORD_1 accessor shared across primitives") {
    auto mesh = buildTriangleMesh();
    mesh.texCoords2 = {{0.1f, 0.2f}, {0.3f, 0.4f}, {0.5f, 0.6f}};

    auto glb = husk::gltf::writeGlb(mesh);
    auto model = loadBack(glb);

    const auto& prim = model.meshes[0].primitives[0];
    REQUIRE(prim.attributes.count("TEXCOORD_1") == 1);
    int uv2Idx = prim.attributes.at("TEXCOORD_1");
    REQUIRE(model.accessors[uv2Idx].count == 3);
    const auto& acc = model.accessors[uv2Idx];
    const auto& view = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[view.buffer];
    std::vector<husk::gltf::Vec2> uv2(3);
    std::memcpy(uv2.data(), buf.data.data() + view.byteOffset + acc.byteOffset,
                3 * sizeof(husk::gltf::Vec2));
    CHECK(uv2[1].x == doctest::Approx(0.3f));
    CHECK(uv2[2].y == doctest::Approx(0.6f));
}

TEST_CASE("writeGlb: no mesh.texCoords2 means no TEXCOORD_1 attribute at all") {
    auto glb = husk::gltf::writeGlb(buildTriangleMesh());
    auto model = loadBack(glb);
    CHECK(model.meshes[0].primitives[0].attributes.count("TEXCOORD_1") == 0);
}

TEST_CASE("writeGlb: mesh.texCoords2 of the wrong length throws") {
    auto mesh = buildTriangleMesh();
    mesh.texCoords2 = {{0, 0}, {1, 1}};  // 2 entries, but 3 positions
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: a material's baseColorTexCoord selects TEXCOORD_1 on its baseColorTexture "
          "when mesh.texCoords2 is present") {
    auto mesh = buildTriangleMesh();
    mesh.texCoords2 = {{0, 0}, {1, 0}, {0, 1}};
    mesh.primitives[0].materialIndex = 0;

    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPng;
    materials[0].baseColorTexCoord = 1;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorTexture.texCoord == 1);
}

TEST_CASE("writeGlb: an animation round-trips translation/rotation/scale keyframes through "
          "tinygltf's own loader") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 1;  // the "mid" joint
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 2, 0}, {0, 3, 0}};
    ja.rotationTimes = {0.0f, 1.0f};
    ja.rotationValues = {{0, 0, 0, 1}, {0.7071f, 0, 0, 0.7071f}};
    ja.scaleTimes = {0.0f};
    ja.scaleValues = {{2, 2, 2}};

    husk::gltf::Animation anim;
    anim.name = "anim_100_0";
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    const auto& ga = model.animations[0];
    CHECK(ga.name == "anim_100_0");
    REQUIRE(ga.channels.size() == 3);

    // Joint 1 -> node index 2 (mesh node is 0, joint i is node 1+i).
    for (const auto& ch : ga.channels) {
        CHECK(ch.target_node == 2);
    }

    auto findChannel = [&](const std::string& path) -> const tinygltf::AnimationChannel& {
        for (const auto& ch : ga.channels) {
            if (ch.target_path == path) return ch;
        }
        FAIL("no channel for path " << path);
        return ga.channels[0];
    };

    const auto& transCh = findChannel("translation");
    const auto& transSamp = ga.samplers[transCh.sampler];
    CHECK(transSamp.interpolation == "LINEAR");
    const auto& transOutAcc = model.accessors[transSamp.output];
    REQUIRE(transOutAcc.count == 2);
    const auto& transOutView = model.bufferViews[transOutAcc.bufferView];
    const auto& transBuf = model.buffers[transOutView.buffer];
    std::vector<husk::gltf::Vec3> transValues(2);
    std::memcpy(transValues.data(), transBuf.data.data() + transOutView.byteOffset,
                2 * sizeof(husk::gltf::Vec3));
    CHECK(transValues[0].y == doctest::Approx(2));
    CHECK(transValues[1].y == doctest::Approx(3));

    const auto& transInAcc = model.accessors[transSamp.input];
    REQUIRE(transInAcc.minValues.size() == 1);
    CHECK(transInAcc.minValues[0] == doctest::Approx(0.0));
    CHECK(transInAcc.maxValues[0] == doctest::Approx(1.0));

    const auto& rotCh = findChannel("rotation");
    const auto& rotSamp = ga.samplers[rotCh.sampler];
    const auto& rotOutAcc = model.accessors[rotSamp.output];
    REQUIRE(rotOutAcc.count == 2);
    CHECK(rotOutAcc.type == TINYGLTF_TYPE_VEC4);
    const auto& rotOutView = model.bufferViews[rotOutAcc.bufferView];
    const auto& rotBuf = model.buffers[rotOutView.buffer];
    std::vector<float> rotValues(4 * 2);
    std::memcpy(rotValues.data(), rotBuf.data.data() + rotOutView.byteOffset,
                rotValues.size() * sizeof(float));
    // Keyframe 1: (0.7071, 0, 0, 0.7071) -- x/y/z/w order preserved.
    CHECK(rotValues[4 + 0] == doctest::Approx(0.7071f));
    CHECK(rotValues[4 + 3] == doctest::Approx(0.7071f));

    const auto& scaleCh = findChannel("scale");
    const auto& scaleSamp = ga.samplers[scaleCh.sampler];
    CHECK(model.accessors[scaleSamp.output].count == 1);
}

TEST_CASE("writeGlb: JointAnimation's per-property step flags become STEP samplers, "
          "independent of one another") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 0, 0}, {1, 1, 1}};
    ja.translationStep = true;
    ja.rotationTimes = {0.0f, 1.0f};
    ja.rotationValues = {{0, 0, 0, 1}, {0, 0, 0, 1}};
    // rotationStep left false (the default) -- proves the three flags are
    // read independently, not one shared setting for the whole joint.

    husk::gltf::Animation anim;
    anim.joints = {ja};
    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    const auto& ga = model.animations[0];
    for (const auto& ch : ga.channels) {
        const auto& samp = ga.samplers[ch.sampler];
        if (ch.target_path == "translation") {
            CHECK(samp.interpolation == "STEP");
        } else if (ch.target_path == "rotation") {
            CHECK(samp.interpolation == "LINEAR");
        }
    }
}

TEST_CASE("writeGlb: a JointAnimation with only some TRS properties populated emits only those "
          "channels") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{1, 1, 1}};
    // rotation/scale left empty.

    husk::gltf::Animation anim;
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    REQUIRE(model.animations[0].channels.size() == 1);
    CHECK(model.animations[0].channels[0].target_path == "translation");
}

TEST_CASE("writeGlb: animations given without a skeleton throws") {
    auto mesh = buildTriangleMesh();
    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, nullptr, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation joint index out of range for the skeleton throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::JointAnimation ja;
    ja.joint = 99;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation joint with mismatched time/value counts throws") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();
    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 0, 0}};  // only 1 value for 2 times
    husk::gltf::Animation anim;
    anim.joints = {ja};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh, {}, &skel, {anim}), husk::gltf::Error);
}

TEST_CASE("writeGlb: an animation's sequenceMetadata round-trips as sequence_metadata on the clip's "
          "own extras (M2_GAPS_TODO.md's former Item 1)") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};

    husk::gltf::Animation anim;
    anim.name = "anim_100_0";
    anim.joints = {ja};
    husk::gltf::Animation::SequenceMetadata sm;
    sm.movespeed = 4.5f;
    sm.frequency = -3;
    sm.replayMin = 10;
    sm.replayMax = 20;
    sm.blendTimeIn = 30;
    sm.blendTimeOut = 40;
    sm.boundsMin = {1, 2, 3};
    sm.boundsMax = {4, 5, 6};
    sm.boundsRadius = 7.5f;
    sm.variationNext = 5;
    sm.aliasNext = 9;
    sm.isAlias = true;
    anim.sequenceMetadata = sm;

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    const auto& extras = model.animations[0].extras;
    REQUIRE(extras.IsObject());
    const auto& meta = extras.Get("sequence_metadata");
    CHECK(meta.Get("movespeed").GetNumberAsDouble() == doctest::Approx(4.5));
    CHECK(meta.Get("frequency").GetNumberAsInt() == -3);
    CHECK(meta.Get("replay_min").GetNumberAsInt() == 10);
    CHECK(meta.Get("replay_max").GetNumberAsInt() == 20);
    CHECK(meta.Get("blend_time_in").GetNumberAsInt() == 30);
    CHECK(meta.Get("blend_time_out").GetNumberAsInt() == 40);
    const auto& boundsMin = meta.Get("bounds_min");
    REQUIRE(boundsMin.IsArray());
    CHECK(boundsMin.Get(0).GetNumberAsDouble() == doctest::Approx(1));
    CHECK(boundsMin.Get(1).GetNumberAsDouble() == doctest::Approx(2));
    CHECK(boundsMin.Get(2).GetNumberAsDouble() == doctest::Approx(3));
    const auto& boundsMax = meta.Get("bounds_max");
    REQUIRE(boundsMax.IsArray());
    CHECK(boundsMax.Get(0).GetNumberAsDouble() == doctest::Approx(4));
    CHECK(boundsMax.Get(1).GetNumberAsDouble() == doctest::Approx(5));
    CHECK(boundsMax.Get(2).GetNumberAsDouble() == doctest::Approx(6));
    CHECK(meta.Get("bounds_radius").GetNumberAsDouble() == doctest::Approx(7.5));
    CHECK(meta.Get("variation_next").GetNumberAsInt() == 5);
    CHECK(meta.Get("alias_next").GetNumberAsInt() == 9);
    CHECK(meta.Get("is_alias").Get<bool>() == true);
}

TEST_CASE("writeGlb: an animation with no sequenceMetadata gets no extras on the clip") {
    auto mesh = buildSkinnedTriangleMesh();
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 0;
    ja.translationTimes = {0.0f};
    ja.translationValues = {{0, 0, 0}};
    husk::gltf::Animation anim;
    anim.joints = {ja};
    // sequenceMetadata left nullopt (default) -- e.g. a global_seq_<n> clip.

    auto glb = husk::gltf::writeGlb(mesh, {}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    CHECK_FALSE(model.animations[0].extras.IsObject());
}

TEST_CASE("writeGlb: baseColorTexCoord=1 without mesh.texCoords2 falls back to TEXCOORD_0, not "
          "a dangling reference") {
    auto mesh = buildTriangleMesh();  // no texCoords2
    mesh.primitives[0].materialIndex = 0;

    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPng;
    materials[0].baseColorTexCoord = 1;  // claims UV1, but there is no UV1

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);
    CHECK(model.materials[0].pbrMetallicRoughness.baseColorTexture.texCoord == 0);
}

// writeGlbMulti (husk export --lod all's underlying primitive, see
// README.md/src/cmd_export.cpp): multiple named meshes -- one node per LOD
// tier -- in a single .glb, optionally sharing one skeleton/animation set.
// writeGlb(mesh, materials, skeleton, animations) is defined in terms of
// this (see gltf.cpp) as the one-entry, unnamed case -- already exercised
// by every test above -- so these focus on what's genuinely new: multiple
// mesh nodes, per-entry material numbering, and joint-node index offsets
// when mesh nodes occupy the low indices instead of just node 0.

TEST_CASE("writeGlbMulti: one node (with its own mesh) per entry, named and scened") {
    husk::gltf::NamedMesh a{"lod0", buildTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildTriangleMesh(), {}};
    auto glb = husk::gltf::writeGlbMulti({a, b});
    auto model = loadBack(glb);

    REQUIRE(model.meshes.size() == 2);
    REQUIRE(model.nodes.size() == 2);
    CHECK(model.nodes[0].name == "lod0");
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[1].name == "lod1");
    CHECK(model.nodes[1].mesh == 1);
    REQUIRE(model.scenes[model.defaultScene].nodes.size() == 2);
    CHECK(model.scenes[model.defaultScene].nodes[0] == 0);
    CHECK(model.scenes[model.defaultScene].nodes[1] == 1);
}

TEST_CASE("writeGlbMulti: empty meshes throws") {
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({}), husk::gltf::Error);
}

TEST_CASE("writeGlbMulti: empty meshes with a skeleton succeeds -- no mesh node, but joints and "
          "emitter anchors still export (the real geometry-less-VFX-model shape, see DESIGN.md)") {
    auto skel = buildChainSkeleton();
    skel.particleAnchors = {{7, 1, {0.1f, 0.2f, 0.3f}}};
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    CHECK(model.meshes.empty());
    REQUIRE(model.nodes.size() == 3);  // just the 3 joint nodes, no mesh node
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 3);
    REQUIRE(model.scenes[model.defaultScene].nodes.size() == 1);  // root joint only
    CHECK(model.scenes[model.defaultScene].nodes[0] == 0);
}

TEST_CASE("writeGlbMulti: empty meshes with a skeleton that has no joints throws (same as no "
          "skeleton at all -- nothing to fall back to)") {
    husk::gltf::Skeleton emptySkel;
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({}, &emptySkel), husk::gltf::Error);
}

TEST_CASE("writeGlbMulti: each entry's materials are numbered locally, remapped into one shared "
          "glTF materials array") {
    auto quadA = buildTwoPrimitiveQuad();  // primitives reference materialIndex 0 and 1
    std::vector<husk::gltf::Material> materialsA(2);
    materialsA[0].name = "a_mat0";
    materialsA[1].name = "a_mat1";

    auto meshB = buildTriangleMesh();
    meshB.primitives[0].materialIndex = 0;  // local index 0 -- should NOT collide with a_mat0/1
    std::vector<husk::gltf::Material> materialsB(1);
    materialsB[0].name = "b_mat0";

    husk::gltf::NamedMesh a{"a", quadA, materialsA};
    husk::gltf::NamedMesh b{"b", meshB, materialsB};
    auto glb = husk::gltf::writeGlbMulti({a, b});
    auto model = loadBack(glb);

    REQUIRE(model.materials.size() == 3);
    CHECK(model.materials[0].name == "a_mat0");
    CHECK(model.materials[1].name == "a_mat1");
    CHECK(model.materials[2].name == "b_mat0");

    REQUIRE(model.meshes[0].primitives.size() == 2);
    CHECK(model.meshes[0].primitives[0].material == 0);
    CHECK(model.meshes[0].primitives[1].material == 1);
    REQUIRE(model.meshes[1].primitives.size() == 1);
    // meshB's local materialIndex 0 must resolve to the *global* index of
    // b_mat0 (2), not be reinterpreted as a_mat0 (0) -- the actual bug this
    // test exists to catch.
    CHECK(model.meshes[1].primitives[0].material == 2);
}

TEST_CASE("writeGlbMulti: a shared skeleton's joint nodes come after every mesh node, and every "
          "mesh node references the one shared skin") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildSkinnedTriangleMesh(), {}};
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlbMulti({a, b}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 2 /* mesh nodes */ + 3 /* joints */);
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[1].mesh == 1);
    REQUIRE(model.skins.size() == 1);
    CHECK(model.nodes[0].skin == 0);
    CHECK(model.nodes[1].skin == 0);

    // Joint node indices are offset by meshCount (2) -- root joint is node
    // 2, its child node 3, that one's child node 4 (see buildChainSkeleton).
    const auto& skin = model.skins[0];
    REQUIRE(skin.joints.size() == 3);
    CHECK(skin.joints[0] == 2);
    CHECK(skin.joints[1] == 3);
    CHECK(skin.joints[2] == 4);
    REQUIRE(model.nodes[2].children.size() == 1);
    CHECK(model.nodes[2].children[0] == 3);
    REQUIRE(model.nodes[3].children.size() == 1);
    CHECK(model.nodes[3].children[0] == 4);

    // Scene roots: both mesh nodes plus the one root joint node -- not the
    // whole joint chain (children are reached via the hierarchy, same as
    // writeGlb's single-mesh case).
    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 3);
    CHECK(sceneNodes[0] == 0);
    CHECK(sceneNodes[1] == 1);
    CHECK(sceneNodes[2] == 2);
}

TEST_CASE("writeGlbMulti: an animation's joint target node is offset by meshCount, same as the "
          "skeleton/skin's own joint nodes") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh b{"lod1", buildSkinnedTriangleMesh(), {}};
    auto skel = buildChainSkeleton();

    husk::gltf::JointAnimation ja;
    ja.joint = 1;  // "mid" joint -> node (meshCount=2) + 1 = 3
    ja.translationTimes = {0.0f, 1.0f};
    ja.translationValues = {{0, 2, 0}, {0, 5, 0}};
    husk::gltf::Animation anim;
    anim.name = "anim0";
    anim.joints = {ja};

    auto glb = husk::gltf::writeGlbMulti({a, b}, &skel, {anim});
    auto model = loadBack(glb);

    REQUIRE(model.animations.size() == 1);
    REQUIRE(model.animations[0].channels.size() == 1);
    CHECK(model.animations[0].channels[0].target_node == 3);
}

TEST_CASE("writeGlbMulti: one entry with skinning present but the wrong length throws, naming "
          "that entry") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    husk::gltf::Mesh badSkinning = buildTriangleMesh();
    badSkinning.skinning = {husk::gltf::JointWeights{}};  // 1 entry, but 3 positions
    husk::gltf::NamedMesh b{"lod1", badSkinning, {}};
    auto skel = buildChainSkeleton();
    CHECK_THROWS_AS(husk::gltf::writeGlbMulti({a, b}, &skel), husk::gltf::Error);
}

// A skeleton in scope doesn't force every mesh entry to be skinned -- an
// entry can opt out by leaving `mesh.skinning` empty (see gltf.hpp's
// writeGlbMulti doc comment; the concrete use case is an unskinned
// collision-mesh node alongside a skinned render mesh, cmd_export.cpp).
TEST_CASE("writeGlbMulti: a mesh entry with no skinning data, alongside a skinned one sharing the "
          "same skeleton, gets no glTF skin reference") {
    husk::gltf::NamedMesh skinned{"render", buildSkinnedTriangleMesh(), {}};
    husk::gltf::NamedMesh unskinned{"aux", buildTriangleMesh(), {}};  // no skinning at all
    auto skel = buildChainSkeleton();

    auto glb = husk::gltf::writeGlbMulti({skinned, unskinned}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() >= 2);
    CHECK(model.nodes[0].skin == 0);
    CHECK(model.nodes[1].skin == -1);
    REQUIRE(model.meshes.size() == 2);
    for (const auto& attr : model.meshes[1].primitives[0].attributes) {
        CHECK(attr.first != "JOINTS_0");
        CHECK(attr.first != "WEIGHTS_0");
    }
}

namespace {

// 3 independent roots, no shared parent -- a real M2 shape (a bone forest,
// not a tree; see gltf.hpp's Skeleton doc comment). Distinct
// from buildChainSkeleton, which is single-root by construction.
husk::gltf::Skeleton buildMultiRootSkeleton() {
    husk::gltf::Skeleton skel;
    skel.joints.push_back({-1, {0, 0, 0}, {0, 0, 0}});
    skel.joints.push_back({-1, {5, 0, 0}, {5, 0, 0}});
    skel.joints.push_back({-1, {0, 5, 0}, {0, 5, 0}});
    return skel;
}

}  // namespace

TEST_CASE("writeGlbMulti: a multi-root skeleton gets one synthesized non-joint parent node") {
    auto skel = buildMultiRootSkeleton();
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    // No mesh nodes -- 3 joint nodes plus exactly one synthetic node.
    REQUIRE(model.nodes.size() == 4);
    int syntheticIdx = 3;  // meshCount(0) + jointCount(3)

    REQUIRE(model.skins.size() == 1);
    const auto& skin = model.skins[0];
    // The concrete difference from Option 2 (a fake extra joint): joints
    // stays exactly skeleton->joints.size(), no bogus extra entry.
    CHECK(skin.joints.size() == 3);
    CHECK(skin.skeleton == syntheticIdx);

    const auto& synth = model.nodes[syntheticIdx];
    REQUIRE(synth.children.size() == 3);
    CHECK(synth.children[0] == skin.joints[0]);
    CHECK(synth.children[1] == skin.joints[1]);
    CHECK(synth.children[2] == skin.joints[2]);
    // Untouched/default transform -- a stray translation/rotation/scale
    // here would silently shift every former-root joint's whole subtree.
    CHECK(synth.translation.empty());
    CHECK(synth.rotation.empty());
    CHECK(synth.scale.empty());

    // Scene has exactly one root entry: the synthetic node, standing in for
    // all 3 real roots (reached via its own .children, not listed
    // individually).
    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 1);
    CHECK(sceneNodes[0] == syntheticIdx);
}

TEST_CASE("writeGlbMulti: a single-root skeleton's output is unaffected by the multi-root "
          "synthesis path -- no synthetic node, skin.skeleton left unset") {
    auto skel = buildChainSkeleton();  // single root by construction
    auto glb = husk::gltf::writeGlbMulti({}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 3);  // exactly the 3 joint nodes, no synthetic node
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 3);
    CHECK(model.skins[0].skeleton == -1);  // unset, same as before this feature existed

    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 1);
    CHECK(sceneNodes[0] == 0);  // the one real root joint node, not a synthetic one
}

TEST_CASE("writeGlbMulti: a multi-root skeleton alongside real mesh nodes -- synthetic node comes "
          "after every joint node, mesh nodes/skinning are unaffected") {
    husk::gltf::NamedMesh a{"lod0", buildSkinnedTriangleMesh(), {}};
    auto skel = buildMultiRootSkeleton();
    // Weight the mesh to a couple of the (now-root) joints directly, to
    // prove vertex joint indices stay raw/unremapped M2 bone indices even
    // though rootJointNodeIndices.size() > 1.
    auto& mesh = a.mesh;
    mesh.skinning[0].joints[0] = 0;
    mesh.skinning[1].joints[0] = 2;

    auto glb = husk::gltf::writeGlbMulti({a}, &skel);
    auto model = loadBack(glb);

    REQUIRE(model.nodes.size() == 1 /* mesh */ + 3 /* joints */ + 1 /* synthetic */);
    int syntheticIdx = 4;  // meshCount(1) + jointCount(3)
    CHECK(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);

    const auto& skin = model.skins[0];
    CHECK(skin.joints.size() == 3);
    CHECK(skin.joints[0] == 1);  // meshCount + 0
    CHECK(skin.joints[2] == 3);  // meshCount + 2
    CHECK(skin.skeleton == syntheticIdx);
    CHECK(model.nodes[syntheticIdx].children.size() == 3);

    const auto& sceneNodes = model.scenes[model.defaultScene].nodes;
    REQUIRE(sceneNodes.size() == 2);  // mesh node + the one synthetic root
    CHECK(sceneNodes[0] == 0);
    CHECK(sceneNodes[1] == syntheticIdx);
}
