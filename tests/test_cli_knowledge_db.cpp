// CLI tier: `husk export --knowledge-db` -- exercises the real compiled
// binary (see run_husk.hpp) against a minimal synthetic knowledge-base
// SQLite file, built directly with the sqlite3 C API against exactly the
// three raw tables/columns src/cmd_export.cpp's own
// resolveObjectSkinTextureFromKb queries (models/model_object_skin_texture/
// textures) -- not `husk db2-build`'s real view-and-join machinery, which
// this resolution code doesn't care about at runtime (a table or a view
// answering the same three SELECTs is indistinguishable to it).
//
// Written alongside REFACTOR/CLI_AND_TOOLING.md §5's fix: --knowledge-db
// is documented-known-wrong (TODO/TEXTURE_POOL_RECALL_TODO.md: same-slot
// cross-item collisions, kept deliberately as diagnostic/future-work
// infrastructure rather than removed) but, before this fix, that
// known-wrongness only ever appeared in a hazards note a caller of this
// flag might never read -- never at the point an answer was actually
// about to be used. This is this flag's first CLI-tier coverage at all.

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <sqlite3.h>
#include <string>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

void execOrFail(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : "(no message)";
        sqlite3_free(errMsg);
        FAIL("sqlite3_exec failed: ", msg, " -- ", sql);
    }
}

// Builds the minimal knowledge-base SQLite fixture resolveObjectSkinTextureFromKb
// needs: one models row mapping `relPath` (already the lowercase, listfile-
// root-relative form husk's own code computes) to `modelFdid`, one
// model_object_skin_texture row resolving that to `textureFdid`, one
// textures row giving that FileDataID a real content path.
fs::path buildKnowledgeDb(const fs::path& path, const std::string& relPath, uint32_t modelFdid,
                           uint32_t textureFdid, const std::string& texturePath) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
            SQLITE_OK);

    execOrFail(db, "CREATE TABLE models(file_data_id INTEGER PRIMARY KEY, path TEXT)");
    execOrFail(db, "CREATE TABLE model_object_skin_texture(model_file_data_id INTEGER, texture_file_data_id INTEGER)");
    execOrFail(db, "CREATE TABLE textures(file_data_id INTEGER PRIMARY KEY, path TEXT)");

    execOrFail(db, "INSERT INTO models(file_data_id, path) VALUES (" + std::to_string(modelFdid) + ", '" + relPath +
                       "')");
    execOrFail(db, "INSERT INTO model_object_skin_texture(model_file_data_id, texture_file_data_id) VALUES (" +
                       std::to_string(modelFdid) + ", " + std::to_string(textureFdid) + ")");
    execOrFail(db, "INSERT INTO textures(file_data_id, path) VALUES (" + std::to_string(textureFdid) + ", '" +
                       texturePath + "')");

    sqlite3_close(db);
    return path;
}

}  // namespace

TEST_CASE("husk export --knowledge-db: a real resolution prints the documented-known-wrong warning "
          "at the point the answer is actually used") {
    auto dir = defaultsDir("knowledgedb_warn");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    auto kbPath = buildKnowledgeDb(dir / "kb.sqlite", "basic.m2", 111, 222, "textures/basic_skin.png");

    auto result = runHusk("export " + (dir / "basic.m2").string() + " --listfile-root " + dir.string() +
                           " --knowledge-db " + kbPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--knowledge-db resolved texture FileDataID 222") != std::string::npos);
    CHECK(result.output.find("known to produce wrong same-slot matches") != std::string::npos);
    CHECK(result.output.find("TODO/TEXTURE_POOL_RECALL_TODO.md") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --knowledge-db: a miss (model not in the knowledge base) resolves nothing "
          "and prints no warning") {
    auto dir = defaultsDir("knowledgedb_miss");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    // Knowledge base only knows about a different model -- "basic.m2" itself
    // has no row, so the lookup misses cleanly (same "empty/0 is a real,
    // expected case, not an error" contract resolveObjectSkinTextureFromKb's
    // own doc comment documents).
    auto kbPath = buildKnowledgeDb(dir / "kb.sqlite", "other.m2", 333, 444, "textures/other_skin.png");

    auto result = runHusk("export " + (dir / "basic.m2").string() + " --listfile-root " + dir.string() +
                           " --knowledge-db " + kbPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--knowledge-db resolved") == std::string::npos);

    fs::remove_all(dir);
}
