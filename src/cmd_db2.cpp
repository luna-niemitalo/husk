#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "commands.hpp"
#include "db2.hpp"
#include "dbd.hpp"
#include "listfile.hpp"

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

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk db2-info <file.db2> [--rows N]\n"
           "\n"
           "Parses a WDC5 DB2 file and prints its header, per-section layout,\n"
           "and per-field storage info. With --rows (default 5, 0 for none,\n"
           "'all' for every record), also dumps that many decoded rows from\n"
           "the first non-encrypted section (fixed-width or offset-map/sparse,\n"
           "both real per-field decode now) -- raw values per field, with a\n"
           "best-effort string heuristic (see db2.hpp's resolveFieldString/\n"
           "decodeOffsetMapRecord).\n"
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

// A WDC2+ string offset is relative to the field's own position in a
// *virtual* blob the client assembles at load time: every section's record
// data back to back, followed by every section's string block back to back
// (DB2.md's "String Block" section, WDC2 subsection -- verified against
// real multi-section data below). A single-section file's record data and
// string block are already contiguous in the real file exactly as in that
// virtual blob, so `fieldAbsPos + rawValue` (this file's original formula)
// lands correctly with no correction needed -- but a file with more than
// one section needs the gap between "this section's own string block" and
// "the virtual blob's string region" bridged explicitly:
//   - every section's record data *after* this one is skipped in the real
//     file layout (it sits between this section's records and this
//     section's own string block) but not in the virtual blob, so it must
//     be subtracted back out;
//   - every section's string block *before* this one is real file content
//     that sits earlier than this section's own string block, so it must
//     be added in.
// Verified against real local data (test_data/db2/chrcustomizationcategory.db2,
// 2 sections, second one TACT-key-encrypted): without this correction,
// row 0's CategoryName_lang decoded as "cessories" (2 bytes into
// "Accessories", not NUL-preceded -- provably wrong); with it, row 0
// decodes as "Body" and row 1 as "Face", both NUL-preceded and both
// exactly the sequential real category list TODO/TODO_correctness.md #4
// was written against.
int64_t stringOffsetSectionCorrection(const db2::File& file, size_t sectionIndex) {
    int64_t correction = 0;
    for (size_t i = 0; i < file.sections.size(); ++i) {
        const db2::Section& s = file.sections[i];
        int64_t recordDataSize = s.hasOffsetMap()
                                      ? static_cast<int64_t>(s.header.offsetRecordsEnd) -
                                            static_cast<int64_t>(s.header.fileOffset)
                                      : static_cast<int64_t>(s.header.recordCount) * file.header.recordSize;
        if (i > sectionIndex) correction -= recordDataSize;
        if (i < sectionIndex) correction += static_cast<int64_t>(s.header.stringTableSize);
    }
    return correction;
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
// stringOffsetSectionCorrection).
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
                                   file.fieldStructures[f].position + stringOffsetSectionCorrection(file, sectionIndex);
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
            uint32_t bits = static_cast<uint32_t>(values.raw[0]);
            std::memcpy(&f, &bits, sizeof(f));
            sqlite3_bind_double(stmt, bindIndex, static_cast<double>(f));
            continue;
        }
        sqlite3_bind_int64(stmt, bindIndex, static_cast<sqlite3_int64>(values.raw[i]));
    }
}

// A `$noninline,relation$` DBD field (e.g. real ChrModelTextureLayer's
// CharComponentTextureLayoutsID under some layouts) -- occupies no WDC5
// field-array slot at all, its real per-record value lives only in the
// section's own relationship_map (db2::nonInlineRelationValuesByRecord).
// `relation` is looked up from the DBD table's own COLUMNS block by name,
// same as an ordinary inline relation column's FK target.
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
        if (lf.dbdNames && plan[f].size() == 1 && (*lf.dbdNames)[f].relation) {
            const dbd::RelationTarget& rel = (*lf.dbdNames)[f].relation.value();
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

}  // namespace

int db2Export(int argc, char** args) {
    static const char* usage =
        "usage: husk db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]\n"
        "       husk db2-export --dir <db2-dir> <out.sqlite> [--dbd-dir DIR]\n"
        "\n"
        "Converts one WDC5 DB2 file, or (with --dir) every *.db2 file in a\n"
        "directory, to a real SQLite database -- one table per file, named\n"
        "from the DBD table name if resolved, else the input file's own\n"
        "basename. Every unencrypted section's records are exported, fixed-\n"
        "width or offset-map/sparse alike (see db2.hpp's module comment for\n"
        "the offset-map decode's own real-data caveats); only TACT-encrypted\n"
        "sections are skipped, and a count of skipped sections is printed,\n"
        "never silently dropped. In --dir mode, a file that can't be parsed\n"
        "or has nothing exportable is skipped (with a diagnostic), not\n"
        "treated as a fatal error for the whole batch.\n"
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

int db2Build(int argc, char** args) {
    static const char* usage =
        "usage: husk db2-build --db2-dir <dir> --dbd-dir <dir> --listfile <path> -o <out.sqlite>\n"
        "\n"
        "Builds husk's own verified knowledge-base SQLite database from a real,\n"
        "local --db2-dir plus a --listfile snapshot -- TODO/KNOWLEDGE_BASE_DESIGN.md.\n"
        "Ingests the DB2 tables today's resolved joins need (ModelFileData/\n"
        "ItemDisplayInfo/TextureFileData) via the same db2-export machinery, a\n"
        "'models' table (every .m2 FileDataID -> real path from --listfile), a\n"
        "'textures' table (every .blp/.png FileDataID -> real path, same source),\n"
        "and one resolved join table -- model_object_skin_texture (model FileDataID\n"
        "-> texture FileDataID via ModelFileData -> ItemDisplayInfo ->\n"
        "TextureFileData). A '_meta' table stamps the source .db2 files' own\n"
        "size+mtime so a consumer can tell when the knowledge base is stale\n"
        "relative to --db2-dir's current contents (`husk export --knowledge-db`).\n"
        "Local-only, rebuilt on demand -- never fetched, never committed.\n";

    if (argc >= 1 && isHelpFlag(args[0])) {
        std::cout << usage;
        return 0;
    }

    std::string db2Dir, dbdDir, listfilePath, outputPath;
    for (int i = 0; i < argc; ++i) {
        std::string a = args[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("husk: db2-build: '" + a + "' needs a value");
            return args[++i];
        };
        if (a == "--db2-dir") db2Dir = next();
        else if (a == "--dbd-dir") dbdDir = next();
        else if (a == "--listfile") listfilePath = next();
        else if (a == "-o" || a == "--output") outputPath = next();
        else {
            std::cerr << usage;
            return 1;
        }
    }
    if (db2Dir.empty() || dbdDir.empty() || listfilePath.empty() || outputPath.empty()) {
        std::cerr << usage;
        return 1;
    }

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
