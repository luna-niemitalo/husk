// CLI tier: `husk export --from-list`/`--output-dir` batch mode
// (TODO/CLEANUP_TODO.md's former item 3) -- exercises husk::commands::
// exportGlb's batch dispatch by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. See
// TEST_DESIGN.md#Four-tier-architecture for how this tier relates to the
// others; tests/test_cli.cpp's own doc comment for why CLI-flag-shaped
// splits (this file included) exist instead of one giant test_cli.cpp.

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export --from-list: exports every listed model into --output-dir, skipping blank "
          "lines and '#'-comments") {
    auto dir = defaultsDir("batch-basic");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    writeFile(dir / "beta.m2", tinyValidM2());
    writeFile(dir / "beta00.skin", tinyMatchingSkin());

    auto listPath = dir / "list.txt";
    {
        std::ofstream list(listPath);
        list << "# a comment line\n";
        list << "\n";
        list << (dir / "alpha.m2").string() << "\n";
        list << (dir / "beta.m2").string() << "\n";
    }
    auto outDir = dir / "out";

    auto result = runHusk("export --from-list " + listPath.string() + " --output-dir " + outDir.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outDir / "alpha.glb"));
    CHECK(fs::exists(outDir / "beta.glb"));
    CHECK(result.output.find("[1/2]") != std::string::npos);
    CHECK(result.output.find("[2/2]") != std::string::npos);
    CHECK(result.output.find("2 succeeded, 0 failed (of 2)") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --from-list: one bad entry is reported and skipped, not fatal to the rest "
          "of the batch -- exit code reflects the partial failure") {
    auto dir = defaultsDir("batch-partial-fail");
    writeFile(dir / "good.m2", tinyValidM2());
    writeFile(dir / "good00.skin", tinyMatchingSkin());

    auto listPath = dir / "list.txt";
    {
        std::ofstream list(listPath);
        list << (dir / "good.m2").string() << "\n";
        list << (dir / "missing.m2").string() << "\n";
    }
    auto outDir = dir / "out";

    auto result = runHusk("export --from-list " + listPath.string() + " --output-dir " + outDir.string());
    CHECK(result.exitCode != 0);
    CHECK(fs::exists(outDir / "good.glb"));
    CHECK_FALSE(fs::exists(outDir / "missing.glb"));
    CHECK(result.output.find("export failed") != std::string::npos);
    CHECK(result.output.find("1 succeeded, 1 failed (of 2)") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --from-list: two entries sharing a basename disambiguate by parent "
          "directory name instead of one silently overwriting the other") {
    auto dir = defaultsDir("batch-collision");
    fs::create_directories(dir / "human");
    fs::create_directories(dir / "orc");
    writeFile(dir / "human" / "model.m2", tinyValidM2());
    writeFile(dir / "human" / "model00.skin", tinyMatchingSkin());
    writeFile(dir / "orc" / "model.m2", tinyValidM2());
    writeFile(dir / "orc" / "model00.skin", tinyMatchingSkin());

    auto listPath = dir / "list.txt";
    {
        std::ofstream list(listPath);
        list << (dir / "human" / "model.m2").string() << "\n";
        list << (dir / "orc" / "model.m2").string() << "\n";
    }
    auto outDir = dir / "out";

    auto result = runHusk("export --from-list " + listPath.string() + " --output-dir " + outDir.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outDir / "model.glb"));
    CHECK(fs::exists(outDir / "orc_model.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --from-list: --output and --input are both rejected as mutually exclusive "
          "with batch mode") {
    auto dir = defaultsDir("batch-mutex");
    writeFile(dir / "one.m2", tinyValidM2());
    writeFile(dir / "one00.skin", tinyMatchingSkin());
    auto listPath = dir / "list.txt";
    {
        std::ofstream list(listPath);
        list << (dir / "one.m2").string() << "\n";
    }

    auto withOutput =
        runHusk("export --from-list " + listPath.string() + " --output " + (dir / "x.glb").string() +
                 " --output-dir " + (dir / "out").string());
    CHECK(withOutput.exitCode != 0);
    CHECK(withOutput.output.find("mutually exclusive") != std::string::npos);

    auto withInput = runHusk("export --input " + (dir / "one.m2").string() + " --from-list " +
                              listPath.string() + " --output-dir " + (dir / "out").string());
    CHECK(withInput.exitCode != 0);
    CHECK(withInput.output.find("mutually exclusive") != std::string::npos);

    auto noOutputDir = runHusk("export --from-list " + listPath.string());
    CHECK(noOutputDir.exitCode != 0);
    CHECK(noOutputDir.output.find("--output-dir") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --from-list: an empty worklist is a no-op, not an error") {
    auto dir = defaultsDir("batch-empty");
    auto listPath = dir / "list.txt";
    {
        std::ofstream list(listPath);
        list << "# nothing here\n\n";
    }
    auto outDir = dir / "out";

    auto result = runHusk("export --from-list " + listPath.string() + " --output-dir " + outDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no entries") != std::string::npos);

    fs::remove_all(dir);
}
