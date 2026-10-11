// CLI tier: `husk export-terrain` and `husk export-world` against a small
// synthetic corpus on disk (tests/test_adt_fixtures.hpp) -- a real tile set
// with its WDT, a real BLP ground texture and a listfile, run through the
// real binary (run_husk.hpp). Model export (`export-world`'s pass 2) is
// covered by the existing `export --bundle-only` tests; these use
// --skip-models and check that placements stay identity-only.

#include <doctest/doctest.h>

#include "bundle_schema.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <string>

#include "run_husk.hpp"
#include "test_adt_fixtures.hpp"

using husk::test::runHusk;
using namespace adtfx;
namespace fs = std::filesystem;

namespace {

nlohmann::json readJson(const fs::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

// A placement_set document's instances, each with its referenced asset
// resolved into "asset"; the document is schema-checked on the way.
nlohmann::json readPlacements(const fs::path& setFile) {
    std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(setFile);
    std::string report;
    for (const husk::bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
    std::ifstream in(setFile);
    REQUIRE(in.good());
    nlohmann::json set = nlohmann::json::parse(in);
    nlohmann::json instances = set["instances"];
    for (auto& instance : instances) instance["asset"] = set["references"][instance["reference"].get<size_t>()]["asset"];
    return instances;
}

// A tile's model document, schema-checked (with its placement sets) on the way.
nlohmann::json readTile(const fs::path& manifest) {
    std::vector<husk::bundle_schema::Problem> problems = husk::bundle_schema::validateDocument(manifest);
    std::string report;
    for (const husk::bundle_schema::Problem& p : problems) report += p.where + ": " + p.message + "\n";
    INFO(report);
    CHECK(problems.empty());
    return readJson(manifest);
}

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string q(const fs::path& p) { return "'" + p.string() + "'"; }

// <root>/world/maps/azeroth/{azeroth.wdt, azeroth_32_48{,_obj0,_tex0}.adt},
// <root>/tileset/grass.blp, <root>/listfile.csv. The tile has one
// FileDataID doodad (555), one name-table doodad (MMDX path, listfile 4242),
// one building (MODF 666) and two ground textures, one of them extracted.
struct Corpus {
    fs::path root;
    fs::path mapDir() const { return root / "world" / "maps" / "azeroth"; }
    fs::path tile() const { return mapDir() / "azeroth_32_48.adt"; }
    fs::path listfile() const { return root / "listfile.csv"; }
    std::string listfileArgs() const { return "--listfile " + q(listfile()) + " --listfile-root " + q(root); }
};

Corpus makeCorpus(const std::string& name) {
    Corpus c{fs::temp_directory_path() / ("husk-cli-terrain-" + name)};
    fs::remove_all(c.root);

    writeBytes(c.mapDir() / "azeroth.wdt", wdtFile(0x4));
    writeBytes(c.tile(), rootFile(RootSpec{}));

    ObjSpec obj;
    obj.doodadNames = {"World\\Generic\\Rock.mdx"};
    obj.doodads.push_back({555, 1, {kMapHalfExtent - 16000.0f, 10.0f, kMapHalfExtent - 25600.0f}, {}, 1024, 0x40});
    obj.doodads.push_back({0, 2, {kMapHalfExtent - 16010.0f, 10.0f, kMapHalfExtent - 25610.0f}, {}, 1024, 0});
    obj.mapObjects.push_back({666, 3, {}, {}, 0x8, 0, 0, 1024});
    writeBytes(c.mapDir() / "azeroth_32_48_obj0.adt", objFile(obj));

    TexSpec tex;
    tex.diffuseIds = {1001, 1002};
    writeBytes(c.mapDir() / "azeroth_32_48_tex0.adt", texFile(tex));

    writeBytes(c.root / "tileset" / "grass.blp", oneBlockBlp());
    std::ofstream(c.listfile()) << "1001;tileset/grass.blp\n"
                                   "555;world/doodad/tree.m2\n"
                                   "4242;world/generic/rock.m2\n"
                                   "666;world/wmo/inn.wmo\n";
    return c;
}

// A _fogs.wdt with one VFOG (id 1659, model 0) and its VFE2.
std::vector<uint8_t> fogsFile() {
    std::vector<uint8_t> out = mverChunk();
    std::vector<uint8_t> vfog(104, 0);
    setF32(vfog, 0x1C, 100.0f);
    setU32(vfog, 0x64, 1659);
    appendChunk(out, "VFOG", vfog);
    std::vector<uint8_t> vfe2(176, 0);
    setF32(vfe2, 0x3C, 2.0f);
    setU32(vfe2, 0x4C, 1659);
    appendChunk(out, "VFE2", vfe2);
    return out;
}

}  // namespace

TEST_CASE("husk export-terrain: a full tile writes a bundle with textures, layers and every placement") {
    Corpus c = makeCorpus("full");
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " " + c.listfileArgs());
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("256 chunks, 2 textures") != std::string::npos);
    CHECK(r.output.find("2 doodads, 1 map objects") != std::string::npos);

    nlohmann::json t = readTile(out / "manifest.json");
    CHECK(t["subject"]["name"] == "azeroth_32_48");
    CHECK(t["kind"] == "model");
    CHECK(t["resources"]["terrain"]["textures"][0]["diffuse"]["state"] == "resolved");
    CHECK(t["resources"]["terrain"]["textures"][0]["diffuse"]["identity"]["name"] == "grass");
    // Listed but not extracted locally.
    CHECK(t["resources"]["terrain"]["textures"][1]["diffuse"]["state"] == "known_unresolved");
    bool ddsWritten = false;
    for (const auto& e : fs::directory_iterator(out / "textures")) ddsWritten |= e.path().extension() == ".dds";
    CHECK(ddsWritten);

    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 2);
    nlohmann::json doodads = readPlacements(out / "sets" / "0.json");
    CHECK(doodads[0]["asset"]["id"]["value"] == 555);
    CHECK(doodads[0]["asset"]["name"] == "tree");
    // The MMDX path resolved through the listfile like any FileDataID.
    CHECK(doodads[1]["asset"]["id"]["value"] == 4242);
    CHECK(doodads[1]["asset"]["name_source"] == "listfile");
    nlohmann::json mapObjects = readPlacements(out / "sets" / "1.json");
    REQUIRE(mapObjects.size() == 1);
    CHECK(mapObjects[0]["asset"]["name"] == "inn");
    CHECK(mapObjects[0]["selection"]["placement_sets"][0] == 0);
}

TEST_CASE("husk export-terrain: without a listfile, name-table paths stay names and textures stay unresolved") {
    Corpus c = makeCorpus("nolistfile");
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out));
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    nlohmann::json t = readTile(out / "manifest.json");
    CHECK(t["resources"]["terrain"]["textures"][0]["diffuse"]["state"] == "known_unresolved");
    nlohmann::json rock = readPlacements(out / "sets" / "0.json")[1]["asset"];
    CHECK(rock["id"]["kind"] == "none");
    CHECK(rock["name"] == "World\\Generic\\Rock.mdx");
    CHECK(rock["name_source"] == "adt_embedded");
}

TEST_CASE("husk export-terrain --list-models prints every referenced model and writes nothing") {
    Corpus c = makeCorpus("list-models");
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " " + c.listfileArgs() + " --list-models");
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("555\tworld/doodad/tree.m2") != std::string::npos);
    CHECK(r.output.find("4242\tworld/generic/rock.m2") != std::string::npos);
    // Buildings are not models.
    CHECK(r.output.find("666") == std::string::npos);
    CHECK_FALSE(fs::exists(out));
}

TEST_CASE("husk export-terrain --models-dir links placements to model and WMO bundles that exist") {
    Corpus c = makeCorpus("models-dir");
    fs::path models = c.root / "models";
    writeBytes(models / "555.canon.bundle" / "manifest.json", {'{', '}'});
    writeBytes(models / "666.canon.bundle" / "manifest.json", {'{', '}'});
    fs::path out = c.root / "tiles" / "t.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " " + c.listfileArgs() + " --models-dir " +
                     q(models));
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("2 linked models") != std::string::npos);
    // Set files sit in sets/, one level below the bundle.
    nlohmann::json doodads = readPlacements(out / "sets" / "0.json");
    CHECK(doodads[0]["asset"]["uri"] == "../../../models/555.canon.bundle/manifest.json");
    CHECK_FALSE(doodads[1]["asset"].contains("uri"));
    nlohmann::json mapObjects = readPlacements(out / "sets" / "1.json");
    CHECK(mapObjects[0]["asset"]["uri"] == "../../../models/666.canon.bundle/manifest.json");
}

TEST_CASE("husk export-terrain --obj none --tex none needs no WDT and exports bare terrain") {
    Corpus c = makeCorpus("bare");
    fs::remove(c.mapDir() / "azeroth.wdt");
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " --obj none --tex none");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    nlohmann::json t = readTile(out / "manifest.json");
    CHECK(t["resources"]["terrain"]["textures"].empty());
    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 0);
    CHECK(t["definition"]["placement_sets"][1]["instance_count"] == 0);
}

TEST_CASE("husk export-terrain announces what it resolved for every sidecar") {
    Corpus c = makeCorpus("announce");
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(c.root / "out.bundle") + " " + c.listfileArgs());
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("husk: note: --obj: azeroth_32_48_obj0.adt (beside the tile)") != std::string::npos);
    CHECK(r.output.find("husk: note: --tex: azeroth_32_48_tex0.adt (beside the tile)") != std::string::npos);
    CHECK(r.output.find("husk: note: --wdt: ") != std::string::npos);
    CHECK(r.output.find("husk: note: listfile: 4 rows from") != std::string::npos);
    CHECK(r.output.find("husk: note: ground effects: none (needs --db2-dir and --dbd-dir)") != std::string::npos);

    fs::remove(c.mapDir() / "azeroth_32_48_obj0.adt");
    auto missing = runHusk("export-terrain " + q(c.tile()) + " " + q(c.root / "out2.bundle") + " --tex none");
    REQUIRE(missing.exitCode == 0);
    CHECK(missing.output.find("--obj: none (no azeroth_32_48_obj0.adt beside the tile)") != std::string::npos);
    CHECK(missing.output.find("--tex: skipped (--tex none)") != std::string::npos);
    CHECK(missing.output.find("listfile: none") != std::string::npos);
}

TEST_CASE("husk export-terrain: a broken auto-found sidecar is warned about and skipped, the tile still exports") {
    Corpus c = makeCorpus("broken-sidecar");
    writeBytes(c.mapDir() / "azeroth_32_48_obj0.adt", {1, 2, 3});
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out));
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("husk: warning: expected a readable azeroth_32_48_obj0.adt, got: ") != std::string::npos);
    CHECK(r.output.find("--obj none silences this") != std::string::npos);
    nlohmann::json t = readTile(out / "manifest.json");
    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 0);
    CHECK(t["definition"]["placement_sets"][1]["instance_count"] == 0);
    CHECK(t["resources"]["terrain"]["textures"].size() == 2);
}

TEST_CASE("husk export-terrain: a missing auto-located WDT drops texture layers with a warning") {
    Corpus c = makeCorpus("no-wdt");
    fs::remove(c.mapDir() / "azeroth.wdt");
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out));
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("husk: warning: expected the map WDT at") != std::string::npos);
    CHECK(r.output.find("pass --wdt <path>, or --tex none") != std::string::npos);
    nlohmann::json t = readTile(out / "manifest.json");
    CHECK(t["resources"]["terrain"]["textures"].empty());
    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 2);
    CHECK(t["definition"]["placement_sets"][1]["instance_count"] == 1);

    writeBytes(c.mapDir() / "azeroth.wdt", {1, 2, 3});
    auto broken = runHusk("export-terrain " + q(c.tile()) + " " + q(c.root / "broken.bundle"));
    INFO(broken.output);
    REQUIRE(broken.exitCode == 0);
    CHECK(broken.output.find("(its MPHD flags pick the alpha-map format), unreadable: ") != std::string::npos);
}

TEST_CASE("husk export-terrain: failures of the tile itself or of an explicit path exit 1 with the reason") {
    Corpus c = makeCorpus("errors");
    fs::path out = c.root / "out.bundle";

    fs::path brokenObj = c.root / "broken_obj0.adt";
    writeBytes(brokenObj, {1, 2, 3});
    auto explicitObj = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " --obj " + q(brokenObj));
    CHECK(explicitObj.exitCode == 1);
    CHECK(explicitObj.output.find("husk: export-terrain:") != std::string::npos);

    auto explicitWdt = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " --wdt " + q(c.root / "nope.wdt"));
    CHECK(explicitWdt.exitCode == 1);
    CHECK(explicitWdt.output.find("cannot open") != std::string::npos);

    fs::path badName = c.mapDir() / "azeroth.adt";
    fs::copy_file(c.tile(), badName);
    auto named = runHusk("export-terrain " + q(badName) + " " + q(out) + " --tex none");
    CHECK(named.exitCode == 1);
    CHECK(named.output.find("expected a root tile name <map>_<x>_<y>.adt") != std::string::npos);

    writeBytes(c.tile(), {1, 2, 3});
    auto corrupt = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " --tex none");
    CHECK(corrupt.exitCode == 1);
    CHECK(corrupt.output.find("husk: export-terrain:") != std::string::npos);
    CHECK_FALSE(fs::exists(out / "manifest.json"));
}

TEST_CASE("husk export-terrain reads per-machine flags from --config, and 'none' opts one back out") {
    Corpus c = makeCorpus("config");
    fs::path config = c.root / "config.toml";
    std::ofstream(config) << "listfile = \"" << c.listfile().string() << "\"\nlistfile-root = \""
                          << c.root.string() << "\"\n";
    fs::path out = c.root / "out.bundle";
    auto r = runHusk("export-terrain " + q(c.tile()) + " " + q(out) + " --config " + q(config));
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(readTile(out / "manifest.json")["resources"]["terrain"]["textures"][0]["diffuse"]["state"] == "resolved");

    fs::path optedOut = c.root / "out-none.bundle";
    auto none = runHusk("export-terrain " + q(c.tile()) + " " + q(optedOut) + " --config " + q(config) +
                        " --listfile none");
    REQUIRE(none.exitCode == 0);
    CHECK(none.output.find("listfile: none") != std::string::npos);
    CHECK(readTile(optedOut / "manifest.json")["resources"]["terrain"]["textures"][0]["diffuse"]["state"] ==
          "known_unresolved");
}

TEST_CASE("husk export-world: shared textures, per-map tile bundles, logged failures, resumable") {
    Corpus c = makeCorpus("world");
    // A second, broken tile on the same map: logged, never fatal.
    writeBytes(c.mapDir() / "azeroth_33_48.adt", {0, 1, 2, 3});
    writeBytes(c.mapDir() / "azeroth.wdt", wdtFile(WdtSpec{.tiles = {{32, 48}, {33, 48}}}));
    // A second map, excluded below by --map.
    writeBytes(c.root / "world" / "maps" / "other" / "other.wdt", wdtFile(0x4));
    writeBytes(c.root / "world" / "maps" / "other" / "other_32_48.adt", rootFile(RootSpec{}));

    fs::path out = c.root / "scene";
    std::string cmd = "export-world " + q(c.root / "world" / "maps") + " " + q(out) + " " + c.listfileArgs() +
                      " --map azeroth --skip-models --jobs 2";
    auto first = runHusk(cmd);
    INFO(first.output);
    REQUIRE(first.exitCode == 0);
    CHECK(first.output.find("2 tiles") != std::string::npos);
    CHECK(first.output.find("1 errors this run") != std::string::npos);

    fs::path tile = out / "maps" / "azeroth" / "azeroth_32_48.bundle";
    nlohmann::json t = readTile(tile / "manifest.json");
    CHECK(t["resources"]["terrain"]["textures"][0]["diffuse"]["payload"]["uri"] == "../../../textures/1001.dds");
    CHECK(fs::exists(out / "textures" / "1001.dds"));
    CHECK_FALSE(fs::exists(tile / "textures"));
    nlohmann::json doodads = readPlacements(tile / "sets" / "0.json");
    CHECK(doodads[1]["asset"]["id"]["value"] == 4242);
    CHECK_FALSE(doodads[0]["asset"].contains("uri"));
    CHECK_FALSE(fs::exists(out / "maps" / "other"));

    std::string log = readText(out / "errors.log");
    CHECK(log.find("tile\t") != std::string::npos);
    CHECK(log.find("azeroth_33_48.adt") != std::string::npos);

    // Rerun: the finished tile is skipped (its manifest is untouched), the
    // broken one is retried and logged again.
    auto before = fs::last_write_time(tile / "manifest.json");
    auto second = runHusk(cmd);
    REQUIRE(second.exitCode == 0);
    CHECK(fs::last_write_time(tile / "manifest.json") == before);
    std::string log2 = readText(out / "errors.log");
    size_t hits = 0;
    for (size_t at = log2.find("azeroth_33_48.adt"); at != std::string::npos; at = log2.find("azeroth_33_48.adt", at + 1)) {
        ++hits;
    }
    CHECK(hits == 2);
}

TEST_CASE("husk export-world: a broken sidecar is logged and its tile still exports without it") {
    Corpus c = makeCorpus("world-broken-obj");
    writeBytes(c.mapDir() / "azeroth_32_48_obj0.adt", {9, 9});
    fs::path out = c.root / "scene";
    auto r = runHusk("export-world " + q(c.root / "world" / "maps") + " " + q(out) + " " + c.listfileArgs() +
                     " --skip-models");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    nlohmann::json t = readTile(out / "maps" / "azeroth" / "azeroth_32_48.bundle" / "manifest.json");
    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 0);
    CHECK(t["definition"]["placement_sets"][1]["instance_count"] == 0);
    CHECK(t["resources"]["terrain"]["textures"].size() == 2);
    std::string log = readText(out / "errors.log");
    CHECK(log.find("obj\t") != std::string::npos);
    CHECK(log.find("expected a readable azeroth_32_48_obj0.adt") != std::string::npos);
}

TEST_CASE("husk export-world: a map whose WDT is unreadable exports its tiles untextured and logs why") {
    Corpus c = makeCorpus("world-nowdt");
    fs::remove(c.mapDir() / "azeroth.wdt");
    fs::path out = c.root / "scene";
    auto r = runHusk("export-world " + q(c.root / "world" / "maps") + " " + q(out) + " " + c.listfileArgs() +
                     " --skip-models");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    nlohmann::json t = readTile(out / "maps" / "azeroth" / "azeroth_32_48.bundle" / "manifest.json");
    CHECK(t["resources"]["terrain"]["textures"].empty());
    CHECK(t["definition"]["placement_sets"][0]["instance_count"] == 2);
    CHECK(t["definition"]["placement_sets"][1]["instance_count"] == 1);
    CHECK(readText(out / "errors.log").find("tiles export untextured") != std::string::npos);
}

TEST_CASE("husk export-world requires --listfile and --listfile-root") {
    auto r = runHusk("export-world /nonexistent /tmp/husk-unused");
    CHECK(r.exitCode != 0);
    CHECK(r.output.find("--listfile") != std::string::npos);
}

TEST_CASE("husk export-map: a map's tiles and _fogs.wdt become a world document; a map with no tiles fails") {
    Corpus c = makeCorpus("map-world");
    writeBytes(c.mapDir() / "azeroth_fogs.wdt", fogsFile());
    fs::path out = c.root / "azeroth.world.bundle";
    auto r = runHusk("export-map " + q(c.mapDir()) + " " + q(out) + " --listfile none");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("1 tiles, 1 off-grid placement sets") != std::string::npos);
    CHECK(r.output.find("warning: 1 instances of placeholder effect classic_volumetric_fog") != std::string::npos);
    nlohmann::json m = readTile(out / "manifest.json");
    CHECK(m["kind"] == "world");
    CHECK(m["subject"]["name"] == "azeroth");
    REQUIRE(m["grid"]["cells"].size() == 1);
    const nlohmann::json& cell = m["grid"]["cells"][0];
    CHECK(cell["i"] == 32);
    CHECK(cell["j"] == 48);
    // The tile's footprint is the grid cell: origin + (i * axis_i + j * axis_j) * cell_size, one cell below it.
    double size = m["grid"]["cell_size"].get<double>();
    CHECK(cell["bounds_max"][0].get<double>() == doctest::Approx(m["grid"]["origin"][0].get<double>() - 48 * size));
    CHECK(cell["bounds_min"][1].get<double>() == doctest::Approx(m["grid"]["origin"][1].get<double>() - 33 * size));
    const nlohmann::json& tile = m["references"][cell["tile"].get<size_t>()];
    CHECK(tile["asset"]["name"] == "azeroth_32_48");
    CHECK_FALSE(tile["asset"].contains("uri"));
    // The map's fog is an effect instance in the world's "fogs" set.
    REQUIRE(m["placements"].size() == 1);
    CHECK(m["references"][m["placements"][0].get<size_t>()]["asset"]["name"] == "fogs");
    nlohmann::json fogs = readTile(out / "sets" / "0.json");
    CHECK(fogs["instances"][0]["id"] == 1659);
    bool modeCarried = false;
    for (const auto& f : fogs["instances"][0]["unresolved"]["fields"]) {
        if (f["field"] == "VFE2+0x3C") modeCarried = f["value"] == 2.0;
    }
    CHECK(modeCarried);

    writeBytes(c.root / "world" / "maps" / "empty" / "empty.wdt", wdtFile(WdtSpec{.tiles = {}}));
    auto none = runHusk("export-map " + q(c.root / "world" / "maps" / "empty") + " " + q(c.root / "none.bundle") +
                        " --listfile none");
    CHECK(none.exitCode == 1);
    CHECK(none.output.find("expected root tiles empty_<x>_<y>.adt") != std::string::npos);
}

TEST_CASE("husk export-map: a map without terrain places its WDT's one WMO off the grid") {
    Corpus c = makeCorpus("map-global-wmo");
    WdtSpec spec;
    spec.flags = 0x1;
    spec.tiles = {};
    spec.mapObject = MapObjectSpec{109782, 3, {kMapHalfExtent - 100.0f, 25.0f, kMapHalfExtent - 200.0f}, {}, 0x8, 1};
    fs::path mapDir = c.root / "world" / "maps" / "dungeon";
    writeBytes(mapDir / "dungeon.wdt", wdtFile(spec));
    fs::path out = c.root / "dungeon.world.bundle";
    auto r = runHusk("export-map " + q(mapDir) + " " + q(out) + " --listfile none");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("0 tiles, 1 off-grid placement sets") != std::string::npos);
    nlohmann::json m = readTile(out / "manifest.json");
    CHECK(m["grid"]["cells"].empty());
    REQUIRE(m["placements"].size() == 1);
    const nlohmann::json& set = m["references"][m["placements"][0].get<size_t>()];
    CHECK(set["kind"] == "placement_set");
    nlohmann::json instances = readPlacements(out / set["asset"]["uri"].get<std::string>());
    REQUIRE(instances.size() == 1);
    CHECK(instances[0]["asset"]["id"]["value"] == 109782);
    CHECK(instances[0]["translation"][0].get<double>() == doctest::Approx(200.0));
    CHECK(instances[0]["selection"]["placement_sets"] == nlohmann::json::array({1}));

    WdtSpec terrain;
    terrain.tiles = {};
    writeBytes(c.root / "world" / "maps" / "empty" / "empty.wdt", wdtFile(terrain));
    auto none = runHusk("export-map " + q(c.root / "world" / "maps" / "empty") + " " + q(c.root / "none.bundle") +
                        " --listfile none");
    CHECK(none.exitCode == 1);
    CHECK(none.output.find("or a map-wide WMO in empty.wdt, found neither") != std::string::npos);
}

// The store keeps stale tiles the WDT no longer lists (11,014 on the 12.1.0
// mount); the WDT decides which tiles the map has.
TEST_CASE("husk export-map: a root tile the WDT's MAIN doesn't list is left off the grid") {
    Corpus c = makeCorpus("map-main-mismatch");
    writeBytes(c.mapDir() / "azeroth_40_40.adt", rootFile(RootSpec{}));
    auto r = runHusk("export-map " + q(c.mapDir()) + " " + q(c.root / "w.bundle") + " --listfile none");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("1 root tiles the WDT's MAIN doesn't list, skipped") != std::string::npos);
    nlohmann::json m = readTile(c.root / "w.bundle" / "manifest.json");
    REQUIRE(m["grid"]["cells"].size() == 1);
    CHECK(m["grid"]["cells"][0]["i"] == 32);
}

TEST_CASE("husk export-world: a root tile the WDT's MAIN doesn't list isn't exported") {
    Corpus c = makeCorpus("world-main-mismatch");
    writeBytes(c.mapDir() / "azeroth_40_40.adt", rootFile(RootSpec{}));
    fs::path out = c.root / "out";
    auto r = runHusk("export-world " + q(c.root / "world" / "maps") + " " + q(out) + " " + c.listfileArgs() +
                     " --skip-models");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("1 tiles (1 more their WDT doesn't list, skipped)") != std::string::npos);
    CHECK(fs::exists(out / "maps" / "azeroth" / "azeroth_32_48.bundle" / "manifest.json"));
    CHECK_FALSE(fs::exists(out / "maps" / "azeroth" / "azeroth_40_40.bundle"));
}

TEST_CASE("husk export-world: every map gets a world document linking its exported tiles") {
    Corpus c = makeCorpus("world-scene");
    writeBytes(c.mapDir() / "azeroth_fogs.wdt", fogsFile());
    writeBytes(c.root / "world" / "maps" / "other" / "other.wdt", wdtFile(0x4));
    writeBytes(c.root / "world" / "maps" / "other" / "other_32_48.adt", rootFile(RootSpec{}));
    writeBytes(c.root / "world" / "maps" / "other" / "other_fogs.wdt", mverChunk());
    fs::path out = c.root / "scene";
    auto r = runHusk("export-world " + q(c.root / "world" / "maps") + " " + q(out) + " " + c.listfileArgs() +
                     " --skip-models");
    INFO(r.output);
    REQUIRE(r.exitCode == 0);
    CHECK(r.output.find("2 worlds") != std::string::npos);
    nlohmann::json m = readTile(out / "maps" / "azeroth" / "azeroth.world.bundle" / "manifest.json");
    CHECK(m["placements"].size() == 1);
    CHECK(m["references"][0]["asset"]["uri"] == "../azeroth_32_48.bundle/manifest.json");
    nlohmann::json other = readTile(out / "maps" / "other" / "other.world.bundle" / "manifest.json");
    CHECK(other["placements"].empty());
    CHECK(other["grid"]["cells"].size() == 1);
}
