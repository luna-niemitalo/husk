#include "db2table.hpp"

#include <algorithm>

#include "db2.hpp"
#include "dbd.hpp"
#include "sources/db2_cache.hpp"

namespace husk::db2table {

namespace {

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

    const sources::ParsedDb2* parsed = sources::getParsedDb2(path, dbdDir, err);
    if (!parsed) return std::nullopt;
    const db2::File& file = parsed->file;
    const dbd::Layout& layout = *parsed->layout;
    const std::optional<std::vector<dbd::Column>>& inlineColumns = parsed->inlineColumns;

    std::optional<std::string> idFieldName = dbd::findIdFieldName(layout);
    std::vector<std::string> relationFieldNames = dbd::findNonInlineNonIdFieldNames(layout);

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
    for (const db2::Section& section : file.sections) {
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
                        row.emplace_back(db2::recordId(file, section, r));
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
                        row.emplace_back(std::nullopt);
                        break;
                }
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::optional<std::vector<StringColumnValues>> readNamedStringColumns(
    const std::string& path, const std::string& dbdDir, const std::vector<std::string>& columnNames,
    std::ostream& err) {
    if (dbdDir.empty()) {
        err << "husk: db2table: '" << path << "': --dbd-dir is required to resolve named columns\n";
        return std::nullopt;
    }

    const sources::ParsedDb2* parsed = sources::getParsedDb2(path, dbdDir, err);
    if (!parsed) return std::nullopt;
    const db2::File& file = parsed->file;
    const std::vector<uint8_t>& fileBytes = parsed->fileBytes;
    const std::optional<std::vector<dbd::Column>>& inlineColumns = parsed->inlineColumns;

    std::vector<std::optional<size_t>> fieldIndices;
    for (const std::string& name : columnNames) {
        std::optional<size_t> found;
        if (inlineColumns) {
            for (size_t i = 0; i < inlineColumns->size(); ++i) {
                if ((*inlineColumns)[i].name == name) {
                    found = i;
                    break;
                }
            }
        }
        if (!found) {
            err << "husk: db2table: '" << path << "': string column '" << name
                << "' not resolved as an inline field against this file's real layout\n";
        }
        fieldIndices.push_back(found);
    }
    if (std::all_of(fieldIndices.begin(), fieldIndices.end(),
                     [](const std::optional<size_t>& f) { return !f.has_value(); })) {
        return std::nullopt;
    }

    std::vector<StringColumnValues> rows;
    for (size_t sectionIndex = 0; sectionIndex < file.sections.size(); ++sectionIndex) {
        const db2::Section& section = file.sections[sectionIndex];
        if (!section.recordsAvailable()) continue;
        if (!section.offsetMap.empty()) continue;

        for (uint32_t r = 0; r < section.header.recordCount; ++r) {
            StringColumnValues row;
            row.reserve(fieldIndices.size());
            for (const std::optional<size_t>& fieldIndex : fieldIndices) {
                if (!fieldIndex) {
                    row.emplace_back(std::nullopt);
                    continue;
                }
                std::vector<uint64_t> values = db2::decodeField(file, section, r, *fieldIndex);
                bool isScalarNone =
                    values.size() == 1 && file.fieldStorageInfo[*fieldIndex].storageType == db2::FieldCompression::None;
                if (!isScalarNone) {
                    row.emplace_back(std::nullopt);
                    continue;
                }
                int64_t fieldAbsPos = static_cast<int64_t>(section.header.fileOffset) +
                                       static_cast<int64_t>(r) * file.header.recordSize +
                                       file.fieldStructures[*fieldIndex].position +
                                       db2::stringOffsetSectionCorrection(file, sectionIndex);
                std::optional<std::string> str;
                if (fieldAbsPos >= 0) {
                    str = db2::resolveFieldString(fileBytes, static_cast<size_t>(fieldAbsPos), values[0]);
                }
                row.push_back(std::move(str));
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

}  // namespace husk::db2table
