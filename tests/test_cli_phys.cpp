// CLI tier: --phys resolution -- exercises husk::commands::exportGlb by
// spawning the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk fixtures. Split out of the original tests/test_cli.cpp
// (FILE_SPLIT_TODO.md Item 5) -- covers same-basename/explicit-path/none
// resolution and out-of-range .phys body bone corrections. See
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

TEST_CASE("husk export: --phys resolves a same-basename '.phys' file and attaches it as inert "
          "physics_bodies extras, end to end") {
    auto dir = defaultsDir("physdefault");
    writeFile(dir / "physdefault.m2", tinyValidM2());
    writeFile(dir / "physdefault00.skin", tinyMatchingSkin());
    writeFile(dir / "physdefault.skel", boneCorrectionSkel());
    writeFile(dir / "physdefault.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physdefault.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1 physics body") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys none never attaches physics bodies, even when a matching "
          "same-basename '.phys' sits in the default (model's own) directory") {
    auto dir = defaultsDir("physnone");
    writeFile(dir / "physnone.m2", tinyValidM2());
    writeFile(dir / "physnone00.skin", tinyMatchingSkin());
    writeFile(dir / "physnone.skel", boneCorrectionSkel());
    writeFile(dir / "physnone.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physnone.m2").string() + " --phys none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("physics body") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys <path> resolves an explicitly-named .phys file, not requiring "
          "the same-basename convention") {
    auto m2Path = tempPath("phys-explicit.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("phys-explicit.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("phys-explicit.skel");
    writeFile(skelPath, boneCorrectionSkel());
    auto physPath = tempPath("physdata-under-a-different-name.phys");
    writeFile(physPath, buildPhysFile(0, 1, 2, 3));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("phys-explicit.glb").string() + " --skel " + skelPath.string() +
                           " --phys " + physPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1 physics body") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove(physPath);
}

TEST_CASE("husk export: a .phys body referencing a bone index out of range for the model's "
          "skeleton fails the export with a clear message, naming the offending file/index") {
    auto m2Path = tempPath("phys-oor.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("phys-oor.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("phys-oor.skel");
    writeFile(skelPath, boneCorrectionSkel());  // 1 bone (index 0) only
    auto physPath = tempPath("phys-oor.phys");
    writeFile(physPath, buildPhysFile(5, 0, 0, 0));  // bone 5 doesn't exist

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("phys-oor.glb").string() + " --skel " + skelPath.string() +
                           " --phys " + physPath.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("phys-oor.phys") != std::string::npos);
    CHECK(result.output.find("body 0") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove(physPath);
}

