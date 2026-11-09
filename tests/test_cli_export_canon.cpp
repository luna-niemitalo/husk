// CLI tier: properties of the model bundle `husk export` writes -- where it
// goes by default, its subject's FileDataID, LOD skins as mesh levels, a
// store model's .skel found by FileDataID -- and export-wmo's GFID levels,
// through the real compiled binary (run_husk.hpp).

#include <algorithm>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"
#include "test_data_paths.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export without -o: writes the bundle directory <model>.bundle/ beside the model, and no "
          "other output") {
    auto dir = defaultsDir("export_default_output");
    fs::remove_all(dir);
    fs::create_directories(dir);
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());

    auto result = runHusk("export " + (dir / "textured.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(fs::is_regular_file(dir / "textured.bundle" / "manifest.json"));
    CHECK(runHusk("validate-bundle " + (dir / "textured.bundle").string()).exitCode == 0);
    std::vector<std::string> entries;
    for (const auto& e : fs::directory_iterator(dir)) entries.push_back(e.path().filename().string());
    std::sort(entries.begin(), entries.end());
    CHECK(entries == std::vector<std::string>{"textured.bundle", "textured.m2", "textured00.skin"});

    fs::remove_all(dir);
}

TEST_CASE("husk export: the bundle's subject carries the model's FileDataID from the listfile") {
    auto dir = defaultsDir("bundleonly_subject");
    fs::create_directories(dir / "world" / "doodad");
    writeFile(dir / "world" / "doodad" / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "world" / "doodad" / "textured00.skin", oneTexturedModelSkin());
    std::ofstream(dir / "listfile.csv") << "777;world/doodad/textured.m2\n";

    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + (dir / "world" / "doodad" / "textured.m2").string() + " -o " + bundle.string() +
                          " --listfile " + (dir / "listfile.csv").string() + " --listfile-root " +
                          dir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    CHECK(m["subject"]["id"]["value"] == 777);
    CHECK(m["subject"]["name"] == "textured");
}

TEST_CASE("husk export: a doodad's _lodNN skins become mesh levels 1..3, coarser each step" *
          doctest::skip(!fs::exists(husk::test::testLodDoodad()))) {
    auto dir = defaultsDir("bundleonly_lod");
    auto bundle = dir / "out.bundle";
    auto result = runHusk("export " + husk::test::testLodDoodad() + " -o " + bundle.string() +
                          " --config /dev/null");
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(runHusk("validate-bundle " + bundle.string()).exitCode == 0);

    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    const nlohmann::json& lod = m["resources"]["geometry"]["lod"];
    REQUIRE(lod["max_level"] == 3);
    REQUIRE(lod["levels"].size() == 4);
    for (size_t l = 1; l < 4; ++l) {
        INFO("level " << l);
        CHECK(lod["levels"][l]["positions"] == m["resources"]["geometry"]["mesh"]["positions"]);  // one vertex list
        CHECK(lod["levels"][l]["indices"]["count"] < lod["levels"][l - 1]["indices"]["count"]);
        CHECK(lod["levels"][l]["source"]["file"]["id"]["value"] == std::vector<int>{3895592, 3895593, 3895594}[l - 1]);
    }
}

TEST_CASE("husk export-wmo: GFID rows become mesh levels, a group past its last file drawing its last one" *
          doctest::skip(!fs::exists(husk::test::testWmoChamberCap()))) {
    // Rows: groups 0 and 1 have lod1 and lod2, only group 1 has lod3; group
    // 2 lacks the LOD flag and has no level files.
    auto dir = defaultsDir("wmo_lod");
    auto bundle = dir / "out.bundle";
    auto result = runHusk("export-wmo --config /dev/null " + husk::test::testWmoChamberCap() + " " + bundle.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(runHusk("validate-bundle " + bundle.string()).exitCode == 0);

    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    const nlohmann::json& lod = m["resources"]["geometry"]["lod"];
    REQUIRE(lod["max_level"] == 3);
    auto partLevels = [&](size_t level) {
        std::vector<std::pair<int, int>> out;
        for (const auto& p : lod["levels"][level]["source"]["part_levels"]) out.emplace_back(p["part"], p["level"]);
        return out;
    };
    CHECK(partLevels(1) == std::vector<std::pair<int, int>>{{0, 1}, {1, 1}});
    CHECK(partLevels(2) == std::vector<std::pair<int, int>>{{0, 2}, {1, 2}});
    CHECK(partLevels(3) == std::vector<std::pair<int, int>>{{0, 2}, {1, 3}});
    CHECK(lod["levels"][1]["indices"]["count"] < lod["levels"][0]["indices"]["count"]);
    CHECK(lod["levels"][1]["positions"] != m["resources"]["geometry"]["mesh"]["positions"]);  // its own vertices
}

TEST_CASE("husk export: a model in the store finds the skeleton its SKID names" *
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
                          " --config /dev/null --listfile " + (dir / "listfile.csv").string() +
                          " --listfile-root " + dir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("_by_fdid/1838505") != std::string::npos);
    nlohmann::json m = nlohmann::json::parse(std::ifstream(bundle / "manifest.json"));
    CHECK(m["resources"]["rig"]["skeleton"]["joint_count"].get<int>() > 0);
}
