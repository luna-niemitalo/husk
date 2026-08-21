#pragma once

#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "db2.hpp"
#include "dbd.hpp"

// Shared surface between cmd_db2.cpp (db2-info/db2-export) and
// cmd_db2_build.cpp (db2-build) -- split out per FILE_SPLIT_TODO.md's Item 2.
// `LoadedFile`/`loadOneFile`/`writeFileTable` are cmd_db2.cpp's own real
// definitions (single-file and `--dir` multi-file export share this exact
// shape, per LoadedFile's own doc comment); `sqliteCheck` is a small shared
// error-checking helper both files call directly. Everything else
// cmd_db2.cpp uses to build/write a table (buildColumnPlan, decodeRecordValues,
// bindFieldValues, OutputColumn, FieldValues, sanitizeIdentifier, ...) stays
// file-local there -- db2Build never needs those, only the two functions
// declared here.
namespace husk::commands {

// A `$noninline,relation$` field (besides the id field, see LoadedFile's own
// `nonInlineIdColumnName`) -- `relation` is looked up from the DBD table's
// own COLUMNS block by name, same as an ordinary inline relation column's FK
// target.
struct NonInlineRelationColumn {
    std::string sqlName;
    std::optional<dbd::RelationTarget> relation;
};

// One .db2 file, parsed and resolved, ready to become one SQLite table --
// shared shape between single-file and `--dir` multi-file export so table
// creation/row insertion (writeFileTable) doesn't care which mode produced
// it.
struct LoadedFile {
    std::string path;
    db2::File file;
    std::vector<uint8_t> bytes;
    std::string tableName;
    std::optional<std::vector<dbd::Column>> dbdNames;
    // Real column name for header.flags & 0x04 ("has non-inline IDs") --
    // this ID occupies no WDC5 field-array slot at all, so it never shows
    // up in dbdNames/generic field_<N> naming and needs a column of its own
    // (see writeFileTable). Resolved from the DBD layout's own "$id$"/
    // "$noninline,id$" annotation when available, else a plain "id"
    // fallback -- same "never fetched, optional" tier as dbdNames.
    std::optional<std::string> nonInlineIdColumnName;
    // Every `$noninline,relation$` field this layout declares (besides the
    // id field above) -- see NonInlineRelationColumn. Empty whenever no DBD
    // layout resolved, same tier as dbdNames/nonInlineIdColumnName.
    std::vector<NonInlineRelationColumn> nonInlineRelationColumns;
    std::vector<size_t> usableSectionIndices;  // indices into file.sections
    size_t skippedEncrypted = 0;
};

// Reads and parses one .db2 file, resolving its table name/columns via
// `dbdDir` if given. Returns nullopt (after printing a diagnostic to `err`)
// for any per-file problem -- a bad or empty file shouldn't abort an entire
// `--dir` batch; the single-file caller treats nullopt as a hard failure.
std::optional<LoadedFile> loadOneFile(const std::string& path, const std::string& dbdDir, std::ostream& err);

// Creates and populates one SQLite table for `lf` (real column names/types
// when `lf.dbdNames` resolved, generic `field_<N>` otherwise), including any
// real WoWDBDefs foreign-key column whose target table is in `availableTables`.
// Returns the number of rows written.
size_t writeFileTable(sqlite3* db, const LoadedFile& lf, const std::set<std::string>& availableTables);

// Throws db2::ParseError (with sqlite3_errmsg(db) appended to `what`) unless
// `rc` is a real success code (SQLITE_OK/SQLITE_DONE/SQLITE_ROW) -- shared
// error-checking for every raw sqlite3_exec/sqlite3_step call in both files.
void sqliteCheck(int rc, sqlite3* db, const std::string& what);

}  // namespace husk::commands
