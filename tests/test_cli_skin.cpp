// CLI tier: --skin/--skin-dir/--lod resolution -- exercises
// husk::commands::exportGlb by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Split out of the
// original tests/test_cli.cpp (FILE_SPLIT_TODO.md Item 5) -- covers 'auto'
// resolution order (SFID vs. same-basename scan), --skin-dir none/default,
// --skin explicit/none conflicts, and --lod <n>/all tier selection. See
// TEST_DESIGN.md#Four-tier-architecture for how this tier relates to the
// others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export: --skin auto (explicit) without --skin-dir defaults to the model's own "
          "directory, and still fails cleanly when the FileDataID-named .skin isn't there") {
    auto m2Path = tempPath("auto-no-skindir.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-no-skindir.glb").string());
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
                           tempPath("auto-omitted.glb").string());
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
                           tempPath("auto-no-sfid.glb").string() + " --skin-dir " +
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
                           tempPath("skindir-without-auto.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
    CHECK(result.output.find("'auto'") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --lod combined with --skin-dir none is its own explicit conflict -- "
          "--lod needs the SFID-based resolution stage --skin-dir 'none' disables") {
    auto m2Path = tempPath("lod-skindir-none-conflict.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111}));

    auto result = runHusk("export " + m2Path.string() + " --skin-dir none --lod 1");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
    CHECK(result.output.find("'none'") != std::string::npos);

    fs::remove(m2Path);
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

    auto outPath = tempPath("auto-resolve.glb");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string());
    CHECK(result.exitCode == 0);
    // resolveSkin's own success note is "'auto' resolved '<path>' (SFID
    // entry 0, ...)" -- note the word order (unlike the --lod-given path's
    // "resolved 'auto' -> ..." in exportGlb, these two success messages are
    // phrased differently for what's conceptually the same event).
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: 'auto' with --skin-dir pointing at a directory missing the resolved "
          "FileDataID's .skin, and no same-basename fallback either, fails cleanly") {
    auto m2Path = tempPath("auto-missing-file.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {999999999}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-missing-file.glb").string() + " --skin-dir " +
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

TEST_CASE("husk export: --lod given while --skin is an explicit path (not 'auto') fails "
          "cleanly") {
    auto m2Path = tempPath("lod-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("lod-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("lod-without-auto.glb").string() + " --lod 1");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("'auto'") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --lod <n> resolves SFID entry n instead of always 0") {
    auto m2Path = tempPath("lod-n.m2");
    uint32_t entry1FileDataId = 777222;
    // Entry 0 (777111) deliberately has no matching file on disk -- this
    // only passes if --lod 1 actually picked entry 1, not entry 0.
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {777111, entry1FileDataId}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath = skinDir / (std::to_string(entry1FileDataId) + ".skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto outPath = tempPath("lod-n.glb");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod 1");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find(std::to_string(entry1FileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: --lod <n> out of range for the SFID chunk fails cleanly, naming the "
          "entry count") {
    auto m2Path = tempPath("lod-oor.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111, 222222}));  // 2 entries

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("lod-oor.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string() + " --lod 5");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("out of range") != std::string::npos);
    CHECK(result.output.find("2") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod given a non-numeric, non-'all' value fails cleanly") {
    auto m2Path = tempPath("lod-nan.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("lod-nan.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string() + " --lod banana");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod all resolves every SFID entry and exports one named node per LOD "
          "tier") {
    auto m2Path = tempPath("lod-all.m2");
    uint32_t id0 = 888001, id1 = 888002;
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {id0, id1}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath0 = skinDir / (std::to_string(id0) + ".skin");
    auto skinPath1 = skinDir / (std::to_string(id1) + ".skin");
    writeFile(skinPath0, tinyMatchingSkin());
    writeFile(skinPath1, tinyMatchingSkin());

    auto outPath = tempPath("lod-all.glb");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod all");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 LOD tier(s)") != std::string::npos);
    CHECK(result.output.find("lod0") != std::string::npos);
    CHECK(result.output.find("lod1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath0);
    fs::remove(skinPath1);
    fs::remove(outPath);
}

