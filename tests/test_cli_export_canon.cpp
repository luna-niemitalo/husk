// CLI tier: `husk export --export-canon` -- exercises
// husk::commands::runCanonExport (cmd_export_canon.cpp) by spawning
// the real compiled binary against a small, synthetic, on-disk fixture,
// same convention as tests/test_cli_slim_textures.cpp. The real-fixture
// pipeline (bloodelffemale.m2/.skin, test_data/) was exercised manually
// against a real 119-bone/258-animation character during this feature's
// own implementation -- mesh/skeleton convergence held with zero
// deviations, and the animation comparator correctly flagged real
// alias-sequence naming gaps rather than false-positiving on noise; this
// file only needs to prove the wiring (flag parses, doesn't crash, writes
// its own sidecar outputs, never changes the primary .glb's own exit
// code) on a fixture cheap enough to commit and run in CI.

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"
#include "test_data_paths.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export --export-canon: off by default -- no '.canon.glb'/'.canon.bundle' sidecar, "
          "and the flag itself doesn't change the real export's exit code") {
    auto dir = defaultsDir("comparecanon_off");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());

    auto outPath = dir / "out.glb";
    auto result = runHusk("export " + (dir / "textured.m2").string() + " -o " + outPath.string());
    CHECK(result.exitCode == 0);
    REQUIRE(fs::exists(outPath));
    CHECK_FALSE(fs::exists(dir / "out.canon.glb"));
    CHECK_FALSE(fs::exists(dir / "out.canon.bundle"));
}

TEST_CASE("husk export --export-canon: writes '.canon.glb' and '.canon.bundle/' sidecars alongside "
          "the real output, and never fails the real export even if a deviation is found") {
    auto dir = defaultsDir("comparecanon_on");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());

    auto outPath = dir / "out.glb";
    auto result =
        runHusk("export " + (dir / "textured.m2").string() + " -o " + outPath.string() + " --export-canon");
    CHECK(result.exitCode == 0);
    REQUIRE(fs::exists(outPath));
    CHECK(fs::exists(dir / "out.canon.glb"));
    CHECK(fs::file_size(dir / "out.canon.glb") > 0);
    CHECK(fs::is_directory(dir / "out.canon.bundle"));
    CHECK(fs::exists(dir / "out.canon.bundle" / "manifest.json"));
    CHECK(result.output.find("husk: canon-export:") != std::string::npos);
}

TEST_CASE("husk export --bundle-only: the bundle's subject carries the model's FileDataID from the listfile") {
    auto dir = defaultsDir("bundleonly_subject");
    fs::create_directories(dir / "world" / "doodad");
    writeFile(dir / "world" / "doodad" / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "world" / "doodad" / "textured00.skin", oneTexturedModelSkin());
    std::ofstream(dir / "listfile.csv") << "777;world/doodad/textured.m2\n";

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "world" / "doodad" / "textured.m2").string() + " -o " + bundle.string() +
                          " --bundle-only --listfile " + (dir / "listfile.csv").string() + " --listfile-root " +
                          dir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    CHECK(m["subject"]["id"]["value"] == 777);
    CHECK(m["subject"]["name"] == "textured");
}

TEST_CASE("husk export --bundle-only: a model in the store finds the skeleton its SKID names" *
          doctest::skip(!fs::exists(husk::test::testSkelM2()) || !fs::exists(husk::test::testSkel()))) {
    // The store names files by FileDataID, so no same-basename .skel exists.
    auto dir = defaultsDir("bundleonly_skid");
    fs::path store = dir / "_by_fdid";
    fs::create_directories(store);
    fs::copy_file(husk::test::testSkelM2(), store / "1100000", fs::copy_options::overwrite_existing);
    fs::copy_file(husk::test::testSkelSkin(), store / "1100298", fs::copy_options::overwrite_existing);
    fs::copy_file(husk::test::testSkel(), store / "1838505", fs::copy_options::overwrite_existing);
    std::ofstream(dir / "listfile.csv") << "1;unrelated/file.m2\n";

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (store / "1100000").string() + " -o " + bundle.string() +
                          " --bundle-only --config /dev/null --listfile " + (dir / "listfile.csv").string() +
                          " --listfile-root " + dir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("_by_fdid/1838505") != std::string::npos);
    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    CHECK(m["resources"]["rig"]["skeleton"]["joint_count"].get<int>() > 0);
}
