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

// Shared by readNamedColumns and readNamedArrayColumns -- both resolve
// `columnNames` against the exact same three real WDC5 column shapes
// (inline field / non-inline id / non-inline relation), only what they do
// with the resolved value differs (first element only vs. every element).
std::vector<ColumnResolution> resolveColumns(const std::string& path, const dbd::Layout& layout,
                                              const std::optional<std::vector<dbd::Column>>& inlineColumns,
                                              const std::vector<std::string>& columnNames, std::ostream& err) {
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
    return resolutions;
}

// The file's rows, copy-table rows included (db2::tableRows), with any copy
// that can't be read said on `err` rather than dropped silently.
std::vector<db2::RowRef> readableRows(const std::string& path, const db2::File& file, std::ostream& err) {
    db2::TableRows table = db2::tableRows(file);
    if (table.unreadableCopies > 0) {
        err << "husk: db2table: '" << path << "': " << table.unreadableCopies
            << " copy-table row(s) copy a record in an unreadable section -- not read\n";
    }
    return std::move(table.rows);
}

// A `$noninline,relation$` column's value per row: the relationship map is
// keyed by record, so a copy row reads its source record's entry.
class RelationCache {
public:
    RelationCache(const db2::File& file, const std::vector<ColumnResolution>& resolutions)
        : file_(file),
          needed_(std::any_of(resolutions.begin(), resolutions.end(),
                              [](const ColumnResolution& r) { return r.kind == ColumnKind::Relation; })),
          bySection_(file.sections.size()) {}

    std::optional<uint32_t> value(const db2::RowRef& ref) {
        if (!needed_) return std::nullopt;
        std::optional<std::vector<std::optional<uint32_t>>>& values = bySection_[ref.section];
        if (!values) values = db2::nonInlineRelationValuesByRecord(file_, file_.sections[ref.section]);
        return ref.record < values->size() ? (*values)[ref.record] : std::nullopt;
    }

private:
    const db2::File& file_;
    bool needed_;
    std::vector<std::optional<std::vector<std::optional<uint32_t>>>> bySection_;
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

    std::vector<ColumnResolution> resolutions = resolveColumns(path, layout, inlineColumns, columnNames, err);
    if (std::all_of(resolutions.begin(), resolutions.end(),
                     [](const ColumnResolution& r) { return r.kind == ColumnKind::Unresolved; })) {
        return std::nullopt;
    }

    RelationCache relations(file, resolutions);
    std::vector<ColumnValues> rows;
    for (const db2::RowRef& ref : readableRows(path, file, err)) {
        const db2::Section& section = file.sections[ref.section];
        ColumnValues row;
        row.reserve(resolutions.size());
        for (const ColumnResolution& res : resolutions) {
            switch (res.kind) {
                case ColumnKind::Id:
                    row.emplace_back(ref.id);
                    break;
                case ColumnKind::Relation:
                    row.push_back(relations.value(ref));
                    break;
                case ColumnKind::Inline: {
                    std::vector<uint64_t> values = db2::decodeField(file, section, ref.record, res.inlineFieldIndex);
                    const dbd::Column& column = (*inlineColumns)[res.inlineFieldIndex];
                    row.push_back(values.empty() ? std::nullopt
                                                 : std::optional<uint32_t>(static_cast<uint32_t>(
                                                       dbd::asDeclaredType(column, values[0]))));
                    break;
                }
                case ColumnKind::Unresolved:
                    row.emplace_back(std::nullopt);
                    break;
            }
        }
        rows.push_back(std::move(row));
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
    for (const db2::RowRef& ref : readableRows(path, file, err)) {
        const db2::Section& section = file.sections[ref.section];
        StringColumnValues row;
        row.reserve(fieldIndices.size());
        for (const std::optional<size_t>& fieldIndex : fieldIndices) {
            if (!fieldIndex) {
                row.emplace_back(std::nullopt);
                continue;
            }
            std::vector<uint64_t> values = db2::decodeField(file, section, ref.record, *fieldIndex);
            bool isScalarNone =
                values.size() == 1 && file.fieldStorageInfo[*fieldIndex].storageType == db2::FieldCompression::None;
            if (!isScalarNone) {
                row.emplace_back(std::nullopt);
                continue;
            }
            // A string offset is relative to its own field's position, so a
            // copy row resolves through its source record.
            int64_t fieldAbsPos = static_cast<int64_t>(section.header.fileOffset) +
                                   static_cast<int64_t>(ref.record) * file.header.recordSize +
                                   file.fieldStructures[*fieldIndex].position +
                                   db2::stringOffsetSectionCorrection(file, ref.section);
            std::optional<std::string> str;
            if (fieldAbsPos >= 0) {
                str = db2::resolveFieldString(fileBytes, static_cast<size_t>(fieldAbsPos), values[0]);
            }
            row.push_back(std::move(str));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

std::optional<std::vector<ArrayColumnValues>> readNamedArrayColumns(const std::string& path,
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

    std::vector<ColumnResolution> resolutions = resolveColumns(path, layout, inlineColumns, columnNames, err);
    if (std::all_of(resolutions.begin(), resolutions.end(),
                     [](const ColumnResolution& r) { return r.kind == ColumnKind::Unresolved; })) {
        return std::nullopt;
    }

    RelationCache relations(file, resolutions);
    std::vector<ArrayColumnValues> rows;
    for (const db2::RowRef& ref : readableRows(path, file, err)) {
        const db2::Section& section = file.sections[ref.section];
        ArrayColumnValues row;
        row.reserve(resolutions.size());
        for (const ColumnResolution& res : resolutions) {
            switch (res.kind) {
                case ColumnKind::Id:
                    row.push_back(std::vector<uint32_t>{ref.id});
                    break;
                case ColumnKind::Relation: {
                    std::optional<uint32_t> v = relations.value(ref);
                    row.push_back(v ? std::optional<std::vector<uint32_t>>(std::vector<uint32_t>{*v}) : std::nullopt);
                    break;
                }
                case ColumnKind::Inline: {
                    std::vector<uint64_t> values = db2::decodeField(file, section, ref.record, res.inlineFieldIndex);
                    if (values.empty()) {
                        row.push_back(std::nullopt);
                        break;
                    }
                    const dbd::Column& column = (*inlineColumns)[res.inlineFieldIndex];
                    std::vector<uint32_t> elements;
                    elements.reserve(values.size());
                    for (uint64_t v : values) elements.push_back(static_cast<uint32_t>(dbd::asDeclaredType(column, v)));
                    row.push_back(std::move(elements));
                    break;
                }
                case ColumnKind::Unresolved:
                    row.emplace_back(std::nullopt);
                    break;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace husk::db2table
