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

#include <cctype>
#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tiny_gltf.h>

#include "m2.hpp"
#include "run_husk.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testAnimDir;
using husk::test::testBonesDir;
using husk::test::testM2;
using husk::test::testMismatchedSkin;
using husk::test::testMultiTextureLayerM2;
using husk::test::testMultiTextureLayerSkin;
using husk::test::testSkel;
using husk::test::testSkelM2;
using husk::test::testSkelSkin;
using husk::test::testSkin;
using husk::test::testSkinDir;
using husk::test::testTextureCoordComboM2;
using husk::test::testTextureCoordComboSkin;
using husk::test::testTexturesDir;
using husk::test::testWeaponParticleA;
using husk::test::testWeaponParticleB;
using husk::test::testWeaponParticleStress;
using husk::test::testWeaponPhys;
using husk::test::testWeaponPhysSkin;
using husk::test::testWeaponRibbon;

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

    auto result =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
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

    auto result =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
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

TEST_CASE(
    "husk export: a .skel-sourced model's external AFSB-shaped .anim files resolve real, "
    "sane (unit-norm, finite) animation clips, end to end (WIKI_FINDINGS.md §2's follow-up)" *
    doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                  testAnimDir().empty())) {
    // Every real bloodelffemale_hd_*.anim file carries AFSB, not AFM2 --
    // per WIKI_FINDINGS.md §2, this was the single biggest unresolved
    // animation gap in the project (essentially 0% external-animation
    // coverage for any modern, .skel-linked character model). This test is
    // the real-data proof the crack holds, mirroring the inline-animation
    // sanity check above rather than asserting on one specific clip's exact
    // values (those belong in test_cli.cpp's synthetic fixtures).
    std::string m2Path = testSkelM2();
    std::string skinPath = testSkelSkin();
    std::string skelPath = testSkel();
    std::string animDir = testAnimDir();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-afsb.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath +
                           "\" --skel \"" + skelPath + "\" --anim \"" + animDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    // Real bloodelffemale_hd.m2/.skel/.anim data resolves 300+ real clips
    // (336, checked by hand against this exact fixture) -- asserting a
    // conservative lower bound here, not the precise count, so this test
    // doesn't become a silent tripwire if the fixture's own AFID/anim-file
    // set ever changes slightly.
    CHECK(model.animations.size() > 100);

    // The count check above passes on inline + global-sequence clips alone
    // and doesn't actually prove the external-.anim-file path ran: --anim's
    // resolution used to only look for '<FileDataID>.anim', but this .skel
    // has no AFID entries mapping to the committed
    // bloodelffemale_hd0069-00.anim/-01.anim fixtures -- they only resolve
    // via findAnimFileByBasename's same-basename fallback (DESIGN.md's AFSB
    // design note, WIKI_FINDINGS.md §2). These two
    // clip names are exact and known in advance: animId=69,
    // variationIndex={0,1}, named "anim_" + id + "_" + variationIndex
    // (buildAnimations, cmd_export.cpp) -- concrete proof the basename
    // fallback resolved real, correctly-named, on-disk files through husk's
    // actual --anim CLI mechanism, not just a loose count.
    bool foundAnim69_0 = false;
    bool foundAnim69_1 = false;
    for (const auto& anim : model.animations) {
        if (anim.name == "anim_69_0") foundAnim69_0 = true;
        if (anim.name == "anim_69_1") foundAnim69_1 = true;
    }
    CHECK(foundAnim69_0);
    CHECK(foundAnim69_1);

    size_t rotationKeyframesChecked = 0;
    size_t translationKeyframesChecked = 0;
    for (const auto& anim : model.animations) {
        for (const auto& ch : anim.channels) {
            const auto& samp = anim.samplers[ch.sampler];
            const auto& acc = model.accessors[samp.output];
            const auto& view = model.bufferViews[acc.bufferView];
            const auto& buf = model.buffers[view.buffer];
            if (ch.target_path == "rotation") {
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
            } else if (ch.target_path == "translation") {
                std::vector<float> vals(acc.count * 3);
                std::memcpy(vals.data(), buf.data.data() + view.byteOffset + acc.byteOffset,
                            vals.size() * sizeof(float));
                for (float v : vals) CHECK(std::isfinite(v));
                translationKeyframesChecked += acc.count;
            }
        }
    }
    CHECK(rotationKeyframesChecked > 0);
    CHECK(translationKeyframesChecked > 0);

    std::filesystem::remove(outPath);
}

TEST_CASE(
    "husk export: bloodelffemale_hd's real M2Sequence fields (movespeed/aliasNext/isAlias) come "
    "through as per-clip sequence_metadata extras, and real alias sequences don't regress the "
    "clip count (M2_GAPS_TODO.md's former Item 1, WIKI_FINDINGS.md §12's follow-up)" *
    doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                  testAnimDir().empty())) {
    // WIKI_FINDINGS.md §12 found 38 real alias sequences (flags & 0x40) in
    // this exact .skel: 31 also carry flags & 0x20 (inline) -- those are
    // unaffected by the aliasNext fix (their own inline data already wins
    // priority, same as before it existed) -- and the remaining 7 are
    // "pure" aliases whose resolved terminal sequence requires an external
    // .anim file. All 7 of those terminal files genuinely exist in the
    // full real corpus (confirmed by hand against
    // /media/luna/data/wow_export), but none of the 6 distinct files they
    // need (bloodelffemale_hd0060/61/62/84/113/123-00.anim) happen to be
    // among the ~104 .anim files committed to this repo's own test_data/
    // subset -- so against *this* fixture set specifically, the real,
    // measured effect of the fix is zero net new clips, not "up to 38" (a
    // real answer to the open question the original implementation plan
    // flagged: "don't assume every alias necessarily gains a clip without
    // checking"). What the fix *does* change for this fixture: those 7
    // pure aliases now attempt real resolution (and cleanly fall back to
    // "not available locally," same as any other unresolvable external
    // sequence) instead of being skipped outright -- this test's job is to
    // prove that path doesn't crash and doesn't change the clip count, not
    // to prove growth that isn't actually there for this specific data.
    std::string m2Path = testSkelM2();
    std::string skinPath = testSkelSkin();
    std::string skelPath = testSkel();
    std::string animDir = testAnimDir();

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-export-seqmeta.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath +
                           "\" --skel \"" + skelPath + "\" --anim \"" + animDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    // The exact, previously-measured clip count for this exact fixture set
    // (WIKI_FINDINGS.md §2's follow-up) -- pinned deliberately, unlike the
    // AFSB test's own conservative ">100" bound above, since this test's
    // whole point is confirming the aliasNext fix doesn't change it for
    // this specific data (see the comment above).
    CHECK(model.animations.size() == 338);

    auto findAnim = [&](const std::string& name) -> const tinygltf::Animation* {
        for (const auto& a : model.animations) {
            if (a.name == name) return &a;
        }
        return nullptr;
    };

    // anim_804_0: a real sequence with flags 0x60 (both inline AND alias)
    // -- confirmed by hand (tools/check_alias_next.py-style byte read)
    // to be one of the 31 "both flags" real sequences. Its own
    // sequence_metadata.is_alias should read true (raw flags exposed
    // verbatim), even though its keyframe data comes from its own inline
    // tracks, not a resolved alias chain (see cmd_export.cpp's
    // buildAnimations doc comment for why 0x20 wins priority).
    const auto* bothFlags = findAnim("anim_804_0");
    REQUIRE(bothFlags != nullptr);
    REQUIRE(bothFlags->extras.IsObject());
    {
        const auto& meta = bothFlags->extras.Get("sequence_metadata");
        CHECK(meta.Get("is_alias").Get<bool>() == true);
    }

    // anim_0_0 ("Stand", AnimationData.dbc id 0 -- WoW's own universal
    // convention) and anim_5_0 (a locomotion sequence) are both real,
    // plain (non-alias) inline sequences -- real movespeed values
    // confirmed by hand: 0.0 and 7.0 respectively, exactly matching
    // M2_GAPS_TODO.md's own test-plan prediction ("a locomotion sequence
    // should have nonzero movespeed, a 'Stand' sequence should have zero").
    const auto* stand = findAnim("anim_0_0");
    REQUIRE(stand != nullptr);
    REQUIRE(stand->extras.IsObject());
    {
        const auto& meta = stand->extras.Get("sequence_metadata");
        CHECK(meta.Get("movespeed").GetNumberAsDouble() == doctest::Approx(0.0));
        CHECK(meta.Get("is_alias").Get<bool>() == false);
    }

    const auto* locomotion = findAnim("anim_5_0");
    REQUIRE(locomotion != nullptr);
    REQUIRE(locomotion->extras.IsObject());
    {
        const auto& meta = locomotion->extras.Get("sequence_metadata");
        CHECK(meta.Get("movespeed").GetNumberAsDouble() == doctest::Approx(7.0));
        CHECK(meta.Get("is_alias").Get<bool>() == false);
    }

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

    auto result =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
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

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath +
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

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin-dir \"" +
                           skinDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // resolveSkin's own phrasing (cmd_export.cpp), not exportGlb's separate
    // --lod-path message ("resolved 'auto' -> ...") -- the two currently
    // read slightly differently depending on whether --lod was given.
    CHECK(result.output.find("'auto' resolved") != std::string::npos);

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

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath +
                           "\" --skel \"" + skelPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    // Confirms the .skel path was actually used, not silently ignored --
    // cmd_export.cpp only prints a bone count when it resolved some bones
    // from *somewhere* (inline or external). Not asserting on "animation(s)"
    // vs. "bind pose only" here -- whether a .skel's own SKS1 sequences
    // resolve to any real clips is real-data-dependent (inline ones do;
    // external ones need --anim, not exercised by this test).
    CHECK(result.output.find(" bones") != std::string::npos);

    checkSkinnedGlb(outPath);

    std::filesystem::remove(outPath);
}

TEST_CASE(
    "husk export: --bones-dir attaches real .bone correction data as inert skin extras, end to "
    "end (WIKI_FINDINGS.md §4/TODO_correctness.md #3)" *
    doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                  testBonesDir().empty())) {
    std::string m2Path = testSkelM2();
    std::string skinPath = testSkelSkin();
    std::string skelPath = testSkel();
    std::string bonesDir = testBonesDir();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-bones.glb").string();
    std::filesystem::remove(outPath);

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath +
                           "\" --skel \"" + skelPath + "\" --bones-dir \"" + bonesDir + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("correction set(s)") != std::string::npos);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& sets = extras.Get("bone_correction_sets");
    REQUIRE(sets.IsArray());
    CHECK(sets.ArrayLen() > 0);
    const auto& set0 = sets.Get(0);
    CHECK(set0.Get("file_data_id").GetNumberAsInt() > 0);
    const auto& corrections = set0.Get("corrections");
    REQUIRE(corrections.IsArray());
    REQUIRE(corrections.ArrayLen() > 0);
    const auto& c0 = corrections.Get(0);
    CHECK(c0.Get("joint").GetNumberAsInt() >= 0);
    CHECK(c0.Get("matrix").ArrayLen() == 16);

    std::filesystem::remove(outPath);
}

// Real ribbon/particle emitter data (weapon models -- see
// tests/test_data_paths.hpp's kWeaponRibbon/kWeaponParticleA/B/Stress doc
// comment for how these were chosen out of a 4112-file scan). Checks the
// exported .glb's skin extras ribbon_emitters/particle_emitters anchor
// arrays against the real header counts -- exact, not a tolerance, same
// precedent the collision-mesh work established (see CLAUDE.md's Resume).
void checkEmitterAnchorCounts(const std::string& m2Path, size_t expectedRibbons,
                               size_t expectedParticles) {
    auto header = husk::m2::loadFile(m2Path);
    CHECK(header.ribbonEmitters.count == expectedRibbons);
    CHECK(header.particleEmitters.count == expectedParticles);

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-emitters.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    const auto& extras = model.skins[0].extras;

    if (expectedRibbons > 0) {
        REQUIRE(extras.IsObject());
        const auto& ribbons = extras.Get("ribbon_emitters");
        REQUIRE(ribbons.IsArray());
        CHECK(static_cast<size_t>(ribbons.ArrayLen()) == expectedRibbons);
        const auto& r0 = ribbons.Get(0);
        CHECK(r0.Get("joint").GetNumberAsInt() >= 0);
        REQUIRE(r0.Get("position").IsObject());
    } else if (extras.IsObject()) {
        CHECK_FALSE(extras.Get("ribbon_emitters").IsArray());
    }

    if (expectedParticles > 0) {
        REQUIRE(extras.IsObject());
        const auto& particles = extras.Get("particle_emitters");
        REQUIRE(particles.IsArray());
        CHECK(static_cast<size_t>(particles.ArrayLen()) == expectedParticles);
    } else if (extras.IsObject()) {
        CHECK_FALSE(extras.Get("particle_emitters").IsArray());
    }

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: a real ribbon-only weapon (Ashbringer) gets exactly 3 ribbon anchors, 0 "
          "particle anchors" *
          doctest::skip(testWeaponRibbon().empty())) {
    checkEmitterAnchorCounts(testWeaponRibbon(), /*expectedRibbons=*/3, /*expectedParticles=*/0);
}

TEST_CASE("husk export: a real combined ribbon+particle weapon gets exactly 1 ribbon anchor and "
          "2 particle anchors" *
          doctest::skip(testWeaponParticleA().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleA(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a second real combined ribbon+particle weapon gets exactly 1 ribbon "
          "anchor and 2 particle anchors" *
          doctest::skip(testWeaponParticleB().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleB(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a real 64-particle-emitter weapon (stress case) gets exactly 64 particle "
          "anchors, 0 ribbon anchors" *
          doctest::skip(testWeaponParticleStress().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleStress(), /*expectedRibbons=*/0,
                              /*expectedParticles=*/64);
}

TEST_CASE("husk export: a real weapon's .phys sidecar (auto-detected, same basename) attaches "
          "exactly 10 physics_bodies extras, one per real body, boneIndex values {0..9} of 17 "
          "real bones" *
          doctest::skip(testWeaponPhys().empty() || testWeaponPhysSkin().empty())) {
    std::string m2Path = testWeaponPhys();
    std::string skinPath = testWeaponPhysSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-phys.glb").string();
    std::filesystem::remove(outPath);
    auto result =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& bodies = extras.Get("physics_bodies");
    REQUIRE(bodies.IsArray());
    REQUIRE(static_cast<size_t>(bodies.ArrayLen()) == 10);

    std::set<int> boneIndices;
    for (int i = 0; i < bodies.ArrayLen(); ++i) {
        const auto& b = bodies.Get(i);
        CHECK(b.Get("id").GetNumberAsInt() == i);
        int joint = b.Get("joint").GetNumberAsInt();
        CHECK(joint >= 0);
        CHECK(joint < 17);  // this fixture's own real bone count
        REQUIRE(b.Get("position").IsObject());
        boneIndices.insert(joint);
    }
    // Every body claims a distinct bone -- confirmed by hand against the
    // real .phys file (boneIndex values {0..9} of 17 real bones).
    CHECK(boneIndices.size() == 10);
    CHECK(*boneIndices.begin() == 0);
    CHECK(*boneIndices.rbegin() == 9);

    std::filesystem::remove(outPath);
}

// M2_GAPS_TODO Item 5: a real material's "texture_type" extras key, cross-
// checked against an independent parse of the M2's own textures array
// (husk::m2::parseTextures, not cmd_export.cpp's internal state) --
// `bloodelffemale.m2` is known (via `husk info`'s own type= printout,
// confirmed by hand before writing this test) to carry both a mix of
// hardcoded slots (nonzero type -- skin/hair/etc.) and at least one real,
// filename/FileDataID-based texture (type 0), so this exercises both the
// present-when-nonzero and absent-when-zero halves of the convention on
// real data, not just the synthetic fixtures in test_gltf.cpp. Material
// names encode their own textureIndex (`..._tex<N>...`, see
// cmd_export.cpp's buildMaterialsAndPrimitives) -- parsed directly rather
// than assuming batch index == material index, since a batch with
// indexCount == 0 is skipped and would desync that assumption.
std::optional<int> textureIndexFromMaterialName(const std::string& name) {
    size_t pos = name.find("_tex");
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += 4;
    size_t end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) {
        ++end;
    }
    if (end == pos) {
        return std::nullopt;
    }
    return std::stoi(name.substr(pos, end - pos));
}

TEST_CASE("husk export: a real character model's per-material 'texture_type' extras match an "
          "independent parse of M2Texture::type, present only when nonzero" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::ifstream mf(testM2(), std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
    auto header = husk::m2::parseHeader(fileBytes);
    auto blob = husk::m2::extractBlob(fileBytes);
    auto textures = husk::m2::parseTextures(blob, header.textures);

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-texture-type.glb").string();
    std::filesystem::remove(outPath);
    auto result =
        runHusk("export \"" + testM2() + "\" -o \"" + outPath + "\" --skin \"" + testSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    bool foundNonzero = false;
    bool foundZero = false;
    for (const auto& mat : model.materials) {
        auto texIdx = textureIndexFromMaterialName(mat.name);
        if (!texIdx || static_cast<size_t>(*texIdx) >= textures.size()) {
            continue;
        }
        uint32_t expectedType = textures[*texIdx].type;
        INFO("material ", mat.name, " -> texture ", *texIdx, " expected type ", expectedType);
        if (expectedType != 0) {
            foundNonzero = true;
            REQUIRE(mat.extras.IsObject());
            CHECK(mat.extras.Get("texture_type").GetNumberAsInt() == static_cast<int>(expectedType));
        } else {
            foundZero = true;
            if (mat.extras.IsObject()) {
                CHECK_FALSE(mat.extras.Get("texture_type").IsInt());
            }
        }
    }
    // Both halves of the present-only-when-nonzero convention are actually
    // exercised by this real fixture, not just theoretically possible.
    CHECK(foundNonzero);
    CHECK(foundZero);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: a real weapon's attachments/events/lights become exactly "
          "header.attachments.count/events.count/lights.count real child nodes, never "
          "counted as skeleton joints" *
          doctest::skip(testWeaponParticleStress().empty())) {
    auto m2Path = testWeaponParticleStress();
    auto header = husk::m2::loadFile(m2Path);
    // This fixture's own real counts (checked by hand via `husk info`) --
    // the case picked deliberately because it's the only committed fixture
    // with all three non-zero at once.
    CHECK(header.attachments.count == 5);
    CHECK(header.events.count == 2);
    CHECK(header.lights.count == 4);

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-export-anchor-nodes.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    size_t attachmentNodes = 0, eventNodes = 0, lightNodes = 0;
    for (const auto& n : model.nodes) {
        if (n.name.rfind("attachment_", 0) == 0) {
            ++attachmentNodes;
        } else if (n.name.rfind("event_", 0) == 0) {
            ++eventNodes;
        } else if (n.name.rfind("light_", 0) == 0) {
            ++lightNodes;
        }
    }
    CHECK(attachmentNodes == header.attachments.count);
    CHECK(eventNodes == header.events.count);
    CHECK(lightNodes == header.lights.count);

    // Never added to skin.joints -- this fixture's real bone count is 78.
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 78);

    std::filesystem::remove(outPath);
}

// M2_GAPS_TODO Item 7: a real weapon's animated tint/fade curve
// ('fade_animation' extras) decodes to plausible, finite, in-range values --
// not garbage or NaN. Ashbringer (kWeaponRibbon) is known (checked by hand
// running `husk export` and inspecting the resulting .glb before writing
// this test) to have real animated M2TextureWeight/M2Color batches.
TEST_CASE("husk export: a real weapon's 'fade_animation' extras decode to finite, in-range "
          "keyframe values" *
          doctest::skip(testWeaponRibbon().empty())) {
    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-fade-animation.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + testWeaponRibbon() + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    auto checkScalarCurves = [](const tinygltf::Value& curves) {
        REQUIRE(curves.IsArray());
        for (int i = 0; i < curves.ArrayLen(); ++i) {
            const auto& c = curves.Get(i);
            REQUIRE(c.Get("keyframes").IsArray());
            double lastTime = -1.0;
            for (int k = 0; k < c.Get("keyframes").ArrayLen(); ++k) {
                const auto& kf = c.Get("keyframes").Get(k);
                double t = kf.Get("time").GetNumberAsDouble();
                double v = kf.Get("value").GetNumberAsDouble();
                CHECK(std::isfinite(t));
                CHECK(std::isfinite(v));
                CHECK(v >= 0.0);
                CHECK(v <= 1.0);
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };

    bool foundFadeAnimation = false;
    for (const auto& mat : model.materials) {
        if (!mat.extras.IsObject()) {
            continue;
        }
        const auto& fade = mat.extras.Get("fade_animation");
        if (!fade.IsObject()) {
            continue;
        }
        foundFadeAnimation = true;
        if (fade.Get("alpha").IsArray()) {
            checkScalarCurves(fade.Get("alpha"));
        }
        if (fade.Get("weight").IsArray()) {
            checkScalarCurves(fade.Get("weight"));
        }
    }
    REQUIRE(foundFadeAnimation);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk dump-chunks: a real weapon's particle_emitters JSON resolves plausible, "
          "finite color/alpha/scale curve values, not garbage or NaN" *
          doctest::skip(testWeaponParticleStress().empty())) {
    auto result = runHusk("dump-chunks \"" + testWeaponParticleStress() + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"particle_id\"") != std::string::npos);
    CHECK(result.output.find("\"color_track\"") != std::string::npos);
    CHECK(result.output.find("\"alpha_track\"") != std::string::npos);
    // A NaN keyframe would serialize as the JSON literal `null` (see
    // json::Writer::value(double)'s isfinite guard) -- if that ever shows
    // up inside a resolved curve, something upstream decoded garbage.
    CHECK(result.output.find("\"value\": null") == std::string::npos);
}

// TODO_correctness.md #3: cross-checks cmd_export.cpp's
// textureComboIndex+layer / textureCoordComboIndex+layer arithmetic (see
// its own doc comment, "Additional texture layers") against an
// independent resolution computed straight from husk::m2::parseHeader/
// husk::m2::extractBlob/husk::m2::parseUint16Array and
// husk::skin::parseHeader/parseBatches -- the same public, already
// unit-tested parsing primitives test_m2.cpp/test_skin.cpp exercise, not
// cmd_export.cpp's own internal buildMaterialsAndPrimitives (which isn't
// exposed outside that translation unit). Deliberately model-agnostic:
// this doesn't hardcode any fixture's specific FileDataIDs or batch
// layout, so it stays meaningful if HUSK_TEST_MULTITEX_*/
// HUSK_TEST_COORDCOMBO_* ever point at a different real file (see
// test_data_paths.hpp's fixture doc comment for how these two were
// found -- a 287k-file .skin scan and a 130k-file .m2 scan of Luna's own
// real WoW extraction). Requires at least one real textureCount > 1
// batch to actually exercise the arithmetic, not just skip it.
void checkMultiTextureLayerArithmetic(const std::string& m2Path, const std::string& skinPath) {
    std::ifstream mf(m2Path, std::ios::binary);
    REQUIRE(mf.good());
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
    auto header = husk::m2::parseHeader(fileBytes);
    auto blob = husk::m2::extractBlob(fileBytes);
    auto textureCombos = husk::m2::parseUint16Array(blob, header.textureCombos);
    auto textureCoordCombos = husk::m2::parseUint16Array(blob, header.textureCoordCombos);

    std::ifstream sf(skinPath, std::ios::binary);
    REQUIRE(sf.good());
    std::vector<uint8_t> skinBytes((std::istreambuf_iterator<char>(sf)),
                                    std::istreambuf_iterator<char>());
    auto skinHeader = husk::skin::parseHeader(skinBytes);
    auto batches = husk::skin::parseBatches(skinBytes, skinHeader.batches);

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-multitex.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath +
                           "\" --textures none --anim none --bones-dir none");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    bool foundMultiLayerBatch = false;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.textureCount <= 1) {
            continue;
        }
        foundMultiLayerBatch = true;
        REQUIRE(bi < model.materials.size());
        const auto& extras = model.materials[bi].extras;
        REQUIRE(extras.IsObject());
        const auto& additional = extras.Get("additional_textures");
        REQUIRE(additional.IsArray());
        CHECK(static_cast<size_t>(additional.ArrayLen()) ==
              static_cast<size_t>(b.textureCount - 1));

        for (uint16_t layer = 1; layer < b.textureCount; ++layer) {
            size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
            uint32_t expectedFdid = 0;
            if (comboIdx < textureCombos.size()) {
                uint16_t texIdx = textureCombos[comboIdx];
                if (header.textureFileDataIds && texIdx < header.textureFileDataIds->size()) {
                    expectedFdid = (*header.textureFileDataIds)[texIdx];
                }
            }
            int expectedTexCoord = 0;
            size_t coordIdx = static_cast<size_t>(b.textureCoordComboIndex) + layer;
            if (!textureCoordCombos.empty() && coordIdx < textureCoordCombos.size() &&
                textureCoordCombos[coordIdx] == 1) {
                expectedTexCoord = 1;
            }

            REQUIRE(static_cast<int>(layer - 1) < additional.ArrayLen());
            const auto& entry = additional.Get(static_cast<int>(layer - 1));
            auto actualFdid = static_cast<uint32_t>(entry.Get("file_data_id").GetNumberAsInt());
            int actualTexCoord = entry.Get("tex_coord").GetNumberAsInt();
            CHECK(actualFdid == expectedFdid);
            CHECK(actualTexCoord == expectedTexCoord);
        }
    }
    REQUIRE(foundMultiLayerBatch);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: real multi-texture-layer batch resolves textureComboIndex+layer exactly, "
          "TODO_correctness.md #3" *
          doctest::skip(testMultiTextureLayerM2().empty() || testMultiTextureLayerSkin().empty())) {
    checkMultiTextureLayerArithmetic(testMultiTextureLayerM2(), testMultiTextureLayerSkin());
}

TEST_CASE("husk export: real file with a nonzero textureCoordCombos table still resolves "
          "textureCoordComboIndex+layer safely, TODO_correctness.md #3" *
          doctest::skip(testTextureCoordComboM2().empty() || testTextureCoordComboSkin().empty())) {
    checkMultiTextureLayerArithmetic(testTextureCoordComboM2(), testTextureCoordComboSkin());
}

TEST_CASE("husk export: skin file that doesn't belong to the given M2 fails cleanly" *
          doctest::skip(testM2().empty() || testMismatchedSkin().empty())) {
    std::string m2Path = testM2();
    std::string mismatchedSkinPath = testMismatchedSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-bad.glb").string();
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" +
                           mismatchedSkinPath + "\"");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("mismatch") != std::string::npos);
}

// M2_GAPS_TODO.md item 10 -- locks in husk's already-correct "throw, don't
// misread" behavior on real non-M2 content, rather than trusting it stays
// correct across future chunk-walker refactors. blp2_7507381.m2 is a real
// FileDataID/listfile mismatch pulled from live CASC (WIKI_FINDINGS.md
// §13): its actual stored content is a genuine BLP2 texture, not an M2 --
// husk correctly refuses it with a ParseError-derived "chunk '..." message
// (the malformed/non-ASCII chunk tag) rather than silently misreading
// texture bytes as M2 structure. Checked across all three top-level
// commands, since each has its own read path into the same chunk walker.
TEST_CASE("husk info: real non-M2 content (a BLP2 texture under a mismatched .m2 FileDataID) "
          "fails cleanly, not a crash or a silent misread" *
          doctest::skip(husk::test::testBlp2AnomalyM2().empty())) {
    auto result = runHusk("info " + husk::test::testBlp2AnomalyM2());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("chunk") != std::string::npos);
}

TEST_CASE("husk export: real non-M2 content (a BLP2 texture under a mismatched .m2 FileDataID) "
          "fails cleanly, not a crash or a silent misread" *
          doctest::skip(husk::test::testBlp2AnomalyM2().empty())) {
    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-blp2-anomaly.glb").string();
    auto result =
        runHusk("export " + husk::test::testBlp2AnomalyM2() + " -o \"" + outPath + "\"");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("chunk") != std::string::npos);
    CHECK(!std::filesystem::exists(outPath));
}

TEST_CASE("husk dump-chunks: real non-M2 content (a BLP2 texture under a mismatched .m2 "
          "FileDataID) fails cleanly, not a crash or a silent misread" *
          doctest::skip(husk::test::testBlp2AnomalyM2().empty())) {
    auto result = runHusk("dump-chunks " + husk::test::testBlp2AnomalyM2());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("chunk") != std::string::npos);
}

TEST_CASE("husk info: nonexistent path fails cleanly, not a crash") {
    auto result = runHusk("info /nonexistent/path/does-not-exist.m2");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("couldn't open") != std::string::npos);
}
