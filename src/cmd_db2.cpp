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
#include <vector>

#include <sqlite3.h>

#include "commands.hpp"
#include "db2.hpp"
#include "dbd.hpp"

// `husk db2-info`/`husk db2-export` -- the WDC5 proof-of-concept's own
// `husk info`/`husk export` analogues (TODO/CHAR_TEXTURE_COMPOSITING_TODO.md
// Stage 1). `db2-info` prints header/section/field structure unconditionally
// (cheap, always useful for "what table is this") and a sample of decoded
// rows on request -- see db2.hpp's module comment for what's out of scope
// (older container versions, offset-map per-field decode, table-name-to-
// struct mapping). `db2-export` converts a whole file to a real SQLite
// database, optionally with real column names/types via `--dbd-dir` (see
// dbd.hpp) -- field decoding itself is shared with db2-info, only the
// output sink differs.
namespace husk::commands {

namespace {

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk db2-info <file.db2> [--rows N]\n"
           "\n"
           "Parses a WDC5 DB2 file and prints its header, per-section layout,\n"
           "and per-field storage info. With --rows (default 5, 0 for none,\n"
           "'all' for every record), also dumps that many decoded rows from\n"
           "the first non-offset-map section -- raw values per field, with a\n"
           "best-effort string heuristic (see db2.hpp's resolveFieldString).\n"
           "\n"
           "Proof of concept: field names are not known (WDC5 carries no\n"
           "column names, only positions/sizes -- see db2.hpp), so fields are\n"
           "identified by index only.\n";
}

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
            std::cout << "  [ENCRYPTED, tact_key_hash=0x" << std::hex << s.header.tactKeyHash
                       << std::dec << ", record bytes unreadable without the TACT key]";
        }
        if (!s.offsetMap.empty()) {
            std::cout << "  (offset-map/sparse -- per-field decode not implemented, see --rows output)";
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

// Prints up to `rowLimit` decoded rows from the first section that has
// fixed-width records (offset-map sections are skipped -- see db2.hpp's
// module comment on what this POC does and doesn't decode). `rowLimit ==
// SIZE_MAX` means "all". `fileBytes` is the original file buffer, needed by
// the string heuristic (db2::resolveFieldString resolves an absolute file
// position, not a section-relative one).
void printRows(const db2::File& file, const std::vector<uint8_t>& fileBytes, size_t rowLimit) {
    const db2::Section* section = nullptr;
    for (const db2::Section& s : file.sections) {
        if (s.header.tactKeyHash != 0) continue;  // encrypted, unreadable
        if (!s.offsetMap.empty()) continue;        // offset-map path, no per-field decode
        section = &s;
        break;
    }
    if (!section) {
        std::cout << "  (no section with decodable fixed-width records -- every section is "
                     "encrypted and/or offset-map/sparse)\n";
        return;
    }

    size_t n = std::min(static_cast<size_t>(section->header.recordCount), rowLimit);
    std::cout << "  rows (" << n << " of " << section->header.recordCount << "):\n";
    for (size_t r = 0; r < n; ++r) {
        std::cout << "    row " << r << ":";
        for (size_t f = 0; f < file.fieldStorageInfo.size(); ++f) {
            std::vector<uint64_t> values = db2::decodeField(file, *section, r, f);
            std::cout << " [" << f << "]=";
            bool isScalarNone =
                values.size() == 1 && file.fieldStorageInfo[f].storageType == db2::FieldCompression::None;
            std::optional<std::string> str;
            if (isScalarNone) {
                size_t fieldAbsPos =
                    section->header.fileOffset + r * file.header.recordSize + file.fieldStructures[f].position;
                str = db2::resolveFieldString(fileBytes, fieldAbsPos, values[0]);
            }
            if (str) {
                std::cout << "\"" << *str << "\"";
            } else if (values.size() > 1) {
                std::cout << "{";
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i) std::cout << ",";
                    std::cout << values[i];
                }
                std::cout << "}";
            } else {
                std::cout << values[0];
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

void sqliteCheck(int rc, sqlite3* db, const std::string& what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string msg = what + ": " + sqlite3_errmsg(db);
        throw db2::ParseError(msg);
    }
}

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
// other record's.
std::vector<std::vector<OutputColumn>> buildColumnPlan(
    const db2::File& file, const db2::Section& sampleSection,
    const std::optional<std::vector<dbd::Column>>& dbdNames) {
    std::vector<std::vector<OutputColumn>> plan(file.fieldStorageInfo.size());
    for (size_t f = 0; f < file.fieldStorageInfo.size(); ++f) {
        std::string baseName = dbdNames ? sanitizeIdentifier((*dbdNames)[f].name)
                                         : "field_" + std::to_string(f);
        bool isFloat = dbdNames && (*dbdNames)[f].type == dbd::ColumnType::Float;
        std::vector<uint64_t> sample = db2::decodeField(file, sampleSection, 0, f);
        if (sample.size() == 1) {
            plan[f].push_back({baseName, isFloat});
        } else {
            for (size_t i = 0; i < sample.size(); ++i) {
                plan[f].push_back({baseName + "_" + std::to_string(i), isFloat});
            }
        }
    }
    return plan;
}

// Binds one decoded field's values into the prepared INSERT statement,
// starting at 1-based bind index `firstBindIndex`. Applies the same
// scalar-string heuristic db2-info's own row preview uses (db2::
// resolveFieldString) when no DBD type is known to force a numeric/float
// reading -- real strings still come through as TEXT even in the no-DBD
// fallback path.
void bindFieldValues(sqlite3_stmt* stmt, int firstBindIndex, const db2::File& file,
                     const db2::Section& section, const std::vector<uint8_t>& fileBytes,
                     size_t recordIndex, size_t fieldIndex, const std::vector<uint64_t>& values,
                     bool isFloat) {
    bool isScalarNone = values.size() == 1 &&
                         file.fieldStorageInfo[fieldIndex].storageType == db2::FieldCompression::None;
    for (size_t i = 0; i < values.size(); ++i) {
        int bindIndex = firstBindIndex + static_cast<int>(i);
        if (isFloat && values.size() == 1) {
            float f;
            uint32_t bits = static_cast<uint32_t>(values[0]);
            std::memcpy(&f, &bits, sizeof(f));
            sqlite3_bind_double(stmt, bindIndex, static_cast<double>(f));
            continue;
        }
        std::optional<std::string> str;
        if (isScalarNone) {
            size_t fieldAbsPos = section.header.fileOffset + recordIndex * file.header.recordSize +
                                  file.fieldStructures[fieldIndex].position;
            str = db2::resolveFieldString(fileBytes, fieldAbsPos, values[i]);
        }
        if (str) {
            sqlite3_bind_text(stmt, bindIndex, str->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_int64(stmt, bindIndex, static_cast<sqlite3_int64>(values[i]));
        }
    }
}

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
    std::vector<size_t> usableSectionIndices;  // indices into file.sections
    size_t skippedEncrypted = 0;
    size_t skippedOffsetMap = 0;
};

// Reads and parses one .db2 file, resolving its table name/columns via
// `dbdDir` if given. Returns nullopt (after printing a diagnostic to `err`)
// for any per-file problem -- a bad or empty file shouldn't abort an entire
// `--dir` batch; the single-file caller treats nullopt as a hard failure.
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
        if (s.header.tactKeyHash != 0) {
            ++lf.skippedEncrypted;
            continue;
        }
        if (!s.offsetMap.empty()) {
            ++lf.skippedOffsetMap;
            continue;
        }
        lf.usableSectionIndices.push_back(i);
    }
    if (lf.usableSectionIndices.empty()) {
        err << "husk: db2-export: '" << path
            << "' has no decodable fixed-width section (every section is encrypted and/or "
               "offset-map/sparse) -- skipped\n";
        return std::nullopt;
    }

    lf.tableName = sanitizeIdentifier(std::filesystem::path(path).stem().string());
    if (!dbdDir.empty()) {
        std::optional<dbd::Table> dbdTable = dbd::loadTableForHash(dbdDir, lf.file.header.tableHash);
        if (dbdTable) {
            lf.tableName = sanitizeIdentifier(dbdTable->tableName);
            const dbd::Layout* layout = dbd::findLayout(*dbdTable, lf.file.header.layoutHash);
            if (layout) {
                lf.dbdNames = dbd::resolveFieldNames(*dbdTable, *layout, lf.file.fieldStorageInfo.size());
                lf.nonInlineIdColumnName = dbd::findIdFieldName(*layout);
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
    std::vector<std::vector<OutputColumn>> plan = buildColumnPlan(lf.file, sampleSection, lf.dbdNames);

    std::string idSqlName = lf.nonInlineIdColumnName ? sanitizeIdentifier(*lf.nonInlineIdColumnName) : "";

    std::ostringstream createSql;
    createSql << "CREATE TABLE \"" << lf.tableName << "\" (db2_section INTEGER, db2_record INTEGER";
    if (!idSqlName.empty()) {
        createSql << ", \"" << idSqlName << "\" INTEGER";
    }
    std::vector<std::string> fkClauses;
    for (size_t f = 0; f < plan.size(); ++f) {
        for (const OutputColumn& c : plan[f]) {
            createSql << ", \"" << c.sqlName << "\" " << (c.isFloat ? "REAL" : "");
        }
        if (lf.dbdNames && plan[f].size() == 1 && (*lf.dbdNames)[f].relation) {
            const dbd::RelationTarget& rel = (*lf.dbdNames)[f].relation.value();
            std::string targetTable = sanitizeIdentifier(rel.targetTable);
            if (availableTables.count(targetTable) > 0) {
                fkClauses.push_back("FOREIGN KEY (\"" + plan[f][0].sqlName + "\") REFERENCES \"" +
                                     targetTable + "\"(\"" + sanitizeIdentifier(rel.targetColumn) + "\")");
            }
        }
    }
    for (const std::string& fk : fkClauses) createSql << ", " << fk;
    createSql << ")";
    sqliteCheck(sqlite3_exec(db, createSql.str().c_str(), nullptr, nullptr, nullptr), db,
                "CREATE TABLE \"" + lf.tableName + "\"");

    std::ostringstream insertSql;
    insertSql << "INSERT INTO \"" << lf.tableName << "\" VALUES (?, ?" << (idSqlName.empty() ? "" : ", ?");
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
            for (size_t f = 0; f < lf.file.fieldStorageInfo.size(); ++f) {
                std::vector<uint64_t> values = db2::decodeField(lf.file, section, r, f);
                bool isFloat = !plan[f].empty() && plan[f][0].isFloat;
                bindFieldValues(stmt, bindIndex, lf.file, section, lf.bytes, r, f, values, isFloat);
                bindIndex += static_cast<int>(values.size());
            }
            int rc = sqlite3_step(stmt);
            sqliteCheck(rc, db, "INSERT into \"" + lf.tableName + "\"");
            ++rowCount;
        }
    }
    sqlite3_finalize(stmt);
    return rowCount;
}

}  // namespace

int db2Export(int argc, char** args) {
    static const char* usage =
        "usage: husk db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]\n"
        "       husk db2-export --dir <db2-dir> <out.sqlite> [--dbd-dir DIR]\n"
        "\n"
        "Converts one WDC5 DB2 file, or (with --dir) every *.db2 file in a\n"
        "directory, to a real SQLite database -- one table per file, named\n"
        "from the DBD table name if resolved, else the input file's own\n"
        "basename. Every fixed-width, unencrypted section's records are\n"
        "exported; offset-map/sparse and TACT-encrypted sections are skipped\n"
        "(see db2.hpp's module comment) -- a count of skipped records is\n"
        "printed, never silently dropped. In --dir mode, a file that can't be\n"
        "parsed or has nothing exportable is skipped (with a diagnostic),\n"
        "not treated as a fatal error for the whole batch.\n"
        "\n"
        "--dbd-dir DIR: a local WoWDBDefs checkout (github.com/wowdev/\n"
        "WoWDBDefs -- manifest.json + definitions/*.dbd), used to resolve\n"
        "real column names/types for each file's own table_hash/layout_hash.\n"
        "Optional -- without it, or if no matching layout is found, columns\n"
        "are named field_<N> instead. husk never fetches or bundles this\n"
        "data itself. In --dir mode, a column with a real WoWDBDefs foreign-\n"
        "key target also gets a real SQLite FOREIGN KEY constraint, but only\n"
        "when the target table is also part of this same export batch --\n"
        "otherwise it stays a plain, unconstrained column.\n";

    if (argc >= 1 && isHelpFlag(args[0])) {
        std::cout << usage;
        return 0;
    }

    bool dirMode = argc >= 1 && std::string(args[0]) == "--dir";
    std::string inputPath, dirPath, outputPath, dbdDir;
    if (dirMode) {
        if (argc != 3 && argc != 5) {
            std::cerr << usage;
            return 1;
        }
        dirPath = args[1];
        outputPath = args[2];
        if (argc == 5) {
            if (std::string(args[3]) != "--dbd-dir") {
                std::cerr << usage;
                return 1;
            }
            dbdDir = args[4];
        }
    } else {
        if (argc != 2 && argc != 4) {
            std::cerr << usage;
            return 1;
        }
        inputPath = args[0];
        outputPath = args[1];
        if (argc == 4) {
            if (std::string(args[2]) != "--dbd-dir") {
                std::cerr << usage;
                return 1;
            }
            dbdDir = args[3];
        }
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
        size_t skippedOffsetMap = 0;
        for (const LoadedFile& lf : loaded) {
            size_t rowCount = writeFileTable(db, lf, availableTables);
            totalRows += rowCount;
            skippedEncrypted += lf.skippedEncrypted;
            skippedOffsetMap += lf.skippedOffsetMap;
            if (lf.dbdNames) ++namedTables;

            std::cout << "husk: db2-export: wrote " << rowCount << " row(s) to table \"" << lf.tableName
                       << "\" (" << (lf.dbdNames ? "real column names via WoWDBDefs" : "generic field_<N> column names")
                       << ")\n";
        }
        sqliteCheck(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr), db, "COMMIT");

        std::cout << "husk: db2-export: " << loaded.size() << " table(s), " << totalRows
                   << " total row(s), " << namedTables << " with real WoWDBDefs column names, written to '"
                   << outputPath << "'\n";
        if (skippedEncrypted > 0 || skippedOffsetMap > 0) {
            std::cout << "husk: db2-export: skipped " << skippedEncrypted << " encrypted and "
                       << skippedOffsetMap << " offset-map/sparse section(s) across all tables, not exported\n";
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

int db2Info(int argc, char** args) {
    if (argc >= 1 && isHelpFlag(args[0])) {
        printUsage(std::cout);
        return 0;
    }
    if (argc < 1 || argc > 3) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    size_t rowLimit = 5;
    if (argc == 3) {
        std::string flag = args[1];
        std::string value = args[2];
        if (flag != "--rows") {
            printUsage();
            return 1;
        }
        if (value == "all") {
            rowLimit = static_cast<size_t>(-1);
        } else {
            try {
                rowLimit = static_cast<size_t>(std::stoul(value));
            } catch (const std::exception&) {
                std::cerr << "husk: db2-info: --rows expects a non-negative integer or 'all', got '"
                          << value << "'\n";
                return 1;
            }
        }
    } else if (argc == 2) {
        printUsage();
        return 1;
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
