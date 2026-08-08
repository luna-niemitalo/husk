// Tests for husk::gltf's mesh module (src/gltf_mesh.hpp/.cpp): JointWeights/
// Material/Primitive/Mesh/NamedMesh -- exercised through writeGlb's
// mesh/material/texture/geoset extras handling.
// Split out of the former tests/test_gltf.cpp -- see FILE_SPLIT_TODO.md
// Item 5.

#include <cmath>

#include "test_gltf_fixtures.hpp"

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
    materials[0].baseColorImageName = "bloodelffemale_hd_hair_color_5196731";

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].mimeType == "image/png");
    // tinygltf's loader decoded it -- confirms it's not just bytes that
    // happen to round-trip, but an image a real glTF consumer can read.
    CHECK(model.images[0].width == 1);
    CHECK(model.images[0].height == 1);
    // Real source filename, not left blank -- without this, Blender's own
    // glTF importer falls back to an auto-generated "Image_<N>" name
    // (reported directly against a real export: every embedded image
    // showed up in Blender as "Image_0".."Image_113", not the real,
    // useful filename husk already knew).
    CHECK(model.images[0].name == "bloodelffemale_hd_hair_color_5196731");

    REQUIRE(model.textures.size() == 1);
    CHECK(model.textures[0].source == 0);
    CHECK(model.textures[0].name == "bloodelffemale_hd_hair_color_5196731");
    REQUIRE(model.materials[0].pbrMetallicRoughness.baseColorTexture.index == 0);
}

// geoset (skinSectionId) and multi-texture-layer metadata are exposed as
// inert glTF `extras` -- husk doesn't filter geosets or fake WoW's
// texture-combiner math, but a custom renderer or Blender script (mesh
// mask / geometry nodes / driven material) can use this to implement its
// own selection, the same "tag it, don't guess at semantics" treatment
// `billboardMode` already gets (see gltf.hpp's Primitive::skinSectionId /
// Material::AdditionalTextureLayer doc comments).
// TODO: Remove: FAILURES2.md #1/#6 (the findings these are regression tests for).
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
    // No filename tracked for this layer -- the FileDataID is still a real
    // improvement over an auto-generated "Image_<N>".
    CHECK(model.images[model.textures[texIdx].source].name == "555");

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

TEST_CASE("writeGlb: a material's textureTransform round-trips as extras") {
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

namespace {
// Shared by the KHR_texture_transform test cases below -- see
// gltf_mesh.cpp's textureTransformToKhr and DESIGN.md's Key design
// decisions for where the hand-derived expected numbers below come from.
std::vector<uint8_t> onePixelPngForTextureTransformTests() {
    return {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
            0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
            0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78,
            0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55,
            0xCE, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
}
}  // namespace

// Real KHR_texture_transform for the constant case -- 180-degree rotation
// (quaternion (0,0,-1,0)) plus a real non-uniform scale (1.0, 1.5), the
// exact real values bloodknightcharger.m2's transform index 2 carries.
// Expected offset/rotation/scale hand-derived (and cross-checked against
// 20,000 randomized trials of the client's own translate-rotate-translate
// composition, see DESIGN.md's Key design decisions and the derivation
// comment on gltf_mesh.cpp's textureTransformToKhr) and confirmed to match
// husk's own real export of that exact fixture.
TEST_CASE("writeGlb: a constant, planar-rotation textureTransform with a baseColorTexture gets a "
          "real KHR_texture_transform, pivot-corrected from M2's texture-center rotation to "
          "glTF's origin-pivoted one") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPngForTextureTransformTests();
    husk::gltf::Material::TextureTransform xf;
    xf.constant = true;
    xf.rotation[0] = 0;
    xf.rotation[1] = 0;
    xf.rotation[2] = -1.0f;
    xf.rotation[3] = 0.0f;
    xf.scaling = {1.0f, 1.5f, 0.0f};
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    REQUIRE(model.extensionsUsed.size() == 1);
    CHECK(model.extensionsUsed[0] == "KHR_texture_transform");

    const auto& bct = model.materials[0].pbrMetallicRoughness.baseColorTexture;
    REQUIRE(bct.extensions.count("KHR_texture_transform") == 1);
    const auto& khr = bct.extensions.at("KHR_texture_transform");
    CHECK(khr.Get("offset").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
    CHECK(khr.Get("offset").Get(1).GetNumberAsDouble() == doctest::Approx(1.25));
    // -pi and pi are the same rotation (cos/sin agree); normalize before comparing.
    double rotation = khr.Get("rotation").GetNumberAsDouble();
    CHECK(std::abs(std::abs(rotation) - std::acos(-1.0)) < 1e-6);
    CHECK(khr.Get("scale").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
    CHECK(khr.Get("scale").Get(1).GetNumberAsDouble() == doctest::Approx(1.5));

    // Raw values still surfaced as extras too (diagnostic, matches every
    // other textureTransform test in this file).
    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("texture_transform").Get("constant").Get<bool>() == true);
}

TEST_CASE("writeGlb: a non-constant (animated) textureTransform never gets a real "
          "KHR_texture_transform, even with a baseColorTexture present") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPngForTextureTransformTests();
    husk::gltf::Material::TextureTransform xf;
    xf.constant = false;  // the animated case -- see TextureTransform's doc comment
    xf.rotation[2] = -1.0f;
    xf.rotation[3] = 0.0f;
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK(model.extensionsUsed.empty());
    const auto& bct = model.materials[0].pbrMetallicRoughness.baseColorTexture;
    CHECK(bct.extensions.count("KHR_texture_transform") == 0);
    // The animated case's only representation is still the raw extras.
    CHECK(model.materials[0].extras.Get("texture_transform").Get("constant").Get<bool>() == false);
}

TEST_CASE("writeGlb: a constant textureTransform with no baseColorTexture to attach to stays "
          "extras-only") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);  // no baseColorImagePng
    husk::gltf::Material::TextureTransform xf;
    xf.constant = true;
    xf.rotation[2] = -1.0f;
    xf.rotation[3] = 0.0f;
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK(model.extensionsUsed.empty());
    REQUIRE(model.materials[0].extras.IsObject());
    CHECK(model.materials[0].extras.Get("texture_transform").Get("constant").Get<bool>() == true);
}

TEST_CASE("writeGlb: a constant textureTransform whose rotation isn't planar (a genuine 3-axis "
          "rotation, never seen in real data) has no honest KHR_texture_transform equivalent and "
          "stays extras-only") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;

    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorImagePng = onePixelPngForTextureTransformTests();
    husk::gltf::Material::TextureTransform xf;
    xf.constant = true;
    // A rotation with a real X component -- not just a Z-axis (planar) one.
    xf.rotation[0] = 0.7071f;
    xf.rotation[1] = 0;
    xf.rotation[2] = 0;
    xf.rotation[3] = 0.7071f;
    materials[0].textureTransform = xf;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK(model.extensionsUsed.empty());
    const auto& bct = model.materials[0].pbrMetallicRoughness.baseColorTexture;
    CHECK(bct.extensions.count("KHR_texture_transform") == 0);
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

// M2Texture::type, when nonzero (a hardcoded/
// replaceable slot -- character skin, hair, item tint), round-trips as a
// "texture_type" material extras key.
TEST_CASE("writeGlb: a material's nonzero textureType round-trips as 'texture_type' extras") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].textureType = 2;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("texture_type").GetNumberAsInt() == 2);
}

// The present-only-when-nonzero decision (gltf.hpp's Material::textureType
// doc comment): 0 is the ordinary, filename-based case, and matches every
// other extras field's own "absence means nothing extra to say" convention
// -- so a batch whose primary texture is real (type 0) gets no
// "texture_type" key at all, same as a material with no textureType set
// (the struct's own default).
TEST_CASE("writeGlb: a material with textureType == 0 gets no 'texture_type' extras key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].textureType = 0;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

// gltf.hpp's Material::baseColorTextureFileDataId doc comment: this is
// recorded independently of which local file actually supplied
// baseColorImagePng -- a real FileDataID resolved by husk stays traceable
// even when a differently-named file won the embed (same "tag it, don't
// guess at semantics" treatment as textureType above).
TEST_CASE("writeGlb: a material's nonzero baseColorTextureFileDataId round-trips as "
          "'texture_file_data_id' extras") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorTextureFileDataId = 1034713;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("texture_file_data_id").GetNumberAsInt() == 1034713);
}

TEST_CASE("writeGlb: a material with baseColorTextureFileDataId == 0 gets no "
          "'texture_file_data_id' extras key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].baseColorTextureFileDataId = 0;

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

// a genuinely-animated M2Color::color curve round-trips
// as "tint_animation" extras -- one entry per resolved M2Sequence, plus a
// global-sequence entry (sequence_index omitted) when present.
TEST_CASE("writeGlb: a material's tintAnimation round-trips as 'tint_animation' extras, "
          "per-sequence and global-sequence alike") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    husk::gltf::Material::AnimatedColorCurve perSeq;
    perSeq.sequenceIndex = 3;
    perSeq.keyframes = {{0.0f, {1.0f, 0.0f, 0.0f}}, {0.5f, {0.0f, 1.0f, 0.0f}}};
    husk::gltf::Material::AnimatedColorCurve global;
    global.sequenceIndex = -1;
    global.keyframes = {{0.0f, {0.2f, 0.3f, 0.4f}}};
    materials[0].tintAnimation = {perSeq, global};

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    const auto& curves = extras.Get("tint_animation");
    REQUIRE(curves.IsArray());
    REQUIRE(curves.ArrayLen() == 2);

    const auto& c0 = curves.Get(0);
    CHECK(c0.Get("sequence_index").GetNumberAsInt() == 3);
    REQUIRE(c0.Get("keyframes").IsArray());
    REQUIRE(c0.Get("keyframes").ArrayLen() == 2);
    CHECK(c0.Get("keyframes").Get(0).Get("time").GetNumberAsDouble() == doctest::Approx(0.0));
    CHECK(c0.Get("keyframes").Get(0).Get("value").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
    CHECK(c0.Get("keyframes").Get(1).Get("value").Get(1).GetNumberAsDouble() == doctest::Approx(1.0));

    const auto& c1 = curves.Get(1);
    CHECK_FALSE(c1.Get("sequence_index").IsInt());  // global-sequence entry omits it
    CHECK(c1.Get("keyframes").Get(0).Get("value").Get(2).GetNumberAsDouble() == doctest::Approx(0.4));
}

TEST_CASE("writeGlb: a material with no tintAnimation gets no 'tint_animation' extras key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

// alphaFadeAnimation/weightFadeAnimation (M2Color::alpha/
// M2TextureWeight::weight) both nest under one "fade_animation" extras key,
// each present independently -- husk doesn't combine the two curves itself
// (see gltf.hpp's weightFadeAnimation doc comment).
TEST_CASE("writeGlb: alphaFadeAnimation/weightFadeAnimation round-trip as 'fade_animation'.'alpha'/"
          "'weight' extras") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    husk::gltf::Material::AnimatedScalarCurve alpha;
    alpha.sequenceIndex = 0;
    alpha.keyframes = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    materials[0].alphaFadeAnimation = {alpha};

    husk::gltf::Material::AnimatedScalarCurve weight;
    weight.sequenceIndex = -1;
    weight.keyframes = {{0.0f, 0.5f}};
    materials[0].weightFadeAnimation = {weight};

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    const auto& fade = extras.Get("fade_animation");
    REQUIRE(fade.IsObject());

    const auto& alphaCurves = fade.Get("alpha");
    REQUIRE(alphaCurves.IsArray());
    CHECK(alphaCurves.Get(0).Get("sequence_index").GetNumberAsInt() == 0);
    CHECK(alphaCurves.Get(0).Get("keyframes").Get(1).Get("value").GetNumberAsDouble() ==
          doctest::Approx(0.0));

    const auto& weightCurves = fade.Get("weight");
    REQUIRE(weightCurves.IsArray());
    CHECK_FALSE(weightCurves.Get(0).Get("sequence_index").IsInt());
    CHECK(weightCurves.Get(0).Get("keyframes").Get(0).Get("value").GetNumberAsDouble() ==
          doctest::Approx(0.5));
}

TEST_CASE("writeGlb: only alphaFadeAnimation set means 'fade_animation' has no 'weight' key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    husk::gltf::Material::AnimatedScalarCurve alpha;
    alpha.keyframes = {{0.0f, 1.0f}};
    materials[0].alphaFadeAnimation = {alpha};

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& fade = model.materials[0].extras.Get("fade_animation");
    REQUIRE(fade.IsObject());
    CHECK(fade.Get("alpha").IsArray());
    CHECK_FALSE(fade.Get("weight").IsArray());
}

TEST_CASE("writeGlb: a material with neither alphaFadeAnimation nor weightFadeAnimation gets no "
          "'fade_animation' extras key") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    CHECK_FALSE(model.materials[0].extras.IsObject());
}

TEST_CASE("writeGlb: textureType/tintAnimation/fade_animation extras coexist with "
          "additionalTextureLayers/textureTransform on the same material") {
    auto mesh = buildTriangleMesh();
    mesh.primitives[0].materialIndex = 0;
    std::vector<husk::gltf::Material> materials(1);
    materials[0].textureType = 5;
    husk::gltf::Material::AdditionalTextureLayer layer;
    layer.fileDataId = 42;
    materials[0].additionalTextureLayers = {layer};
    husk::gltf::Material::TextureTransform xf;
    materials[0].textureTransform = xf;
    husk::gltf::Material::AnimatedColorCurve tint;
    tint.keyframes = {{0.0f, {1, 1, 1}}};
    materials[0].tintAnimation = {tint};
    husk::gltf::Material::AnimatedScalarCurve alpha;
    alpha.keyframes = {{0.0f, 1.0f}};
    materials[0].alphaFadeAnimation = {alpha};

    auto glb = husk::gltf::writeGlb(mesh, materials);
    auto model = loadBack(glb);

    const auto& extras = model.materials[0].extras;
    REQUIRE(extras.IsObject());
    CHECK(extras.Get("texture_type").GetNumberAsInt() == 5);
    CHECK(extras.Get("additional_textures").IsArray());
    CHECK(extras.Get("texture_transform").IsObject());
    CHECK(extras.Get("tint_animation").IsArray());
    CHECK(extras.Get("fade_animation").Get("alpha").IsArray());
}

// glTF 2.0 requires every accessor's total byte offset to be a multiple of
// its component type's size (4 bytes for the FLOAT/UNSIGNED_INT accessors
// husk emits) -- the ACCESSOR_TOTAL_OFFSET_ALIGNMENT rule the Khronos
// glTF-Validator enforces. An embedded image of a byte length that isn't
// itself a multiple of 4 (true of essentially every real PNG, and true of
// this fixture's own 70-byte onePixelPng, reused from the test above)
// would silently misalign every bufferView appended after it -- concretely,
// the very next primitive's index accessor -- if appendBufferView didn't
// pad the shared buffer. Checked generically (every bufferView in the
// whole document, not just the one known-affected pair) so this also
// guards the inverse-bind-matrix/animation-sampler buffer views against
// the same class of regression.
// TODO: Remove: FAILURES2.md #2 (the finding this is a regression test for).
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
