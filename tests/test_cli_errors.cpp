// CLI tier: crash-avoidance/error-path tests -- exercises husk::commands::
// info/exportGlb by spawning the real compiled binary (see run_husk.hpp)
// against small, synthetic, on-disk fixtures -- always run, no real game
// files or HUSK_TEST_* env vars needed. See TEST_DESIGN.md#Four-tier-
// architecture for how this tier relates to the others. Every fixture below
// targets one specific, previously-confirmed-broken behavior; if any of
// these start failing again, it's a real regression, not a flake.
//
// Split out of tests/test_cli.cpp (FILE_SPLIT_TODO.md's post-completion
// audit -- the original file was still over the 1000-line hard limit after
// Item 5's own split): every case whose whole point is "this corrupted/
// adversarial/malformed model-file *content* must fail cleanly (a real
// diagnostic message and a controlled exit), not crash, hang, or silently
// misread" lives here -- corrupted chunk/count/index data, bone-parent
// cycles, non-finite vertices, and batch->submesh->material->color/
// textureWeight/texture/textureCoord out-of-range chains. CLI *argv*-
// grammar errors (CLI11's RequiredError/ExtrasError/missing-value cases,
// info/dump-chunks' hand-written argc guards) live in
// tests/test_cli_argv.cpp instead, grouped with the rest of that file's
// argument-grammar cases rather than here by failure shape -- see that
// file's own doc comment. `husk info`/`husk export` cases that instead
// prove a *well-formed* file prints the right output moved to
// tests/test_cli_info.cpp/test_cli.cpp -- see those files' own doc
// comments. Shared byte-builder helpers live in tests/test_cli_fixtures.hpp,
// included by all test_cli*.cpp files.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk info: directory as path fails cleanly, not a crash") {
    auto result = runHusk("info " + fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);
}

TEST_CASE("husk info: chunked file with a truncated trailing chunk header fails cleanly, not a "
          "crash") {
    // Valid MD21 wrapper around a minimal MD20 blob, followed by a
    // truncated second chunk header (a tag with no size field) -- used to
    // throw husk::ChunkError straight through info()'s then-too-narrow
    // catch and abort the whole process.
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SKID");
    bytes.push_back(0xAA);  // 1 of 4 size bytes -- truncated

    auto path = tempPath("chunked-truncated.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: generic non-M2 garbage fails cleanly, not a crash") {
    // Not MD20, and not a well-formed chunk stream either -- exercises
    // "falls through to 'maybe this is chunked', then runs out of buffer
    // mid-header", using nothing more exotic than a wrong magic and
    // zero-filled padding. This is the realistic case: pointing husk at the
    // wrong file entirely, not a hand-crafted one.
    // TODO: Remove: FAILURES.md #1.
    std::vector<uint8_t> bytes;
    putTag(bytes, "XXXX");
    bytes.resize(300, 0);

    auto path = tempPath("garbage.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk export: corrupted huge vertex count fails with a real message, not "
          "std::bad_alloc") {
    auto m2 = minimalMd20();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x03C, &count, 4);
    std::memcpy(m2.data() + 0x040, &off, 4);
    auto m2Path = tempPath("huge-vertices.m2");
    writeFile(m2Path, m2);

    // m2::parseVertices runs (and, pre-fix, would have OOM'd) before
    // cmd_export.cpp ever opens the .skin file, so this path doesn't need
    // to exist or be valid.
    auto result = runHusk("export " + m2Path.string() + " --skin /nonexistent.skin -o " +
                           tempPath("huge-vertices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("vertices array claims") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: corrupted huge bone count fails with a real message, not "
          "std::bad_alloc") {
    auto m2 = tinyValidM2();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x02C, &count, 4);
    std::memcpy(m2.data() + 0x030, &off, 4);
    auto m2Path = tempPath("huge-bones.m2");
    writeFile(m2Path, m2);

    auto skinPath = tempPath("huge-bones.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("huge-bones.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("bones array claims") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: .skin file with a corrupted huge indices count fails with a real "
          "message, not std::bad_alloc") {
    auto m2Path = tempPath("for-huge-skin-indices.m2");
    writeFile(m2Path, minimalMd20());  // 0 vertices -- never reached, indices fails first

    std::vector<uint8_t> skin;
    putTag(skin, "SKIN");
    putU32(skin, 0);
    putU32(skin, 0);           // vertices: count=0, offset=0
    putU32(skin, 0xFFFFFFF0);  // indices: corrupted huge count
    putU32(skin, 8);
    putU32(skin, 0);
    putU32(skin, 0);  // bones: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // submeshes: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // batches: count=0, offset=0
    auto skinPath = tempPath("huge-indices.skin");
    writeFile(skinPath, skin);

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("huge-skin-indices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("uint16 entries") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a 2-cycle in the bones' parent chain is rejected, not silently "
          "exported") {
    auto m2Path = tempPath("cycle.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("cycle.skin");
    writeFile(skinPath, tinyMatchingSkin());
    // Bone 0's parent is bone 1, bone 1's parent is bone 0: every
    // individual parentBone value is in-range and non-self-referential, so
    // only chain-walking (not a plain range check) can catch this.
    auto skelPath = tempPath("cycle.skel");
    writeFile(skelPath, buildSkel({{-1, 1}, {-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("cycle.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a bone that is its own parent (a 1-node cycle) is still rejected, "
          "guarded against regressing while fixing the longer-cycle case above") {
    // A self-parent is the degenerate 1-node case of a bone-parent-chain
    // cycle. It was already caught before this fix, by a dedicated check
    // in gltf::writeGlb -- cmd_export.cpp's new checkNoBoneCycles() (in
    // buildSkeleton(), which runs first) now catches it too and throws
    // its own "loops back on itself" first, so writeGlb's check never
    // gets a chance to fire for this particular caller. That's fine --
    // writeGlb's check is still live for any caller that skips
    // buildSkeleton() -- but it does mean this test should expect the
    // newer message, not the older one.
    auto m2Path = tempPath("self-parent.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("self-parent.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("self-parent.skel");
    writeFile(skelPath, buildSkel({{-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("self-parent.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: non-finite (NaN/Inf) vertex position is rejected, not silently baked "
          "into the glb") {
    auto m2 = tinyValidM2();
    size_t vertexOff = m2.size() - 0x30;
    uint32_t nanBits = 0x7FC00000;  // quiet NaN
    uint32_t infBits = 0x7F800000;  // +Infinity
    std::memcpy(m2.data() + vertexOff + 0x00, &nanBits, 4);
    std::memcpy(m2.data() + vertexOff + 0x04, &infBits, 4);
    auto m2Path = tempPath("nan-vertex.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("nan-vertex.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("nan-vertex.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// Regression test for the second fix above: a wrong-
// .skin pairing references hundreds of out-of-range vertex indices in real
// corpus files, not one -- the error message now names how many and the
// worst offender, not just the first index iteration happened to hit.
TEST_CASE("husk export: a .skin referencing multiple out-of-range M2 vertices reports the count "
          "and the worst offender, not just the first") {
    auto dir = defaultsDir("outofrangevtx");
    writeFile(dir / "mismatch.m2", tinyValidM2());
    writeFile(dir / "mismatch00.skin", outOfRangeVertexSkin());

    auto result = runHusk("export " + (dir / "mismatch.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("references 6 out-of-range M2 vertex index(es)") != std::string::npos);
    CHECK(result.output.find("up to 6") != std::string::npos);
    CHECK(result.output.find("only has 1 vertices") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: no .skin path given and none found next to the model fails cleanly, "
          "naming what was expected") {
    auto dir = defaultsDir("noskin");
    writeFile(dir / "lonely.m2", tinyValidM2());

    auto result = runHusk("export " + (dir / "lonely.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no same-named") != std::string::npos);

    fs::remove_all(dir);
}

// Adversarial/out-of-range coverage for buildMaterialsAndPrimitives
// (cmd_export.cpp): six real bounds checks chaining batch -> submesh ->
// material -> color/textureWeight/texture/textureCoord. A real mismatched
// .skin/.m2 pairing hits exactly these paths.
// TODO: Remove: FINDINGS.md §4.2.

TEST_CASE("husk export: batch skinSectionIndex out of range for submeshes fails cleanly") {
    auto dir = defaultsDir("badskinsection");
    writeFile(dir / "m.m2", tinyValidM2());
    writeFile(dir / "m.skin", oneBatchSkin({.skinSectionIndex = 1}));  // only submesh 0 exists

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("skinSectionIndex (1) is out of range for 1 submeshes") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: submesh index range past the resolved triangle-index buffer fails "
          "cleanly (corrupted .skin?)") {
    auto dir = defaultsDir("badindexrange");
    writeFile(dir / "m.m2", tinyValidM2());
    // Submesh claims 10 indices; the .skin's own indices array (and thus
    // the resolved triangle-index buffer) only has 3.
    writeFile(dir / "m.skin", oneBatchSkin({}, /*submeshIndexCount=*/10));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("corrupted .skin?") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch materialIndex out of range for materials fails cleanly") {
    auto dir = defaultsDir("badmaterial");
    writeFile(dir / "m.m2", tinyValidM2());       // 0 materials
    writeFile(dir / "m.skin", oneBatchSkin({}));  // materialIndex defaults to 0

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("materialIndex (0) is out of range for 0 materials") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch colorIndex out of range for colors fails cleanly") {
    auto dir = defaultsDir("badcolor");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 1 material, 0 colors
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("colorIndex (0) is out of range for 0 colors") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureWeightComboIndex out of range for textureWeightCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badweightcombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0));  // 1 textureWeightCombos entry
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureWeightComboIndex (5) is out of range for 1 "
                              "textureWeightCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture weight, resolved via textureWeightCombos, out of range "
          "for textureWeights fails cleanly") {
    auto dir = defaultsDir("badweightresolved");
    // textureWeightCombos[0] = 99, but there are 0 real textureWeights.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0, /*textureWeightCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture weight (index 99 via textureWeightCombos[0]) is out of "
                              "range for 0 textureWeights entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureComboIndex out of range for textureCombos fails cleanly") {
    auto dir = defaultsDir("badtexturecombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 0 textureCombos
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureComboIndex (0) is out of range for 0 textureCombos "
                              "entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture, resolved via textureCombos, out of range for textures "
          "fails cleanly") {
    auto dir = defaultsDir("badtextureresolved");
    // textureCombos[0] = 99, but there are 0 real textures.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 0, 1, 0, std::nullopt, /*textureCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture (index 99 via textureCombos[0]) is out of range for 0 "
                              "textures") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureCoordComboIndex out of range for textureCoordCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badtexcoordcombo");
    // 1 real texture, textureCombos[0]=0 (valid, points at it), but only
    // 1 textureCoordCombos entry even though the batch claims index 5.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 1, 1, 1, std::nullopt, /*textureCombo0=*/0));
    writeFile(dir / "m.skin",
              oneBatchSkin({.textureCount = 1, .textureComboIndex = 0, .textureCoordComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureCoordComboIndex (5) is out of range for 1 "
                              "textureCoordCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

