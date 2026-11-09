// CLI tier: how `husk export` stores texture payloads as files beside the
// bundle's manifest (REFACTOR/BUNDLE_FORMAT.md "Embed or reference") --
// exercises husk::commands::exportBundle by spawning the real compiled
// binary (see run_husk.hpp) against small, synthetic, on-disk fixtures with
// real, decodable PNG textures. Which file a slot resolves to is
// tests/test_cli_textures.cpp's concern; this file covers the written
// files themselves: one per distinct image, referenced by payload uri.

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
using Json = nlohmann::json;

TEST_CASE("husk export: the resolved texture is a real '<bundle>/textures/<name>.png' file "
          "referenced by its payload uri, its bytes the source PNG's") {
    auto dir = defaultsDir("bundletex");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    std::vector<uint8_t> png = solidColorPng(64, 64, 200, 50, 50);
    writeFile(dir / "555.png", png);

    auto bundle = dir / "out";
    auto result = runHusk("export " + (dir / "textured.m2").string() + " -o " + bundle.string());
    INFO(result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    const Json& textures = manifest["resources"]["textures"];
    REQUIRE(textures.size() == 1);
    REQUIRE(textures[0]["state"] == "resolved");
    CHECK(textures[0]["payload"]["media_type"] == "image/png");
    std::string uri = textures[0]["payload"]["uri"].get<std::string>();
    CHECK(fs::path(uri).parent_path() == "textures");
    CHECK(readFile((bundle / uri).string()) == png);

    fs::remove_all(dir);
}

TEST_CASE("husk export: two materials whose slots share one pool image write that image once, "
          "as one resolved texture entry") {
    auto dir = defaultsDir("bundletex-dedup");
    // Both texture slots are hardcoded char_hair (type 6), with exactly one
    // matching candidate file in --textures: the "many materials, one
    // image" shape.
    writeFile(dir / "twohard.m2", twoHardcodedTexturedModel(6, 6));
    writeFile(dir / "twohard00.skin", twoBatchSkin());
    writeFile(dir / "twohard_hair_color_6001.png", solidColorPng(8, 8, 10, 20, 30));

    auto bundle = dir / "out";
    auto result = runHusk("export " + (dir / "twohard.m2").string() + " -o " + bundle.string());
    INFO(result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    INFO(manifest["resources"]["textures"].dump());
    REQUIRE(manifest["resources"]["materials"].size() == 2);
    size_t resolved = 0;
    for (const Json& texture : manifest["resources"]["textures"]) {
        if (texture["state"] != "resolved") continue;
        ++resolved;
        CHECK(fs::exists(bundle / texture["payload"]["uri"].get<std::string>()));
    }
    CHECK(resolved == 1);
    size_t written = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(bundle / "textures")) ++written;
    CHECK(written == 1);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a texture found by its FileDataID-named file takes the listfile's name, and the "
          "written file is named after it") {
    auto dir = defaultsDir("bundletex-listfile");
    writeFile(dir / "textured.m2", oneTexturedModel(5050505));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "5050505.png", solidColorPng(8, 8, 1, 2, 3));
    std::string listfile = "5050505;character/human/male/deathknighteyeglow.blp\n";
    writeFile(dir / "listfile.csv", std::vector<uint8_t>(listfile.begin(), listfile.end()));

    auto bundle = dir / "out";
    auto result = runHusk("export " + (dir / "textured.m2").string() + " -o " + bundle.string() + " --listfile " +
                           (dir / "listfile.csv").string() + " --listfile-root " + dir.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    const Json& texture = manifest["resources"]["textures"][0];
    REQUIRE(texture["state"] == "resolved");
    CHECK(texture["identity"]["id"]["value"] == 5050505);
    CHECK(texture["identity"]["name"] == "deathknighteyeglow");
    CHECK(texture["identity"]["name_source"] == "listfile");
    CHECK(texture["payload"]["uri"] == "textures/deathknighteyeglow.png");

    fs::remove_all(dir);
}

TEST_CASE("husk export: a texture no tier finds keeps the FileDataID it was asked for") {
    auto dir = defaultsDir("bundletex-missing");
    writeFile(dir / "textured.m2", oneTexturedModel(7777777));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());

    auto bundle = dir / "out";
    auto result = runHusk("export " + (dir / "textured.m2").string() + " -o " + bundle.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    const Json& texture = manifest["resources"]["textures"][0];
    CHECK(texture["state"] == "known_unresolved");
    CHECK(texture["identity"]["id"]["value"] == 7777777);

    fs::remove_all(dir);
}
