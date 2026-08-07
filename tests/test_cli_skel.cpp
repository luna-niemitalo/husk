// CLI tier: --skel/--bones-dir resolution -- exercises
// husk::commands::exportGlb by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Split out of the
// original tests/test_cli.cpp (FILE_SPLIT_TODO.md Item 5) -- covers
// --bones-dir default/none/BFID resolution, out-of-range .bone corrections,
// --skel none, and multi-root .skel skeletons (alone and combined with
// --lod all/--bones-dir). See TEST_DESIGN.md#Four-tier-architecture for how
// this tier relates to the others.

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

TEST_CASE("husk export: a 0-inline-bone model with a same-basename .skel next to it resolves the "
          "skeleton automatically, end to end") {
    auto dir = defaultsDir("skel");
    writeFile(dir / "rigged.m2", tinyValidM2());  // inline bones empty
    writeFile(dir / "rigged00.skin", tinyMatchingSkin());
    writeFile(dir / "rigged.skel", buildSkel({{-1, -1}}));  // one root bone

    auto result = runHusk("export " + (dir / "rigged.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("found and using") != std::string::npos);
    CHECK(result.output.find("rigged.skel") != std::string::npos);
    CHECK(result.output.find("1 bones") != std::string::npos);

    fs::remove_all(dir);
}

// boneCorrectionSkel() (used by every --bones-dir test below) now lives in
// tests/test_cli_fixtures.hpp -- also shared with tests/test_cli_phys.cpp's
// --phys cases, which need a real .skel next to the model too.

TEST_CASE("husk export: --bones-dir resolves a .skel's BFID-declared FileDataID to a real "
          "'<FileDataID>.bone' file and attaches it as inert extras, end to end") {
    auto m2Path = tempPath("bonesdir.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir.skel");
    writeFile(skelPath, boneCorrectionSkel());
    auto dir = defaultsDir("bonesdir");
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);
    CHECK(result.output.find("'.bone' correction set(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

TEST_CASE("husk export: --bones-dir none never attaches corrections, even when a matching "
          "'<FileDataID>.bone' sits in the default (model's own) directory") {
    auto dir = defaultsDir("bonesdirnone");
    writeFile(dir / "bonesdirnone.m2", tinyValidM2());
    writeFile(dir / "bonesdirnone00.skin", tinyMatchingSkin());
    writeFile(dir / "bonesdirnone.skel", boneCorrectionSkel());
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "bonesdirnone.m2").string() + " --bones-dir none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("correction set(s)") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --bones-dir defaults to the model's own directory -- a FileDataID-named "
          "'.bone' file already sitting there is attached without passing the flag") {
    auto dir = defaultsDir("bonesdirdefault");
    writeFile(dir / "bonesdirdefault.m2", tinyValidM2());
    writeFile(dir / "bonesdirdefault00.skin", tinyMatchingSkin());
    writeFile(dir / "bonesdirdefault.skel", boneCorrectionSkel());
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "bonesdirdefault.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a .bone file correcting a bone index out of range for the model's "
          "skeleton fails the export with a clear message, naming the offending file/index") {
    auto m2Path = tempPath("bonesdir-oor.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir-oor.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir-oor.skel");
    writeFile(skelPath, boneCorrectionSkel());  // 1 bone (index 0) only
    auto dir = defaultsDir("bonesdiroor");
    writeFile(dir / "424242.bone", buildBoneFile(5, 0, 0, 0));  // bone 5 doesn't exist

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir-oor.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("424242.bone") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

// --lod all + multi-root and --bones-dir + multi-root are otherwise-untested
// combinations, since each half only had its own dedicated fixture before
// this. A 3-independent-
// root .skel (buildSkel's own multi-root shape, matching the real corpus
// finding that root count can be large) exercises both without a large real
// fixture: writeGlbMulti's synthesis is exercised alongside --lod's shared-
// skeleton-across-LOD-tiers path and --bones-dir's CorrectionSet::joint
// indices, neither of which this file otherwise checks against a multi-root
// skeleton at all.
TEST_CASE("husk export: --lod all combined with a multi-root .skel skeleton exports cleanly -- "
          "the shared skeleton/synthetic-root logic runs once, not per LOD tier") {
    auto m2Path = tempPath("lod-all-multiroot.m2");
    uint32_t id0 = 888101, id1 = 888102;
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {id0, id1}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath0 = skinDir / (std::to_string(id0) + ".skin");
    auto skinPath1 = skinDir / (std::to_string(id1) + ".skin");
    writeFile(skinPath0, tinyMatchingSkin());
    writeFile(skinPath1, tinyMatchingSkin());

    auto skelPath = tempPath("lod-all-multiroot.skel");
    writeFile(skelPath, buildSkel({{-1, -1}, {-1, -1}, {-1, -1}}));  // 3 independent roots

    auto outPath = tempPath("lod-all-multiroot.glb");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod all --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 LOD tier(s)") != std::string::npos);
    CHECK(result.output.find("3 bones") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath0);
    fs::remove(skinPath1);
    fs::remove(skelPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: --bones-dir combined with a multi-root .skel skeleton attaches "
          "corrections cleanly -- CorrectionSet::joint indices stay raw M2 bone indices, "
          "unaffected by the synthesized root node") {
    auto m2Path = tempPath("bonesdir-multiroot.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir-multiroot.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir-multiroot.skel");
    // 3 independent roots (indices 0, 1, 2), plus a BFID chunk so
    // --bones-dir has something to resolve -- same shape as
    // boneCorrectionSkel() above, just multi-root instead of single-root.
    auto skel = buildSkel({{-1, -1}, {-1, -1}, {-1, -1}});
    std::vector<uint8_t> bfid;
    putU32(bfid, 424242);
    appendChunkTo(skel, "BFID", bfid);
    writeFile(skelPath, skel);
    auto dir = defaultsDir("bonesdirmultiroot");
    // Corrects joint 2 -- the last of the 3 roots, not joint 0 -- so this
    // would fail loudly (an out-of-range or misattributed correction) if the
    // synthesized node's presence ever shifted a real joint's index.
    writeFile(dir / "424242.bone", buildBoneFile(2, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir-multiroot.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

// appendChunkReversed/buildPhysFile (used only by tests/test_cli_phys.cpp's
// --phys cases, not by anything in this file) now live in
// tests/test_cli_fixtures.hpp.

TEST_CASE("husk export: --skel none forces an unskinned mesh even when a same-basename .skel "
          "exists next to the model") {
    auto dir = defaultsDir("skelnone");
    writeFile(dir / "rigged.m2", tinyValidM2());  // inline bones empty
    writeFile(dir / "rigged00.skin", tinyMatchingSkin());
    // Same fixture as the "0-inline-bone model... resolves the skeleton
    // automatically" test above, minus the flag -- this one's whole point
    // is that --skel none must still ignore it.
    writeFile(dir / "rigged.skel", buildSkel({{-1, -1}}));

    auto result = runHusk("export " + (dir / "rigged.m2").string() + " --skel none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("found and using") == std::string::npos);
    CHECK(result.output.find("bones") == std::string::npos);

    fs::remove_all(dir);
}

