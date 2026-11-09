// CLI tier: --skel/--bones-dir resolution -- exercises
// husk::commands::exportBundle by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures, then reading
// the skeleton and correction sets back from the bundle's resources.rig.
// Covers --bones-dir default/none/BFID resolution, out-of-range .bone
// corrections, --skel none, and multi-root .skel skeletons (alone and
// combined with LOD skins/--bones-dir). See TEST_DESIGN.md#Four-tier-
// architecture for how this tier relates to the others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_read.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

nlohmann::json rigIn(const fs::path& bundle) { return parseManifest(bundle)["resources"]["rig"]; }

// buildBoneFile(joint, 0.01, 0.02, 0.03)'s one correction, as the bundle
// carries it: the file's FileDataID, the joint, and the row-major matrix
// with the translation in its last row.
void checkSingleBoneFileCorrection(const nlohmann::json& rig, int64_t joint) {
    REQUIRE(rig.contains("correction_sets"));
    REQUIRE(rig["correction_sets"].size() == 1);
    const auto& set = rig["correction_sets"][0];
    CHECK(set["ref"]["id"]["kind"] == "file_data_id");
    CHECK(set["ref"]["id"]["value"] == 424242);
    REQUIRE(set["corrections"].size() == 1);
    CHECK(set["corrections"][0]["joint"] == joint);
    const auto& m = set["corrections"][0]["matrix"];
    REQUIRE(m.size() == 16);
    CHECK(m[12].get<double>() == doctest::Approx(0.01));
    CHECK(m[13].get<double>() == doctest::Approx(0.02));
    CHECK(m[14].get<double>() == doctest::Approx(0.03));
}

}  // namespace

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
    CHECK(rigIn(dir / "rigged.bundle")["skeleton"]["joint_count"] == 1);

    fs::remove_all(dir);
}

// boneCorrectionSkel() (used by every --bones-dir test below) lives in
// tests/test_cli_fixtures_scenes.hpp -- also shared with
// tests/test_cli_phys.cpp's --phys cases, which need a real .skel next to
// the model too.

TEST_CASE("husk export: --bones-dir resolves a .skel's BFID-declared FileDataID to a real "
          "'<FileDataID>.bone' file and attaches it as a correction set, end to end") {
    auto m2Path = tempPath("bonesdir.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir.skel");
    writeFile(skelPath, boneCorrectionSkel());
    auto dir = defaultsDir("bonesdir");
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));
    auto out = tempPath("bonesdir.bundle");
    fs::remove_all(out);

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           out.string() + " --skel " + skelPath.string() + " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("weren't found locally") == std::string::npos);
    checkSingleBoneFileCorrection(rigIn(out), 0);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
    fs::remove_all(out);
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
    CHECK_FALSE(rigIn(dir / "bonesdirnone.bundle").contains("correction_sets"));

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
    checkSingleBoneFileCorrection(rigIn(dir / "bonesdirdefault.bundle"), 0);

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
                           tempPath("bonesdir-oor.bundle").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("424242.bone") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

// LOD skins + multi-root and --bones-dir + multi-root are combinations no
// other test covers. A 3-independent-root .skel (buildSkel's own multi-root shape,
// matching the real corpus finding that root count can be large) exercises
// both without a large real fixture: the skeleton shared by every LOD level,
// and --bones-dir's CorrectionSet::joint indices, neither of which this
// file otherwise checks against a multi-root skeleton at all.
TEST_CASE("husk export: SFID LOD skins combined with a multi-root .skel skeleton export cleanly -- "
          "every level shares the one skeleton, roots kept as roots") {
    auto m2Path = tempPath("lod-all-multiroot.m2");
    uint32_t id0 = 888101, id1 = 888102;
    // One skin profile (header numSkinProfiles at 0x044), so SFID entry 0 is
    // the level-0 skin and entry 1 the one LOD skin after it.
    auto md20 = tinyValidM2();
    uint32_t numSkinProfiles = 1;
    std::memcpy(md20.data() + 0x044, &numSkinProfiles, 4);
    writeFile(m2Path, chunkedM2WithSfid(md20, {id0, id1}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath0 = skinDir / (std::to_string(id0) + ".skin");
    auto skinPath1 = skinDir / (std::to_string(id1) + ".skin");
    writeFile(skinPath0, tinyMatchingSkin());
    writeFile(skinPath1, tinyMatchingSkin());

    auto skelPath = tempPath("lod-all-multiroot.skel");
    writeFile(skelPath, buildSkel({{-1, -1}, {-1, -1}, {-1, -1}}));  // 3 independent roots

    auto out = tempPath("lod-all-multiroot.bundle");
    fs::remove_all(out);
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + out.string() +
                           " --skin-dir " + skinDir.string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    auto manifest = parseManifest(out);
    const auto& lod = manifest["resources"]["geometry"]["lod"];
    CHECK(lod["max_level"] == 1);
    CHECK(lod["levels"].size() == 2);
    const auto& skeleton = manifest["resources"]["rig"]["skeleton"];
    CHECK(skeleton["joint_count"] == 3);
    // parents is i32, so read it raw rather than through readF32Slice.
    const auto& parents = skeleton["parents"];
    CHECK(parents["count"] == 3);
    std::ifstream in(out / parents["file"].get<std::string>(), std::ios::binary);
    REQUIRE(in.good());
    in.seekg(parents["byte_offset"].get<std::streamoff>());
    int32_t parentValues[3] = {0, 0, 0};
    in.read(reinterpret_cast<char*>(parentValues), sizeof(parentValues));
    REQUIRE(in.good());
    CHECK(parentValues[0] == -1);
    CHECK(parentValues[1] == -1);
    CHECK(parentValues[2] == -1);

    fs::remove(m2Path);
    fs::remove(skinPath0);
    fs::remove(skinPath1);
    fs::remove(skelPath);
    fs::remove_all(out);
}

TEST_CASE("husk export: --bones-dir combined with a multi-root .skel skeleton attaches "
          "corrections cleanly -- CorrectionSet::joint indices stay raw M2 bone indices") {
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
    // Corrects joint 2 -- the last of the 3 roots, not joint 0 -- so a
    // shifted or misattributed joint index shows up as the wrong joint.
    writeFile(dir / "424242.bone", buildBoneFile(2, 0.01f, 0.02f, 0.03f));
    auto out = tempPath("bonesdir-multiroot.bundle");
    fs::remove_all(out);

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           out.string() + " --skel " + skelPath.string() + " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    checkSingleBoneFileCorrection(rigIn(out), 2);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
    fs::remove_all(out);
}

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
    auto manifest = parseManifest(dir / "rigged.bundle");
    CHECK(manifest["resources"]["rig"]["skeleton"]["joint_count"] == 0);
    CHECK_FALSE(manifest["resources"]["geometry"]["mesh"].contains("joints0"));

    fs::remove_all(dir);
}
