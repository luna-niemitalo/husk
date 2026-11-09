// CLI tier: --skin/--skin-dir resolution and LOD skins -- exercises
// husk::commands::exportBundle by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Covers 'auto'
// resolution order (SFID vs. same-basename scan), --skin-dir none/default,
// --skin explicit/none conflicts, and the LOD skins that fill the bundle's
// mesh levels. See TEST_DESIGN.md#Four-tier-architecture for how this tier
// relates to the others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export: --skin auto (explicit) without --skin-dir defaults to the model's own "
          "directory, and still fails cleanly when the FileDataID-named .skin isn't there") {
    auto m2Path = tempPath("auto-no-skindir.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-no-skindir.bundle").string());
    CHECK(result.exitCode == 1);
    // --skin-dir now defaults to the model's own directory (same one
    // tempPath() writes m2Path into) rather than being required -- since no
    // '12345.skin' actually exists there, this fails on the file itself,
    // not on a missing flag. resolveSkin's own "not found" reason names
    // the specific candidate path it checked, not just the directory.
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("12345.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --skin omitted resolves identically to --skin auto explicitly") {
    auto m2Path = tempPath("auto-omitted.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " -o " +
                           tempPath("auto-omitted.bundle").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("12345.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: 'auto' resolution order -- the SFID-declared FileDataID wins over a "
          "same-basename numbered scan match when both exist, not just 'some' resolution") {
    auto dir = defaultsDir("sfidwins");
    uint32_t fileDataId = 424242;
    writeFile(dir / "sfidwins.m2", chunkedM2WithSfid(tinyValidM2(), {fileDataId}));
    writeFile(dir / (std::to_string(fileDataId) + ".skin"), tinyMatchingSkin());
    // A same-basename numbered file also sits right next to the model --
    // if resolveSkin tried the fallback scan first (or instead of the SFID
    // stage), this file would get picked, and the assertions below would
    // fail.
    writeFile(dir / "sfidwins00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "sfidwins.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("SFID entry 0, highest-detail LOD") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") == std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId) + ".skin") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: 'auto' reads the SFID FileDataID from --listfile-root's _by_fdid/ before "
          "trusting a same-basename skin, which a listfile can give to another FileDataID") {
    auto root = defaultsDir("storeskin");
    uint32_t fileDataId = 6217937;
    fs::create_directories(root / "item");
    fs::create_directories(root / "_by_fdid");
    writeFile(root / "item" / "storeskin.m2", chunkedM2WithSfid(tinyValidM2(), {fileDataId}));
    writeFile(root / "_by_fdid" / std::to_string(fileDataId), tinyMatchingSkin());
    // The decoy: named like this model's skin, but in the real case it is
    // another FileDataID's bytes.
    writeFile(root / "item" / "storeskin00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (root / "item" / "storeskin.m2").string() + " --listfile-root " +
                          root.string() + " -o " + (root / "out.bundle").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("_by_fdid/" + std::to_string(fileDataId) + "' (SFID entry 0") !=
          std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") == std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("husk export: --skin-dir none skips the SFID stage entirely -- 'auto' falls straight "
          "to the same-basename scan even when a matching FileDataID-named file also sits in "
          "the model's own directory") {
    auto dir = defaultsDir("skindirnone");
    uint32_t fileDataId = 646464;
    writeFile(dir / "skindirnone.m2", chunkedM2WithSfid(tinyValidM2(), {fileDataId}));
    // Both the SFID-declared FileDataID's own file and a same-basename
    // numbered file sit in the model's directory -- --skin-dir none must
    // still pick the same-basename one, never even looking at the
    // FileDataID match.
    writeFile(dir / (std::to_string(fileDataId) + ".skin"), tinyMatchingSkin());
    writeFile(dir / "skindirnone00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "skindirnone.m2").string() + " --skin-dir none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
    CHECK(result.output.find("SFID entry 0") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: 'auto' on a model with no SFID chunk fails cleanly, naming the reason") {
    auto m2Path = tempPath("auto-no-sfid.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {}));  // no SFID chunk at all

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-no-sfid.bundle").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no SFID chunk") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --skin none is rejected by CLI11 at parse time, naming the real "
          "expected values (a path, or 'auto') -- never silently accepted") {
    auto result = runHusk("export some.m2 --skin none");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("auto") != std::string::npos);
}

TEST_CASE("husk export: --skin-dir given while --skin is an explicit path (not 'auto') fails "
          "cleanly") {
    auto m2Path = tempPath("skindir-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("skindir-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skindir-without-auto.bundle").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
    CHECK(result.output.find("'auto'") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: 'auto' + --skin-dir resolves SFID entry 0 (highest-detail LOD) and "
          "exports successfully") {
    auto m2Path = tempPath("auto-resolve.m2");
    uint32_t fileDataId = 555111;
    // Entry 0 (555111) is the one that should get used -- entry 1 (555112)
    // deliberately has no matching file on disk, so this only passes if
    // resolution actually picked entry 0, not "some" entry.
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {fileDataId, 555112}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath = skinDir / (std::to_string(fileDataId) + ".skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto outPath = tempPath("auto-resolve.bundle");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(outPath);
}

TEST_CASE("husk export: 'auto' with --skin-dir pointing at a directory missing the resolved "
          "FileDataID's .skin, and no same-basename fallback either, fails cleanly") {
    auto m2Path = tempPath("auto-missing-file.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {999999999}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-missing-file.bundle").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    // resolveSkin falls back to the same-basename scan (which also finds
    // nothing here) before giving up -- its "not found" reason names the
    // specific candidate path it checked (<skin-dir>/999999999.skin), not
    // just the directory.
    CHECK(result.output.find("'auto' couldn't resolve a .skin file") != std::string::npos);
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("999999999.skin") != std::string::npos);

    fs::remove(m2Path);
}


// The bundle carries every level of detail; these replace choosing one
// with a flag. The SFID entries past the model's numSkinProfiles view skins
// are its LOD skins (REFACTOR/BUNDLE_FORMAT.md "Levels of detail").
namespace {
std::vector<uint8_t> lodModel(const std::vector<uint32_t>& skinFileDataIds) {
    auto md20 = tinyValidM2();
    uint32_t viewSkins = 1;
    std::memcpy(md20.data() + 0x044, &viewSkins, 4);  // numSkinProfiles
    return chunkedM2WithSfid(md20, skinFileDataIds);
}
}  // namespace

TEST_CASE("husk export: SFID entries past the view skins become the bundle's mesh levels 1..n, "
          "each from its own FileDataID-named skin") {
    auto dir = defaultsDir("lodlevels");
    std::vector<uint32_t> ids = {888001, 888002, 888003};
    writeFile(dir / "lodlevels.m2", lodModel(ids));
    for (uint32_t id : ids) writeFile(dir / (std::to_string(id) + ".skin"), tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "lodlevels.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "lodlevels.bundle");
    const nlohmann::json& lod = m["resources"]["geometry"]["lod"];
    REQUIRE(lod["max_level"] == 2);
    REQUIRE(lod["levels"].size() == 3);
    CHECK(lod["levels"][1]["source"]["file"]["id"]["value"] == ids[1]);
    CHECK(lod["levels"][2]["source"]["file"]["id"]["value"] == ids[2]);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a LOD skin missing on disk ends the bundle's levels before it, with a "
          "note naming the level and FileDataID") {
    auto dir = defaultsDir("lodmissing");
    std::vector<uint32_t> ids = {889001, 889002, 889003};
    writeFile(dir / "lodmissing.m2", lodModel(ids));
    writeFile(dir / "889001.skin", tinyMatchingSkin());
    writeFile(dir / "889002.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "lodmissing.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("LOD skin 2 (FileDataID 889003) not found") != std::string::npos);
    nlohmann::json m = parseManifest(dir / "lodmissing.bundle");
    const nlohmann::json& lod = m["resources"]["geometry"]["lod"];
    CHECK(lod["max_level"] == 1);
    CHECK(lod["levels"].size() == 2);

    fs::remove_all(dir);
}
