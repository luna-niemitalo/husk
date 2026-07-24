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

#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tiny_gltf.h>

namespace {

std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

struct RunResult {
    std::string output;
    int exitCode;
};

RunResult runHusk(const std::string& args) {
    std::string cmd = std::string(HUSK_BINARY) + " " + args + " 2>&1";
    std::array<char, 4096> buf;
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buf.data(), buf.size(), pipe)) {
        output += buf.data();
    }
    int status = pclose(pipe);
    return {output, WEXITSTATUS(status)};
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

    // Shape-only skinning check: a real character model has bones, so this
    // must have produced a glTF skin, not silently dropped it. Doesn't
    // assert any model-specific bone count -- that belongs in test_m2.cpp's
    // synthetic tests.
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
