// Runs the actual compiled `husk` binary against a real, game-extracted M2
// file. Deliberately not mocked, and deliberately not asserting on any
// specific model's field values (those belong in test_m2.cpp's synthetic
// fixtures, which encode the spec) -- this tier only answers "does it
// survive contact with a real file from the live game," the same
// smoke-test role test_integration.cpp plays in casc-tool.
//
// Point HUSK_TEST_M2 at any real .m2 extracted via casc-tool to run this
// for real:
//   HUSK_TEST_M2=/path/to/some.m2 ./build/husk-tests
// Without it, every case here is skipped (counted as passed), not failed.
//
// `husk export` additionally needs a matching .skin file (same model, same
// LOD) -- point HUSK_TEST_SKIN at one alongside HUSK_TEST_M2 to exercise it.
// A mismatched .skin (wrong model/LOD) is a real failure mode, not
// something to silently tolerate: skin::resolveTriangleIndices/cmd_export's
// own bounds check will throw if the skin references M2 vertices that don't
// exist, so get this pairing right (e.g. bloodelffemale.m2 pairs with
// bloodelffemale00.skin, its LOD0 -- not an "_hd" variant, which is a
// different, separate M2 file's sidecar).

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <tiny_gltf.h>

#include "run_husk.hpp"

namespace {

using husk::test::envOrEmpty;
using husk::test::runHusk;

// Shape-only skinning check: a real character model has bones, so this
// must have produced a glTF skin, not silently dropped it. Doesn't assert
// any model-specific bone count -- that belongs in test_m2.cpp's/
// test_skel.cpp's synthetic tests.
void checkSkinnedGlb(const std::string& outPath) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() > 0);
    CHECK(model.skins[0].inverseBindMatrices >= 0);
    REQUIRE(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);
    const auto& prim = model.meshes[0].primitives[0];
    CHECK(prim.attributes.count("JOINTS_0") == 1);
    CHECK(prim.attributes.count("WEIGHTS_0") == 1);
}

}  // namespace

TEST_CASE("husk info: real game-extracted M2 parses without error") {
    std::string path = envOrEmpty("HUSK_TEST_M2");
    if (path.empty()) {
        MESSAGE("SKIPPED (no real M2 file available -- set HUSK_TEST_M2)");
        return;
    }

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("format:") != std::string::npos);
    CHECK(result.output.find("version:") != std::string::npos);
    // A model file always has *some* geometry; a zero vertex count would
    // mean the parser landed on the wrong offsets even though it didn't crash.
    CHECK(result.output.find("vertices: 0 ") == std::string::npos);
}

TEST_CASE("husk export: real game-extracted M2 + matching .skin produce a well-formed glb") {
    std::string m2Path = envOrEmpty("HUSK_TEST_M2");
    std::string skinPath = envOrEmpty("HUSK_TEST_SKIN");
    if (m2Path.empty() || skinPath.empty()) {
        MESSAGE("SKIPPED (no real M2+.skin pair available -- set HUSK_TEST_M2 and HUSK_TEST_SKIN)");
        return;
    }

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    std::ifstream glb(outPath, std::ios::binary | std::ios::ate);
    REQUIRE(glb.is_open());
    // A real character model always has substantial geometry; a tiny or
    // empty file would mean something silently produced garbage instead of
    // failing loudly.
    CHECK(glb.tellg() > 10000);

    glb.seekg(0);
    char magic[4] = {};
    glb.read(magic, 4);
    CHECK(std::string(magic, 4) == "glTF");

    checkSkinnedGlb(outPath);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: real M2 + .skin resolves per-batch materials with a plausible alphaMode "
          "spread") {
    std::string m2Path = envOrEmpty("HUSK_TEST_M2");
    std::string skinPath = envOrEmpty("HUSK_TEST_SKIN");
    if (m2Path.empty() || skinPath.empty()) {
        MESSAGE("SKIPPED (no real M2+.skin pair available -- set HUSK_TEST_M2 and HUSK_TEST_SKIN)");
        return;
    }

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-mats.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // cmd_export.cpp only prints a material count when it actually built
    // some -- confirms the .skin's batches array wasn't silently empty.
    CHECK(result.output.find(" materials (") != std::string::npos);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    // A real character model has multiple submesh/batch groups (skin,
    // hair, tabard, ...) that don't all share one blend mode -- if every
    // primitive ended up OPAQUE, the batch->material resolution chain
    // silently fell back to defaults instead of actually reading the
    // .skin's batches. Shape-only, per this file's own stated philosophy
    // -- exact per-material values belong in tests/test_m2.cpp/test_skin.cpp's
    // synthetic fixtures.
    REQUIRE(model.meshes[0].primitives.size() > 1);
    REQUIRE(model.materials.size() > 1);
    std::set<std::string> alphaModes;
    for (const auto& m : model.materials) {
        alphaModes.insert(m.alphaMode);
    }
    CHECK(alphaModes.size() > 1);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: --textures embeds a real baseColorTexture image when one resolves") {
    std::string m2Path = envOrEmpty("HUSK_TEST_M2");
    std::string skinPath = envOrEmpty("HUSK_TEST_SKIN");
    std::string texturesDir = envOrEmpty("HUSK_TEST_TEXTURES_DIR");
    if (m2Path.empty() || skinPath.empty() || texturesDir.empty()) {
        MESSAGE(
            "SKIPPED (set HUSK_TEST_M2, HUSK_TEST_SKIN, and HUSK_TEST_TEXTURES_DIR -- a "
            "directory of husk-blp-converted '<FileDataID>.png' files -- to run this)");
        return;
    }

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-tex.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath +
                           "\" --textures \"" + texturesDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find(" with an embedded texture)") != std::string::npos);
    // The whole point of this test: at least one texture actually got
    // embedded, not just "materials exist but all imageless" (that's
    // already covered by the no-textures-dir test above).
    CHECK(result.output.find("(0 with an embedded texture)") == std::string::npos);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.images.size() > 0);
    // tinygltf decoded it -- a real image, not just bytes that happen to
    // sit in a bufferView.
    CHECK(model.images[0].width > 0);
    CHECK(model.images[0].height > 0);

    std::filesystem::remove(outPath);
}

TEST_CASE(
    "husk export: M2 with an external .skel (SKID-linked) skeleton produces a well-formed, "
    "skinned glb") {
    std::string m2Path = envOrEmpty("HUSK_TEST_SKEL_M2");
    std::string skinPath = envOrEmpty("HUSK_TEST_SKEL_SKIN");
    std::string skelPath = envOrEmpty("HUSK_TEST_SKEL");
    if (m2Path.empty() || skinPath.empty() || skelPath.empty()) {
        MESSAGE(
            "SKIPPED (no real SKID-linked M2/.skin/.skel trio available -- set "
            "HUSK_TEST_SKEL_M2, HUSK_TEST_SKEL_SKIN, HUSK_TEST_SKEL)");
        return;
    }

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-skel.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\" \"" +
                           skelPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // Confirms the .skel path was actually used, not silently ignored --
    // cmd_export.cpp only prints a bone count when it resolved some bones
    // from *somewhere* (inline or external).
    CHECK(result.output.find(" bones ") != std::string::npos);

    checkSkinnedGlb(outPath);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: skin file that doesn't belong to the given M2 fails cleanly") {
    std::string m2Path = envOrEmpty("HUSK_TEST_M2");
    std::string mismatchedSkinPath = envOrEmpty("HUSK_TEST_MISMATCHED_SKIN");
    if (m2Path.empty() || mismatchedSkinPath.empty()) {
        MESSAGE(
            "SKIPPED (set HUSK_TEST_M2 and HUSK_TEST_MISMATCHED_SKIN to a .skin from a "
            "different model to exercise this)");
        return;
    }

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-bad.glb").string();
    auto result = runHusk("export \"" + m2Path + "\" \"" + mismatchedSkinPath + "\" \"" + outPath +
                           "\"");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("mismatch") != std::string::npos);
}

TEST_CASE("husk info: nonexistent path fails cleanly, not a crash") {
    auto result = runHusk("info /nonexistent/path/does-not-exist.m2");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("couldn't open") != std::string::npos);
}
