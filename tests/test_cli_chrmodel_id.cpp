// CLI tier: `husk export --chr-model-id` (auto|none|<id>) -- which
// ChrModel's customization choices fill the character-texture tier (an
// explicit --customization-choice-ids, else the lowest-OrderIndex choice
// per option), derived from the model's FileDataID (CreatureModelData ->
// CreatureDisplayInfo -> ChrModel) or, without one, its <race><sex>
// filename; and the customization catalog the bundle carries for the
// ChrModel drawing the file. Synthetic M2/.skin/.skel plus DB2/WoWDBDefs
// fixtures, real compiled binary (run_husk.hpp).

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_db2_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;

namespace {

constexpr uint32_t kLayout = 42;
constexpr uint32_t kTarget = 7;

// A skin-textured (type 1), rigged model named `stem`, plus a PNG for every
// FileDataID in `textures` beside it.
fs::path writeCharacterModel(const fs::path& dir, const std::string& stem, const std::vector<uint32_t>& textures) {
    writeFile(dir / (stem + ".m2"), oneTexturedModelWithType(1));
    writeFile(dir / (stem + "00.skin"), oneTexturedModelSkin());
    writeFile(dir / (stem + ".skel"), boneCorrectionSkel());
    for (uint32_t fdid : textures) writeFile(dir / (std::to_string(fdid) + ".png"), solidColorPng(4, 4, 10, 20, 30));
    return dir / (stem + ".m2");
}

// Layout kLayout draws the skin (type 1) with one layer, from texture
// target kTarget; every choice in `choiceTextures` ({choice id, FileDataID})
// puts its texture on that target.
void addSkinLayout(db2fx::CharacterTables& rows, const std::vector<std::pair<uint32_t, uint32_t>>& choiceTextures) {
    rows.chrModelMaterial = {{1, kLayout, 1, 4, 4, 0}};
    rows.layouts = {{kLayout, 4, 4}};
    rows.layers = {{1, kLayout, 1, 0, 0, 0, 0xFFFFFFFFu, kTarget}};
    uint32_t id = 100;
    for (auto [choice, fdid] : choiceTextures) {
        rows.elements.push_back({id, choice, 0, 0, id, 0});
        rows.materials.push_back({id, kTarget, id + 1000});
        rows.textureFileData.push_back({fdid, id + 1000, 0});
        ++id;
    }
}

// The race-token chain for "dracthyr<sex>": race 52, each {sex, ChrModelID}.
void addDracthyr(db2fx::CharacterTables& rows, const std::vector<std::pair<uint32_t, uint32_t>>& models) {
    rows.chrRaces = {{52, "Dracthyr"}};
    uint32_t id = 1;
    for (auto [sex, chrModel] : models) rows.chrRaceXChrModel.push_back({id++, 52, sex, chrModel});
}

struct Exported {
    husk::test::RunResult result;
    nlohmann::json manifest;
};

Exported exportModel(const fs::path& model, const db2fx::Tables& tables, const std::string& extra) {
    auto bundle = model.parent_path() / "out.bundle";
    auto result = runHusk("export " + model.string() + " -o " + bundle.string() + tables.flags() +
                          " --explain-textures" + extra);
    INFO(result.output);
    REQUIRE(result.exitCode == 0);
    return {result, parseManifest(bundle)};
}

bool skinFilledBy(const std::string& output, uint32_t fdid) {
    return output.find("type=1 -> db2-character HIT") != std::string::npos &&
           output.find("names FileDataID " + std::to_string(fdid) + " for ChrModelTextureTargetID " +
                       std::to_string(kTarget)) != std::string::npos;
}

}  // namespace

TEST_CASE("husk export --chr-model-id <id>: the lowest-OrderIndex choice per option fills the skin slot (not "
          "the lowest ID, not declaration order), and the catalog lists every option and choice") {
    auto dir = defaultsDir("chrmodelidauto");
    fs::path model = writeCharacterModel(dir, "chrmodelidauto", {881, 882});
    std::ofstream(dir / "listfile.csv") << "9300;chrmodelidauto.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, 9300, 42, kLayout);
    rows.options = {{1, 42, 0, 0, "Face"}, {2, 42, 1, 0, "Hair"}};
    rows.choices = {{10, 1, 1, "Long"}, {11, 1, 0, "Short"}, {20, 2, 0, "Braid"}};
    addSkinLayout(rows, {{10, 881}, {11, 882}});
    rows.elements.push_back({1, 11, 6, 0, 0, 0});
    rows.elements.push_back({2, 20, 7, 0, 0, 0});
    rows.geosets = {{6, 2, 5}, {7, 3, 1}};  // 205, 301
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id 42 --listfile " + (dir / "listfile.csv").string());
    INFO(result.output);
    CHECK(skinFilledBy(result.output, 882));

    nlohmann::json options = m["definition"]["customization"]["options"];
    INFO(options.dump());
    REQUIRE(options.size() == 2);
    CHECK(options[0]["ref"]["name"] == "Face");
    CHECK(options[0]["chr_model"]["id"]["row"] == 42);
    REQUIRE(options[0]["choices"].size() == 2);
    CHECK(options[0]["choices"][0]["ref"]["name"] == "Short");
    CHECK(options[0]["choices"][0]["geoset_id"] == 205);
    CHECK(options[0]["choices"][1]["ref"]["name"] == "Long");
    CHECK(options[1]["ref"]["name"] == "Hair");
    REQUIRE(options[1]["choices"].size() == 1);
    CHECK(options[1]["choices"][0]["ref"]["name"] == "Braid");
    CHECK(options[1]["choices"][0]["geoset_id"] == 301);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id: an explicit --customization-choice-ids wins over the default choice") {
    auto dir = defaultsDir("chrmodelidoverride");
    fs::path model = writeCharacterModel(dir, "chrmodelidoverride", {881, 882});
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    rows.chrModel = {{42, 0, kLayout}};
    rows.options = {{1, 42, 0, 0, "Face"}};
    rows.choices = {{10, 1, 0, "Default"}, {11, 1, 1, "Other"}};
    addSkinLayout(rows, {{10, 881}, {11, 882}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --customization-choice-ids 11 --chr-model-id 42");
    CHECK(skinFilledBy(result.output, 882));
    CHECK_FALSE(skinFilledBy(result.output, 881));

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id: no ChrCustomizationOption/Choice rows for this model gives no "
          "catalog and no character texture, and doesn't fail the export") {
    auto dir = defaultsDir("chrmodelidempty");
    fs::path model = writeCharacterModel(dir, "chrmodelidempty", {881});
    std::ofstream(dir / "listfile.csv") << "9301;chrmodelidempty.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, 9301, 42, kLayout);
    rows.options = {{1, 77, 0, 0, "Another model's"}};
    rows.choices = {{10, 1, 0, "Default"}};
    addSkinLayout(rows, {{10, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id 42 --listfile " + (dir / "listfile.csv").string());
    INFO(m["definition"].dump());
    CHECK_FALSE(skinFilledBy(result.output, 881));
    CHECK((!m["definition"].contains("customization") || m["definition"]["customization"]["options"].empty()));

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: without a FileDataID, derives the ChrModelID from the "
          "<race><sex> filename and fills the skin slot from its default choice") {
    auto dir = defaultsDir("chrmodelidautoderive");
    fs::path model = writeCharacterModel(dir, "dracthyrfemale", {881});
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    addDracthyr(rows, {{1, 89}});
    rows.chrModel = {{89, 0, kLayout}};
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id auto");
    CHECK(result.output.find("ChrModelID 89 from the filename (race 'dracthyr', sex 1)") != std::string::npos);
    CHECK(skinFilledBy(result.output, 881));

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: an ambiguous race+sex match (an alternate form: several "
          "ChrModelIDs) is reported and skipped, never guessed") {
    auto dir = defaultsDir("chrmodelidautoambig");
    fs::path model = writeCharacterModel(dir, "dracthyrmale", {881});
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    // Dragon form 89 and Visage form 127, the real ChrRaceXChrModel shape.
    addDracthyr(rows, {{0, 89}, {0, 127}});
    rows.chrModel = {{89, 0, kLayout}, {127, 0, kLayout}};
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id auto");
    CHECK(result.output.find("resolves to 2 distinct real ChrModelIDs (89, 127)") != std::string::npos);
    CHECK(result.output.find("not guessing") != std::string::npos);
    CHECK(result.output.find("ChrModelID 89 from") == std::string::npos);
    CHECK(result.output.find("ChrModelID 127 from") == std::string::npos);
    CHECK_FALSE(skinFilledBy(result.output, 881));

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: a filename that doesn't fit the <race><sex> convention is "
          "skipped, not force-matched") {
    auto dir = defaultsDir("chrmodelidautonomatch");
    fs::path model = writeCharacterModel(dir, "dwagonbiddies69", {881});
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    addDracthyr(rows, {{0, 89}});
    rows.chrModel = {{89, 0, kLayout}};
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id auto");
    CHECK(result.output.find("ChrModelID 89 from") == std::string::npos);
    CHECK_FALSE(skinFilledBy(result.output, 881));

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id auto: the FileDataID chain (via --listfile) comes first and settles a "
          "race+sex pair the filename alone leaves ambiguous -- the real Dracthyr case; the catalog is that "
          "ChrModel's") {
    auto dir = defaultsDir("chrmodelidautofdid");
    fs::path model = writeCharacterModel(dir, "dracthyrmale", {881});
    std::ofstream(dir / "listfile.csv") << "9001;dracthyrmale.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    addDracthyr(rows, {{0, 89}, {0, 127}});
    // Only the Visage form (127) draws FileDataID 9001.
    db2fx::drawModelAs(rows, 9001, 127, kLayout);
    rows.chrModel.push_back({89, 0, kLayout});
    rows.options = {{1, 127, 0, 0, "Ears"}, {2, 89, 0, 0, "Horns"}};
    rows.choices = {{11, 1, 0, "Short Fin"}, {21, 2, 0, "Curled"}};
    rows.elements = {{1, 11, 6, 0, 0, 0}};
    rows.geosets = {{6, 2, 5}};  // 205
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --listfile " + (dir / "listfile.csv").string() +
                                                      " --listfile-root " + dir.string() + " --chr-model-id auto");
    CHECK(result.output.find("ChrModelID 127 from FileDataID 9001") != std::string::npos);
    CHECK(result.output.find("genuinely ambiguous") == std::string::npos);
    CHECK(result.output.find("from the filename") == std::string::npos);
    CHECK(skinFilledBy(result.output, 881));

    nlohmann::json options = m["definition"]["customization"]["options"];
    INFO(options.dump());
    REQUIRE(options.size() == 1);
    CHECK(options[0]["chr_model"]["id"]["row"] == 127);
    CHECK(options[0]["choices"][0]["ref"]["name"] == "Short Fin");
    CHECK(options[0]["choices"][0]["geoset_id"] == 205);

    fs::remove_all(dir);
}

TEST_CASE("husk export --customization-choice-ids, no --chr-model-id: the catalog is still attached -- it "
          "depends on no flag beyond --db2-dir/--dbd-dir") {
    auto dir = defaultsDir("chrcustchoiceenrich");
    fs::path model = writeCharacterModel(dir, "dracthyrfemale", {881});
    std::ofstream(dir / "listfile.csv") << "9002;dracthyrfemale.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, 9002, 89, kLayout);
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --listfile " + (dir / "listfile.csv").string() +
                                                      " --customization-choice-ids 11");
    CHECK(result.output.find("ChrModelID 89 from FileDataID 9002") != std::string::npos);
    CHECK(skinFilledBy(result.output, 881));
    nlohmann::json options = m["definition"]["customization"]["options"];
    INFO(options.dump());
    REQUIRE(options.size() == 1);
    CHECK(options[0]["ref"]["name"] == "Ears");
    CHECK(options[0]["choices"][0]["ref"]["name"] == "Short Fin");

    fs::remove_all(dir);
}

TEST_CASE("husk export --db2-dir/--dbd-dir alone: 'auto' is --chr-model-id's default -- the ChrModelID is "
          "derived, its default choice fills the skin slot, and the catalog is attached") {
    auto dir = defaultsDir("chrcustdefaultauto");
    fs::path model = writeCharacterModel(dir, "dracthyrfemale", {881});
    std::ofstream(dir / "listfile.csv") << "9003;dracthyrfemale.m2\n";
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    db2fx::drawModelAs(rows, 9003, 89, kLayout);
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    rows.elements = {{1, 11, 6, 0, 0, 0}};
    rows.geosets = {{6, 2, 5}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --listfile " + (dir / "listfile.csv").string());
    CHECK(result.output.find("ChrModelID 89 from FileDataID 9003") != std::string::npos);
    CHECK(skinFilledBy(result.output, 881));
    nlohmann::json options = m["definition"]["customization"]["options"];
    INFO(options.dump());
    REQUIRE(options.size() == 1);
    CHECK(options[0]["choices"][0]["ref"]["name"] == "Short Fin");
    CHECK(options[0]["choices"][0]["geoset_id"] == 205);

    fs::remove_all(dir);
}

TEST_CASE("husk export --chr-model-id none: no ChrModelID derivation and no character texture, even with "
          "--db2-dir/--dbd-dir given") {
    auto dir = defaultsDir("chrmodelidnone");
    fs::path model = writeCharacterModel(dir, "dracthyrfemale", {881});
    db2fx::Tables tables(dir / "db2", dir / "dbd");
    db2fx::CharacterTables rows;
    addDracthyr(rows, {{1, 89}});
    rows.chrModel = {{89, 0, kLayout}};
    rows.options = {{1, 89, 0, 0, "Ears"}};
    rows.choices = {{11, 1, 0, "Short Fin"}};
    addSkinLayout(rows, {{11, 881}});
    db2fx::writeCharacterTables(tables, rows);

    auto [result, m] = exportModel(model, tables, " --chr-model-id none");
    CHECK(result.output.find("ChrModelID 89 from") == std::string::npos);
    CHECK_FALSE(skinFilledBy(result.output, 881));

    fs::remove_all(dir);
}
