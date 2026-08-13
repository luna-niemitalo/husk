// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- real
// M2TextureTransform fixtures specifically (KHR_texture_transform on the
// constant, planar-rotation case -- gltf_mesh.cpp's textureTransformToKhr,
// derivation in DESIGN.md's Key design decisions) -- split out of
// test_integration.cpp to keep it under FILE_SIZE.md's 1000-line ceiling,
// same "deliberately not mocked, shape-only assertions where possible"
// policy as the file it was split from. See that file's own doc comment.

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testTextureTransformRotationM2;
using husk::test::testTextureTransformRotationSkin;
using husk::test::testTextureTransformScaleM2;
using husk::test::testTextureTransformScaleSkin;
using husk::test::testTextureTransformTranslationM2;
using husk::test::testTextureTransformTranslationSkin;

// Constant-case M2TextureTransform gets a real, pivot-corrected
// KHR_texture_transform (gltf_mesh.cpp's textureTransformToKhr, derivation
// in DESIGN.md's Key design decisions) instead of staying extras-only.
// These two fixtures exercise the two ends of the real-corpus behavior
// found while building it: bloodknightcharger.m2's transform index 2 is a
// genuine single resolved value (rotation + non-uniform scale, no
// per-sequence structure at all) and gets the real extension;
// brewfestmount.m2's transform index 0 turned out, on closer inspection, to
// carry real per-sequence translation/scaling tracks whose values just
// happen to all be identity -- husk's own stricter constant check
// (m2_animation.cpp's trackHasAnimatedData, which distinguishes "no track
// at all" from "a structured-but-trivial track") correctly refuses to treat
// that as the constant case, so it stays extras-only even though a cruder
// scan (this fixture's own discovery tool,
// tools/find_texture_transform_files.py) reported it as constant. A real,
// useful negative case, not a fixture mistake -- see DESIGN.md's Key design
// decisions for the exact real values.
//
// Neither fixture has its real embedded texture committed to test_data/
// (the .blp files are large and, for this test, unnecessary -- only a
// *file existing* at the expected FileDataID path matters, not its real
// pixel content) -- a synthetic 1x1 PNG is written to a scratch dir under
// the real FileDataID name instead, same fixture-construction shape
// test_data_paths.hpp's autoSkinDir/autoBonesDir already use.
void writeOnePixelPng(const std::filesystem::path& path) {
    static const std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::ofstream f(path, std::ios::binary);
    REQUIRE(f.good());
    f.write(reinterpret_cast<const char*>(onePixelPng.data()),
            static_cast<std::streamsize>(onePixelPng.size()));
}

// The one material, out of a real export's whole material list, whose
// texture_transform extras' scaling.y is close to `expectedScaleY` --
// bloodknightcharger's fdid 7449869 backs two materials (transform indices
// 1 and 2), and only index 2 has a non-default scale, so this is enough to
// pick it out uniquely without hardcoding a material-array index that a
// dedup/ordering change could silently shift.
const tinygltf::Material* findMaterialByTransformScaleY(const tinygltf::Model& model,
                                                          double expectedScaleY) {
    for (const auto& m : model.materials) {
        if (!m.extras.IsObject()) continue;
        const auto& tf = m.extras.Get("texture_transform");
        if (!tf.IsObject()) continue;
        if (std::abs(tf.Get("scaling").Get(1).GetNumberAsDouble() - expectedScaleY) < 1e-4) {
            return &m;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("husk export: bloodknightcharger.m2's real constant rotation+scale textureTransform "
          "(transform index 2) gets a real, pivot-corrected KHR_texture_transform matching the "
          "hand-derived expected values" *
          doctest::skip(testTextureTransformScaleM2().empty() ||
                         testTextureTransformScaleSkin().empty())) {
    auto texDir = std::filesystem::temp_directory_path() / "husk-test-texture-transform-scale-tex";
    std::filesystem::create_directories(texDir);
    writeOnePixelPng(texDir / "7449869.png");

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-texture-transform-scale.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + testTextureTransformScaleM2() + "\" -o \"" + outPath +
                           "\" --skin \"" + testTextureTransformScaleSkin() + "\" --textures \"" +
                           texDir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    REQUIRE(std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(),
                       "KHR_texture_transform") != model.extensionsUsed.end());

    const tinygltf::Material* mat = findMaterialByTransformScaleY(model, 1.5);
    REQUIRE(mat != nullptr);
    CHECK(mat->extras.Get("texture_transform").Get("constant").Get<bool>() == true);

    const auto& bct = mat->pbrMetallicRoughness.baseColorTexture;
    REQUIRE(bct.extensions.count("KHR_texture_transform") == 1);
    const auto& khr = bct.extensions.at("KHR_texture_transform");
    // Hand-derived (DESIGN.md's Key design decisions) from the real
    // rotation (0,0,-1,0) / scale (1.0, 1.5, 0.0) this batch's
    // M2TextureTransform record carries, and cross-checked against 20,000
    // randomized trials of the client's own translate-rotate-translate
    // matrix composition.
    CHECK(khr.Get("offset").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
    CHECK(khr.Get("offset").Get(1).GetNumberAsDouble() == doctest::Approx(1.25));
    CHECK(std::abs(std::abs(khr.Get("rotation").GetNumberAsDouble()) - std::acos(-1.0)) < 1e-5);
    CHECK(khr.Get("scale").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
    CHECK(khr.Get("scale").Get(1).GetNumberAsDouble() == doctest::Approx(1.5));

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: brewfestmount.m2's real texture-center-pivot rotation, structurally "
          "non-constant despite every per-sequence value being identity, stays extras-only" *
          doctest::skip(testTextureTransformRotationM2().empty() ||
                         testTextureTransformRotationSkin().empty())) {
    auto texDir = std::filesystem::temp_directory_path() / "husk-test-texture-transform-rot-tex";
    std::filesystem::create_directories(texDir);
    writeOnePixelPng(texDir / "7194160.png");

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-texture-transform-rot.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + testTextureTransformRotationM2() + "\" -o \"" + outPath +
                           "\" --skin \"" + testTextureTransformRotationSkin() + "\" --textures \"" +
                           texDir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    CHECK(std::find(model.extensionsUsed.begin(), model.extensionsUsed.end(),
                     "KHR_texture_transform") == model.extensionsUsed.end());

    bool foundNonConstantTransform = false;
    for (const auto& m : model.materials) {
        if (!m.extras.IsObject()) continue;
        const auto& tf = m.extras.Get("texture_transform");
        if (!tf.IsObject()) continue;
        CHECK(tf.Get("constant").Get<bool>() == false);
        CHECK(m.pbrMetallicRoughness.baseColorTexture.extensions.count("KHR_texture_transform") == 0);
        foundNonConstantTransform = true;
    }
    REQUIRE(foundNonConstantTransform);

    std::filesystem::remove(outPath);
}

// The ANIMATED_TEXTURE_EFFECTS_TODO.md simple-test-case fixture -- see
// test_data_paths.hpp's kTextureTransformTranslationM2 doc comment. Unlike
// the two fixtures above (which only ever exercise the *constant* case),
// this one's translation track is genuinely animated -- the first real
// coverage of gltf::Material::textureTransformTranslationAnimation/
// resolveAnimatedColorCurve applied to a UV transform (export_materials.cpp)
// rather than a tint/fade curve.
TEST_CASE("husk export: unk_exp11_7037014.m2's genuinely-animated M2TextureTransform translation "
          "track (a real one-way X-axis UV scroll) is exported as a full "
          "'texture_transform_animation'.'translation' keyframe curve, not just the extras-only "
          "default the constant case gets" *
          doctest::skip(testTextureTransformTranslationM2().empty() ||
                         testTextureTransformTranslationSkin().empty())) {
    auto texDir = std::filesystem::temp_directory_path() / "husk-test-texture-transform-translate-tex";
    std::filesystem::create_directories(texDir);
    writeOnePixelPng(texDir / "7051491.png");

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-texture-transform-translate.glb")
                       .string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + testTextureTransformTranslationM2() + "\" -o \"" + outPath +
                           "\" --skin \"" + testTextureTransformTranslationSkin() + "\" --textures \"" +
                           texDir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    bool foundCurve = false;
    for (const auto& m : model.materials) {
        if (!m.extras.IsObject()) continue;
        const auto& tf = m.extras.Get("texture_transform");
        if (tf.IsObject()) {
            CHECK(tf.Get("constant").Get<bool>() == false);
        }
        const auto& anim = m.extras.Get("texture_transform_animation");
        if (!anim.IsObject()) continue;
        const auto& translation = anim.Get("translation");
        REQUIRE(translation.IsArray());
        REQUIRE(translation.ArrayLen() == 1);
        const auto& curve = translation.Get(0);
        CHECK(curve.Get("sequence_index").GetNumberAsInt() == 0);
        const auto& kfs = curve.Get("keyframes");
        REQUIRE(kfs.ArrayLen() == 2);
        // Real values found via a real-byte scan of this fixture (see
        // test_data_paths.hpp's doc comment): 0ms/(0,0,0), 4167ms/(1,0,0) --
        // a one-way X-axis UV scroll, not a ping-pong.
        CHECK(kfs.Get(0).Get("time").GetNumberAsDouble() == doctest::Approx(0.0));
        CHECK(kfs.Get(0).Get("value").Get(0).GetNumberAsDouble() == doctest::Approx(0.0));
        CHECK(kfs.Get(1).Get("time").GetNumberAsDouble() == doctest::Approx(4.167));
        CHECK(kfs.Get(1).Get("value").Get(0).GetNumberAsDouble() == doctest::Approx(1.0));
        // Rotation/scaling tracks are both genuinely empty on this fixture
        // (see test_data_paths.hpp) -- their curve arrays must stay absent,
        // not present-but-empty (gltf_mesh.cpp only ever writes a sub-key
        // when its own vector is non-empty).
        CHECK(!anim.Get("rotation").IsArray());
        CHECK(!anim.Get("scaling").IsArray());
        foundCurve = true;
    }
    REQUIRE(foundCurve);

    std::filesystem::remove(outPath);
}
