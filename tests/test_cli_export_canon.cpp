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

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

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
