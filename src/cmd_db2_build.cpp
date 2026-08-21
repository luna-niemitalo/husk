#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <CLI/CLI.hpp>
#include <sqlite3.h>

#include "cmd_db2_shared.hpp"
#include "commands.hpp"
#include "husk_config.hpp"
#include "listfile.hpp"

// `husk db2-build` -- builds husk's own verified knowledge-base SQLite
// database (TODO/KNOWLEDGE_BASE_DESIGN.md), split out of cmd_db2.cpp per
// FILE_SPLIT_TODO.md's Item 2: a distinct sub-feature (the knowledge-base
// builder) sharing only loadOneFile/writeFileTable/sqliteCheck with
// cmd_db2.cpp's own db2-export/db2-info (see cmd_db2_shared.hpp).
namespace husk::commands {

namespace {

// One entry per resolved table this knowledge base knows how to build --
// TODO/KNOWLEDGE_BASE_DESIGN.md's "general mechanism" decision: adding the
// next resolved table (animations, skins, materials, ...) means adding one
// entry here plus one CREATE+INSERT SELECT below, not a new script.
struct KbSourceTable {
    const char* db2Filename;  // lowercase, matches real casc-tool export names
};
const KbSourceTable kKbSourceTables[] = {
    {"modelfiledata.db2"},
    {"itemdisplayinfo.db2"},
    {"texturefiledata.db2"},
    {"itemdisplayinfomodelmatres.db2"},
    {"item.db2"},
    {"itemappearance.db2"},
    {"itemmodifiedappearance.db2"},
};

// The real InventoryType enum husk cares about here (a small, well-known
// WoW client constant, not reverse-engineered) -- only the slots this
// corpus's item/objectcomponents/ directories actually need.
namespace InventoryType {
constexpr int kHead = 1;
constexpr int kShoulder = 3;
constexpr int kChest = 5;
constexpr int kWaist = 6;
constexpr int kLegs = 7;
constexpr int kFeet = 8;
constexpr int kWrist = 9;
constexpr int kHands = 10;
constexpr int kCloak = 16;
constexpr int kShield = 14;
constexpr int kRobe = 20;
}  // namespace InventoryType

// Expected real equip slot(s) for a model, derived purely from its own
// listfile path -- a cheap, already-available, independent signal used to
// reject an object-skin candidate whose real item is for a completely
// different slot (e.g. a two-handed staff for a model under
// objectcomponents/head/, the real case that exposed this whole
// correctness bug -- TODO/KNOWLEDGE_BASE_DESIGN.md). Returns nullopt for
// any path this table doesn't have a confident rule for (most of
// objectcomponents/weapon/'s many subtypes, anything outside
// objectcomponents/ entirely) -- deliberately conservative: an unknown
// path emits no verified answer at all, rather than a filter that might
// be wrong.
std::optional<std::vector<int>> expectedInventoryTypesForPath(const std::string& lowerPath) {
    static const std::string kPrefix = "item/objectcomponents/";
    if (lowerPath.compare(0, kPrefix.size(), kPrefix) != 0) return std::nullopt;
    std::string rest = lowerPath.substr(kPrefix.size());
    size_t slash = rest.find('/');
    if (slash == std::string::npos) return std::nullopt;
    std::string dir = rest.substr(0, slash);
    std::string basename = std::filesystem::path(rest).stem().string();

    static const std::unordered_map<std::string, std::vector<int>> kDirRules = {
        {"head", {InventoryType::kHead}},
        {"shoulder", {InventoryType::kShoulder}},
        {"waist", {InventoryType::kWaist}},
        {"shield", {InventoryType::kShield}},
        {"cape", {InventoryType::kCloak}},
    };
    if (dir != "collections" && dir != "collection") {
        auto it = kDirRules.find(dir);
        return it != kDirRules.end() ? std::optional(it->second) : std::nullopt;
    }

    // "collections" is a mixed bag (every body-armor slot in one flat
    // directory) -- disambiguated by real filename prefix convention
    // instead (helm_/chest_/shoulder_/... -- the same convention already
    // visible across every real file in this directory).
    static const std::vector<std::pair<std::string, std::vector<int>>> kPrefixRules = {
        {"helm_", {InventoryType::kHead}},
        {"shoulder_", {InventoryType::kShoulder}},
        {"chest_", {InventoryType::kChest, InventoryType::kRobe}},
        {"robe_", {InventoryType::kChest, InventoryType::kRobe}},
        {"waist_", {InventoryType::kWaist}},
        {"belt_", {InventoryType::kWaist}},
        {"leg_", {InventoryType::kLegs}},
        {"pant_", {InventoryType::kLegs}},
        {"boot_", {InventoryType::kFeet}},
        {"foot_", {InventoryType::kFeet}},
        {"wrist_", {InventoryType::kWrist}},
        {"bracer_", {InventoryType::kWrist}},
        {"glove_", {InventoryType::kHands}},
        {"hand_", {InventoryType::kHands}},
        {"cape_", {InventoryType::kCloak}},
        {"cloak_", {InventoryType::kCloak}},
        {"shield_", {InventoryType::kShield}},
    };
    for (const auto& [prefix, types] : kPrefixRules) {
        if (basename.compare(0, prefix.size(), prefix) == 0) return types;
    }
    return std::nullopt;
}

std::string stampSourceFiles(const std::string& db2Dir, const std::vector<KbSourceTable>& tables) {
    std::ostringstream stamp;
    for (const auto& t : tables) {
        std::error_code ec;
        auto p = std::filesystem::path(db2Dir) / t.db2Filename;
        auto size = std::filesystem::file_size(p, ec);
        auto mtime = std::filesystem::last_write_time(p, ec).time_since_epoch().count();
        stamp << t.db2Filename << ":" << size << ":" << mtime << ";";
    }
    return stamp.str();
}

}  // namespace

void addDb2BuildOptions(CLI::App& app, Db2BuildOptions& opts) {
    app.set_config("--config", husk::defaultConfigPath(),
                    "TOML file of default flag values -- explicit CLI flags always override a "
                    "config value")
        ->envname("HUSK_CONFIG");
    app.add_option("--db2-dir", opts.db2Dir, "a real, local, already-extracted DB2 directory")
        ->required();
    app.add_option("--dbd-dir", opts.dbdDir, "a local WoWDBDefs checkout")->required();
    app.add_option("--listfile", opts.listfilePath, "a local community-listfile.csv-style snapshot")
        ->required();
    app.add_option("-o,--output", opts.outputPath, "the output .sqlite path")->required();
}

int db2Build(int argc, char** args) {
    Db2BuildOptions opts;
    CLI::App app{
        "Builds husk's own verified knowledge-base SQLite database from a real, local --db2-dir "
        "plus a --listfile snapshot -- TODO/KNOWLEDGE_BASE_DESIGN.md. Ingests the DB2 tables "
        "today's resolved joins need (ModelFileData/ItemDisplayInfo/TextureFileData) via the "
        "same db2-export machinery, a 'models' table (every .m2 FileDataID -> real path from "
        "--listfile), a 'textures' table (every .blp/.png FileDataID -> real path, same source), "
        "and one resolved join table -- model_object_skin_texture (model FileDataID -> texture "
        "FileDataID via ModelFileData -> ItemDisplayInfo -> TextureFileData). A '_meta' table "
        "stamps the source .db2 files' own size+mtime so a consumer can tell when the knowledge "
        "base is stale relative to --db2-dir's current contents (`husk export "
        "--knowledge-db`). Local-only, rebuilt on demand -- never fetched, never committed.",
        "husk db2-build"};
    addDb2BuildOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    const std::string& db2Dir = opts.db2Dir;
    const std::string& dbdDir = opts.dbdDir;
    const std::string& listfilePath = opts.listfilePath;
    const std::string& outputPath = opts.outputPath;

    std::vector<KbSourceTable> sourceTables(std::begin(kKbSourceTables), std::end(kKbSourceTables));

    std::vector<LoadedFile> loaded;
    for (const auto& t : sourceTables) {
        auto p = (std::filesystem::path(db2Dir) / t.db2Filename).string();
        std::optional<LoadedFile> lf = loadOneFile(p, dbdDir, std::cerr);
        if (!lf) {
            std::cerr << "husk: db2-build: couldn't load required table '" << t.db2Filename << "'\n";
            return 1;
        }
        loaded.push_back(std::move(*lf));
    }
    std::set<std::string> availableTables;
    for (const LoadedFile& lf : loaded) {
        if (lf.dbdNames) availableTables.insert(lf.tableName);
    }

    std::error_code ec;
    std::filesystem::remove(outputPath, ec);

    sqlite3* db = nullptr;
    if (sqlite3_open(outputPath.c_str(), &db) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        std::cerr << "husk: db2-build: couldn't create '" << outputPath << "': " << msg << "\n";
        return 1;
    }

    try {
        sqliteCheck(sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr), db, "BEGIN");

        for (const LoadedFile& lf : loaded) {
            size_t rowCount = writeFileTable(db, lf, availableTables);
            std::cout << "husk: db2-build: ingested " << rowCount << " row(s) into \"" << lf.tableName
                       << "\"\n";
        }

        auto listfile = husk::loadListfile(listfilePath);

        // Shared by 'models'/'textures' below -- both are the same
        // "listfile row -> one extension-filtered flat table" shape, just a
        // different extension set. TODO/KNOWLEDGE_BASE_DESIGN.md's "textures
        // table" step: this is the one place a texture FileDataID -> real
        // path lookup lives now, replacing every consumer's own in-memory
        // --listfile re-parse.
        auto ingestListfileTable = [&](const char* tableName, const std::set<std::string>& extensions) {
            sqliteCheck(sqlite3_exec(db,
                                      (std::string("CREATE TABLE ") + tableName +
                                       "(file_data_id INTEGER PRIMARY KEY, path TEXT)")
                                          .c_str(),
                                      nullptr, nullptr, nullptr),
                        db, std::string("CREATE TABLE ") + tableName);
            sqlite3_stmt* stmt = nullptr;
            sqliteCheck(sqlite3_prepare_v2(db, (std::string("INSERT INTO ") + tableName + " VALUES (?, ?)").c_str(),
                                            -1, &stmt, nullptr),
                        db, std::string("prepare ") + tableName + " insert");
            size_t count = 0;
            for (const auto& [fid, path] : listfile) {
                std::string ext = std::filesystem::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (!extensions.count(ext)) continue;
                sqlite3_bind_int64(stmt, 1, fid);
                sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
                sqliteCheck(sqlite3_step(stmt), db, std::string("insert ") + tableName + " row");
                sqlite3_reset(stmt);
                ++count;
            }
            sqlite3_finalize(stmt);
            std::cout << "husk: db2-build: ingested " << count << " " << tableName << " path(s) from '"
                       << listfilePath << "'\n";
        };
        ingestListfileTable("models", {".m2"});
        ingestListfileTable("textures", {".blp", ".png"});

        // The join algorithm matches reference/wow.export's own working
        // source (DBItemDisplays.js/DBItemDisplayInfoModelMatRes.js) --
        // that's "matches trusted reference code," not yet "confirmed
        // correct against a real rendered/visually-checked file," see
        // TODO/KNOWLEDGE_BASE_DESIGN.md for the open verification status.
        // NOT ItemDisplayInfo.ModelMaterialResourcesID_0/1 directly (an
        // earlier version of this used that column as the texture source
        // and shipped real wrong-texture data: checked live against two
        // real files by cross-referencing the resolved FileDataID's own
        // listfile name, both resolved to a completely unrelated item's
        // texture -- ModelMaterialResourcesID_0/1 turned out to only be a
        // wow.export existence check, discarded, never dereferenced for
        // the actual texture). Only ModelResourcesID_0 is used for the
        // reverse model -> ItemDisplayInfo lookup (matching wow.export's
        // own `modelResIDs[0]`, never an OR across both slots). One model
        // can legitimately match several ItemDisplayInfoIDs (recolor/
        // seasonal variants sharing one base mesh -- a real, expected
        // shape of this data, not an error to collapse away) -- every
        // candidate is kept, not just one.
        sqliteCheck(sqlite3_exec(db,
                                  "CREATE TABLE model_object_skin_candidates("
                                  "model_file_data_id INTEGER, item_display_info_id INTEGER, "
                                  "texture_file_data_id INTEGER, "
                                  "PRIMARY KEY(model_file_data_id, item_display_info_id, texture_file_data_id))",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE TABLE model_object_skin_candidates");
        // Without these, the joins below are a nested-loop scan across
        // 131k x 72k x 141k x 214k rows -- minutes, not seconds (measured
        // directly this session before adding indexes the first time).
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_mfd_fid ON ModelFileData(FileDataID)", nullptr,
                                  nullptr, nullptr),
                    db, "CREATE INDEX idx_mfd_fid");
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_idi_mr0 ON ItemDisplayInfo(ModelResourcesID_0)",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE INDEX idx_idi_mr0");
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_idi_id ON ItemDisplayInfo(ID)", nullptr, nullptr,
                                  nullptr),
                    db, "CREATE INDEX idx_idi_id");
        sqliteCheck(sqlite3_exec(db,
                                  "CREATE INDEX idx_idimr_idid ON ItemDisplayInfoModelMatRes(ItemDisplayInfoID)",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE INDEX idx_idimr_idid");
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_tfd_matid ON TextureFileData(MaterialResourcesID)",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE INDEX idx_tfd_matid");
        sqliteCheck(sqlite3_exec(db,
                                  "INSERT OR IGNORE INTO model_object_skin_candidates "
                                  "SELECT mfd.FileDataID, idi.ID, tfd.FileDataID "
                                  "FROM ModelFileData mfd "
                                  "JOIN ItemDisplayInfo idi ON idi.ModelResourcesID_0 = mfd.ModelResourcesID "
                                  "JOIN ItemDisplayInfoModelMatRes idimr ON idimr.ItemDisplayInfoID = idi.ID "
                                  "JOIN TextureFileData tfd ON tfd.MaterialResourcesID = idimr.MaterialResourcesID "
                                  "WHERE idimr.TextureType = 2",
                                  nullptr, nullptr, nullptr),
                    db, "INSERT model_object_skin_candidates");
        {
            int candidateCount = 0, resolvedCount = 0;
            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, "SELECT COUNT(*), COUNT(DISTINCT model_file_data_id) FROM model_object_skin_candidates",
                                -1, &stmt, nullptr);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                candidateCount = sqlite3_column_int(stmt, 0);
                resolvedCount = sqlite3_column_int(stmt, 1);
            }
            sqlite3_finalize(stmt);
            std::cout << "husk: db2-build: resolved " << resolvedCount << " model(s) to " << candidateCount
                       << " raw (unverified) object-skin-texture candidate(s)\n";
        }

        // Real ItemDisplayInfoID -> real equip slot(s), via the actual
        // item(s) that reference it -- the disambiguator
        // TODO/KNOWLEDGE_BASE_DESIGN.md's correctness bug needed:
        // ItemDisplayInfoID 113510 (the real case that exposed the bug)
        // turned out to be *real*, current data for an actual two-handed
        // staff, not orphaned junk -- ModelResourcesID really can be
        // shared across unrelated item types (measured: 194/3700, 5.2%,
        // span more than one real Item.ClassID). The raw candidates table
        // above can't tell those apart on its own; this can.
        sqliteCheck(sqlite3_exec(db,
                                  "CREATE TABLE item_display_inventory_type("
                                  "item_display_info_id INTEGER, inventory_type INTEGER, "
                                  "PRIMARY KEY(item_display_info_id, inventory_type))",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE TABLE item_display_inventory_type");
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_ima_iaid ON ItemModifiedAppearance(ItemAppearanceID)",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE INDEX idx_ima_iaid");
        sqliteCheck(sqlite3_exec(db, "CREATE INDEX idx_item_id ON Item(ID)", nullptr, nullptr, nullptr),
                    db, "CREATE INDEX idx_item_id");
        sqliteCheck(sqlite3_exec(db,
                                  "INSERT OR IGNORE INTO item_display_inventory_type "
                                  "SELECT ia.ItemDisplayInfoID, it.InventoryType "
                                  "FROM ItemAppearance ia "
                                  "JOIN ItemModifiedAppearance ima ON ima.ItemAppearanceID = ia.ID "
                                  "JOIN Item it ON it.ID = ima.ItemID",
                                  nullptr, nullptr, nullptr),
                    db, "INSERT item_display_inventory_type");

        // The real filter pass: reject any candidate whose ItemDisplayInfoID
        // isn't confirmed (via the real item chain above) to actually be
        // for the equip slot this model's own listfile path implies
        // (expectedInventoryTypesForPath). A path this table has no
        // confident rule for, or an ItemDisplayInfoID with no real item
        // data available (TACT-key-gated sections in item.db2/
        // itemappearance.db2/itemmodifiedappearance.db2 -- real, not a
        // bug, see db2::Section::recordsAvailable()), yields no verified
        // row at all -- conservative by design, so `husk export`'s
        // existing local basename-fuzzy-matching fallback (already there
        // for every unresolved slot) is what actually fires instead of a
        // wrong-but-confident answer.
        sqliteCheck(sqlite3_exec(db,
                                  "CREATE TABLE model_object_skin_verified("
                                  "model_file_data_id INTEGER, item_display_info_id INTEGER, "
                                  "texture_file_data_id INTEGER, "
                                  "PRIMARY KEY(model_file_data_id, item_display_info_id, texture_file_data_id))",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE TABLE model_object_skin_verified");
        {
            std::unordered_map<uint32_t, std::string> modelPathByFid;
            {
                sqlite3_stmt* stmt = nullptr;
                sqlite3_prepare_v2(db, "SELECT file_data_id, path FROM models", -1, &stmt, nullptr);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    uint32_t fid = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
                    std::string path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    std::transform(path.begin(), path.end(), path.begin(),
                                    [](unsigned char c) { return std::tolower(c); });
                    modelPathByFid.emplace(fid, std::move(path));
                }
                sqlite3_finalize(stmt);
            }
            std::unordered_map<uint32_t, std::set<int>> invTypesByDisplayId;
            {
                sqlite3_stmt* stmt = nullptr;
                sqlite3_prepare_v2(db, "SELECT item_display_info_id, inventory_type FROM item_display_inventory_type",
                                    -1, &stmt, nullptr);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    uint32_t displayId = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
                    int invType = sqlite3_column_int(stmt, 1);
                    invTypesByDisplayId[displayId].insert(invType);
                }
                sqlite3_finalize(stmt);
            }

            sqlite3_stmt* insVerified = nullptr;
            sqliteCheck(sqlite3_prepare_v2(db, "INSERT OR IGNORE INTO model_object_skin_verified VALUES (?, ?, ?)",
                                            -1, &insVerified, nullptr),
                        db, "prepare model_object_skin_verified insert");
            sqlite3_stmt* candStmt = nullptr;
            sqlite3_prepare_v2(db,
                                "SELECT model_file_data_id, item_display_info_id, texture_file_data_id "
                                "FROM model_object_skin_candidates",
                                -1, &candStmt, nullptr);
            size_t verifiedCount = 0;
            while (sqlite3_step(candStmt) == SQLITE_ROW) {
                uint32_t modelFid = static_cast<uint32_t>(sqlite3_column_int64(candStmt, 0));
                uint32_t displayId = static_cast<uint32_t>(sqlite3_column_int64(candStmt, 1));
                uint32_t texFid = static_cast<uint32_t>(sqlite3_column_int64(candStmt, 2));

                auto pathIt = modelPathByFid.find(modelFid);
                if (pathIt == modelPathByFid.end()) continue;
                auto expected = expectedInventoryTypesForPath(pathIt->second);
                if (!expected) continue;
                auto invIt = invTypesByDisplayId.find(displayId);
                if (invIt == invTypesByDisplayId.end()) continue;
                bool matches = std::any_of(expected->begin(), expected->end(),
                                            [&](int t) { return invIt->second.count(t) > 0; });
                if (!matches) continue;

                sqlite3_bind_int64(insVerified, 1, modelFid);
                sqlite3_bind_int64(insVerified, 2, displayId);
                sqlite3_bind_int64(insVerified, 3, texFid);
                sqliteCheck(sqlite3_step(insVerified), db, "insert model_object_skin_verified row");
                sqlite3_reset(insVerified);
                ++verifiedCount;
            }
            sqlite3_finalize(candStmt);
            sqlite3_finalize(insVerified);
            std::cout << "husk: db2-build: verified " << verifiedCount
                       << " object-skin-texture candidate(s) against real Item.InventoryType data\n";
        }

        // One arbitrary (lowest ItemDisplayInfoID, deterministic) default
        // per model for callers that just want *a* texture, sourced only
        // from the verified (slot-confirmed) set -- the same "one wins,
        // the rest become extras/alternates" convention
        // export_materials.cpp's own ambiguous-fuzzy-match handling
        // already established (gltf::Material::alternateTextureCandidates,
        // not yet wired to these DB2-sourced alternates -- future work).
        sqliteCheck(sqlite3_exec(db,
                                  "CREATE VIEW model_object_skin_texture AS "
                                  "SELECT model_file_data_id, texture_file_data_id FROM model_object_skin_verified v "
                                  "WHERE item_display_info_id = (SELECT MIN(item_display_info_id) "
                                  "  FROM model_object_skin_verified v2 WHERE v2.model_file_data_id = v.model_file_data_id)",
                                  nullptr, nullptr, nullptr),
                    db, "CREATE VIEW model_object_skin_texture");
        int resolvedCount = 0;
        {
            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, "SELECT COUNT(DISTINCT model_file_data_id) FROM model_object_skin_verified", -1,
                                &stmt, nullptr);
            if (sqlite3_step(stmt) == SQLITE_ROW) resolvedCount = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        std::cout << "husk: db2-build: " << resolvedCount
                   << " model(s) have a slot-verified object-skin-texture answer\n";

        sqliteCheck(sqlite3_exec(db, "CREATE TABLE _meta(key TEXT PRIMARY KEY, value TEXT)", nullptr,
                                  nullptr, nullptr),
                    db, "CREATE TABLE _meta");
        sqlite3_stmt* insMeta = nullptr;
        sqliteCheck(sqlite3_prepare_v2(db, "INSERT INTO _meta VALUES (?, ?)", -1, &insMeta, nullptr), db,
                    "prepare _meta insert");
        auto putMeta = [&](const char* key, const std::string& value) {
            sqlite3_bind_text(insMeta, 1, key, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insMeta, 2, value.c_str(), -1, SQLITE_TRANSIENT);
            sqliteCheck(sqlite3_step(insMeta), db, "insert _meta row");
            sqlite3_reset(insMeta);
        };
        putMeta("db2_dir", db2Dir);
        putMeta("db2_dir_stamp", stampSourceFiles(db2Dir, sourceTables));
        putMeta("built_at", std::to_string(std::time(nullptr)));
        sqlite3_finalize(insMeta);

        sqliteCheck(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db, "COMMIT");
    } catch (const std::exception& e) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        std::cerr << "husk: db2-build: " << e.what() << "\n";
        return 1;
    }

    sqlite3_close(db);
    std::cout << "husk: db2-build: wrote '" << outputPath << "'\n";
    return 0;
}

}  // namespace husk::commands
