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
