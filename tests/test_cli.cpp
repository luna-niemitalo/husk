// CLI tier: general `husk export` behavior -- exercises husk::commands::
// exportGlb by spawning the real compiled binary (see run_husk.hpp) against
// small, synthetic, on-disk fixtures -- always run, no real game files or
// HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-architecture for
// how this tier relates to the others. Every fixture below targets one
// specific, previously-confirmed-broken behavior; if any of these start
// failing again, it's a real regression, not a flake.
//
// TODO: Remove: this file exists because of a real, confirmed gap
// (FAILURES.md #5) -- cmd_info.cpp/cmd_export.cpp, the only place several
// bugs actually lived (FAILURES.md #1-#4), had zero committed test
// coverage that didn't require a personal WoW install.
//
// --skin/--skin-dir/--lod, --anim/.skel-sourced animation, --skel/
// --bones-dir, and --phys cases moved to tests/test_cli_skin.cpp/
// test_cli_anim.cpp/test_cli_skel.cpp/test_cli_phys.cpp respectively
// (FILE_SPLIT_TODO.md Item 5, CLI-flag-shaped, not module-shaped). `husk
// info`-only output cases moved to tests/test_cli_info.cpp, corrupted/
// adversarial-input "fails cleanly" cases to tests/test_cli_errors.cpp, and
// argv-grammar cases (--help/--version/named-vs-positional/argc guards) to
// tests/test_cli_argv.cpp (FILE_SPLIT_TODO.md's post-completion audit --
// this file was still over the 1000-line hard limit after Item 5's own
// split; see those three files' own doc comments for their exact scope).
// The texture-resolution cluster (fuzzy/hardcoded-slot basename matching,
// ambiguous-slot size/category/blend-mode ranking, .blp decode, --listfile/
// --listfile-root, the '_sdr' fallback) moved to tests/test_cli_textures.cpp
// (FILE_SPLIT_TODO.md Item 3). What's left here is `husk export`'s own
// default-resolution and general-flag behavior on well-formed input:
// same-basename .skin/output defaulting, --collision, geoset/multi-texture-
// layer/global-sequence/animated-tint/UV-transform notes. Shared
// byte-builder helpers live in tests/test_cli_fixtures.hpp, included by all
// test_cli*.cpp files.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
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
// TODO: Remove: regression test for FAILURES2.md #3.
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
          "path, end to end") {
    auto dir = defaultsDir("basic");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "basic.m2").string());
    CHECK(result.exitCode == 0);
    // --skin's default is "auto" (not a separate "no .skin path given" note
    // the old positional grammar printed) -- resolveSkin's own success note
    // covers this instead.
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
    CHECK(result.output.find("no output path given") != std::string::npos);
    CHECK(fs::exists(dir / "basic.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: -o pointed at an existing directory infers <dir>/<model-basename>.glb "
          "instead of failing with 'Is a directory'") {
    auto dir = defaultsDir("outdir");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto outDir = dir / "out";
    fs::create_directories(outDir);

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + outDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("-o pointed at a directory") != std::string::npos);
    CHECK(fs::exists(outDir / "basic.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: -o with a trailing slash on an existing directory also infers the "
          "output filename") {
    auto dir = defaultsDir("outdirslash");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto outDir = dir / "out";
    fs::create_directories(outDir);

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + outDir.string() + "/");
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outDir / "basic.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: -o with missing parent directories creates them instead of failing "
          "with 'No such file or directory'") {
    auto dir = defaultsDir("outmkdirp");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());
    auto nestedOut = dir / "deep" / "nested" / "dir" / "basic.glb";
    REQUIRE_FALSE(fs::exists(nestedOut.parent_path()));

    auto result = runHusk("export " + (dir / "basic.m2").string() + " -o " + nestedOut.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(nestedOut));

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

// Regression test: a real corpus scan found
// findSameBasenameSkins silently pairing "mogu_library_crate_10.m2" with
// "mogu_library_crate_100.skin" -- which actually belongs to the shorter
// sibling model "mogu_library_crate_1.m2" ("...crate_1" + "00" LOD suffix),
// not "...crate_10" ("...crate_10" + "0", a spurious 1-digit match that
// used to tie with the real "...crate_1000.skin" ("...crate_10" + "00")
// candidate and lose the std::sort tie-break (lexicographically, "0" <
// "00.skin"'s leading "0" but ".skin" < "0" in ASCII, so the *wrong* file
// sorted first). Reproduced here with a minimal same-shape pair: "crate_1"/
// "crate_10" siblings, each with its own real 2-digit-suffix .skin.
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

TEST_CASE("husk export: the collision mesh is omitted by default even when the model has one -- "
          "Blender's stock importer would otherwise render it like a real mesh and occlude the "
          "character") {
    auto m2Path = tempPath("collision-default.m2");
    writeFile(m2Path, tinyValidM2WithCollision());
    auto skinPath = tempPath("collision-default.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("collision-default.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("collision mesh") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --collision attaches the collision mesh when the model has one") {
    auto m2Path = tempPath("collision-on.m2");
    writeFile(m2Path, tinyValidM2WithCollision());
    auto skinPath = tempPath("collision-on.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("collision-on.glb").string() + " --collision");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached a 3-position/1-triangle collision mesh") !=
          std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: batches spanning more than one distinct skinSectionId (geoset ID) print a "
          "loud note naming them") {
    auto dir = defaultsDir("geosets");
    writeFile(dir / "geoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "geoset00.skin", twoGeosetSkin());

    auto result = runHusk("export " + (dir / "geoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 distinct geoset IDs") != std::string::npos);
    CHECK(result.output.find("skinSectionId: 0, 401") != std::string::npos);
    CHECK(result.output.find("doesn't filter geosets yet") != std::string::npos);

    fs::remove_all(dir);
}

// The same-skinSectionId case (the ordinary shape -- every submesh really is
// part of one consistent mesh) must stay quiet: no note at all.
TEST_CASE("husk export: batches that all share one skinSectionId print no geoset note") {
    auto dir = defaultsDir("samegeoset");
    auto skin = twoGeosetSkin();
    // Patch submesh 1's skinSectionId (offset 60 + 0x30 + 0x00) from 401 to
    // 0, matching submesh 0 -- same fixture, single-geoset shape.
    uint16_t zero16 = 0;
    std::memcpy(skin.data() + 60 + 0x30 + 0x00, &zero16, 2);
    writeFile(dir / "onegeoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "onegeoset00.skin", skin);

    auto result = runHusk("export " + (dir / "onegeoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("distinct geoset IDs") == std::string::npos);

    fs::remove_all(dir);
}

// Regression tests: a genuinely geometry-less M2 (0
// vertices, an empty .skin) used to make buildMaterialsAndPrimitives
// manufacture one primitive with empty `indices`, which writeGlbMulti's
// hard "primitive indices must not be empty" check then rejected outright
// -- 3,807 real corpus files (particle/ribbon-only VFX models) failed this
// way. Fixed by skipping mesh output for a geometry-less LOD tier entirely
// (see cmd_export.cpp/gltf.cpp) rather than trying to represent "zero
// triangles" as a mesh at all.
TEST_CASE("husk export: a genuinely geometry-less model (0 vertices, empty .skin) exports "
          "successfully with no mesh, keeping its skeleton") {
    auto dir = defaultsDir("novtx");
    writeFile(dir / "vfx.m2", zeroVertexOneBoneM2());
    writeFile(dir / "vfx00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "vfx.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no renderable geometry") != std::string::npos);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(fs::exists(dir / "vfx.glb"));
    CHECK(fs::file_size(dir / "vfx.glb") > 0);

    fs::remove_all(dir);
}

// A model with genuinely nothing to export at all -- 0 vertices, 0 bones --
// still fails loudly (writeGlbMulti's "meshes must not be empty without a
// skeleton to fall back to" case): there's no mesh *and* no skeleton for the
// zero-mesh path above to fall back to, so this isn't the real corpus shape
// (every real particle-only file found in the corpus had at least one bone)
// and should stay a hard error rather than silently emitting an empty glTF.
TEST_CASE("husk export: a model with 0 vertices and 0 bones (nothing at all to export) still "
          "fails cleanly") {
    auto dir = defaultsDir("nothingatall");
    writeFile(dir / "nothing.m2", minimalMd20());
    writeFile(dir / "nothing00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "nothing.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);

    fs::remove_all(dir);
}

// TODO: Remove: regression test for FAILURES2.md #6 -- a batch with
// textureCount > 1 used to be silently reduced to a single texture with
// zero indication anything was dropped.
TEST_CASE("husk export: a batch with textureCount > 1 prints a note that extra texture layers "
          "are dropped") {
    auto dir = defaultsDir("multitex");
    writeFile(dir / "shiny.m2", oneTextureOneMaterialM2());
    writeFile(dir / "shiny00.skin", oneBatchTwoTexturesSkin());

    auto result = runHusk("export " + (dir / "shiny.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with more than one texture") != std::string::npos);
    CHECK(result.output.find("additional layers are exported as inert 'extras' metadata") !=
          std::string::npos);

    fs::remove_all(dir);
}

// The ordinary (single-texture) case must stay quiet.
TEST_CASE("husk export: a batch with textureCount == 1 prints no multi-texture note") {
    auto dir = defaultsDir("singletex");
    writeFile(dir / "plain.m2", oneTextureOneMaterialM2());
    auto skin = oneBatchTwoTexturesSkin();
    uint16_t one16 = 1;
    // textureCount lives at submesh-block-end + batch offset 0x0E (see
    // oneBatchTwoTexturesSkin's own layout comment: 52 + 0x30 is the batch's
    // start).
    std::memcpy(skin.data() + 52 + 0x30 + 0x0E, &one16, 2);
    writeFile(dir / "plain00.skin", skin);

    auto result = runHusk("export " + (dir / "plain.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("more than one texture") == std::string::npos);

    fs::remove_all(dir);
}

// A bone track whose global_sequence field is set (continuous,
// M2Sequence-independent looping animation -- glow pulses, idle sway)
// resolves to a real global-sequence clip via
// `m2::resolveVec3GlobalSequenceTrack`/`buildGlobalSequenceAnimations`
// (src/m2.cpp, src/cmd_export.cpp) -- see m2::TrackMeta's doc comment for
// why it must not be misattributed to whichever M2Sequence happens to
// occupy outer-array position 0. This checks it end to end through the
// real CLI, not just the underlying parser (see tests/test_m2.cpp for
// that).
// TODO: Remove: regression test for FAILURES2.md #7.
TEST_CASE("husk export: a global-sequence-driven bone track resolves to a real animation clip") {
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
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// The ordinary case (tinyAnimatedM2's per-M2Sequence-only bone -- fillTrack's
// default global_sequence is "none") must not gain a phantom extra clip.
TEST_CASE("husk export: a model with no global-sequence-driven tracks gains no extra clip") {
    auto dir = defaultsDir("noglobalseq");
    writeFile(dir / "normal.m2", tinyAnimatedM2());
    writeFile(dir / "normal00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "normal.m2").string());
    CHECK(result.exitCode == 0);
    // tinyAnimatedM2 resolves to exactly 1 real (per-sequence) clip already
    // (see its own doc comment/other tests using it) -- must stay exactly 1,
    // not 2, confirming no spurious global-sequence clip appears when no
    // track is actually global-sequence-driven.
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// --skel/--textures/--anim/--skin-dir three(-plus)-state coverage
// (DESIGN.md's "CLI argument grammar for export") not already exercised above.

// A batch's M2Color/M2TextureWeight can be genuinely animated (per-sequence
// or global-sequence keyframes), the same track shape a bone's
// translation/rotation/scale can be -- but unlike a bone track (a real,
// animatable glTF node property), core glTF has no way to animate a
// material's baseColorFactor at all, so there's no real clip to build.
// These tests confirm husk says so instead of silently exporting the batch
// as if the track were the ordinary constant-value case.
// TODO: Remove: FINDINGS.md §3.2/FAILURES2.md #7.

TEST_CASE("husk export: an animated (non-constant) M2Color track is dropped with a note, not "
          "silently treated as constant") {
    auto dir = defaultsDir("animatedcolor");
    auto m2 = materialsFixtureM2(1, 1, 0, 0, 0, 0, 0);  // 1 material, 1 color
    patchColorTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);
    CHECK(result.output.find("core glTF has no way to animate a material's baseColorFactor") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an animated (non-constant) M2TextureWeight track is dropped with a "
          "note, not silently treated as constant") {
    auto dir = defaultsDir("animatedweight");
    // 1 material, 1 textureWeight, 1 textureWeightCombos entry (its
    // zeroed default value, 0, already points at textureWeights[0]).
    auto m2 = materialsFixtureM2(1, 0, 1, 1, 0, 0, 0);
    patchWeightTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a constant (non-animated) M2Color track gets no animated-tint note") {
    auto dir = defaultsDir("constantcolor");
    // materialsFixtureM2's zeroed color record has outer.count=0 (empty,
    // not animated) -- the ordinary, already-well-tested case; this just
    // confirms it doesn't spuriously trigger the new note.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 1, 0, 0, 0, 0, 0));
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("color tint") == std::string::npos);

    fs::remove_all(dir);
}

// A batch's textureTransformComboIndex resolving to a real
// M2TextureTransform gets noted and exported as inert extras, not silently
// dropped -- see gltf.hpp's TextureTransform doc comment for why it's
// never applied to the actual render.
// TODO: Remove: FINDINGS.md §3.1.

TEST_CASE("husk export: a batch referencing a texture transform gets a note, and husk info "
          "counts texture_transforms") {
    auto dir = defaultsDir("texturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    writeFile(dir / "m.skin", oneBatchSkin({.textureTransformComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with a UV transform (M2TextureTransform)") !=
          std::string::npos);

    auto infoResult = runHusk("info " + (dir / "m.m2").string());
    CHECK(infoResult.exitCode == 0);
    CHECK(infoResult.output.find("texture_transforms: 1 ") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with textureTransformComboIndex = 0xFFFF (none) gets no "
          "UV-transform note") {
    auto dir = defaultsDir("notexturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    // Default BatchFields::textureTransformComboIndex is 0xFFFF -- the
    // model has a real texture_transforms entry, but this batch doesn't
    // reference it.
    writeFile(dir / "m.skin", oneBatchSkin({}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("UV transform") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch whose material blend mode has no core-glTF equivalent (WoW's "
          "own additive/multiply modes, > 2) gets a real 'blend_mode' extras value, only for "
          "the range alphaMode's Opaque/Mask/Blend collapse actually loses information") {
    auto dir = defaultsDir("blendmodeadd");
    writeFile(dir / "m.m2", oneTexturedModelWithBlendMode(9001, 4));  // 4 = Add
    writeFile(dir / "m.skin", oneTexturedModelSkin());
    writeFile(dir / "9001.png", {'A', 'D', 'D', '1'});

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "m.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("blend_mode") != std::string::npos);
    CHECK(text.find("BLEND") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: blend modes 0-2 (Opaque/AlphaKey/Alpha) never get a 'blend_mode' "
          "extras value -- alphaMode alone already says everything for those") {
    auto dir = defaultsDir("blendmodealpha");
    writeFile(dir / "m.m2", oneTexturedModelWithBlendMode(9002, 2));  // 2 = a real alpha blend
    writeFile(dir / "m.skin", oneTexturedModelSkin());
    writeFile(dir / "9002.png", {'A', 'L', 'P', 'H'});

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "m.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("blend_mode") == std::string::npos);
    CHECK(text.find("BLEND") != std::string::npos);

    fs::remove_all(dir);
}

