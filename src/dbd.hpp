#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// WoWDBDefs (github.com/wowdev/WoWDBDefs) .dbd text-format parser -- resolves
// real column names/types for a WDC5 DB2 file, closing the "no table-name-
// to-struct mapping" gap db2.hpp's own module comment documents. Grammar
// per WoWDBDefs' own README.md (a plain, documented text format, no
// reverse-engineering needed here, unlike most of this project's other
// formats).
//
// Optional, local-only, never a hard dependency: this parser only ever
// reads whatever directory --dbd-dir points at (expected layout:
// <dir>/manifest.json + <dir>/definitions/<TableName>.dbd -- i.e. a plain
// WoWDBDefs checkout), the same "local filesystem, user-populated, never
// fetched or bundled" pattern --textures/--skin-dir/--bones-dir already
// use. husk never clones, fetches, or ships WoWDBDefs itself, and this
// file's own logic is an independent implementation of the documented
// grammar -- no WoWDBDefs code (C#/Python) is linked or vendored.
// reference/WoWDBDefs (gitignored) is a dev-only investigation checkout for
// humans/tests, never read by this code at runtime.
//
// Scope, deliberately: fields are matched to a real LAYOUT block purely by
// *position* (declaration order, skipping `noninline` fields -- those occupy
// no slot in WDC5's own field array) -- not cross-validated against
// db2::FieldStorageInfo's own bit sizes. A mismatch between the .dbd's
// claimed inline field count and the real file's actual field count means a
// lookup simply fails (nullopt), never a guessed/misaligned name.
// manifest.json parsing is a narrow hand-rolled scan (brace-depth object
// splitting + two regex field extractions), not a real JSON parser --
// sufficient because the file is a flat array of flat objects with no
// nesting, checked against the real file, same "no new dependency for a
// narrow, well-understood need" policy blp/'s synthetic-DDS-wrapper
// approach already uses.
namespace husk::dbd {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class ColumnType { Int, Float, String, LocString };

// One field as declared inside a LAYOUT block -- `nonInline` fields (id or
// relation columns stored outside the record's own field array) don't
// occupy a WDC5 field-array slot and must be skipped when position-matching
// against db2::File's fields.
struct Field {
    std::string name;
    bool nonInline = false;
};

// A LAYOUT block: one or more layout hashes (WDC5's own header.layoutHash),
// all sharing the same field list.
struct Layout {
    std::vector<uint32_t> hashes;
    std::vector<Field> fields;
};

struct Table {
    std::string tableName;
    std::vector<std::pair<std::string, ColumnType>> columns;  // COLUMNS block, declaration order
    std::vector<Layout> layouts;
};

// Parses one already-read .dbd file's text content. `tableName` is supplied
// by the caller (the .dbd's own filename minus extension), not derived from
// the text -- .dbd files don't self-name.
Table parseDbd(const std::string& text, std::string tableName);

// Looks up manifest.json's tableHash -> tableName mapping under `dbdDir`,
// then loads and parses <dbdDir>/definitions/<tableName>.dbd. Returns
// nullopt (never throws) if `dbdDir` doesn't exist, manifest.json has no
// entry for this hash, or the corresponding .dbd file isn't present --
// every one of those is a normal "no naming data available" outcome, not an
// error condition.
std::optional<Table> loadTableForHash(const std::string& dbdDir, uint32_t tableHash);

// Finds the Layout whose hash list contains layoutHash, if any.
const Layout* findLayout(const Table& table, uint32_t layoutHash);

// Returns real (name, type) pairs for each of a file's inline fields, in
// order, if `layout`'s inline (non-`nonInline`) field count matches
// `fieldCount` exactly -- nullopt on any mismatch, never a partial/guessed
// result.
std::optional<std::vector<std::pair<std::string, ColumnType>>> resolveFieldNames(
    const Table& table, const Layout& layout, size_t fieldCount);

}  // namespace husk::dbd
