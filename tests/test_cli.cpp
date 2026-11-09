// CLI tier: general `husk export` behavior -- exercises husk::commands::
// exportBundle by spawning the real compiled binary (see run_husk.hpp) against
// small, synthetic, on-disk fixtures -- always run, no real game files or
// HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-architecture for
// how this tier relates to the others. Every fixture below targets one
// specific, previously-confirmed-broken behavior; if any of these start
// failing again, it's a real regression, not a flake.
//
// Sibling files, split by CLI flag: --skin/--skin-dir and LOD skins in
// test_cli_skin.cpp, --anim/.skel-sourced animation in test_cli_anim.cpp,
// --skel/--bones-dir in test_cli_skel.cpp, --phys in test_cli_phys.cpp,
// `husk info` output in test_cli_info.cpp, corrupted/adversarial input in
// test_cli_errors.cpp, argv grammar in test_cli_argv.cpp, texture
// resolution in test_cli_textures.cpp. This file covers `husk export`'s own
// default-resolution and general-flag behavior on well-formed input:
// same-basename .skin/output defaulting, and what the bundle carries for
// collision, geosets, texture layers, global sequences, animated tints, UV
// transforms and blend modes. Shared byte-builder helpers live in
// tests/test_cli_fixtures.hpp, included by all test_cli*.cpp files.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

// CLI default-resolution tests: `husk export <file.m2>` alone (no .skin,
// output, .skel, or --textures/--skin-dir/--anim) should resolve
// everything it reasonably can from what's already sitting next to the
// model, rather than requiring every argument spelled out even when it's
// exactly where the tool could have found it itself. Each fixture below
// gets its own dedicated subdirectory (not the shared system temp root
// tempPath() otherwise writes into) so directory-scan-based defaulting has
// a clean, isolated view -- no risk of an unrelated file from a different
// test case being picked up.

// Export-side version of the two `husk info` cases above.
TEST_CASE("husk export: a version below Wrath (264) prints a loud warning") {
    auto dir = defaultsDir("prewrathexport");
    auto md20 = minimalMd20(/*version=*/256);  // Classic
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x03C, &count, 4);
    std::memcpy(md20.data() + 0x040, &off, 4);
    md20.resize(md20.size() + 0x30, 0);
    writeFile(dir / "classic.m2", md20);
    writeFile(dir / "classic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "classic.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") != std::string::npos);
    CHECK(result.output.find("256") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: model path alone resolves a same-basename .skin and defaults the output "
          "to '<model>.bundle' beside it, end to end") {
    auto dir = defaultsDir("basic");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "basic.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
    CHECK(result.output.find("no output path given") != std::string::npos);
    nlohmann::json m = parseManifest(dir / "basic.bundle");
    CHECK(m["kind"] == "model");

    fs::remove_all(dir);
}

// -o names the bundle directory itself, so an existing directory is the
// bundle written into, not a parent to infer a filename under.
TEST_CASE("husk export: -o pointed at an existing directory writes the bundle into it instead of "
          "failing with 'Is a directory'") {
    auto dir = defaultsDir("outdir");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto outDir = dir / "out";
    fs::create_directories(outDir);

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + outDir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    parseManifest(outDir);

    fs::remove_all(dir);
}

TEST_CASE("husk export: -o with a trailing slash on an existing directory also writes the bundle "
          "into it") {
    auto dir = defaultsDir("outdirslash");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto outDir = dir / "out";
    fs::create_directories(outDir);

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + outDir.string() + "/");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    parseManifest(outDir);

    fs::remove_all(dir);
}

TEST_CASE("husk export: -o with missing parent directories creates them instead of failing "
          "with 'No such file or directory'") {
    auto dir = defaultsDir("outmkdirp");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto nestedOut = dir / "deep" / "nested" / "dir" / "basic.bundle";
    REQUIRE_FALSE(fs::exists(nestedOut.parent_path()));

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + nestedOut.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    parseManifest(nestedOut);

    fs::remove_all(dir);
}

TEST_CASE("husk export: default .skin resolution never matches a different model's file that "
          "merely extends this one's name as a string (the _hd-variant trap)") {
    auto dir = defaultsDir("hdtrap");
    writeFile(dir / "hero.m2", tinyValidM2());
    writeFile(dir / "hero00.skin", tinyMatchingSkin());
    // A real, unrelated, much-higher-poly model -- "hero_hd" starts with
    // "hero" as a plain string, but the character right after the basename
    // is '_', not a digit, so it must never be picked for hero.m2.
    writeFile(dir / "hero_hd00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "hero.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("hero00.skin") != std::string::npos);
    CHECK(result.output.find("hero_hd00.skin") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: multiple same-basename .skin candidates resolves to the lowest-numbered "
          "one, and says so") {
    auto dir = defaultsDir("ambiguous");
    writeFile(dir / "multi.m2", tinyValidM2());
    writeFile(dir / "multi01.skin", tinyMatchingSkin());
    writeFile(dir / "multi00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "multi.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("multi00.skin") != std::string::npos);
    CHECK(result.output.find("2 same-basename .skin files") != std::string::npos);

    fs::remove_all(dir);
}

// A real corpus scan found findSameBasenameSkins pairing
// "mogu_library_crate_10.m2" with "mogu_library_crate_100.skin", which
// belongs to the shorter sibling "mogu_library_crate_1.m2" ("...crate_1" +
// "00" LOD suffix): "...crate_10" + "0" is a spurious 1-digit match that
// ties with the real "...crate_1000.skin" and can win the sort. Reproduced
// with a minimal same-shape pair.
TEST_CASE("husk export: a model basename that's a numeric-suffix prefix of a sibling model's own "
          "basename resolves its own 2-digit-suffix .skin, not the sibling's colliding shorter/"
          "longer match") {
    auto dir = defaultsDir("basenamecollision");
    writeFile(dir / "crate_1.m2", tinyValidM2());
    writeFile(dir / "crate_10.m2", tinyValidM2());
    writeFile(dir / "crate_100.skin", tinyMatchingSkin());   // crate_1's real skin: "crate_1"+"00"
    writeFile(dir / "crate_1000.skin", tinyMatchingSkin());  // crate_10's real skin: "crate_10"+"00"

    auto shortResult = runHusk("export " + (dir / "crate_1.m2").string());
    CHECK(shortResult.exitCode == 0);
    CHECK(shortResult.output.find("crate_100.skin") != std::string::npos);

    auto longResult = runHusk("export " + (dir / "crate_10.m2").string());
    CHECK(longResult.exitCode == 0);
    CHECK(longResult.output.find("crate_1000.skin") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: the model's collision mesh is the bundle's geometry.collision, its own "
          "3 positions and 1 triangle") {
    auto dir = defaultsDir("collision");
    writeFile(dir / "c.m2", tinyValidM2WithCollision());
    writeFile(dir / "c00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "c.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "c.bundle");
    const nlohmann::json& geometry = m["resources"]["geometry"];
    REQUIRE(geometry.contains("collision"));
    CHECK(geometry["collision"]["positions"]["count"] == 3);
    CHECK(geometry["collision"]["indices"]["count"] == 3);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a model without collision data gets no geometry.collision") {
    auto dir = defaultsDir("nocollision");
    writeFile(dir / "c.m2", tinyValidM2());
    writeFile(dir / "c00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "c.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "c.bundle");
    CHECK_FALSE(m["resources"]["geometry"].contains("collision"));

    fs::remove_all(dir);
}

namespace {
std::set<int> geosetIds(const nlohmann::json& manifest) {
    std::set<int> ids;
    for (const auto& p : manifest["resources"]["geometry"]["mesh"]["primitives"]) ids.insert(p["geoset_id"].get<int>());
    return ids;
}
}  // namespace

TEST_CASE("husk export: batches spanning more than one distinct skinSectionId keep each geoset "
          "ID on its own primitives") {
    auto dir = defaultsDir("geosets");
    writeFile(dir / "geoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "geoset00.skin", twoGeosetSkin());

    auto result = runHusk("export " + (dir / "geoset.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "geoset.bundle");
    CHECK(geosetIds(m) == std::set<int>{0, 401});

    fs::remove_all(dir);
}

TEST_CASE("husk export: batches that all share one skinSectionId give one geoset ID") {
    auto dir = defaultsDir("samegeoset");
    auto skin = twoGeosetSkin();
    // Patch submesh 1's skinSectionId (offset 60 + 0x30 + 0x00) from 401 to
    // 0, matching submesh 0 -- same fixture, single-geoset shape.
    uint16_t zero16 = 0;
    std::memcpy(skin.data() + 60 + 0x30 + 0x00, &zero16, 2);
    writeFile(dir / "onegeoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "onegeoset00.skin", skin);

    auto result = runHusk("export " + (dir / "onegeoset.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "onegeoset.bundle");
    CHECK(geosetIds(m) == std::set<int>{0});

    fs::remove_all(dir);
}

// 3,807 real corpus files (particle/ribbon-only VFX models) have no
// geometry at all: such a model is its skeleton and effects.
TEST_CASE("husk export: a genuinely geometry-less model (0 vertices, empty .skin) exports "
          "successfully with no triangles, keeping its skeleton") {
    auto dir = defaultsDir("novtx");
    writeFile(dir / "vfx.m2", zeroVertexOneBoneM2());
    writeFile(dir / "vfx00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "vfx.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    nlohmann::json m = parseManifest(dir / "vfx.bundle");
    CHECK(m["resources"]["geometry"]["mesh"]["indices"]["count"] == 0);
    CHECK(m["resources"]["rig"]["skeleton"]["joints"].size() == 1);

    fs::remove_all(dir);
}

namespace {
const nlohmann::json& onlyMaterial(const nlohmann::json& manifest) {
    const nlohmann::json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 1);
    return materials[0];
}

// Points a tinyValidM2-based fixture's textures/textureCombos/
// textureCoordCombos at `textureCount` zeroed textures, combo entry i -> texture i,
// coordinate combo i -> set 0. A batch's tint, fade and UV animation live
// on its texture layers, so a textureless batch has nowhere to carry them.
void giveTextures(std::vector<uint8_t>& m2, uint16_t textureCount) {
    auto appendArray = [&](size_t headerOff, uint32_t count, size_t recordSize) {
        uint32_t off = static_cast<uint32_t>(m2.size());
        std::memcpy(m2.data() + headerOff, &count, 4);
        std::memcpy(m2.data() + headerOff + 4, &off, 4);
        m2.resize(m2.size() + count * recordSize, 0);
        return off;
    };
    appendArray(0x050, textureCount, 0x10);  // textures
    uint32_t comboOff = appendArray(0x080, textureCount, 2);
    for (uint16_t i = 0; i < textureCount; ++i) std::memcpy(m2.data() + comboOff + 2 * i, &i, 2);
    appendArray(0x088, textureCount, 2);  // textureCoordCombos, all set 0
}
}  // namespace

TEST_CASE("husk export: a batch with textureCount > 1 keeps every texture as a material layer") {
    auto dir = defaultsDir("multitex");
    auto m2 = oneTextureOneMaterialM2();
    giveTextures(m2, 2);  // layer i reads textureCombos[textureComboIndex + i]
    writeFile(dir / "shiny.m2", m2);
    writeFile(dir / "shiny00.skin", oneBatchTwoTexturesSkin());

    auto result = runHusk("export " + (dir / "shiny.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "shiny.bundle");
    CHECK(onlyMaterial(m)["layers"].size() == 2);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with textureCount == 1 gives a one-layer material") {
    auto dir = defaultsDir("singletex");
    auto m2 = oneTextureOneMaterialM2();
    giveTextures(m2, 2);  // a second texture exists; only textureCount decides
    writeFile(dir / "plain.m2", m2);
    auto skin = oneBatchTwoTexturesSkin();
    uint16_t one16 = 1;
    // textureCount lives at submesh-block-end + batch offset 0x0E (see
    // oneBatchTwoTexturesSkin's own layout comment: 52 + 0x30 is the batch's
    // start).
    std::memcpy(skin.data() + 52 + 0x30 + 0x0E, &one16, 2);
    writeFile(dir / "plain00.skin", skin);

    auto result = runHusk("export " + (dir / "plain.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "plain.bundle");
    CHECK(onlyMaterial(m)["layers"].size() == 1);

    fs::remove_all(dir);
}

// A bone track whose global_sequence field is set (continuous,
// M2Sequence-independent looping animation -- glow pulses, idle sway) is
// its own global-sequence clip; see m2::TrackMeta's doc comment for why it
// must not be misattributed to whichever M2Sequence happens to occupy
// outer-array position 0.
TEST_CASE("husk export: a global-sequence-driven bone track resolves to a real global-sequence "
          "clip") {
    auto m2 = tinyValidM2();
    uint32_t boneOff = static_cast<uint32_t>(m2.size());
    uint32_t boneCount = 1;
    std::memcpy(m2.data() + 0x02C, &boneCount, 4);
    std::memcpy(m2.data() + 0x030, &boneOff, 4);
    m2.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(m2.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(m2.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(m2, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    // fillTrack always writes global_sequence = 0xFFFF ("none") at the
    // track's own header -- patch it to a real global-sequence index (3),
    // marking this specific track as global-sequence-driven instead of
    // per-M2Sequence (see fillTrack's own doc comment).
    uint16_t globalSeq = 3;
    std::memcpy(m2.data() + boneOff + 0x10 + 0x02, &globalSeq, 2);
    // Rotation/scale get a trivial single keyframe each, left "none" (
    // fillTrack's default) -- isolates this test to exactly the translation
    // track actually under test.
    fillTrack(m2, boneOff + 0x24, {0}, {identityQuatBytes()});
    fillTrack(m2, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto dir = defaultsDir("globalseq");
    writeFile(dir / "glow.m2", m2);
    writeFile(dir / "glow00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "glow.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "glow.bundle");
    const nlohmann::json& clips = m["resources"]["rig"]["animation"];
    REQUIRE(clips.size() == 1);
    CHECK(clips[0]["sequence_kind"] == "global_sequence");
    CHECK(clips[0]["sequence_index"] == 3);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a model with no global-sequence-driven tracks gains no extra clip") {
    auto dir = defaultsDir("noglobalseq");
    writeFile(dir / "normal.m2", tinyAnimatedM2());
    writeFile(dir / "normal00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "normal.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "normal.bundle");
    // tinyAnimatedM2 has exactly 1 real (per-sequence) clip.
    const nlohmann::json& clips = m["resources"]["rig"]["animation"];
    REQUIRE(clips.size() == 1);
    CHECK(clips[0]["sequence_kind"] == "sequence");

    fs::remove_all(dir);
}

// A batch's M2Color/M2TextureWeight can be genuinely animated (per-sequence
// or global-sequence keyframes), the same track shape a bone's
// translation/rotation/scale can be: the bundle carries it as the layer's
// tint/alpha_fade curve rather than collapsing it to a constant.

namespace {
uint32_t arrayOffset(const std::vector<uint8_t>& m2, size_t headerOff) {
    uint32_t off;
    std::memcpy(&off, m2.data() + headerOff + 4, 4);
    return off;
}
}  // namespace

TEST_CASE("husk export: an animated (non-constant) M2Color track is the layer's tint curve") {
    auto dir = defaultsDir("animatedcolor");
    auto m2 = materialsFixtureM2(1, 1, 0, 0, 0, 0, 0);  // 1 material, 1 color
    // M2Color.color (offset 0x00): red to blue over sequence 0.
    fillTrack(m2, arrayOffset(m2, 0x048), {0, 1000}, {vec3Bytes(1, 0, 0), vec3Bytes(0, 0, 1)});
    giveTextures(m2, 1);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0, .textureCount = 1}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    const nlohmann::json& layers = onlyMaterial(m)["layers"];
    REQUIRE(layers.size() >= 1);
    CHECK(layers[0].contains("tint"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: an animated (non-constant) M2TextureWeight track is the layer's "
          "alpha_fade curve") {
    auto dir = defaultsDir("animatedweight");
    // 1 material, 1 textureWeight, 1 textureWeightCombos entry (its
    // zeroed default value, 0, already points at textureWeights[0]).
    auto m2 = materialsFixtureM2(1, 0, 1, 1, 0, 0, 0);
    // M2TextureWeight.weight, fixed16: opaque (0x7FFF) to transparent.
    fillTrack(m2, arrayOffset(m2, 0x058), {0, 1000}, {{0xFF, 0x7F}, {0x00, 0x00}});
    giveTextures(m2, 1);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    const nlohmann::json& layers = onlyMaterial(m)["layers"];
    REQUIRE(layers.size() >= 1);
    CHECK(layers[0].contains("alpha_fade"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: a constant (non-animated) M2Color track gets no tint curve") {
    auto dir = defaultsDir("constantcolor");
    // materialsFixtureM2's zeroed color record has outer.count=0 (empty,
    // not animated).
    auto m2 = materialsFixtureM2(1, 1, 0, 0, 0, 0, 0);
    giveTextures(m2, 1);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0, .textureCount = 1}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    for (const auto& layer : onlyMaterial(m)["layers"]) CHECK_FALSE(layer.contains("tint"));

    fs::remove_all(dir);
}

// A batch's textureTransformComboIndex resolving to a real
// M2TextureTransform is the coordinate animation of the uv_map its layer
// samples with.

namespace {
bool anyUvMapAnimated(const nlohmann::json& manifest) {
    if (!manifest["resources"].contains("uv_maps")) return false;
    for (const auto& uvMap : manifest["resources"]["uv_maps"]) {
        if (uvMap.contains("transform")) return true;
    }
    return false;
}
}  // namespace

TEST_CASE("husk export: a batch referencing an animated texture transform gets an animated "
          "uv_map, and husk info counts texture_transforms") {
    auto dir = defaultsDir("texturetransform");
    auto m2 = oneTextureTransformM2();
    // Its translation (offset 0x00) scrolls along u over sequence 0.
    fillTrack(m2, arrayOffset(m2, 0x060), {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 0, 0)});
    giveTextures(m2, 1);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureTransformComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    CHECK(anyUvMapAnimated(m));

    auto infoResult = runHusk("info " + (dir / "m.m2").string());
    CHECK(infoResult.exitCode == 0);
    CHECK(infoResult.output.find("texture_transforms: 1 ") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with textureTransformComboIndex = 0xFFFF (none) gets no "
          "animated uv_map") {
    auto dir = defaultsDir("notexturetransform");
    auto m2 = oneTextureTransformM2();
    giveTextures(m2, 1);
    writeFile(dir / "m.m2", m2);
    // Default BatchFields::textureTransformComboIndex is 0xFFFF -- the
    // model has a real texture_transforms entry, but this batch doesn't
    // reference it.
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    CHECK_FALSE(anyUvMapAnimated(m));

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with WoW's additive blend mode (4) gets an additive framebuffer "
          "blend, not plain alpha") {
    auto dir = defaultsDir("blendmodeadd");
    writeFile(dir / "m.m2", oneTexturedModelWithBlendMode(9001, 4));  // 4 = Add
    writeFile(dir / "m.skin", oneTexturedModelSkin());
    writeFile(dir / "9001.png", {'A', 'D', 'D', '1'});

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    const nlohmann::json& material = onlyMaterial(m);
    INFO(material.dump());
    CHECK(material["framebuffer_blend"] == "add");

    fs::remove_all(dir);
}

TEST_CASE("husk export: blend mode 2 (Alpha) gets a plain alpha framebuffer blend") {
    auto dir = defaultsDir("blendmodealpha");
    writeFile(dir / "m.m2", oneTexturedModelWithBlendMode(9002, 2));  // 2 = a real alpha blend
    writeFile(dir / "m.skin", oneTexturedModelSkin());
    writeFile(dir / "9002.png", {'A', 'L', 'P', 'H'});

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    const nlohmann::json& material = onlyMaterial(m);
    INFO(material.dump());
    CHECK(material["framebuffer_blend"] == "alpha");

    fs::remove_all(dir);
}
