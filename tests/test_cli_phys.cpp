// CLI tier: --phys resolution -- exercises husk::commands::exportBundle by
// spawning the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk fixtures. Covers same-basename/explicit-path/none
// resolution into the bundle's resources.physics, and out-of-range .phys
// body bone references. See TEST_DESIGN.md#Four-tier-architecture for how
// this tier relates to the others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export: --phys resolves a same-basename '.phys' file into the bundle's "
          "resources.physics, end to end") {
    auto dir = defaultsDir("physdefault");
    writeFile(dir / "physdefault.m2", tinyValidM2());
    writeFile(dir / "physdefault00.skin", tinyMatchingSkin());
    writeFile(dir / "physdefault.skel", boneCorrectionSkel());
    writeFile(dir / "physdefault.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physdefault.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "physdefault.bundle");
    REQUIRE(m["resources"].contains("physics"));
    const nlohmann::json& bodies = m["resources"]["physics"]["bodies"];
    REQUIRE(bodies.size() == 1);
    CHECK(bodies[0]["joint"] == 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys none never adds physics, even when a matching same-basename "
          "'.phys' sits in the default (model's own) directory") {
    auto dir = defaultsDir("physnone");
    writeFile(dir / "physnone.m2", tinyValidM2());
    writeFile(dir / "physnone00.skin", tinyMatchingSkin());
    writeFile(dir / "physnone.skel", boneCorrectionSkel());
    writeFile(dir / "physnone.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physnone.m2").string() + " --phys none");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "physnone.bundle");
    CHECK_FALSE(m["resources"].contains("physics"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys <path> resolves an explicitly-named .phys file, not requiring "
          "the same-basename convention") {
    auto dir = defaultsDir("physexplicit");
    writeFile(dir / "phys-explicit.m2", tinyValidM2());
    writeFile(dir / "phys-explicit.skin", tinyMatchingSkin());
    writeFile(dir / "phys-explicit.skel", boneCorrectionSkel());
    auto physPath = dir / "physdata-under-a-different-name.phys";
    writeFile(physPath, buildPhysFile(0, 1, 2, 3));

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "phys-explicit.m2").string() + " --skin " +
                          (dir / "phys-explicit.skin").string() + " -o " + bundle.string() + " --skel " +
                          (dir / "phys-explicit.skel").string() + " --phys " + physPath.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(bundle);
    REQUIRE(m["resources"].contains("physics"));
    CHECK(m["resources"]["physics"]["bodies"].size() == 1);

    fs::remove_all(dir);
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
                           tempPath("phys-oor.bundle").string() + " --skel " + skelPath.string() +
                           " --phys " + physPath.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("phys-oor.phys") != std::string::npos);
    CHECK(result.output.find("body 0") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove(physPath);
    fs::remove_all(tempPath("phys-oor.bundle"));
}
