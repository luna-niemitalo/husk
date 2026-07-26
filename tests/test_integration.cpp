// Runs the actual compiled `husk` binary against a real, game-extracted M2
// file. Deliberately not mocked, and deliberately not asserting on any
// specific model's field values (those belong in test_m2.cpp's synthetic
// fixtures, which encode the spec) -- this tier only answers "does it
// survive contact with a real file from the live game," the same
// smoke-test role test_integration.cpp plays in casc-tool.
//
// Every fixture below resolves via tests/test_data_paths.hpp: an explicit
// HUSK_TEST_* env var (pointing at a different real model) always wins;
// otherwise it falls back to this repo's own gitignored test_data/
// directory when the matching file is actually there. Either way, a test
// whose fixture doesn't resolve is marked `* doctest::skip(...)` -- it
// shows up as a distinct, non-zero "skipped" count in doctest's own
// summary (see test_main.cpp's startup banner for *why*), not folded
// silently into "passed" the way a runtime `return` would report it.
//
// A mismatched .skin (wrong model/LOD) is a real failure mode, not
// something to silently tolerate: skin::resolveTriangleIndices/cmd_export's
// own bounds check will throw if the skin references M2 vertices that don't
// exist, so HUSK_TEST_MISMATCHED_SKIN needs to actually be from a
// different model (the default, bloodelffemale_hd00.skin, is -- it's a
// separate, higher-poly M2's sidecar, not an LOD of the base model).

#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <tiny_gltf.h>

#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testM2;
using husk::test::testMismatchedSkin;
using husk::test::testSkel;
using husk::test::testSkelM2;
using husk::test::testSkelSkin;
using husk::test::testSkin;
using husk::test::testSkinDir;
using husk::test::testTexturesDir;

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

TEST_CASE("husk info: real game-extracted M2 parses without error" * doctest::skip(testM2().empty())) {
    std::string path = testM2();

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("format:") != std::string::npos);
    CHECK(result.output.find("version:") != std::string::npos);
    // A model file always has *some* geometry; a zero vertex count would
    // mean the parser landed on the wrong offsets even though it didn't crash.
    CHECK(result.output.find("vertices: 0 ") == std::string::npos);
}

TEST_CASE("husk info: real Legion+ M2 surfaces skin_file_data_ids (SFID) and anim_file_ids "
          "(AFID) -- both were previously unread" * doctest::skip(testM2().empty())) {
    std::string path = testM2();

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // Not every real M2 necessarily has these chunks (pre-Legion files
    // never do), but every chunked file used as HUSK_TEST_M2 so far in
    // this project's own testing does -- if that stops being true for
    // some other real file, loosen this rather than deleting it outright.
    CHECK(result.output.find("skin_file_data_ids:") != std::string::npos);
    CHECK(result.output.find("anim_file_ids:") != std::string::npos);
}

TEST_CASE("husk export: real game-extracted M2 + matching .skin produce a well-formed glb" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

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

TEST_CASE("husk export: real M2 + .skin produces real glTF animation clips with sane (unit-norm, "
          "finite) rotation keyframes" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    // Codifies a manual check performed while building roadmap stage 6
    // (animation): the M2Sequence stride bug (see husk::m2::Sequence's doc
    // comment -- 64 bytes, not the 36 a naive wiki reading suggests) was
    // caught by exactly this kind of real-data check, not by any synthetic
    // fixture (a hand-built fixture would have just as easily encoded the
    // same wrong assumption test_m2.cpp's *own* spec transcription made).
    // This test exists so that class of bug -- structurally plausible
    // output that's still quietly wrong -- can't silently regress.
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-anim.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    // A real character model with inline bones (the common case) should
    // produce at least one usable animation clip -- if this is ever 0
    // against a real file, either the model genuinely has none (rare for a
    // playable character) or something upstream broke silently.
    CHECK(model.animations.size() > 0);

    size_t rotationKeyframesChecked = 0;
    for (const auto& anim : model.animations) {
        for (const auto& ch : anim.channels) {
            if (ch.target_path != "rotation") continue;
            const auto& samp = anim.samplers[ch.sampler];
            const auto& acc = model.accessors[samp.output];
            const auto& view = model.bufferViews[acc.bufferView];
            const auto& buf = model.buffers[view.buffer];
            std::vector<float> vals(acc.count * 4);
            std::memcpy(vals.data(), buf.data.data() + view.byteOffset + acc.byteOffset,
                        vals.size() * sizeof(float));
            for (size_t i = 0; i < vals.size(); i += 4) {
                float x = vals[i], y = vals[i + 1], z = vals[i + 2], w = vals[i + 3];
                CHECK(std::isfinite(x));
                CHECK(std::isfinite(y));
                CHECK(std::isfinite(z));
                CHECK(std::isfinite(w));
                float norm = std::sqrt(x * x + y * y + z * z + w * w);
                CHECK(norm == doctest::Approx(1.0f).epsilon(0.05));
                ++rotationKeyframesChecked;
            }
        }
    }
    CHECK(rotationKeyframesChecked > 0);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk info: real game-extracted M2 has no chunk tags outside husk's known M2 chunk "
          "list" *
          doctest::skip(testM2().empty())) {
    // The actual canary: cmd_info.cpp's documentedM2ChunkTags is a
    // snapshot of wowdev.wiki/M2#Chunks from one fetch date, and this
    // format adds new top-level chunks fairly often (see README.md's
    // Design notes). If this starts failing against a freshly re-extracted
    // real file, that's the live signal a client update shipped a chunk
    // tag nobody's taught husk about yet -- go update
    // documentedM2ChunkTags (and decide whether the new chunk needs real
    // support) rather than silently ignoring it.
    std::string path = testM2();

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("not in husk's known M2 chunk list") == std::string::npos);
}

TEST_CASE("husk export: real M2 + .skin resolves per-batch materials with a plausible alphaMode "
          "spread" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    std::string skinPath = testSkin();

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

    // Second UV set: M2Vertex always carries tex_coords[1], so a real
    // model's export should always include TEXCOORD_1 now, not just
    // TEXCOORD_0.
    CHECK(model.meshes[0].primitives[0].attributes.count("TEXCOORD_1") == 1);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: --textures embeds a real baseColorTexture image when one resolves" *
          doctest::skip(testM2().empty() || testSkin().empty() || testTexturesDir().empty())) {
    // No default fixture for this one: test_data/ has no husk-blp-converted
    // PNGs committed (BLP->PNG conversion is a separate Python toolchain,
    // see blp/), so HUSK_TEST_TEXTURES_DIR still needs to be set by hand,
    // pointing at a directory of '<FileDataID>.png' files, to run this for
    // real -- see test_main.cpp's banner for whether it resolved.
    std::string m2Path = testM2();
    std::string skinPath = testSkin();
    std::string texturesDir = testTexturesDir();

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

TEST_CASE("husk export: 'auto' + --skin-dir resolves a real model's LOD0 .skin via SFID" *
          doctest::skip(testM2().empty() || testSkinDir().empty())) {
    // testSkinDir() builds this fixture itself (see test_data_paths.hpp's
    // autoSkinDir) by copying testSkin() to a scratch dir under the real
    // FileDataID read from testM2()'s own SFID chunk -- HUSK_TEST_SKIN_DIR
    // still overrides it entirely if set.
    std::string m2Path = testM2();
    std::string skinDir = testSkinDir();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-auto.glb").string();
    std::filesystem::remove(outPath);

    auto result =
        runHusk("export \"" + m2Path + "\" auto \"" + outPath + "\" --skin-dir \"" + skinDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("resolved 'auto'") != std::string::npos);

    std::ifstream glb(outPath, std::ios::binary | std::ios::ate);
    REQUIRE(glb.is_open());
    CHECK(glb.tellg() > 10000);

    std::filesystem::remove(outPath);
}

TEST_CASE(
    "husk export: M2 with an external .skel (SKID-linked) skeleton produces a well-formed, "
    "skinned glb" *
    doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty())) {
    std::string m2Path = testSkelM2();
    std::string skinPath = testSkelSkin();
    std::string skelPath = testSkel();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-skel.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" \"" + skinPath + "\" \"" + outPath + "\" \"" +
                           skelPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // Confirms the .skel path was actually used, not silently ignored --
    // cmd_export.cpp only prints a bone count when it resolved some bones
    // from *somewhere* (inline or external). Not asserting on "animation(s)"
    // vs. "bind pose only" here -- whether a .skel's own SKS1 sequences
    // resolve to any real clips is real-data-dependent (inline ones do;
    // external ones need --anim-dir, not exercised by this test).
    CHECK(result.output.find(" bones") != std::string::npos);

    checkSkinnedGlb(outPath);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: skin file that doesn't belong to the given M2 fails cleanly" *
          doctest::skip(testM2().empty() || testMismatchedSkin().empty())) {
    std::string m2Path = testM2();
    std::string mismatchedSkinPath = testMismatchedSkin();

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
