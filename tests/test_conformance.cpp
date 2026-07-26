// Real downstream consumers of a husk-exported .glb, as opposed to
// tests/test_integration.cpp's "does husk itself run correctly" role:
//
//   - The Khronos glTF-Validator: independent spec-conformance check.
//     tinygltf being able to load a file back doesn't prove the file is
//     spec-valid -- tinygltf is a fairly permissive reader.
//   - Blender's own glTF importer, run headlessly: proves a real,
//     independent glTF implementation agrees with tinygltf's reading of
//     the same file (see tests/blender_import_check.py for why that
//     agreement is the actual point).
//
// Both tools are optional in the dev shell (HUSK_GLTF_VALIDATOR/
// HUSK_BLENDER compile definitions, set by CMakeLists.txt via
// find_program) -- absent either, or absent the real-data fixtures this
// file shares with test_integration.cpp (see tests/test_data_paths.hpp),
// these tests are marked `* doctest::skip(...)` rather than silently
// "passing" with zero assertions -- see test_main.cpp's startup banner
// for which case applies on this run.

#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <tiny_gltf.h>

#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runCommand;
using husk::test::runHusk;
using husk::test::testM2;
using husk::test::testSkin;

// Pulls an integer out of a "HUSK_PROBE key=value" line (see
// tests/blender_import_check.py) -- REQUIREs the key is present so a
// probe script that silently stopped printing something fails loudly
// rather than comparing against a default.
int parseProbeInt(const std::string& output, const std::string& key) {
    std::string marker = "HUSK_PROBE " + key + "=";
    auto pos = output.find(marker);
    INFO("looking for '", marker, "' in blender output:\n", output);
    REQUIRE(pos != std::string::npos);
    return std::stoi(output.substr(pos + marker.size()));
}

}  // namespace

#ifdef HUSK_GLTF_VALIDATOR
TEST_CASE("husk export: real M2 + .skin produces a glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-conformance.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    // -a: print every issue message (not just errors) to stderr, captured
    // into .output below, so a failure here shows exactly what the
    // validator objected to instead of just a bare nonzero exit code.
    // The validator's own documented contract (--help): nonzero exit iff
    // at least one error was found -- warnings/infos/hints don't fail this.
    auto validation = runCommand(std::string(HUSK_GLTF_VALIDATOR) + " -a \"" + outPath + "\"");
    INFO("gltf_validator output:\n", validation.output);
    CHECK(validation.exitCode == 0);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real M2 + .skin produces a glb the Khronos glTF-Validator "
          "accepts with zero errors" *
          doctest::skip(true)) {
    // gltf_validator not found on PATH at configure time (see
    // CMakeLists.txt's find_program) -- available via this project's nix
    // flake devShell.
}
#endif

#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
TEST_CASE("husk export: real M2 + .skin imports into Blender (headless) with bone/animation "
          "counts matching tinygltf's own reading of the same file" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-blender.glb").string();
    std::filesystem::remove(outPath);

    auto exportResult = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\"");
    INFO("husk export output:\n", exportResult.output);
    REQUIRE(exportResult.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);

    // --factory-startup: skip whatever addons/preferences happen to be
    // installed in the invoking user's own Blender config -- this test
    // exercises husk's glb output against Blender's importer, not against
    // Luna's personal addon set, which shouldn't be able to fail this.
    // --python-exit-code 1: an unhandled exception in the import script
    // must actually fail this process -- Blender's own --background
    // default is to exit 0 regardless.
    auto blenderResult = runCommand(std::string(HUSK_BLENDER) +
                                     " --background --factory-startup --python-exit-code 1 --python \"" +
                                     std::string(HUSK_BLENDER_IMPORT_SCRIPT) + "\" -- \"" + outPath + "\"");
    INFO("blender output:\n", blenderResult.output);
    REQUIRE(blenderResult.exitCode == 0);

    CHECK(parseProbeInt(blenderResult.output, "armature_count") == 1);
    CHECK(parseProbeInt(blenderResult.output, "bone_count") ==
          static_cast<int>(model.skins[0].joints.size()));
    CHECK(parseProbeInt(blenderResult.output, "action_count") ==
          static_cast<int>(model.animations.size()));
    CHECK(parseProbeInt(blenderResult.output, "mesh_object_count") > 0);
    CHECK(parseProbeInt(blenderResult.output, "total_vertex_count") > 0);

    std::filesystem::remove(outPath);
}
#else
TEST_CASE("husk export: real M2 + .skin imports into Blender (headless) with bone/animation "
          "counts matching tinygltf's own reading of the same file" *
          doctest::skip(true)) {
    // blender not found on PATH at configure time (see CMakeLists.txt's
    // find_program) -- available via this project's nix flake devShell.
}
#endif
