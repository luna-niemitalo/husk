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

TEST_CASE("writeGlb: skeleton given without matching mesh.skinning throws") {
    auto mesh = buildTriangleMesh();  // no skinning data
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
