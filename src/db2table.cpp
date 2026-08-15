#include "db2table.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>

#include "db2.hpp"
#include "dbd.hpp"

namespace husk::db2table {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw db2::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return bytes;
}

enum class ColumnKind { Inline, Id, Relation, Unresolved };

struct ColumnResolution {
    ColumnKind kind = ColumnKind::Unresolved;
    size_t inlineFieldIndex = 0;
};

}  // namespace

std::optional<std::vector<ColumnValues>> readNamedColumns(const std::string& path,
                                                           const std::string& dbdDir,
                                                           const std::vector<std::string>& columnNames,
                                                           std::ostream& err) {
    if (dbdDir.empty()) {
        err << "husk: db2table: '" << path << "': --dbd-dir is required to resolve named columns\n";
        return std::nullopt;
    }

    db2::File file;
    try {
        std::vector<uint8_t> bytes = readFileBytes(path);
        file = db2::parse(bytes);
    } catch (const std::exception& e) {
        err << "husk: db2table: couldn't read '" << path << "': " << e.what() << "\n";
        return std::nullopt;
    }

    std::optional<dbd::Table> dbdTable = dbd::loadTableForHash(dbdDir, file.header.tableHash);
    if (!dbdTable) {
        err << "husk: db2table: no matching WoWDBDefs table for '" << path << "' (table_hash=0x"
            << std::hex << file.header.tableHash << std::dec << ")\n";
        return std::nullopt;
    }
    const dbd::Layout* layout = dbd::findLayout(*dbdTable, file.header.layoutHash);
    if (!layout) {
        err << "husk: db2table: no matching WoWDBDefs layout for '" << path << "' (layout_hash=0x"
            << std::hex << file.header.layoutHash << std::dec << ")\n";
        return std::nullopt;
    }

    std::optional<std::vector<dbd::Column>> inlineColumns =
        dbd::resolveFieldNames(*dbdTable, *layout, file.fieldStorageInfo);
    std::optional<std::string> idFieldName = dbd::findIdFieldName(*layout);
    std::vector<std::string> relationFieldNames = dbd::findNonInlineNonIdFieldNames(*layout);

    std::vector<ColumnResolution> resolutions;
    for (const std::string& name : columnNames) {
        if (idFieldName && *idFieldName == name) {
            resolutions.push_back({ColumnKind::Id, 0});
            continue;
        }
        if (std::find(relationFieldNames.begin(), relationFieldNames.end(), name) !=
            relationFieldNames.end()) {
            resolutions.push_back({ColumnKind::Relation, 0});
            continue;
        }
        bool found = false;
        if (inlineColumns) {
            for (size_t i = 0; i < inlineColumns->size(); ++i) {
                if ((*inlineColumns)[i].name == name) {
                    resolutions.push_back({ColumnKind::Inline, i});
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            err << "husk: db2table: '" << path << "': column '" << name
                << "' not resolved against this file's real layout\n";
            resolutions.push_back({ColumnKind::Unresolved, 0});
        }
    }
    if (std::all_of(resolutions.begin(), resolutions.end(),
                     [](const ColumnResolution& r) { return r.kind == ColumnKind::Unresolved; })) {
        return std::nullopt;
    }

    std::vector<ColumnValues> rows;
    for (db2::Section& section : file.sections) {
        if (!section.recordsAvailable()) continue;
        if (!section.offsetMap.empty()) continue;

        bool needsRelation = std::any_of(resolutions.begin(), resolutions.end(),
                                          [](const ColumnResolution& r) { return r.kind == ColumnKind::Relation; });
        std::vector<std::optional<uint32_t>> relationValues =
            needsRelation ? db2::nonInlineRelationValuesByRecord(file, section)
                          : std::vector<std::optional<uint32_t>>{};

        for (uint32_t r = 0; r < section.header.recordCount; ++r) {
            ColumnValues row;
            row.reserve(resolutions.size());
            for (const ColumnResolution& res : resolutions) {
                switch (res.kind) {
                    case ColumnKind::Id:
                        row.push_back(db2::recordId(file, section, r));
                        break;
                    case ColumnKind::Relation:
                        row.push_back(r < relationValues.size() ? relationValues[r] : std::nullopt);
                        break;
                    case ColumnKind::Inline: {
                        std::vector<uint64_t> values = db2::decodeField(file, section, r, res.inlineFieldIndex);
                        row.push_back(values.empty() ? std::nullopt
                                                      : std::optional<uint32_t>(static_cast<uint32_t>(values[0])));
                        break;
                    }
                    case ColumnKind::Unresolved:
                        row.push_back(std::nullopt);
                        break;
                }
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

}  // namespace husk::db2table
