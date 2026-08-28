#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "../db2.hpp"
#include "../dbd.hpp"

// First slice of REFACTOR/RESOURCE_CATALOG.md's stage-2 catalog: the DB2
// table cache called out in REFACTOR/AUDIT.md #1.3 as a "free" fix (no
// semantic change, just reading each file once). Nothing about *which*
// table answers a resolution question moves here yet -- that's the rest of
// stage 1/2, not started. This is purely "don't parse the same .db2 bytes
// and re-resolve the same WoWDBDefs layout twice within one process".
//
// db2table.cpp's readNamedColumns/readNamedStringColumns each independently
// read the file, ran db2::parse, and re-resolved the WoWDBDefs table/layout/
// inline-column names before this existed -- same file, same dbdDir, twice,
// every time both were called on one table (chrraces.db2,
// chrcustomizationoption.db2, etc. -- AUDIT.md #1.3's own list). This cache
// makes that resolution happen once per (path, dbdDir) pair per process and
// hands back a reference to the cached result.
namespace husk::sources {

struct ParsedDb2 {
    std::vector<uint8_t> fileBytes;  // raw bytes; db2::resolveFieldString needs these separately from `file`
    db2::File file;
    std::optional<dbd::Table> dbdTable;
    const dbd::Layout* layout = nullptr;  // points into dbdTable above; null if no layout matched
    std::optional<std::vector<dbd::Column>> inlineColumns;
};

// Returns the cached parse/resolve for (path, dbdDir), doing the real work
// only on first request. Returns nullptr on any failure (file unreadable,
// parse error, no matching WoWDBDefs table) -- same "report to err, return
// an empty answer" convention db2table.cpp's own functions already use;
// the failure itself is not cached, so a transient error (e.g. dbdDir not
// mounted yet) can succeed on a later call within the same process.
const ParsedDb2* getParsedDb2(const std::string& path, const std::string& dbdDir, std::ostream& err);

// Test-only: drops every cached entry so tests don't leak state into each
// other via process-lifetime caching.
void clearDb2CacheForTests();

}  // namespace husk::sources
