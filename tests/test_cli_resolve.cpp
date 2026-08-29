// CLI tier: `husk resolve` (REFACTOR/CLI_AND_TOOLING.md §3, src/cmd_resolve.cpp)
// -- exercises the real compiled binary (see run_husk.hpp) against small,
// synthetic, on-disk fixtures, mirroring the exact tier fixtures
// tests/test_cli_textures.cpp already established for `export
// --explain-textures`'s prose ledger (literal/listfile hit, ambiguous
// tier-3 hit, tier-3 miss) -- this file proves the same four outcomes are
// each recoverable from `resolve`'s structured JSON twin instead of parsed
// prose. Full JSON grammar check via nlohmann (already on the include path
// through tinygltf, same convention test_cli_info_json.cpp established --
// see that file's own header comment for why a real parser, not a
// substring/balance check, is the right bar here).

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

std::optional<nlohmann::json> parseJson(const std::string& s) {
    auto parsed = nlohmann::json::parse(s, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) return std::nullopt;
    return parsed;
}

// run_husk.hpp's runCommand deliberately merges stdout+stderr (`2>&1`,
// shared by every test_cli*.cpp file) -- but `resolve` shares
// resolveSkinsToExport with `export`, which legitimately prints real
// "'auto' resolved ..." notes to stderr (the same skin-resolution
// diagnostics `export`'s own tests already assert on). A real consumer
// spawning `husk resolve` captures stdout alone (Python's own
// `subprocess.run(capture_output=True)` keeps the two separate by
// default) and never sees this text at all -- only this merged-capture
// test harness does. `resolve`'s own JSON is always the last thing
// written, as one contiguous block with no '{' anywhere in the note text
// before it, so locating the first '{' recovers exactly what a real
// stdout-only caller would have received.
std::optional<nlohmann::json> parseJsonFromMixedOutput(const std::string& combined) {
    auto bracePos = combined.find('{');
    if (bracePos == std::string::npos) return std::nullopt;
    return parseJson(combined.substr(bracePos));
}

}  // namespace

TEST_CASE("husk resolve: a literal (tier 1) hit reports the fdid, tier, and resolved name -- and "
          "never writes a .glb, unlike `export`") {
    auto dir = defaultsDir("resolve-literal");
    writeFile(dir / "littex.m2", oneTexturedModel(555));
    writeFile(dir / "littex00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});  // raw bytes -- husk embeds/reads, doesn't decode

    auto result = runHusk("resolve " + (dir / "littex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(!fs::exists(dir / "littex.glb"));

    auto doc = parseJsonFromMixedOutput(result.output);
    REQUIRE(doc.has_value());
    REQUIRE(doc->contains("slots"));
    REQUIRE((*doc)["slots"].size() == 1);
    auto slot = (*doc)["slots"][0];
    CHECK(slot["found"].get<bool>() == true);
    CHECK(slot["tier"].get<std::string>() == "literal");
    CHECK(slot["file_data_id"].get<uint32_t>() == 555);
    CHECK(slot["resolved_name"].get<std::string>() == "555");
    CHECK(slot["byte_count"].get<int64_t>() == 4);
    CHECK(slot["alternate_count"].get<int64_t>() == 0);

    fs::remove_all(dir);
}

TEST_CASE("husk resolve: a --listfile (tier 2) hit reports tier 'listfile', not 'literal' -- same "
          "real-corpus shape as test_cli_textures.cpp's 'listfiletex' export test, resolved "
          "through the JSON ledger instead of the embedded .glb bytes") {
    auto dir = defaultsDir("resolve-listfile");
    writeFile(dir / "listfiletex.m2", oneTexturedModel(1018799));
    writeFile(dir / "listfiletex00.skin", oneTexturedModelSkin());
    auto corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "character/human/male");
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});
    auto listfilePath = dir / "listfile.csv";
    {
        std::ofstream f(listfilePath);
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }

    auto result = runHusk("resolve " + (dir / "listfiletex.m2").string() + " --textures " +
                           corpusRoot.string() + " --listfile " + listfilePath.string());
    CHECK(result.exitCode == 0);

    auto doc = parseJsonFromMixedOutput(result.output);
    REQUIRE(doc.has_value());
    REQUIRE((*doc)["slots"].size() == 1);
    auto slot = (*doc)["slots"][0];
    CHECK(slot["found"].get<bool>() == true);
    CHECK(slot["tier"].get<std::string>() == "listfile");
    CHECK(slot["file_data_id"].get<uint32_t>() == 1018799);
    CHECK(slot["resolved_name"].get<std::string>() == "deathknighteyeglow");
    CHECK(slot["alternate_count"].get<int64_t>() == 0);

    fs::remove_all(dir);
}

TEST_CASE("husk resolve: a genuinely ambiguous tier-3 hit reports its real alternate_count -- the "
          "distinction a consumer needs to tell 'tier 2 answered' from 'tier 3 guessed among N "
          "candidates' (REFACTOR/README.md I4/I6) -- mirrors test_cli_textures.cpp's "
          "'fuzzytex-ambiguous' export fixture exactly") {
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("resolve-ambiguous");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fuzzytexfaceupper00_00_hd.png", onePixelPng);
    writeFile(dir / "fuzzytexhair00_00.png", onePixelPng);

    auto result = runHusk("resolve " + (dir / "fuzzytex.m2").string());
    CHECK(result.exitCode == 0);

    auto doc = parseJsonFromMixedOutput(result.output);
    REQUIRE(doc.has_value());
    REQUIRE((*doc)["slots"].size() == 1);
    auto slot = (*doc)["slots"][0];
    CHECK(slot["found"].get<bool>() == true);
    CHECK(slot["tier"].get<std::string>() == "fuzzy-same-basename-pool");
    CHECK(slot["alternate_count"].get<int64_t>() == 2);
    CHECK(slot["file_data_id"].get<uint32_t>() == 0);  // hardcoded slot, no TXID of its own

    fs::remove_all(dir);
}

TEST_CASE("husk resolve: a miss reports found=false with a real reason, never a bare absence") {
    auto dir = defaultsDir("resolve-miss");
    writeFile(dir / "misstex.m2", oneTexturedModelWithType(1));
    writeFile(dir / "misstex00.skin", oneTexturedModelSkin());
    // No same-basename candidate anywhere, no --textures/--listfile match.

    auto result = runHusk("resolve " + (dir / "misstex.m2").string());
    CHECK(result.exitCode == 0);

    auto doc = parseJsonFromMixedOutput(result.output);
    REQUIRE(doc.has_value());
    REQUIRE((*doc)["slots"].size() == 1);
    auto slot = (*doc)["slots"][0];
    CHECK(slot["found"].get<bool>() == false);
    CHECK(slot["tier"].get<std::string>() == "fuzzy-same-basename-pool");
    CHECK(!slot["reason"].get<std::string>().empty());
    CHECK(slot["resolved_name"].get<std::string>().empty());
    CHECK(slot["alternate_count"].get<int64_t>() == 0);

    fs::remove_all(dir);
}

TEST_CASE("husk resolve: --skin/--skin-dir/--lod share export's exact grammar -- an unresolvable "
          "'auto' fails loudly with the same message export's own resolveSkinsToExport produces "
          "(proving the two commands share one implementation, not two)") {
    auto dir = defaultsDir("resolve-noskin");
    writeFile(dir / "noskin.m2", oneTexturedModel(555));
    // No .skin file anywhere, no SFID chunk in the model -- 'auto' can't resolve.

    auto result = runHusk("resolve " + (dir / "noskin.m2").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("couldn't resolve a .skin file") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk resolve and export --explain-textures agree on the same slot -- the drift this "
          "ledger exists to expose, pinned so a divergence fails here instead of silently "
          "mis-attributing a tier to a corpus task") {
    // Found for real, not hypothetically: `resolve` shipped without
    // `export`'s config-file wiring, so a configured listfile reached one
    // command and not the other and four slots on a real character model
    // reported tier 3 here while `export` resolved them via tier 2. Same
    // question, two answers -- exactly what REFACTOR/README.md's I2 forbids.
    // A config file is what diverged, so this test supplies one rather than
    // passing --listfile explicitly (which both commands already honored).
    auto dir = defaultsDir("resolve-agrees");
    writeFile(dir / "agreetex.m2", oneTexturedModel(1018799));
    writeFile(dir / "agreetex00.skin", oneTexturedModelSkin());
    auto corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "character/human/male");
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});
    {
        std::ofstream f(dir / "listfile.csv");
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }
    {
        std::ofstream f(dir / "config.toml");
        f << "listfile = \"" << (dir / "listfile.csv").string() << "\"\n";
    }
    auto configArg = " --config " + (dir / "config.toml").string();

    auto resolved = runHusk("resolve " + (dir / "agreetex.m2").string() + " --textures " +
                             corpusRoot.string() + configArg);
    CHECK(resolved.exitCode == 0);
    auto doc = parseJsonFromMixedOutput(resolved.output);
    REQUIRE(doc.has_value());
    REQUIRE((*doc)["slots"].size() == 1);
    auto tier = (*doc)["slots"][0]["tier"].get<std::string>();
    CHECK(tier == "listfile");

    auto exported = runHusk("export " + (dir / "agreetex.m2").string() + " --textures " +
                             corpusRoot.string() + " --explain-textures --output " +
                             (dir / "agreetex.glb").string() + configArg);
    CHECK(exported.exitCode == 0);
    // The prose ledger must name the same tier resolve just reported.
    CHECK(exported.output.find(tier + " HIT") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk resolve --listfile none: overrides a config-supplied value") {
    // REFACTOR/AUDIT.md §7's "missing none" gap, extended past `export`
    // alone (DESIGN.md's "Three-state resolution, not two") -- `resolve`
    // shares `export`'s own --config wiring for --listfile/--listfile-root,
    // so a configured listfile needs the same per-invocation opt-out.
    // Same fixture shape as the "agree" test above: with the config
    // honored, the slot resolves via the 'listfile' tier; with 'none', the
    // listfile tier is never tried at all, so the slot falls through to a
    // miss (nothing else in --textures matches this FileDataID).
    auto dir = defaultsDir("resolve-listfile-none");
    writeFile(dir / "nonetex.m2", oneTexturedModel(1018799));
    writeFile(dir / "nonetex00.skin", oneTexturedModelSkin());
    auto corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "character/human/male");
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});
    {
        std::ofstream f(dir / "listfile.csv");
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }
    {
        std::ofstream f(dir / "config.toml");
        f << "listfile = \"" << (dir / "listfile.csv").string() << "\"\n";
    }
    auto configArg = " --config " + (dir / "config.toml").string();

    auto configHonored = runHusk("resolve " + (dir / "nonetex.m2").string() + " --textures " +
                                  corpusRoot.string() + configArg);
    CHECK(configHonored.exitCode == 0);
    auto honoredDoc = parseJsonFromMixedOutput(configHonored.output);
    REQUIRE(honoredDoc.has_value());
    CHECK((*honoredDoc)["slots"][0]["tier"].get<std::string>() == "listfile");

    auto explicitNone = runHusk("resolve " + (dir / "nonetex.m2").string() + " --textures " +
                                 corpusRoot.string() + configArg + " --listfile none");
    CHECK(explicitNone.exitCode == 0);
    auto noneDoc = parseJsonFromMixedOutput(explicitNone.output);
    REQUIRE(noneDoc.has_value());
    CHECK((*noneDoc)["slots"][0]["tier"].get<std::string>() != "listfile");

    fs::remove_all(dir);
}
