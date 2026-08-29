// CLI tier: `husk export`'s TOML config-file support (CLI11's own
// App::set_config(), wired in addExportOptions -- src/cmd_export.cpp) --
// exercises path resolution (--config flag > $HUSK_CONFIG > XDG default) and
// CLI-flag > config > default precedence by spawning the real compiled
// binary (see run_husk.hpp) against small, synthetic, on-disk fixtures.

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
using husk::test::runCommand;
namespace fs = std::filesystem;

namespace {
void writeConfig(const fs::path& path, const std::string& toml) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << toml;
}
}  // namespace

TEST_CASE("husk export --config: a config-supplied 'output' is used when --output is omitted") {
    auto dir = defaultsDir("config-basic");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto outPath = dir / "from-config.glb";
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "output = \"" + outPath.string() + "\"\n");

    auto result = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    fs::remove_all(dir);
}

TEST_CASE("husk export --config: an explicit --output flag overrides the config file's own value") {
    auto dir = defaultsDir("config-precedence");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto configOut = dir / "from-config.glb";
    auto cliOut = dir / "from-cli.glb";
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "output = \"" + configOut.string() + "\"\n");

    auto result = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string() +
                           " --output " + cliOut.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(cliOut));
    CHECK_FALSE(fs::exists(configOut));

    fs::remove_all(dir);
}

TEST_CASE("husk export: $HUSK_CONFIG is honored when --config is not given") {
    auto dir = defaultsDir("config-envvar");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto outPath = dir / "from-env-config.glb";
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "output = \"" + outPath.string() + "\"\n");

    auto result = husk::test::runCommand("HUSK_CONFIG=" + configPath.string() + " " +
                                          std::string(HUSK_BINARY) + " export " + (dir / "alpha.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    fs::remove_all(dir);
}

TEST_CASE("husk export: XDG_CONFIG_HOME/husk/config.toml is auto-discovered with no --config/$HUSK_CONFIG") {
    auto dir = defaultsDir("config-xdg");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto outPath = dir / "from-xdg-config.glb";
    auto xdgHome = dir / "xdg-config-home";
    writeConfig(xdgHome / "husk" / "config.toml", "output = \"" + outPath.string() + "\"\n");

    auto result = husk::test::runCommand("XDG_CONFIG_HOME=" + xdgHome.string() + " " +
                                          std::string(HUSK_BINARY) + " export " + (dir / "alpha.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir none / --dbd-dir none: overrides a config-supplied value") {
    // REFACTOR/CLI_AND_TOOLING.md §2's "missing state" fix: --db2-dir/
    // --dbd-dir/--listfile/--listfile-root are optional two-state flags
    // (AUDIT.md §7) that, before this, had no way to be switched off for a
    // single invocation once a config file supplied a value. Real dirs
    // with no actual .db2/WoWDBDefs content in them, on purpose -- this
    // test only checks whether husk *attempts* customization-choice DB2
    // resolution at all (a real "no data resolved from '<dir>'" note),
    // not whether that resolution succeeds.
    auto dir = defaultsDir("config-db2-none");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    // attachCustomizationChoices (the function whose stderr note this test
    // looks for) is only reached when the model resolves real bones
    // (cmd_export.cpp's `if (!bones.empty())` gate) -- a same-basename
    // .skel sidecar is the same external-bones convention
    // test_cli_chrmodel_id.cpp's own DB2-driven tests already use.
    writeFile(dir / "alpha.skel", boneCorrectionSkel());
    fs::create_directories(dir / "empty-db2");
    fs::create_directories(dir / "empty-dbd");
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "db2-dir = \"" + (dir / "empty-db2").string() +
                                 "\"\ndbd-dir = \"" + (dir / "empty-dbd").string() + "\"\n");

    auto configHonored = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string());
    CHECK(configHonored.exitCode == 0);
    CHECK(configHonored.output.find("DB2 data resolved from") != std::string::npos);

    auto explicitNone = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string() +
                                 " --db2-dir none --dbd-dir none");
    CHECK(explicitNone.exitCode == 0);
    CHECK(explicitNone.output.find("DB2 data resolved from") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --listfile none: overrides a config-supplied value") {
    auto dir = defaultsDir("config-listfile-none");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto listfilePath = dir / "empty-listfile.csv";
    writeFile(listfilePath, {});  // opens fine, zero usable entries -- triggers the real warning path
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "listfile = \"" + listfilePath.string() + "\"\n");

    auto configHonored = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string());
    CHECK(configHonored.exitCode == 0);
    CHECK(configHonored.output.find("loaded but contained no usable entries") != std::string::npos);

    auto explicitNone = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string() +
                                 " --listfile none");
    CHECK(explicitNone.exitCode == 0);
    CHECK(explicitNone.output.find("loaded but contained no usable entries") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --listfile-root none: overrides a config-supplied value") {
    // Closes the fourth of AUDIT.md §7's four named flags (--db2-dir/
    // --dbd-dir/--listfile above; --listfile-root here).
    //
    // --listfile-root's own fallback-to-default only fires downstream, at
    // the texture-resolution call site (`buildLodTierMeshes`'s
    // `listfileRoot.empty() ? texturesDir : listfileRoot`, cmd_export.cpp)
    // -- reached only once `listfileRoot` is truly empty. So the config-
    // honored case must be the one where the (wrong) root fails to
    // resolve, and 'none' the one that succeeds by falling back to
    // --textures -- checked directly (temporarily reverting the
    // `listfileRoot == "none" -> clear()` fix and re-running just this
    // case) before trusting it: the opposite assertion shape (real content
    // under a --listfile-root-supplied corpus dir, 'none' expected to miss
    // it) turned out NOT to discriminate the fix at all, since a literal,
    // uncleared 'none' used as a directory prefix just fails to resolve
    // the same way an empty string substituted for --textures would if
    // --textures also lacked the file -- both look like "no listfile HIT"
    // regardless of whether the fix exists. This shape does discriminate:
    // confirmed the disabled-fix rebuild fails this exact case.
    auto dir = defaultsDir("config-listfile-root-none");
    writeFile(dir / "alpha.m2", oneTexturedModel(1018799));
    writeFile(dir / "alpha00.skin", oneTexturedModelSkin());
    auto texturesDir = dir / "textures";
    fs::create_directories(texturesDir / "character/human/male");
    writeFile(texturesDir / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});
    auto wrongRoot = dir / "wrong-root";  // real dir, deliberately missing the file above
    fs::create_directories(wrongRoot);
    auto listfilePath = dir / "listfile.csv";
    {
        std::ofstream f(listfilePath);
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }
    auto configPath = dir / "husk-config.toml";
    writeConfig(configPath, "listfile = \"" + listfilePath.string() + "\"\nlistfile-root = \"" +
                                 wrongRoot.string() + "\"\n");

    auto configHonored = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string() +
                                  " --textures " + texturesDir.string() + " --explain-textures --output " +
                                  (dir / "config.glb").string());
    CHECK(configHonored.exitCode == 0);
    // The configured root doesn't have the file -- the listfile tier must miss.
    CHECK(configHonored.output.find("listfile HIT") == std::string::npos);

    auto explicitNone = runHusk("export " + (dir / "alpha.m2").string() + " --config " + configPath.string() +
                                 " --textures " + texturesDir.string() + " --listfile-root none " +
                                 "--explain-textures --output " + (dir / "none.glb").string());
    CHECK(explicitNone.exitCode == 0);
    // 'none' clears the configured (wrong) root, falling back to
    // --textures -- which does have the file -- so the listfile tier hits.
    CHECK(explicitNone.output.find("listfile HIT") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a nonexistent config path (autodiscovery finds nothing) is not an error") {
    auto dir = defaultsDir("config-missing");
    writeFile(dir / "alpha.m2", tinyValidM2());
    writeFile(dir / "alpha00.skin", tinyMatchingSkin());
    auto emptyXdgHome = dir / "xdg-config-home-empty";
    fs::create_directories(emptyXdgHome);

    auto result = husk::test::runCommand("XDG_CONFIG_HOME=" + emptyXdgHome.string() + " " +
                                          std::string(HUSK_BINARY) + " export " + (dir / "alpha.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(dir / "alpha.glb"));

    fs::remove_all(dir);
}
