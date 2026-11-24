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

namespace {

// `chunkedM2WithSfid(tinyValidM2(), {})` plus one more chunk.
std::vector<uint8_t> chunkedM2With(const char* tag, const std::vector<uint8_t>& payload) {
    auto b = chunkedM2WithSfid(tinyValidM2(), {});
    putTag(b, tag);
    putU32(b, static_cast<uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

std::vector<uint8_t> u32Bytes(uint32_t v) {
    std::vector<uint8_t> b;
    putU32(b, v);
    return b;
}

const nlohmann::json* physSource(const nlohmann::json& manifest) {
    for (const nlohmann::json& s : manifest["provenance"]["sources"]) {
        if (s["name"] == "phys") return &s;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("husk export: the model's PFDC chunk (an inline .phys, padded) is its physics, before a "
          "same-basename .phys") {
    auto dir = defaultsDir("physpfdc");
    auto pfdc = buildPhysFile(0, 0.5f, 0, 0);
    pfdc.insert(pfdc.end(), {0, 0, 0, 0});  // PFDC's alignment padding
    writeFile(dir / "physpfdc.m2", chunkedM2With("PFDC", pfdc));
    writeFile(dir / "physpfdc00.skin", tinyMatchingSkin());
    writeFile(dir / "physpfdc.skel", boneCorrectionSkel());
    writeFile(dir / "physpfdc.phys", buildPhysFile(5, 0, 0, 0));  // would fail: bone 5 doesn't exist

    auto result = runHusk("export " + (dir / "physpfdc.m2").string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "physpfdc.bundle");
    REQUIRE(m["resources"].contains("physics"));
    CHECK(m["resources"]["physics"]["bodies"].size() == 1);
    const nlohmann::json* source = physSource(m);
    REQUIRE(source != nullptr);
    CHECK((*source)["available"] == true);
    CHECK((*source)["detail"] == "PFDC chunk");

    fs::remove_all(dir);
}

TEST_CASE("husk export: an explicit --phys wins over the model's PFDC") {
    auto dir = defaultsDir("physpfdcexplicit");
    writeFile(dir / "m.m2", chunkedM2With("PFDC", buildPhysFile(5, 0, 0, 0)));  // would fail
    writeFile(dir / "m00.skin", tinyMatchingSkin());
    writeFile(dir / "m.skel", boneCorrectionSkel());
    auto physPath = dir / "chosen.phys";
    writeFile(physPath, buildPhysFile(0, 1, 2, 3));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --phys " + physPath.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(dir / "m.bundle");
    CHECK(m["resources"]["physics"]["bodies"].size() == 1);
    CHECK((*physSource(m))["detail"] == "--phys " + physPath.string());

    fs::remove_all(dir);
}

TEST_CASE("husk export: a PFID FileDataID is read from --listfile-root's _by_fdid/") {
    auto root = defaultsDir("physpfid");
    fs::create_directories(root / "item");
    fs::create_directories(root / "_by_fdid");
    writeFile(root / "item" / "m.m2", chunkedM2With("PFID", u32Bytes(4242)));
    writeFile(root / "item" / "m00.skin", tinyMatchingSkin());
    writeFile(root / "item" / "m.skel", boneCorrectionSkel());
    writeFile(root / "_by_fdid" / "4242", buildPhysFile(0, 0, 0, 1));

    auto bundle = root / "out.bundle";
    auto result = runHusk("export " + (root / "item" / "m.m2").string() + " --listfile-root " + root.string() +
                          " -o " + bundle.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(bundle);
    CHECK(m["resources"]["physics"]["bodies"].size() == 1);
    std::string detail = (*physSource(m))["detail"];
    CHECK(detail.rfind("PFID 4242: ", 0) == 0);
    CHECK(detail.find("_by_fdid/4242") != std::string::npos);

    fs::remove_all(root);
}

TEST_CASE("husk export: a PFID not found locally leaves physics out and marks the source unavailable") {
    auto root = defaultsDir("physpfidmissing");
    fs::create_directories(root / "item");
    writeFile(root / "item" / "m.m2", chunkedM2With("PFID", u32Bytes(4243)));
    writeFile(root / "item" / "m00.skin", tinyMatchingSkin());
    writeFile(root / "item" / "m.skel", boneCorrectionSkel());

    auto bundle = root / "out.bundle";
    auto result = runHusk("export " + (root / "item" / "m.m2").string() + " --listfile-root " + root.string() +
                          " -o " + bundle.string());
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json m = parseManifest(bundle);
    CHECK_FALSE(m["resources"].contains("physics"));
    CHECK((*physSource(m))["available"] == false);
    CHECK((*physSource(m))["detail"] == "PFID 4243 not found locally");

    fs::remove_all(root);
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
