// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- runs the
// actual compiled `husk` binary against real, game-extracted fixtures.
// Deliberately not mocked, and deliberately not asserting on any specific
// model's field values (those belong in test_m2.cpp's synthetic fixtures) --
// see TEST_DESIGN.md#Shape-only-vs-exact-checks for what these do assert
// instead. Fixture resolution: see TEST_DESIGN.md#Fixture-resolution-model
// and tests/test_data_paths.hpp.
//
// A mismatched .skin (wrong model/LOD) is a real failure mode, not
// something to silently tolerate: the .skin's indices are checked against
// the M2 vertex count, so HUSK_TEST_MISMATCHED_SKIN needs to actually be
// from a different model (the default, bloodelffemale_hd00.skin, is -- a
// separate, higher-poly M2's sidecar, not an LOD of the base model).

#include <cctype>
#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tiny_gltf.h>

#include "m2.hpp"
#include "m2_shader_names.hpp"
#include "run_husk.hpp"
#include "skel.hpp"
#include "skin.hpp"
#include "test_bundle_fixtures.hpp"
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

// A fresh bundle directory under the temp dir; any previous run's output
// is removed so a stale manifest can't satisfy a check.
fs::path freshBundleDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / (name + ".bundle");
    fs::remove_all(dir);
    return dir;
}

// A BufferSlice's f32 values, flat, out of an already-read payload file.
std::vector<float> f32Slice(const std::vector<uint8_t>& fileBytes, const nlohmann::json& slice) {
    REQUIRE(slice["component_type"] == "f32");
    auto offset = slice["byte_offset"].get<size_t>();
    auto length = slice["byte_length"].get<size_t>();
    REQUIRE(offset + length <= fileBytes.size());
    std::vector<float> values(length / sizeof(float));
    if (length > 0) std::memcpy(values.data(), fileBytes.data() + offset, length);
    return values;
}

// Shape-only check (see TEST_DESIGN.md#Shape-only-vs-exact-checks): a real
// character model has bones, so the bundle must carry a skeleton and
// per-vertex skinning for every vertex the M2 declares, not silently drop
// either.
void checkSkinnedBundle(const nlohmann::json& manifest, uint32_t expectedVertexCount, size_t expectedJointCount) {
    const auto& mesh = manifest["resources"]["geometry"]["mesh"];
    CHECK(mesh["positions"]["count"] == expectedVertexCount);
    REQUIRE(mesh.contains("joints0"));
    REQUIRE(mesh.contains("weights0"));
    CHECK(mesh["joints0"]["count"] == expectedVertexCount);
    CHECK(mesh["weights0"]["count"] == expectedVertexCount);
    CHECK(expectedJointCount > 0);
    CHECK(manifest["resources"]["rig"]["skeleton"]["joint_count"] == expectedJointCount);
}

// Full per-keyframe sanity coverage (every rotation component finite and
// unit-norm, every translation component finite) for every clip in a
// bundle, aggregated into a single pass/fail verdict instead of one CHECK
// per scalar -- see TEST_DESIGN.md#Shape-only-vs-exact-checks. Nothing is
// skipped: every keyframe of every clip is walked; on failure the message
// names the first offending clip and up to 5 concrete bad keyframes in it.
// Returns the totals so callers can assert real keyframes were exercised.
struct AnimationSanityResult {
    size_t rotationKeyframesChecked = 0;
    size_t translationKeyframesChecked = 0;
};

AnimationSanityResult checkAllClipsSane(const fs::path& bundleDir, const nlohmann::json& manifest) {
    std::map<std::string, std::vector<uint8_t>> payloads;
    auto payload = [&](const nlohmann::json& slice) -> const std::vector<uint8_t>& {
        auto file = slice["file"].get<std::string>();
        auto it = payloads.find(file);
        if (it == payloads.end()) it = payloads.emplace(file, readFile((bundleDir / file).string())).first;
        return it->second;
    };

    AnimationSanityResult totals;
    std::optional<std::string> firstFailure;
    const auto& clips = manifest["resources"]["rig"]["animation"];
    for (size_t ci = 0; ci < clips.size(); ++ci) {
        std::vector<std::string> problems;
        for (const auto& joint : clips[ci]["joints"]) {
            if (joint.contains("rotation")) {
                std::vector<float> q = f32Slice(payload(joint["rotation"]["values"]), joint["rotation"]["values"]);
                for (size_t i = 0; i + 3 < q.size(); i += 4) {
                    float x = q[i], y = q[i + 1], z = q[i + 2], w = q[i + 3];
                    std::string where = "joint " + std::to_string(joint["joint_index"].get<int>()) +
                                        " rotation keyframe " + std::to_string(i / 4);
                    if (!(std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w))) {
                        problems.push_back(where + ": expected a finite quaternion, got (" + std::to_string(x) +
                                           ", " + std::to_string(y) + ", " + std::to_string(z) + ", " +
                                           std::to_string(w) + ")");
                    } else if (float norm = std::sqrt(x * x + y * y + z * z + w * w); std::abs(norm - 1.0f) > 0.05f) {
                        problems.push_back(where + ": expected unit norm (1.0 +/- 0.05), got " + std::to_string(norm));
                    }
                    ++totals.rotationKeyframesChecked;
                }
            }
            if (joint.contains("translation")) {
                std::vector<float> t =
                    f32Slice(payload(joint["translation"]["values"]), joint["translation"]["values"]);
                for (size_t i = 0; i < t.size(); ++i) {
                    if (!std::isfinite(t[i])) {
                        problems.push_back("joint " + std::to_string(joint["joint_index"].get<int>()) +
                                           " translation keyframe " + std::to_string(i / 3) + " component " +
                                           std::to_string(i % 3) + ": expected a finite value, got " +
                                           std::to_string(t[i]));
                    }
                }
                totals.translationKeyframesChecked += t.size() / 3;
            }
        }
        if (problems.empty() || firstFailure) continue;
        std::string detail = "clip " + std::to_string(ci) + " (" + clips[ci]["sequence_kind"].get<std::string>() +
                             " " + std::to_string(clips[ci]["sequence_index"].get<int>()) + ") had " +
                             std::to_string(problems.size()) + " sanity problem(s)";
        constexpr size_t kMaxShown = 5;
        for (size_t i = 0; i < problems.size() && i < kMaxShown; ++i) detail += "\n  " + problems[i];
        if (problems.size() > kMaxShown) {
            detail += "\n  ... and " + std::to_string(problems.size() - kMaxShown) + " more";
        }
        firstFailure = detail;
    }
    INFO("first clip with a sanity problem (if any): ", firstFailure.value_or("none"));
    CHECK(!firstFailure.has_value());
    return totals;
}

// The sequence indices (into the model's own or its .skel's sequence
// array) that the bundle carries a clip for.
std::set<int> clipSequenceIndices(const nlohmann::json& manifest) {
    std::set<int> indices;
    for (const auto& clip : manifest["resources"]["rig"]["animation"]) {
        if (clip["sequence_kind"] == "sequence") indices.insert(clip["sequence_index"].get<int>());
    }
    return indices;
}

// The index of the sequence with this AnimationData id and variation, or
// -1 -- clips are keyed by sequence index, so a test naming an animation
// by id resolves it against an independent parse of the sequence array.
int sequenceIndexFor(const std::vector<m2::Sequence>& sequences, uint16_t id, uint16_t variation) {
    for (size_t i = 0; i < sequences.size(); ++i) {
        if (sequences[i].id == id && sequences[i].variationIndex == variation) return static_cast<int>(i);
    }
    return -1;
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

TEST_CASE("husk export: real game-extracted M2 + matching .skin produce a schema-valid, skinned bundle, "
          "and --glb a skinned lean projection" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    fs::path dir = freshBundleDir("husk-test-export");
    auto glbPath = (fs::temp_directory_path() / "husk-test-export-lean.glb").string();
    fs::remove(glbPath);

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() +
                          "\" --glb \"" + glbPath + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    husk::m2::Header header = husk::m2::loadFile(m2Path);
    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["kind"] == "model");
    checkSkinnedBundle(manifest, header.vertices.count, header.bones.count);
    CHECK(fs::file_size(dir / "mesh.bin") > 10000);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, glbPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == header.bones.count);
    CHECK(model.skins[0].inverseBindMatrices >= 0);
    REQUIRE(model.nodes[0].mesh == 0);
    CHECK(model.nodes[0].skin == 0);
    const auto& prim = model.meshes[0].primitives[0];
    CHECK(prim.attributes.count("JOINTS_0") == 1);
    CHECK(prim.attributes.count("WEIGHTS_0") == 1);

    fs::remove_all(dir);
    fs::remove(glbPath);
}

TEST_CASE("husk export: the bundle's subject is named after the model's basename, not left blank -- "
          "an explicit --skin path included" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    std::string m2Path = testM2();
    fs::path dir = freshBundleDir("husk-test-subject-name");

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["subject"]["name"] == fs::path(m2Path).stem().string());

    fs::remove_all(dir);
}

TEST_CASE("husk export: real M2 + .skin produces real animation clips with sane (unit-norm, "
          "finite) rotation keyframes" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    // Structurally plausible but quietly wrong animation output (e.g. a
    // wrong M2Sequence stride) was caught by exactly this kind of real-data
    // check, not by any synthetic fixture -- a hand-built fixture can
    // encode the same wrong assumption the parser makes.
    std::string m2Path = testM2();
    fs::path dir = freshBundleDir("husk-test-anim");

    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    // A real character model with inline bones should produce at least one
    // usable animation clip -- 0 against a real file means something
    // upstream broke silently.
    CHECK(manifest["resources"]["rig"]["animation"].size() > 0);

    AnimationSanityResult totals = checkAllClipsSane(dir, manifest);
    CHECK(totals.rotationKeyframesChecked > 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a .skel-sourced model's external AFSB-shaped .anim files resolve real, "
          "sane (unit-norm, finite) animation clips, end to end" *
          doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                        testAnimDir().empty())) {
    // Every real bloodelffemale_hd_*.anim file carries AFSB, not AFM2.
    fs::path dir = freshBundleDir("husk-test-export-afsb");

    auto result = runHusk("export \"" + testSkelM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkelSkin() +
                          "\" --skel \"" + testSkel() + "\" --anim \"" + testAnimDir() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    // Real bloodelffemale_hd.m2/.skel/.anim data resolves 300+ real clips
    // -- a conservative lower bound, so this doesn't become a tripwire if
    // the fixture's own AFID/anim-file set changes slightly.
    CHECK(manifest["resources"]["rig"]["animation"].size() > 100);

    // The count check passes on inline + global-sequence clips alone and
    // doesn't prove the external-.anim path ran: this .skel has no AFID
    // entries mapping to the committed bloodelffemale_hd0069-00.anim/-01.anim
    // fixtures, so animation 69's two variations only resolve through the
    // same-basename fallback. Mutation-tested (see
    // TEST_DESIGN.md#Mutation-tested-regressions): both are absent without
    // that fallback.
    std::vector<m2::Sequence> sequences = skel::parseSequences(readFile(testSkel()));
    int anim69v0 = sequenceIndexFor(sequences, 69, 0);
    int anim69v1 = sequenceIndexFor(sequences, 69, 1);
    REQUIRE(anim69v0 >= 0);
    REQUIRE(anim69v1 >= 0);
    std::set<int> clipped = clipSequenceIndices(manifest);
    CHECK(clipped.count(anim69v0) == 1);
    CHECK(clipped.count(anim69v1) == 1);

    AnimationSanityResult totals = checkAllClipsSane(dir, manifest);
    CHECK(totals.rotationKeyframesChecked > 0);
    CHECK(totals.translationKeyframesChecked > 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: bloodelffemale_hd's real alias sequences don't change the clip count, and "
          "inline, alias+inline and locomotion sequences all get clips" *
          doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                        testAnimDir().empty())) {
    // This .skel has 38 real alias sequences (flags & 0x40); 31 also carry
    // flags & 0x20 (inline) and are unaffected by alias resolution. None of
    // the 7 "pure" aliases' terminal .anim files are among those committed
    // to test_data/, so for this fixture set alias resolution adds zero
    // clips: this proves that path doesn't crash or change the count.
    fs::path dir = freshBundleDir("husk-test-export-seqmeta");

    auto result = runHusk("export \"" + testSkelM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkelSkin() +
                          "\" --skel \"" + testSkel() + "\" --anim \"" + testAnimDir() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    // The exact, previously-measured clip count for this fixture set,
    // pinned deliberately: the point is that alias handling doesn't move it.
    CHECK(manifest["resources"]["rig"]["animation"].size() == 338);

    std::vector<m2::Sequence> sequences = skel::parseSequences(readFile(testSkel()));
    std::set<int> clipped = clipSequenceIndices(manifest);

    // anim 804 variation 0: flags 0x60 (both inline AND alias) -- its
    // keyframes come from its own inline tracks (0x20 wins).
    int bothFlags = sequenceIndexFor(sequences, 804, 0);
    REQUIRE(bothFlags >= 0);
    CHECK((sequences[static_cast<size_t>(bothFlags)].flags & 0x60) == 0x60);
    CHECK(clipped.count(bothFlags) == 1);

    // anim 0 ("Stand") and anim 5 (locomotion): plain, non-alias inline
    // sequences with real movespeeds 0.0 and 7.0.
    int stand = sequenceIndexFor(sequences, 0, 0);
    int locomotion = sequenceIndexFor(sequences, 5, 0);
    REQUIRE(stand >= 0);
    REQUIRE(locomotion >= 0);
    CHECK((sequences[static_cast<size_t>(stand)].flags & 0x40) == 0);
    CHECK(sequences[static_cast<size_t>(locomotion)].movespeed == doctest::Approx(7.0));
    CHECK(clipped.count(stand) == 1);
    CHECK(clipped.count(locomotion) == 1);

    fs::remove_all(dir);
}

TEST_CASE("husk info: real game-extracted M2 has no chunk tags outside husk's known M2 chunk "
          "list" *
          doctest::skip(testM2().empty())) {
    // The actual canary: cmd_info.cpp's documentedM2ChunkTags is a
    // snapshot of wowdev.wiki/M2#Chunks from one fetch date, and this
    // format adds new top-level chunks fairly often. If this starts failing
    // against a freshly re-extracted real file, a client update shipped a
    // chunk tag nobody's taught husk about yet -- update
    // documentedM2ChunkTags (and decide whether the new chunk needs real
    // support) rather than silently ignoring it.
    std::string path = testM2();

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("not in husk's known M2 chunk list") == std::string::npos);
}

TEST_CASE("husk export: real M2 + .skin resolves per-batch materials with a plausible framebuffer "
          "blend spread" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    fs::path dir = freshBundleDir("husk-test-export-mats");

    auto result = runHusk("export \"" + testM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    // A real character model has multiple submesh/batch groups (skin,
    // hair, tabard, ...) that don't all share one blend mode -- if every
    // material ended up opaque, the batch->material chain silently fell
    // back to defaults instead of reading the .skin's batches.
    REQUIRE(manifest["resources"]["geometry"]["mesh"]["primitives"].size() > 1);
    const auto& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() > 1);
    std::set<std::string> blends;
    for (const auto& m : materials) blends.insert(m["framebuffer_blend"].get<std::string>());
    CHECK(blends.size() > 1);

    // M2Vertex always carries tex_coords[1], so a real model's export
    // always carries the second UV set too.
    std::set<std::string> uvSemantics;
    for (const auto& uv : manifest["resources"]["geometry"]["mesh"]["uv_sets"]) {
        uvSemantics.insert(uv["semantic"].get<std::string>());
    }
    CHECK(uvSemantics.count("TEXCOORD_1") == 1);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures writes a real texture payload into the bundle when one resolves" *
          doctest::skip(testM2().empty() || testSkin().empty() || testTexturesDir().empty())) {
    // No default fixture for this one: test_data/ has no husk-blp-converted
    // PNGs committed, so HUSK_TEST_TEXTURES_DIR needs to be set by hand,
    // pointing at a directory of '<FileDataID>.png' files -- see
    // test_main.cpp's banner for whether it resolved.
    fs::path dir = freshBundleDir("husk-test-export-tex");

    auto result = runHusk("export \"" + testM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() +
                          "\" --textures \"" + testTexturesDir() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    size_t written = 0;
    for (const auto& texture : manifest["resources"]["textures"]) {
        if (!texture.contains("payload")) continue;
        fs::path file = dir / texture["payload"]["uri"].get<std::string>();
        REQUIRE(fs::exists(file));
        CHECK(fs::file_size(file) > 0);
        ++written;
    }
    CHECK(written > 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: 'auto' + --skin-dir resolves a real model's LOD0 .skin via SFID" *
          doctest::skip(testM2().empty() || testSkinDir().empty())) {
    // testSkinDir() builds this fixture itself (see test_data_paths.hpp's
    // autoSkinDir) by copying testSkin() to a scratch dir under the real
    // FileDataID read from testM2()'s own SFID chunk -- HUSK_TEST_SKIN_DIR
    // still overrides it entirely if set.
    fs::path dir = freshBundleDir("husk-test-export-auto");

    auto result = runHusk("export \"" + testM2() + "\" -o \"" + dir.string() + "\" --skin-dir \"" + testSkinDir() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("'auto' resolved") != std::string::npos);

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["resources"]["geometry"]["mesh"]["indices"]["count"].get<size_t>() > 0);
    CHECK(manifest["resources"]["geometry"]["mesh"]["primitives"].size() > 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: M2 with an external .skel (SKID-linked) skeleton produces a skinned bundle "
          "with the .skel's bones" *
          doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty())) {
    fs::path dir = freshBundleDir("husk-test-export-skel");

    auto result = runHusk("export \"" + testSkelM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkelSkin() +
                          "\" --skel \"" + testSkel() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    husk::m2::Header header = husk::m2::loadFile(testSkelM2());
    // The model's own bones are empty; every joint comes from the .skel.
    CHECK(header.bones.count == 0);
    size_t skelBones = skel::parseBones(readFile(testSkel())).size();
    checkSkinnedBundle(parseManifest(dir), header.vertices.count, skelBones);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --bones-dir writes real .bone correction sets into the rig, end to end" *
          doctest::skip(testSkelM2().empty() || testSkelSkin().empty() || testSkel().empty() ||
                        testBonesDir().empty())) {
    fs::path dir = freshBundleDir("husk-test-export-bones");

    auto result = runHusk("export \"" + testSkelM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkelSkin() +
                          "\" --skel \"" + testSkel() + "\" --bones-dir \"" + testBonesDir() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    const auto& rig = manifest["resources"]["rig"];
    REQUIRE(rig.contains("correction_sets"));
    const auto& sets = rig["correction_sets"];
    REQUIRE(sets.size() > 0);
    CHECK(sets[0]["ref"]["id"]["kind"] == "file_data_id");
    CHECK(sets[0]["ref"]["id"]["value"].get<int64_t>() > 0);
    const auto& corrections = sets[0]["corrections"];
    REQUIRE(corrections.size() > 0);
    CHECK(corrections[0]["joint"].get<int64_t>() >= 0);
    CHECK(corrections[0]["joint"].get<int64_t>() < rig["skeleton"]["joint_count"].get<int64_t>());
    CHECK(corrections[0]["matrix"].size() == 16);

    fs::remove_all(dir);
}

TEST_CASE("husk export: each real material layer reads a texture slot exactly when its M2Texture::type "
          "is nonzero, and the slot carries that type" *
          doctest::skip(testM2().empty() || testSkin().empty())) {
    // bloodelffemale.m2 carries both hardcoded slots (nonzero type --
    // skin/hair/etc.) and at least one FileDataID-based texture (type 0),
    // so both halves of the convention are exercised on real data. A
    // layer's identity is the M2 texture index it draws (m2_material_input's
    // layerIdentity), cross-checked here against an independent parse of
    // the M2's own textures array.
    std::vector<uint8_t> fileBytes = readFile(testM2());
    auto header = husk::m2::parseHeader(fileBytes);
    auto blob = husk::m2::extractBlob(fileBytes);
    auto textures = husk::m2::parseTextures(blob, header.textures);

    fs::path dir = freshBundleDir("husk-test-texture-type");
    auto result = runHusk("export \"" + testM2() + "\" -o \"" + dir.string() + "\" --skin \"" + testSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);
    const auto& slots = manifest["definition"]["texture_slots"];

    // One CHECK per kind of fact, not one per layer: every layer is still
    // examined, only the assertion count stops scaling with the fixture.
    bool foundNonzero = false;
    bool foundZero = false;
    std::optional<std::string> identityMismatch, nonzeroTypeMismatch, zeroTypeMismatch;
    for (size_t mi = 0; mi < manifest["resources"]["materials"].size(); ++mi) {
        const auto& layers = manifest["resources"]["materials"][mi]["layers"];
        for (size_t li = 0; li < layers.size(); ++li) {
            std::string where = "material " + std::to_string(mi) + " layer " + std::to_string(li);
            const auto& id = layers[li]["identity"]["id"];
            if (id["kind"] != "record_index" || id["value"].get<size_t>() >= textures.size()) {
                if (!identityMismatch) identityMismatch = where + ": identity " + id.dump();
                continue;
            }
            uint32_t expectedType = textures[id["value"].get<size_t>()].type;
            bool hasSlot = layers[li].contains("slot");
            if (expectedType != 0) {
                foundNonzero = true;
                if (!hasSlot || slots[layers[li]["slot"].get<size_t>()]["source"]["texture_type"] != expectedType) {
                    if (!nonzeroTypeMismatch) {
                        nonzeroTypeMismatch = where + ": expected a slot of type " + std::to_string(expectedType) +
                                              ", got " + (hasSlot ? slots[layers[li]["slot"].get<size_t>()].dump()
                                                                  : std::string("no slot"));
                    }
                }
            } else {
                foundZero = true;
                if (hasSlot && !zeroTypeMismatch) zeroTypeMismatch = where + ": type 0 but reads a slot";
            }
        }
    }
    INFO("first layer whose identity isn't an M2 texture index (if any): ", identityMismatch.value_or("none"));
    CHECK(!identityMismatch.has_value());
    INFO("first layer with a mismatched nonzero texture type (if any): ", nonzeroTypeMismatch.value_or("none"));
    CHECK(!nonzeroTypeMismatch.has_value());
    INFO("first type-0 layer reading a slot (if any): ", zeroTypeMismatch.value_or("none"));
    CHECK(!zeroTypeMismatch.has_value());
    CHECK(foundNonzero);
    CHECK(foundZero);

    fs::remove_all(dir);
}

// Cross-checks the textureComboIndex+layer / textureCoordComboIndex+layer
// arithmetic (wowdev.wiki M2/.skin#Texture_units) against an independent
// resolution computed straight from the public m2::/skin:: parsing
// primitives, not husk's own material assembly. Deliberately
// model-agnostic: no fixture's FileDataIDs or batch layout are hardcoded,
// so it stays meaningful if HUSK_TEST_MULTITEX_*/HUSK_TEST_COORDCOMBO_*
// point at a different real file. Requires at least one real
// textureCount > 1 batch, not just a skip of the arithmetic.
void checkMultiTextureLayerArithmetic(const std::string& m2Path, const std::string& skinPath) {
    std::vector<uint8_t> fileBytes = readFile(m2Path);
    auto header = husk::m2::parseHeader(fileBytes);
    auto blob = husk::m2::extractBlob(fileBytes);
    auto textureCombos = husk::m2::parseUint16Array(blob, header.textureCombos);
    auto textureCoordCombos = husk::m2::parseUint16Array(blob, header.textureCoordCombos);

    std::vector<uint8_t> skinBytes = readFile(skinPath);
    auto skinHeader = husk::skin::parseHeader(skinBytes);
    auto batches = husk::skin::parseBatches(skinBytes, skinHeader.batches);
    auto submeshes = husk::skin::parseSubmeshes(skinBytes, skinHeader.submeshes);

    fs::path dir = freshBundleDir("husk-test-multitex");
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\" --skin \"" + skinPath +
                          "\" --textures none --anim none --bones-dir none");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);
    const auto& primitives = manifest["resources"]["geometry"]["mesh"]["primitives"];
    const auto& materials = manifest["resources"]["materials"];
    const auto& uvMaps = manifest["resources"]["uv_maps"];

    // One CHECK per kind of fact, not one per batch/layer (see the
    // texture-type test above). A structural precondition that would crash
    // on indexing if false is recorded and `continue`d past instead of
    // REQUIREd, so one bad batch doesn't abort inspection of the rest.
    bool foundMultiLayerBatch = false;
    std::optional<std::string> primitiveIndexOOR, materialIndexMismatch, layerCountMismatch, textureIndexMismatch,
        uvSetMismatch, pixelShaderMismatch, vertexShaderMismatch;
    size_t primitiveIndex = 0;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        // A batch whose submesh has no indices gets no primitive, so the
        // primitive cursor advances only past batches that draw.
        if (submeshes.at(b.skinSectionIndex).indexCount == 0) continue;
        size_t pi = primitiveIndex++;
        if (b.textureCount <= 1) continue;
        foundMultiLayerBatch = true;
        std::string where = "batch " + std::to_string(bi);

        if (pi >= primitives.size()) {
            if (!primitiveIndexOOR) primitiveIndexOOR = where;
            continue;
        }
        if (!primitives[pi].contains("material_index") ||
            primitives[pi]["material_index"].get<size_t>() >= materials.size()) {
            if (!materialIndexMismatch) materialIndexMismatch = where + ": " + primitives[pi].dump();
            continue;
        }
        const auto& material = materials[primitives[pi]["material_index"].get<size_t>()];
        const auto& layers = material["layers"];
        if (layers.size() != b.textureCount) {
            if (!layerCountMismatch) {
                layerCountMismatch = where + ": expected " + std::to_string(b.textureCount) + " layers, got " +
                                     std::to_string(layers.size());
            }
            continue;
        }

        for (uint16_t layer = 0; layer < b.textureCount; ++layer) {
            const auto& l = layers[layer];
            size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
            if (comboIdx < textureCombos.size() && l["identity"]["id"]["value"] != textureCombos[comboIdx] &&
                !textureIndexMismatch) {
                textureIndexMismatch = where + " layer " + std::to_string(layer) + ": expected M2 texture " +
                                       std::to_string(textureCombos[comboIdx]) + ", got " + l["identity"].dump();
            }
            int expectedSet = 0;
            size_t coordIdx = static_cast<size_t>(b.textureCoordComboIndex) + layer;
            if (coordIdx < textureCoordCombos.size() && textureCoordCombos[coordIdx] == 1) expectedSet = 1;
            int actualSet = uvMaps[l["uv_map"].get<size_t>()]["set"].get<int>();
            if (actualSet != expectedSet && !uvSetMismatch) {
                uvSetMismatch = where + " layer " + std::to_string(layer) + ": expected UV set " +
                                std::to_string(expectedSet) + ", got " + std::to_string(actualSet);
            }
        }

        // shaderId -> {pixel, vertex} names (src/m2_shader_names.hpp),
        // resolved independently. The real guild-pennant fixture resolves
        // to the literal "Guild" pixel shader; not asserted by name, so the
        // check stays meaningful for another fixture.
        auto expectedNames = husk::m2::resolveShaderNames(b.shaderId, b.textureCount);
        if (!expectedNames.resolved) continue;
        if (material.value("pixel_shader", "") != expectedNames.pixel && !pixelShaderMismatch) {
            pixelShaderMismatch = where + ": expected " + expectedNames.pixel + ", got " + material.dump();
        }
        if (material.value("vertex_shader", "") != expectedNames.vertex && !vertexShaderMismatch) {
            vertexShaderMismatch = where + ": expected " + expectedNames.vertex + ", got " + material.dump();
        }
    }
    REQUIRE(foundMultiLayerBatch);

    INFO("first batch with an out-of-range primitive index (if any): ", primitiveIndexOOR.value_or("none"));
    CHECK(!primitiveIndexOOR.has_value());
    INFO("first batch with an out-of-range/missing material index (if any): ", materialIndexMismatch.value_or("none"));
    CHECK(!materialIndexMismatch.has_value());
    INFO("first batch with a mismatched layer count (if any): ", layerCountMismatch.value_or("none"));
    CHECK(!layerCountMismatch.has_value());
    INFO("first (batch, layer) with a mismatched M2 texture index (if any): ", textureIndexMismatch.value_or("none"));
    CHECK(!textureIndexMismatch.has_value());
    INFO("first (batch, layer) with a mismatched UV set (if any): ", uvSetMismatch.value_or("none"));
    CHECK(!uvSetMismatch.has_value());
    INFO("first batch with a mismatched pixel shader (if any): ", pixelShaderMismatch.value_or("none"));
    CHECK(!pixelShaderMismatch.has_value());
    INFO("first batch with a mismatched vertex shader (if any): ", vertexShaderMismatch.value_or("none"));
    CHECK(!vertexShaderMismatch.has_value());

    fs::remove_all(dir);
}

TEST_CASE("husk export: real multi-texture-layer batch resolves textureComboIndex+layer exactly" *
          doctest::skip(testMultiTextureLayerM2().empty() || testMultiTextureLayerSkin().empty())) {
    checkMultiTextureLayerArithmetic(testMultiTextureLayerM2(), testMultiTextureLayerSkin());
}

TEST_CASE("husk export: real file with a nonzero textureCoordCombos table still resolves "
          "textureCoordComboIndex+layer safely" *
          doctest::skip(testTextureCoordComboM2().empty() || testTextureCoordComboSkin().empty())) {
    checkMultiTextureLayerArithmetic(testTextureCoordComboM2(), testTextureCoordComboSkin());
}

TEST_CASE("husk export: skin file that doesn't belong to the given M2 fails cleanly" *
          doctest::skip(testM2().empty() || testMismatchedSkin().empty())) {
    fs::path dir = freshBundleDir("husk-test-export-bad");
    auto result = runHusk("export \"" + testM2() + "\" -o \"" + dir.string() + "\" --skin \"" +
                          testMismatchedSkin() + "\"");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("mismatch") != std::string::npos);
}

// Locks in husk's "throw, don't misread" behavior on real non-M2 content,
// rather than trusting it stays correct across future chunk-walker
// refactors. blp2_7507381.m2 is a real FileDataID/listfile mismatch pulled
// from live CASC: its stored content is a genuine BLP2 texture, not an M2
// -- husk refuses it with a ParseError-derived "chunk '..." message (the
// malformed/non-ASCII chunk tag) rather than misreading texture bytes as M2
// structure. Checked across all three top-level commands, since each has
// its own read path into the same chunk walker.
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
    fs::path dir = freshBundleDir("husk-test-blp2-anomaly");
    auto result = runHusk("export " + husk::test::testBlp2AnomalyM2() + " -o \"" + dir.string() + "\"");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("chunk") != std::string::npos);
    CHECK(!fs::exists(dir / "manifest.json"));
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
