#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// A generic, real-column-name-driven row reader built on top of db2.hpp's
// name-agnostic WDC5 parser and dbd.hpp's WoWDBDefs name resolution --
// closes the gap `cmd_db2.cpp`'s own per-table logic used to reimplement
// per command, giving every DB2 consumer a real per-table C++ struct
// instead of bespoke decode code per table.
//
// Handles all three of WDC5's real column storage shapes transparently, by
// real column name rather than raw field index:
//   - a normal inline field (dbd::resolveFieldNames' by-position result)
//   - the file's own non-inline ID (dbd::findIdFieldName / db2::recordId)
//   - a non-inline relation column (dbd::findNonInlineNonIdFieldNames /
//     db2::nonInlineRelationValuesByRecord)
//
// Scope: readNamedColumns/readNamedStringColumns are scalar integer/
// relation/string columns only (every column those were originally built
// for -- ChrModelMaterial/CharComponentTextureSections/
// ChrModelTextureLayer/CharComponentTextureLayouts -- is a plain scalar
// int). readNamedArrayColumns (below) closes the one real gap that scope
// note used to flag: a genuine multi-element WDC5 array field (e.g.
// ChrCustomizationChoice.SwatchColor<32>[2]) -- see its own doc comment.
// --dbd-dir is required, not optional, for all three, since there's no
// way to know which raw field index a semantic column name corresponds to
// without it.
namespace husk::db2table {

// One row's value for one requested column -- nullopt when this specific
// row's value couldn't be resolved (e.g. no relationship_map entry for it),
// never fabricated.
using ColumnValues = std::vector<std::optional<uint32_t>>;

// Reads `path` (a real WDC5 .db2 file) and extracts each of `columnNames`,
// in "section order, then record order" (matching husk db2-export's own row
// order) -- one ColumnValues per real, decodable row (encrypted and
// offset-map/sparse sections are skipped, same as db2-export). A column
// name that can't be resolved against this file's own real WoWDBDefs
// layout is reported to `err` and comes back all-nullopt for every row,
// rather than failing the whole read -- a partial result (some columns
// resolved, others not) is still useful. Returns nullopt only when the
// file itself can't be read/parsed, `dbdDir` is empty, or its table/layout
// can't be resolved at all (nothing to offer).
std::optional<std::vector<ColumnValues>> readNamedColumns(const std::string& path,
                                                           const std::string& dbdDir,
                                                           const std::vector<std::string>& columnNames,
                                                           std::ostream& err);

// One row's value for one requested *string* column -- nullopt when this
// row's field didn't resolve to a plausible string (db2::resolveFieldString's
// own heuristic, including its `rawValue == 0` "no string" sentinel case).
using StringColumnValues = std::vector<std::optional<std::string>>;

// Same named-column resolution as readNamedColumns, for inline string
// fields (e.g. `Name_lang`) instead of scalar integers -- resolved via
// db2::resolveFieldString, correcting for multi-section files via
// db2::stringOffsetSectionCorrection (see both for the real string-table
// layout this depends on). Fixed-width sections only, same as
// readNamedColumns -- an offset-map/sparse section is skipped, not
// guessed at (no real caller of this function has hit one yet). A
// requested column that isn't a real inline field at all (the file's own
// ID column, a non-inline relation column, or simply unresolved) comes
// back all-nullopt for that column, same "partial result over total
// failure" convention as readNamedColumns.
std::optional<std::vector<StringColumnValues>> readNamedStringColumns(
    const std::string& path, const std::string& dbdDir, const std::vector<std::string>& columnNames,
    std::ostream& err);

// One row's value for one requested *array* column -- nullopt when this
// row's field couldn't be resolved, matching readNamedColumns' own
// "partial result over total failure" convention.
using ArrayColumnValues = std::vector<std::optional<std::vector<uint32_t>>>;

// Same named-column resolution as readNamedColumns (inline field / file ID /
// non-inline relation column, all three), except every element a resolved
// field actually carries is kept, not just the first -- closing the gap
// this file's own top comment used to flag: a real WDC5 array field
// (WoWDBDefs' "ColName<Size>[Length]" annotation with Length > 1, e.g.
// ChrCustomizationChoice's real `SwatchColor<32>[2]`) was previously
// flattened down to one scalar by readNamedColumns, silently dropping
// every element past the first.
//
// The ID and non-inline-relation cases are still always exactly one
// element -- a row's own ID or a non-inline relation value is never an
// array in this codebase's real data (confirmed against every real
// WoWDBDefs layout this project has needed so far) -- so both still come
// back as a genuine 1-element vector, same shape as any scalar inline
// field. The real work is only in the inline-field case, and it needs no
// extra bookkeeping to do: db2::decodeField already returns one element
// per real array slot for storage type None (DB2.md's "Further Quirks"
// array-length formula, cross-validated at resolution time by
// dbd::resolveFieldNames' own field_size_bits check -- see its doc
// comment) and BitpackedIndexedArray (info.arrayCount elements), and
// exactly one element for every other real storage type -- so a plain
// "keep every element decodeField hands back" is already correct for
// arrayLength == 1 columns too, without this function ever having to
// consult dbd::Field::arrayLength itself to decide scalar vs. array.
//
// Same failure contract as readNamedColumns: nullopt only when the file
// itself can't be read/parsed, `dbdDir` is empty, or nothing at all
// resolved; a single unresolvable column name is reported to `err` and
// comes back all-nullopt for that column, not a hard failure.
std::optional<std::vector<ArrayColumnValues>> readNamedArrayColumns(const std::string& path,
                                                                     const std::string& dbdDir,
                                                                     const std::vector<std::string>& columnNames,
                                                                     std::ostream& err);

}  // namespace husk::db2table
