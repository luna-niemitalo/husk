#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>
#include <sqlite3.h>

#include "cmd_db2_shared.hpp"
#include "commands.hpp"
#include "db2.hpp"
#include "dbd.hpp"
#include "husk_config.hpp"

// `husk db2-info`/`husk db2-export` -- the WDC5 proof-of-concept's own
// `husk info`/`husk export` analogues (TODO/CHAR_TEXTURE_COMPOSITING_TODO.md
// Stage 1). `db2-info` prints header/section/field structure unconditionally
// (cheap, always useful for "what table is this") and a sample of decoded
// rows on request, from either a fixed-width or offset-map section (see
// db2.hpp's module comment for what's out of scope -- older container
// versions, table-name-to-struct mapping). `db2-export` converts a whole
// file to a real SQLite database, optionally with real column names/types
// via `--dbd-dir` (see dbd.hpp) -- field decoding itself is shared with
// db2-info (decodeRecordValues, this file), only the output sink differs.
namespace husk::commands {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw db2::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    errno = 0;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw db2::ParseError("error reading '" + path + "': " + std::strerror(errno));
    }
    return bytes;
}

// One field's resolved value(s) for one record -- unifies db2::decodeField's
// raw-array output (fixed-width sections, string resolution via
// db2::resolveFieldString's string-table heuristic) and
// db2::decodeOffsetMapRecord's already-resolved output (offset-map
// sections, inline strings) behind one shape, so every downstream consumer
// (row printing, SQL column planning/binding) shares one code path
// regardless of section shape.
struct FieldValues {
    std::vector<uint64_t> raw;
    std::optional<std::string> str;  // set only for a resolved string; raw is then empty
};

// Decodes every field of one record, from whichever section shape `section`
// actually is. `fileBytes` is only used by the fixed-width path (the
// offset-map path resolves strings inline, no file-wide string-table lookup
// needed). `sectionIndex` is `section`'s own index into `file.sections` --
// needed by the fixed-width path's string offsets, which are relative to a
// virtual all-sections blob, not to this file's own bytes directly (see
// db2::stringOffsetSectionCorrection).
std::vector<FieldValues> decodeRecordValues(const db2::File& file, const db2::Section& section,
                                             size_t sectionIndex, const std::vector<uint8_t>& fileBytes,
                                             size_t recordIndex) {
    std::vector<FieldValues> out;
    if (section.hasOffsetMap()) {
        std::vector<db2::OffsetMapFieldValue> fields = db2::decodeOffsetMapRecord(file, section, recordIndex);
        out.reserve(fields.size());
        for (db2::OffsetMapFieldValue& f : fields) {
            out.push_back({std::move(f.raw), std::move(f.str)});
        }
        return out;
    }

    out.reserve(file.fieldStorageInfo.size());
    for (size_t f = 0; f < file.fieldStorageInfo.size(); ++f) {
        std::vector<uint64_t> values = db2::decodeField(file, section, recordIndex, f);
        bool isScalarNone =
            values.size() == 1 && file.fieldStorageInfo[f].storageType == db2::FieldCompression::None;
        std::optional<std::string> str;
        if (isScalarNone) {
            int64_t fieldAbsPos = static_cast<int64_t>(section.header.fileOffset) +
                                   static_cast<int64_t>(recordIndex) * file.header.recordSize +
                                   file.fieldStructures[f].position + db2::stringOffsetSectionCorrection(file, sectionIndex);
            if (fieldAbsPos >= 0) {
                str = db2::resolveFieldString(fileBytes, static_cast<size_t>(fieldAbsPos), values[0]);
            }
        }
        out.push_back({std::move(values), std::move(str)});
    }
    return out;
}

const char* compressionName(db2::FieldCompression c) {
    switch (c) {
        case db2::FieldCompression::None:
            return "none";
        case db2::FieldCompression::Bitpacked:
            return "bitpacked";
        case db2::FieldCompression::CommonData:
            return "common_data";
        case db2::FieldCompression::BitpackedIndexed:
            return "bitpacked_indexed";
        case db2::FieldCompression::BitpackedIndexedArray:
            return "bitpacked_indexed_array";
        case db2::FieldCompression::BitpackedSigned:
            return "bitpacked_signed";
    }
    return "unknown";
}

void printHeader(const db2::Header& h) {
    std::cout << "  schema: " << h.schemaString << " (WDC5 version " << h.versionNum << ")\n";
    std::cout << "  record_count: " << h.recordCount << "   field_count: " << h.fieldCount
               << " (total " << h.totalFieldCount << ")\n";
    std::cout << "  record_size: " << h.recordSize << " bytes   string_table_size: "
               << h.stringTableSize << " bytes\n";
    std::cout << "  table_hash: 0x" << std::hex << h.tableHash << "   layout_hash: 0x"
               << h.layoutHash << std::dec << "\n";
    std::cout << "  id range: [" << h.minId << ", " << h.maxId << "]   id_index: " << h.idIndex
               << "\n";
    std::cout << "  flags: 0x" << std::hex << h.flags << std::dec << " (";
    bool any = false;
    auto flag = [&](uint16_t bit, const char* name) {
        if (h.flags & bit) {
            if (any) std::cout << ", ";
            std::cout << name;
            any = true;
        }
    };
    flag(0x01, "has offset map");
    flag(0x02, "has relationship data");
    flag(0x04, "has non-inline IDs");
    flag(0x10, "is bitpacked");
    if (!any) std::cout << "none set";
    std::cout << ")\n";
    std::cout << "  common_data: " << h.commonDataSize << " bytes   pallet_data: "
               << h.palletDataSize << " bytes\n";
    std::cout << "  sections: " << h.sectionCount << "\n";
}

void printSections(const db2::File& file) {
    for (size_t i = 0; i < file.sections.size(); ++i) {
        const db2::Section& s = file.sections[i];
        std::cout << "  section " << i << ": " << s.header.recordCount << " records at file offset 0x"
                   << std::hex << s.header.fileOffset << std::dec;
        if (s.header.tactKeyHash != 0) {
            if (s.recordsAvailable()) {
                std::cout << "  [TACT-key-gated, tact_key_hash=0x" << std::hex << s.header.tactKeyHash
                           << std::dec << ", but already decrypted by the real CASC extraction -- "
                              "record bytes readable]";
            } else {
                std::cout << "  [ENCRYPTED, tact_key_hash=0x" << std::hex << s.header.tactKeyHash
                           << std::dec << ", record bytes still all-zero -- genuinely unreadable, "
                              "the key was unavailable when this file was extracted]";
            }
        }
        if (!s.offsetMap.empty()) {
            std::cout << "  (offset-map/sparse -- per-field decode via a sequential bit cursor, see "
                          "db2.hpp's decodeOffsetMapRecord doc comment)";
        }
        std::cout << "\n";
        if (s.hasRelationshipMap()) {
            std::cout << "    relationship_map: " << s.relationshipEntries.size() << " entries, id range ["
                       << s.relationshipMinId << ", " << s.relationshipMaxId << "]\n";
            for (size_t j = 0; j < std::min<size_t>(3, s.relationshipEntries.size()); ++j) {
                std::cout << "      foreign_id=" << s.relationshipEntries[j].foreignId
                           << "  record=" << s.relationshipEntries[j].recordIndexOrId << "\n";
            }
        }
    }
}

void printFields(const db2::File& file) {
    std::cout << "  fields (" << file.fieldStorageInfo.size() << "):\n";
    for (size_t i = 0; i < file.fieldStorageInfo.size(); ++i) {
        const db2::FieldStorageInfo& info = file.fieldStorageInfo[i];
        std::cout << "    [" << i << "] " << compressionName(info.storageType) << "  offset_bits="
                   << info.fieldOffsetBits << " size_bits=" << info.fieldSizeBits;
        if (info.storageType == db2::FieldCompression::CommonData) {
            std::cout << " default=" << info.defaultValue;
        }
        if (info.storageType == db2::FieldCompression::BitpackedIndexedArray) {
            std::cout << " array_count=" << info.arrayCount;
        }
        if (i == file.header.idIndex && !(file.header.flags & 0x04)) {
            std::cout << "  (id field)";
        }
        std::cout << "\n";
    }
}

// Prints up to `rowLimit` decoded rows from the first non-encrypted section
// (fixed-width or offset-map -- both decode via decodeRecordValues now).
// `rowLimit == SIZE_MAX` means "all". `fileBytes` is the original file
// buffer, needed by the fixed-width path's string heuristic (db2::
// resolveFieldString resolves an absolute file position, not a
// section-relative one); unused for an offset-map section.
void printRows(const db2::File& file, const std::vector<uint8_t>& fileBytes, size_t rowLimit) {
    const db2::Section* section = nullptr;
    size_t sectionIndex = 0;
    for (size_t i = 0; i < file.sections.size(); ++i) {
        if (!file.sections[i].recordsAvailable()) continue;
        section = &file.sections[i];
        sectionIndex = i;
        break;
    }
    if (!section) {
        std::cout << "  (no section with decodable records -- every section is genuinely still encrypted)\n";
        return;
    }

    size_t n = std::min(static_cast<size_t>(section->header.recordCount), rowLimit);
    std::cout << "  rows (" << n << " of " << section->header.recordCount << "):\n";
    for (size_t r = 0; r < n; ++r) {
        std::cout << "    row " << r << ":";
        std::vector<FieldValues> fields = decodeRecordValues(file, *section, sectionIndex, fileBytes, r);
        for (size_t f = 0; f < fields.size(); ++f) {
            const FieldValues& values = fields[f];
            std::cout << " [" << f << "]=";
            if (values.str) {
                std::cout << "\"" << *values.str << "\"";
            } else if (values.raw.size() > 1) {
                std::cout << "{";
                for (size_t i = 0; i < values.raw.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << values.raw[i];
                }
                std::cout << "}";
            } else {
                std::cout << values.raw[0];
            }
        }
        std::cout << "\n";
    }
}

// SQLite identifiers can't start with a digit and shouldn't carry characters
// SQLite would otherwise need quoting for -- real DBD field names are
// already clean C-identifier-shaped, but this also has to handle the
// generic `field_<N>` fallback and arbitrary input-file basenames used as
// table names, so it's applied unconditionally rather than assumed
// unnecessary.
std::string sanitizeIdentifier(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_';
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) {
        out = "_" + out;
    }
    return out;
}

}  // namespace

// See cmd_db2_shared.hpp's own doc comment -- shared with cmd_db2_build.cpp.
void sqliteCheck(int rc, sqlite3* db, const std::string& what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = what + ": " + sqlite3_errmsg(db);
        throw db2::ParseError(msg);
    }
}

namespace {

// One real output column: `baseName` is the field's real (DBD) or generic
// (`field_<N>`) name; `arrayIndex` is set only when the field is a real
// WDC5 array (decodeField returned more than one value), giving each
// element its own `<baseName>_<i>` column -- SQLite has no array column
// type, and DBD carries no per-array-element name to draw on instead.
struct OutputColumn {
    std::string sqlName;
    bool isFloat = false;  // reinterpret the raw uint32 bits as float when writing
};

// Determines every output column, in field order, by decoding one
// representative record (record 0 of the first usable section) -- WDC5
// array lengths are fixed per field (field_storage_info's arrayCount), so
// any record's element count for a given field is representative of every
// other record's. A field resolved as a string always counts as exactly one
// column, same as a plain scalar -- decodeOffsetMapRecord only ever
// classifies a genuinely scalar field as a string (see its own doc comment),
// so this never disagrees with another record's element count for the same
// field.
std::vector<std::vector<OutputColumn>> buildColumnPlan(
    const db2::File& file, const db2::Section& sampleSection, size_t sampleSectionIndex,
    const std::vector<uint8_t>& sampleBytes, const std::optional<std::vector<dbd::Column>>& dbdNames) {
    std::vector<FieldValues> sample = decodeRecordValues(file, sampleSection, sampleSectionIndex, sampleBytes, 0);
    std::vector<std::vector<OutputColumn>> plan(file.fieldStorageInfo.size());
    for (size_t f = 0; f < file.fieldStorageInfo.size(); ++f) {
        std::string baseName = dbdNames ? sanitizeIdentifier((*dbdNames)[f].name)
                                         : "field_" + std::to_string(f);
        bool isFloat = dbdNames && (*dbdNames)[f].type == dbd::ColumnType::Float;
        size_t count = sample[f].str ? 1 : sample[f].raw.size();
        if (count == 1) {
            plan[f].push_back({baseName, isFloat});
        } else {
            for (size_t i = 0; i < count; ++i) {
                plan[f].push_back({baseName + "_" + std::to_string(i), isFloat});
            }
        }
    }
    return plan;
}

// Binds one decoded field's values into the prepared INSERT statement,
// starting at 1-based bind index `firstBindIndex`. `values` is already
// fully resolved (decodeRecordValues) -- a real string always binds as
// TEXT, a raw array as one INTEGER (or REAL, if `isFloat`) per element.
void bindFieldValues(sqlite3_stmt* stmt, int firstBindIndex, const FieldValues& values, bool isFloat) {
    if (values.str) {
        sqlite3_bind_text(stmt, firstBindIndex, values.str->c_str(), -1, SQLITE_TRANSIENT);
        return;
    }
    for (size_t i = 0; i < values.raw.size(); ++i) {
        int bindIndex = firstBindIndex + static_cast<int>(i);
        if (isFloat && values.raw.size() == 1) {
            float f;
            auto bits = static_cast<uint32_t>(values.raw[0]);
            std::memcpy(&f, &bits, sizeof(f));
            sqlite3_bind_double(stmt, bindIndex, static_cast<double>(f));
            continue;
        }
        sqlite3_bind_int64(stmt, bindIndex, static_cast<sqlite3_int64>(values.raw[i]));
    }
}

}  // namespace

// See cmd_db2_shared.hpp's own doc comments -- NonInlineRelationColumn/
// LoadedFile/loadOneFile/writeFileTable are all shared with cmd_db2_build.cpp.
std::optional<LoadedFile> loadOneFile(const std::string& path, const std::string& dbdDir,
                                       std::ostream& err) {
    LoadedFile lf;
    lf.path = path;
    try {
        lf.bytes = readFileBytes(path);
        lf.file = db2::parse(lf.bytes);
    } catch (const std::exception& e) {
        err << "husk: db2-export: couldn't read '" << path << "': " << e.what() << " -- skipped\n";
        return std::nullopt;
    }

    for (size_t i = 0; i < lf.file.sections.size(); ++i) {
        const db2::Section& s = lf.file.sections[i];
        if (!s.recordsAvailable()) {
            ++lf.skippedEncrypted;
            continue;
        }
        lf.usableSectionIndices.push_back(i);
    }
    if (lf.usableSectionIndices.empty()) {
        err << "husk: db2-export: '" << path << "' has no decodable section (every section is encrypted) -- skipped\n";
        return std::nullopt;
    }

    lf.tableName = sanitizeIdentifier(std::filesystem::path(path).stem().string());
    if (!dbdDir.empty()) {
        std::optional<dbd::Table> dbdTable = dbd::loadTableForHash(dbdDir, lf.file.header.tableHash);
        if (dbdTable) {
            lf.tableName = sanitizeIdentifier(dbdTable->tableName);
            const dbd::Layout* layout = dbd::findLayout(*dbdTable, lf.file.header.layoutHash);
            if (layout) {
                lf.dbdNames = dbd::resolveFieldNames(*dbdTable, *layout, lf.file.fieldStorageInfo);
                lf.nonInlineIdColumnName = dbd::findIdFieldName(*layout);
                for (const std::string& name : dbd::findNonInlineNonIdFieldNames(*layout)) {
                    NonInlineRelationColumn col;
                    col.sqlName = sanitizeIdentifier(name);
                    for (const dbd::Column& c : dbdTable->columns) {
                        if (c.name == name) {
                            col.relation = c.relation;
                            break;
                        }
                    }
                    lf.nonInlineRelationColumns.push_back(std::move(col));
                }
            }
        }
        if (!lf.dbdNames) {
            err << "husk: db2-export: no matching WoWDBDefs layout for '" << path
                << "' (table_hash=0x" << std::hex << lf.file.header.tableHash << " layout_hash=0x"
                << lf.file.header.layoutHash << std::dec
                << ") -- falling back to generic field_<N> column names\n";
        }
    }
    if ((lf.file.header.flags & 0x04) != 0 && !lf.nonInlineIdColumnName) {
        lf.nonInlineIdColumnName = "id";  // no DBD name resolved -- still real, decodable data
    }
    return lf;
}

// Creates `lf`'s table and inserts every row, returning the row count.
// `availableTables` is the set of real (DBD-resolved) table names present
// elsewhere in this same export batch -- a column's FK constraint is only
// ever emitted when its relation target is one of these; a target table not
// in the batch degrades to a plain, unconstrained column (a real, expected
// case, not an error).
size_t writeFileTable(sqlite3* db, const LoadedFile& lf, const std::set<std::string>& availableTables) {
    const db2::Section& sampleSection = lf.file.sections[lf.usableSectionIndices[0]];
    std::vector<std::vector<OutputColumn>> plan =
        buildColumnPlan(lf.file, sampleSection, lf.usableSectionIndices[0], lf.bytes, lf.dbdNames);

    std::string idSqlName = lf.nonInlineIdColumnName ? sanitizeIdentifier(*lf.nonInlineIdColumnName) : "";

    std::ostringstream createSql;
    createSql << "CREATE TABLE \"" << lf.tableName << "\" (db2_section INTEGER, db2_record INTEGER";
    if (!idSqlName.empty()) {
        createSql << ", \"" << idSqlName << "\" INTEGER";
    }
    for (const NonInlineRelationColumn& col : lf.nonInlineRelationColumns) {
        createSql << ", \"" << col.sqlName << "\" INTEGER";
    }
    std::vector<std::string> fkClauses;
    for (size_t f = 0; f < plan.size(); ++f) {
        for (const OutputColumn& c : plan[f]) {
            createSql << ", \"" << c.sqlName << "\" " << (c.isFloat ? "REAL" : "");
        }
        const std::optional<dbd::RelationTarget>* relationOpt =
            (lf.dbdNames && plan[f].size() == 1) ? &(*lf.dbdNames)[f].relation : nullptr;
        if (relationOpt && *relationOpt) {
            const dbd::RelationTarget& rel = relationOpt->value();
            std::string targetTable = sanitizeIdentifier(rel.targetTable);
            if (availableTables.count(targetTable) > 0) {
                fkClauses.push_back("FOREIGN KEY (\"" + plan[f][0].sqlName + "\") REFERENCES \"" +
                                     targetTable + "\"(\"" + sanitizeIdentifier(rel.targetColumn) + "\")");
            }
        }
    }
    for (const NonInlineRelationColumn& col : lf.nonInlineRelationColumns) {
        if (!col.relation) continue;
        std::string targetTable = sanitizeIdentifier(col.relation->targetTable);
        if (availableTables.count(targetTable) > 0) {
            fkClauses.push_back("FOREIGN KEY (\"" + col.sqlName + "\") REFERENCES \"" + targetTable +
                                 "\"(\"" + sanitizeIdentifier(col.relation->targetColumn) + "\")");
        }
    }
    for (const std::string& fk : fkClauses) createSql << ", " << fk;
    createSql << ")";
    sqliteCheck(sqlite3_exec(db, createSql.str().c_str(), nullptr, nullptr, nullptr), db,
                "CREATE TABLE \"" + lf.tableName + "\"");

    std::ostringstream insertSql;
    insertSql << "INSERT INTO \"" << lf.tableName << "\" VALUES (?, ?" << (idSqlName.empty() ? "" : ", ?");
    for (size_t i = 0; i < lf.nonInlineRelationColumns.size(); ++i) insertSql << ", ?";
    size_t totalColumns = 0;
    for (const auto& cols : plan) totalColumns += cols.size();
    for (size_t i = 0; i < totalColumns; ++i) insertSql << ", ?";
    insertSql << ")";

    sqlite3_stmt* stmt = nullptr;
    sqliteCheck(sqlite3_prepare_v2(db, insertSql.str().c_str(), -1, &stmt, nullptr), db,
                "prepare INSERT into \"" + lf.tableName + "\"");

    size_t rowCount = 0;
    for (size_t sectionIndex : lf.usableSectionIndices) {
        const db2::Section& section = lf.file.sections[sectionIndex];
        // One relationship_map decode per section, shared by every
        // non-inline relation column -- the section carries a single
        // relationship_map, not one per DBD field, matching db2table.cpp's
        // own "needsRelation" computation.
        std::vector<std::optional<uint32_t>> relationValues =
            lf.nonInlineRelationColumns.empty() ? std::vector<std::optional<uint32_t>>{}
                                                 : db2::nonInlineRelationValuesByRecord(lf.file, section);
        for (uint32_t r = 0; r < section.header.recordCount; ++r) {
            sqlite3_reset(stmt);
            sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(sectionIndex));
            sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(r));
            int bindIndex = 3;
            if (!idSqlName.empty()) {
                sqlite3_bind_int64(stmt, bindIndex,
                                    static_cast<sqlite3_int64>(db2::recordId(lf.file, section, r)));
                ++bindIndex;
            }
            for (size_t i = 0; i < lf.nonInlineRelationColumns.size(); ++i) {
                std::optional<uint32_t> v = r < relationValues.size() ? relationValues[r] : std::nullopt;
                if (v) {
                    sqlite3_bind_int64(stmt, bindIndex, static_cast<sqlite3_int64>(*v));
                } else {
                    sqlite3_bind_null(stmt, bindIndex);
                }
                ++bindIndex;
            }
            std::vector<FieldValues> fields = decodeRecordValues(lf.file, section, sectionIndex, lf.bytes, r);
            for (size_t f = 0; f < fields.size(); ++f) {
                bool isFloat = !plan[f].empty() && plan[f][0].isFloat;
                bindFieldValues(stmt, bindIndex, fields[f], isFloat);
                bindIndex += static_cast<int>(fields[f].str ? 1 : fields[f].raw.size());
            }
            int rc = sqlite3_step(stmt);
            sqliteCheck(rc, db, "INSERT into \"" + lf.tableName + "\"");
            ++rowCount;
        }
    }
    sqlite3_finalize(stmt);
    return rowCount;
}

void addDb2ExportOptions(CLI::App& app, Db2ExportOptions& opts) {
    app.set_config("--config", husk::defaultConfigPath(),
                    "TOML file of default flag values -- explicit CLI flags always override a "
                    "config value")
        ->envname("HUSK_CONFIG");
    app.add_option("--dir", opts.dirArg, "directory of .db2 files to convert -- batch mode");
    app.add_option("--dbd-dir", opts.dbdDir,
                    "a local WoWDBDefs checkout (github.com/wowdev/WoWDBDefs -- manifest.json + "
                    "definitions/*.dbd), used to resolve real column names/types for each file's "
                    "own table_hash/layout_hash. Optional -- without it, or if no matching "
                    "layout is found, columns are named field_<N> instead. In --dir mode, a "
                    "column with a real WoWDBDefs foreign-key target also gets a real SQLite "
                    "FOREIGN KEY constraint, but only when the target table is also part of this "
                    "same export batch");
    app.add_option("pos1", opts.pos1,
                    "the .db2 file to convert (single-file mode), or the .db2 directory (--dir "
                    "mode)");
    app.add_option("pos2", opts.pos2, "the output .sqlite path");
}

int db2Export(int argc, char** args) {
    Db2ExportOptions opts;
    CLI::App app{
        "Converts one WDC5 DB2 file, or (with --dir) every *.db2 file in a directory, to a real "
        "SQLite database -- one table per file, named from the DBD table name if resolved, else "
        "the input file's own basename. Every unencrypted section's records are exported, "
        "fixed-width or offset-map/sparse alike; only TACT-encrypted sections are skipped, and a "
        "count of skipped sections is printed, never silently dropped. In --dir mode, a file "
        "that can't be parsed or has nothing exportable is skipped (with a diagnostic), not "
        "treated as a fatal error for the whole batch.",
        "husk db2-export"};
    addDb2ExportOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    const std::string& dbdDir = opts.dbdDir;

    bool dirMode = app.count("--dir") > 0;
    std::string inputPath, dirPath, outputPath;
    if (dirMode) {
        if (app.count("pos1") != 1 || app.count("pos2") != 0) {
            std::cerr << "husk: db2-export --dir: expected exactly one positional argument (the "
                         "output .sqlite path)\n";
            return 1;
        }
        dirPath = opts.dirArg;
        outputPath = opts.pos1;
    } else {
        if (app.count("pos1") != 1 || app.count("pos2") != 1) {
            std::cerr << "husk: db2-export: expected exactly two positional arguments (the "
                         "input .db2 and the output .sqlite path), or --dir for batch mode\n";
            return 1;
        }
        inputPath = opts.pos1;
        outputPath = opts.pos2;
    }

    std::vector<LoadedFile> loaded;
    if (dirMode) {
        std::vector<std::string> paths;
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (entry.path().extension() == ".db2") paths.push_back(entry.path().string());
        }
        std::sort(paths.begin(), paths.end());
        for (const std::string& p : paths) {
            std::optional<LoadedFile> lf = loadOneFile(p, dbdDir, std::cerr);
            if (lf) loaded.push_back(std::move(*lf));
        }
        if (loaded.empty()) {
            std::cerr << "husk: db2-export: no exportable .db2 files found under '" << dirPath << "'\n";
            return 1;
        }
    } else {
        std::optional<LoadedFile> lf = loadOneFile(inputPath, dbdDir, std::cerr);
        if (!lf) return 1;
        loaded.push_back(std::move(*lf));
    }

    // Only a real, DBD-resolved table name counts as "present in this batch"
    // for FK-target purposes -- a generic field_<N>-fallback table's name is
    // just the input file's own basename, which has no reliable relationship
    // to any WoWDBDefs table name it might coincidentally match.
    std::set<std::string> availableTables;
    for (const LoadedFile& lf : loaded) {
        if (lf.dbdNames) availableTables.insert(lf.tableName);
    }

    std::error_code ec;
    std::filesystem::remove(outputPath, ec);  // real re-export, not an append -- start clean

    sqlite3* db = nullptr;
    if (sqlite3_open(outputPath.c_str(), &db) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        std::cerr << "husk: db2-export: couldn't create '" << outputPath << "': " << msg << "\n";
        return 1;
    }

    try {
        sqliteCheck(sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr), db, "BEGIN");

        size_t totalRows = 0;
        size_t namedTables = 0;
        size_t skippedEncrypted = 0;
        for (const LoadedFile& lf : loaded) {
            size_t rowCount = writeFileTable(db, lf, availableTables);
            totalRows += rowCount;
            skippedEncrypted += lf.skippedEncrypted;
            if (lf.dbdNames) ++namedTables;

            std::cout << "husk: db2-export: wrote " << rowCount << " row(s) to table \"" << lf.tableName
                       << "\" (" << (lf.dbdNames ? "real column names via WoWDBDefs" : "generic field_<N> column names")
                       << ")\n";
        }
        sqliteCheck(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db, "COMMIT");

        std::cout << "husk: db2-export: " << loaded.size() << " table(s), " << totalRows
                   << " total row(s), " << namedTables << " with real WoWDBDefs column names, written to '"
                   << outputPath << "'\n";
        if (skippedEncrypted > 0) {
            std::cout << "husk: db2-export: skipped " << skippedEncrypted
                       << " encrypted section(s) across all tables, not exported\n";
        }
    } catch (const std::exception& e) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        std::cerr << "husk: db2-export: " << e.what() << "\n";
        return 1;
    }

    sqlite3_close(db);
    return 0;
}

void addDb2InfoOptions(CLI::App& app, Db2InfoOptions& opts) {
    app.add_option("model", opts.model, "the .db2 file to inspect")->required();
    app.add_option("--rows", opts.rowsArg, "how many decoded rows to dump, or 'all'")
        ->capture_default_str();
}

int db2Info(int argc, char** args) {
    Db2InfoOptions opts;
    CLI::App app{
        "Parses a WDC5 DB2 file and prints its header, per-section layout, and per-field storage "
        "info. With --rows (default 5, 0 for none, 'all' for every record), also dumps that many "
        "decoded rows from the first non-encrypted section (fixed-width or offset-map/sparse) -- "
        "raw values per field, with a best-effort string heuristic. Proof of concept: field "
        "names are not known (WDC5 carries no column names, only positions/sizes), so fields are "
        "identified by index only.",
        "husk db2-info"};
    addDb2InfoOptions(app, opts);

    try {
        std::vector<std::string> argVec(args, args + argc);
        std::reverse(argVec.begin(), argVec.end());
        app.parse(argVec);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    const std::string& path = opts.model;

    size_t rowLimit = 5;
    if (opts.rowsArg == "all") {
        rowLimit = static_cast<size_t>(-1);
    } else {
        try {
            size_t pos = 0;
            rowLimit = static_cast<size_t>(std::stoul(opts.rowsArg, &pos));
            if (pos != opts.rowsArg.size()) throw std::invalid_argument(opts.rowsArg);
        } catch (const std::exception&) {
            std::cerr << "husk: db2-info: --rows expects a non-negative integer or 'all', got '"
                      << opts.rowsArg << "'\n";
            return 1;
        }
    }

    db2::File file;
    std::vector<uint8_t> bytes;
    try {
        bytes = readFileBytes(path);
        file = db2::parse(bytes);
    } catch (const std::exception& e) {
        std::cerr << "husk: couldn't read '" << path << "': " << e.what() << "\n";
        return 1;
    }

    std::cout << path << "\n";
    printHeader(file.header);
    printSections(file);
    printFields(file);
    if (rowLimit > 0) {
        printRows(file, bytes, rowLimit);
    }

    return 0;
}

}  // namespace husk::commands
