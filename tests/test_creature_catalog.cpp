// The creature catalog (REFACTOR/BUNDLE_FORMAT.md "Creature catalogs"):
// assembly from synthetic rows, the written document against
// schema/creatures.schema.json, the index reader's boundary, and the real
// 12.1.0 tables on the casc-tool mount (skipped without it).

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <json.hpp>
#include <sstream>
#include <string>

#include "bundle_schema.hpp"
#include "corpus_index.hpp"
#include "creature_catalog_input.hpp"
#include "creature_db2.hpp"
#include "run_husk.hpp"
#include "test_data_paths.hpp"
#include "uuid.hpp"
#include "writers/creatures_writer.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/builds/12.1.0.69933/dbfilesclient";

bool haveRealCreatureTables() {
    return fs::exists(fs::path(kMountDb2Dir) / "creature.db2") && !test::testDbdDir().empty();
}

fs::path freshDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("husk-creatures-test-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string problemsOf(const fs::path& file) {
    std::string report;
    for (const bundle_schema::Problem& p : bundle_schema::validateDocument(file)) report += p.where + ": " + p.message + "\n";
    return report;
}

// Two creatures sharing display 100 (once twice in one creature's slots),
// a display whose model data row is missing (101), one the display table
// lacks (200), an unknown type row and no family.
creaturedb::Data syntheticRows() {
    creaturedb::Data data;
    data.types = {{1, "Beast"}};
    data.families = {{1, "Wolf"}};
    data.creatures.push_back({3, "Mystery", "Guard", 99, 0, 1, {200, 101, 0, 0}, {1, 1, 0, 0}});
    data.creatures.push_back({10, "Wolf", "", 1, 1, 0, {100, 0, 100, 0}, {1, 0, 0.5f, 0}});
    data.displays = {{100, {7, 1.5f}}, {101, {8, 1}}};
    data.displays[100].sizeClass = 2;
    data.displays[100].stateSpellVisualKit = 555;
    data.displays[100].dissolveEffect = 9;
    data.models = {{7, {1234, 0.8f}}};
    return data;
}

}  // namespace

TEST_CASE("creature catalog: shared displays, stable ids, and rows the tables lack keep their identity") {
    canon::CreatureCatalog catalog = creaturecatalog::assemble(syntheticRows());
    REQUIRE(catalog.displays.size() == 3);
    CHECK(std::get<canon::Db2Row>(catalog.displays[0].ref.id).row == 100);
    CHECK(catalog.displays[0].id == nameUuid("creature_display/CreatureDisplayInfo/100"));
    CHECK(catalog.displays[0].model == 1234u);
    CHECK(catalog.displays[0].scale == 1.5f);
    CHECK(catalog.displays[0].baseScale == 0.8f);
    CHECK_FALSE(catalog.displays[1].model);
    CHECK(catalog.displays[1].scale == 1.0f);
    CHECK_FALSE(catalog.displays[1].baseScale);
    CHECK_FALSE(catalog.displays[2].scale);
    REQUIRE(catalog.models.size() == 1);
    CHECK(catalog.models[0].id == nameUuid("model/file_data_id/1234"));
    CHECK(std::get<canon::FileDataId>(catalog.models[0].ref.id).value == 1234u);

    REQUIRE(catalog.creatures.size() == 2);
    const canon::Creature& mystery = catalog.creatures[0];
    CHECK(mystery.id == nameUuid("creature/Creature/3"));
    CHECK(mystery.title == "Guard");
    CHECK(mystery.classification == 1);
    REQUIRE(mystery.type);
    CHECK(std::get<canon::Db2Row>(mystery.type->id).row == 99);
    CHECK(mystery.type->name.empty());
    CHECK_FALSE(mystery.family);
    const canon::Creature& wolf = catalog.creatures[1];
    CHECK(wolf.type->name == "Beast");
    CHECK(wolf.family->name == "Wolf");
    REQUIRE(wolf.displays.size() == 2);
    CHECK(wolf.displays[0].display == 0);
    CHECK(wolf.displays[1].display == 0);
    CHECK(wolf.displays[1].weight == 0.5f);

    catalog.displays[0].preset = 2;
    catalog.models[0].ref.name = "wolf";
    catalog.models[0].ref.source = canon::NameSource::Listfile;
    fs::path file = freshDir("document") / "creatures.json";
    writeText(file.parent_path() / "models/1234.canon.bundle/manifest.json", "{}");
    writers::Provenance provenance{"husk test", "db2", {}};
    writeText(file, writers::creatureCatalogDocument(catalog, provenance, {{1234, "models/1234.canon.bundle/manifest.json"}}));
    std::string problems = problemsOf(file);
    INFO(problems);
    CHECK(problems.empty());

    nlohmann::json doc = nlohmann::json::parse(readText(file));
    REQUIRE(doc["references"].size() == 1);
    CHECK(doc["references"][0]["asset"]["uri"] == "models/1234.canon.bundle/manifest.json");
    CHECK(doc["references"][0]["id"] == nameUuid("model/file_data_id/1234"));
    CHECK(doc["references"][0]["asset"]["name"] == "wolf");
    CHECK(doc["references"][0]["asset"]["name_source"] == "listfile");
    CHECK(doc["displays"][0]["model"] == doc["references"][0]["id"]);
    CHECK(doc["creatures"][0]["classification"] == 1);
    CHECK(doc["displays"][0]["preset"] == 2);
    CHECK(doc["displays"][0]["size_class"] == 2);
    CHECK(doc["displays"][0]["state_spell_visual_kit"]["id"]["table"] == "SpellVisualKit");
    CHECK(doc["displays"][0]["state_spell_visual_kit"]["id"]["row"] == 555);
    CHECK(doc["displays"][0]["dissolve_effect"]["id"]["table"] == "DissolveEffect");
    CHECK_FALSE(doc["displays"][0].contains("object_effect_package"));
    CHECK(doc["displays"][1]["size_class"] == 0);
    CHECK_FALSE(doc["displays"][1].contains("dissolve_effect"));
    // A display the client's table lacks has no row values at all.
    CHECK_FALSE(doc["displays"][2].contains("size_class"));
    CHECK_FALSE(doc["displays"][1].contains("model"));
    CHECK(doc["creatures"][1]["displays"][0]["display"] == doc["displays"][0]["id"]);
    CHECK(doc["creatures"][1]["family"]["name"] == "Wolf");
    CHECK_FALSE(doc["creatures"][0].contains("family"));
}

TEST_CASE("creature catalog: validation rejects an unknown display id, an unknown model id, and a non-model reference") {
    canon::CreatureCatalog catalog = creaturecatalog::assemble(syntheticRows());
    writers::Provenance provenance{"husk test", "db2", {}};
    nlohmann::json doc = nlohmann::json::parse(writers::creatureCatalogDocument(catalog, provenance, {}));
    doc["creatures"][0]["displays"][0]["display"] = nameUuid("creature_display/CreatureDisplayInfo/999");
    doc["displays"][0]["model"] = nameUuid("model/file_data_id/999");
    fs::path file = freshDir("invalid") / "creatures.json";
    writeText(file, doc.dump());
    std::string problems = problemsOf(file);
    INFO(problems);
    CHECK(problems.find("/creatures/0/displays/0/display") != std::string::npos);
    CHECK(problems.find("/displays/0/model") != std::string::npos);

    nlohmann::json world = nlohmann::json::parse(writers::creatureCatalogDocument(catalog, provenance, {}));
    world["references"][0]["kind"] = "world";
    writeText(file, world.dump());
    std::string kindProblems = problemsOf(file);
    INFO(kindProblems);
    CHECK(kindProblems.find("/references/0/kind") != std::string::npos);
}

TEST_CASE("readCreatureDisplays: index rows, and what isn't one, with the line") {
    fs::path tree = freshDir("index");
    CHECK_THROWS_WITH_AS(corpus::readCreatureDisplays(tree), doctest::Contains("run husk index-tree first"), std::runtime_error);
    writeText(tree / "index" / "creature_displays.tsv",
              "display\tmodel\tpreset\n7\tmodels/5.canon.bundle/manifest.json\t1\n");
    std::vector<corpus::CreatureDisplayRow> rows = corpus::readCreatureDisplays(tree);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].display == 7);
    CHECK(rows[0].model == "models/5.canon.bundle/manifest.json");
    CHECK(rows[0].preset == 1);
    writeText(tree / "index" / "creature_displays.tsv", "display\tmodel\tpreset\n7\tmodels/5.canon.bundle/manifest.json\tx\n");
    CHECK_THROWS_WITH_AS(corpus::readCreatureDisplays(tree),
                         doctest::Contains(":2: expected an unsigned 32-bit number, got 'x'"), std::runtime_error);
    writeText(tree / "index" / "creature_displays.tsv", "display\tmodel\n");
    CHECK_THROWS_WITH_AS(corpus::readCreatureDisplays(tree), doctest::Contains(":1: expected the header"),
                         std::runtime_error);
}

TEST_CASE("creaturedb::load: real 12.1.0 tables -- a wolf, a boar, a dragon" * doctest::skip(!haveRealCreatureTables())) {
    std::ostringstream err;
    std::optional<creaturedb::Data> data = creaturedb::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    CHECK(data->types.at(1) == "Beast");
    CHECK(data->types.at(2) == "Dragonkin");
    CHECK(data->families.at(1) == "Wolf");
    CHECK(data->families.at(5) == "Boar");
    auto creature = [&](uint32_t id) -> const creaturedb::Creature& {
        auto it = std::find_if(data->creatures.begin(), data->creatures.end(), [&](const creaturedb::Creature& c) { return c.id == id; });
        REQUIRE(it != data->creatures.end());
        return *it;
    };
    const creaturedb::Creature& battlewolf = creature(71116);
    CHECK(battlewolf.name == "Kor'kron Battlewolf");
    CHECK(battlewolf.type == 1);
    CHECK(battlewolf.family == 1);
    CHECK(battlewolf.displays[0] == 17408);
    CHECK(battlewolf.displays[1] == 247);
    const creaturedb::Creature& boar = creature(83094);
    CHECK(boar.name == "Boar (Brown)");
    CHECK(boar.type == 1);
    const creaturedb::Creature& deathwing = creature(46471);
    CHECK(deathwing.name == "Deathwing");
    CHECK(deathwing.type == 2);
    CHECK(deathwing.weights[0] == 1.0f);
    CHECK(deathwing.classification == 1);
    CHECK(data->models.count(data->displays.at(17408).modelData) == 1);
}

TEST_CASE("husk export-creatures: real 12.1.0 tables into a tree's creatures.json" * doctest::skip(!haveRealCreatureTables())) {
    std::ostringstream err;
    std::optional<creaturedb::Data> data = creaturedb::load(kMountDb2Dir, test::testDbdDir(), err);
    REQUIRE(data.has_value());
    uint32_t wolfModel = data->models.at(data->displays.at(17408).modelData).fileDataId;
    std::string manifest = "models/" + std::to_string(wolfModel) + ".canon.bundle/manifest.json";

    fs::path tree = freshDir("cli");
    writeText(tree / manifest, "{}");
    writeText(tree / "index" / "creature_displays.tsv", "display\tmodel\tpreset\n17408\t" + manifest + "\t3\n");
    std::string flags = " --db2-dir " + std::string(kMountDb2Dir) + " --dbd-dir " + test::testDbdDir() + " --listfile none";
    test::RunResult run = test::runHusk("export-creatures " + tree.string() + flags);
    INFO(run.output);
    REQUIRE(run.exitCode == 0);
    std::string problems = problemsOf(tree / "creatures.json");
    INFO(problems);
    CHECK(problems.empty());

    nlohmann::json doc = nlohmann::json::parse(readText(tree / "creatures.json"));
    const nlohmann::json* battlewolf = nullptr;
    for (const nlohmann::json& c : doc["creatures"]) {
        if (c["id"] == nameUuid("creature/Creature/71116")) battlewolf = &c;
    }
    REQUIRE(battlewolf);
    CHECK((*battlewolf)["type"]["name"] == "Beast");
    CHECK((*battlewolf)["family"]["name"] == "Wolf");
    std::string displayId = (*battlewolf)["displays"][0]["display"];
    const nlohmann::json* display = nullptr;
    for (const nlohmann::json& d : doc["displays"]) {
        if (d["id"] == displayId) display = &d;
    }
    REQUIRE(display);
    CHECK((*display)["preset"] == 3);
    const nlohmann::json* model = nullptr;
    for (const nlohmann::json& r : doc["references"]) {
        if (r["id"] == (*display)["model"]) model = &r;
    }
    REQUIRE(model);
    CHECK((*model)["asset"]["uri"] == manifest);
    CHECK_FALSE((*model)["asset"].contains("name"));

    writeText(tree / "index" / "creature_displays.tsv", "display\tmodel\tpreset\n17408\tmodels/1.canon.bundle/manifest.json\t0\n");
    test::RunResult wrong = test::runHusk("export-creatures " + tree.string() + flags);
    CHECK(wrong.exitCode == 1);
    CHECK(wrong.output.find("display 17408: expected " + manifest + ", got models/1.canon.bundle/manifest.json") !=
          std::string::npos);
}
