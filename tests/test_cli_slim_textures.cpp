// CLI tier: `husk export --slim-textures` (TODO/
// SLIM_GLB_EXTERNAL_TEXTURES_TODO.md) -- exercises husk::commands::exportGlb
// by spawning the real compiled binary (see run_husk.hpp) against a small,
// synthetic, on-disk fixture with a real, decodable PNG texture. Split out
// of tests/test_cli_textures.cpp's own file per FILE_SPLIT_TODO.md's
// convention (one focused .cpp per feature cluster), since this is a new
// flag rather than a texture-resolution-cluster addition.

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <tiny_gltf.h>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runCommand;
using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export --slim-textures: writes the resolved base-color texture as a real "
          "'<output-dir>/textures/<FileDataID>.png' file, references it via a real glTF image "
          "'uri' (no 'bufferView'), and produces a smaller .glb than the default embedded "
          "export of the exact same model") {
    auto dir = defaultsDir("slimtex");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    // A real, sizeable, tinygltf/stb_image-decodable PNG -- large enough
    // (64x64 RGBA, uncompressed-deflate IDAT) that embedded-vs-external
    // .glb size is a meaningful, not noise-level, difference.
    writeFile(dir / "555.png", solidColorPng(64, 64, 200, 50, 50));

    auto embeddedPath = dir / "embedded.glb";
    auto embeddedResult =
        runHusk("export " + (dir / "textured.m2").string() + " -o " + embeddedPath.string());
    CHECK(embeddedResult.exitCode == 0);
    REQUIRE(fs::exists(embeddedPath));

    auto slimDir = dir / "slimout";
    fs::create_directories(slimDir);
    auto slimPath = slimDir / "slim.glb";
    auto slimResult = runHusk("export " + (dir / "textured.m2").string() + " -o " +
                               slimPath.string() + " --slim-textures");
    CHECK(slimResult.exitCode == 0);
    REQUIRE(fs::exists(slimPath));

    // (a) smaller: the ~16 KiB (64*64*4, one uncompressed-deflate IDAT
    // block) of raw texture bytes no longer live inside the .glb's own
    // binary buffer.
    CHECK(fs::file_size(slimPath) < fs::file_size(embeddedPath));

    // (b) a real 'textures/' directory sits next to the slim .glb, holding
    // a real, non-empty PNG file.
    auto texFile = slimDir / "textures" / "555.png";
    REQUIRE(fs::exists(texFile));
    CHECK(fs::file_size(texFile) > 0);

    // (c) the glTF JSON's own images array has 'uri' set (a real external
    // reference, resolved by tinygltf -- and, per the conformance-tier
    // Blender test, by Blender's own importer too -- relative to the .glb's
    // own directory) and no 'bufferView' (tinygltf::Image::bufferView
    // defaults to -1, "unset").
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, slimPath.string()));
    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].uri == "textures/555.png");
    CHECK(model.images[0].bufferView == -1);

    // The embedded (default) export, by contrast, has no external image at
    // all -- a real bufferView, no uri -- confirming the two runs actually
    // took different code paths rather than --slim-textures being a no-op.
    tinygltf::Model embeddedModel;
    REQUIRE(loader.LoadBinaryFromFile(&embeddedModel, &err, &warn, embeddedPath.string()));
    REQUIRE(embeddedModel.images.size() == 1);
    CHECK(embeddedModel.images[0].uri.empty());
    CHECK(embeddedModel.images[0].bufferView >= 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export --slim-textures: two materials sharing the same resolved image write "
          "the file once and share the same glTF image/uri (the existing alternateTextureCache "
          "dedup, now also gating the write, not just the embed)") {
    auto dir = defaultsDir("slimtex-dedup");
    // Both texture slots are hardcoded char_hair (type 6) -- with exactly
    // one matching candidate file in --textures, both independently
    // resolve to that same sole match (export_materials.cpp's own "sole
    // match" fuzzy-resolution case), the real "many materials, one shared
    // image" shape this test targets.
    writeFile(dir / "twohard.m2", twoHardcodedTexturedModel(6, 6));
    writeFile(dir / "twohard00.skin", twoBatchSkin());
    writeFile(dir / "twohard_hair_color_6001.png", solidColorPng(8, 8, 10, 20, 30));

    auto slimDir = dir / "slimout";
    fs::create_directories(slimDir);
    auto slimPath = slimDir / "slim.glb";
    auto result = runHusk("export " + (dir / "twohard.m2").string() + " -o " + slimPath.string() +
                           " --slim-textures");
    CHECK(result.exitCode == 0);
    INFO(result.output);
    REQUIRE(fs::exists(slimPath));

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, slimPath.string()));
    // One shared image (not two), one real external uri.
    REQUIRE(model.images.size() == 1);
    CHECK(!model.images[0].uri.empty());
    CHECK(model.images[0].bufferView == -1);

    fs::remove_all(dir);
}

#ifdef HUSK_GLTF_VALIDATOR
// Pulls the integer after "Errors: " out of a real `gltf_validator -a`
// text report -- the same "N errors" summary line every -a invocation
// prints first.
int parseValidatorErrorCount(const std::string& output) {
    auto pos = output.find("Errors: ");
    REQUIRE(pos != std::string::npos);
    return std::stoi(output.substr(pos + 8));
}

TEST_CASE("husk export --slim-textures: a real external (uri-only, no bufferView) image "
          "introduces no new Khronos glTF-Validator findings versus the same model's default "
          "embedded export -- same error count either way") {
    // oneTexturedModel()/oneTexturedModelSkin()'s single degenerate
    // single-vertex triangle (see that fixture's own doc comment) already
    // trips one real, pre-existing gltf_validator finding (a zero-length
    // NORMAL) that has nothing to do with --slim-textures -- so this
    // compares slim-vs-embedded error *counts* for the exact same model,
    // rather than asserting zero errors outright, to isolate what this
    // flag itself changes from what the synthetic fixture already carries.
    auto dir = defaultsDir("slimtex-validator");
    writeFile(dir / "valtex.m2", oneTexturedModel(777));
    writeFile(dir / "valtex00.skin", oneTexturedModelSkin());
    writeFile(dir / "777.png", solidColorPng(16, 16, 30, 60, 90));

    auto embeddedPath = dir / "embedded.glb";
    auto embeddedResult =
        runHusk("export " + (dir / "valtex.m2").string() + " -o " + embeddedPath.string());
    CHECK(embeddedResult.exitCode == 0);
    REQUIRE(fs::exists(embeddedPath));
    auto embeddedValidation =
        runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + embeddedPath.string() + "\"");
    INFO("embedded gltf_validator output:\n", embeddedValidation.output);

    auto slimDir = dir / "slimout";
    fs::create_directories(slimDir);
    auto slimPath = slimDir / "valid.glb";
    auto result = runHusk("export " + (dir / "valtex.m2").string() + " -o " + slimPath.string() +
                           " --slim-textures");
    CHECK(result.exitCode == 0);
    REQUIRE(fs::exists(slimPath));
    REQUIRE(fs::exists(slimDir / "textures" / "777.png"));
    auto slimValidation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + slimPath.string() + "\"");
    INFO("slim gltf_validator output:\n", slimValidation.output);

    CHECK(parseValidatorErrorCount(slimValidation.output) ==
          parseValidatorErrorCount(embeddedValidation.output));

    fs::remove_all(dir);
}
#endif
