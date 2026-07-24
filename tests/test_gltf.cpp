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
    mesh.indices = {0, 1, 2};
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

TEST_CASE("writeGlb: empty indices throws") {
    auto mesh = buildTriangleMesh();
    mesh.indices.clear();
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}

TEST_CASE("writeGlb: indices count not a multiple of 3 throws") {
    auto mesh = buildTriangleMesh();
    mesh.indices = {0, 1};
    CHECK_THROWS_AS(husk::gltf::writeGlb(mesh), husk::gltf::Error);
}
