#include "db2_cache.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <utility>

namespace husk::sources {

namespace {

std::vector<uint8_t> readFileBytes(const std::string& path) {
    errno = 0;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw db2::ParseError("couldn't open '" + path + "' for reading: " + std::strerror(errno));
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

using CacheKey = std::pair<std::string, std::string>;  // (path, dbdDir)

// std::map so a ParsedDb2's own `layout` pointer (into `dbdTable`, which
// lives in the same node) stays valid across further insertions -- a
// std::vector/unordered_map with a vector bucket could reallocate/rehash
// and dangle it.
std::map<CacheKey, ParsedDb2>& cache() {
    static std::map<CacheKey, ParsedDb2> instance;
    return instance;
}

}  // namespace

const ParsedDb2* getParsedDb2(const std::string& path, const std::string& dbdDir, std::ostream& err) {
    CacheKey key{path, dbdDir};
    auto it = cache().find(key);
    if (it != cache().end()) return &it->second;

    ParsedDb2 parsed;
    try {
        parsed.fileBytes = readFileBytes(path);
        parsed.file = db2::parse(parsed.fileBytes);
    } catch (const std::exception& e) {
        err << "husk: db2 cache: couldn't read '" << path << "': " << e.what() << "\n";
        return nullptr;
    }

    parsed.dbdTable = dbd::loadTableForHash(dbdDir, parsed.file.header.tableHash);
    if (!parsed.dbdTable) {
        err << "husk: db2 cache: no matching WoWDBDefs table for '" << path << "' (table_hash=0x" << std::hex
            << parsed.file.header.tableHash << std::dec << ")\n";
        return nullptr;
    }

    auto [insertedIt, inserted] = cache().emplace(std::move(key), std::move(parsed));
    ParsedDb2& stored = insertedIt->second;
    stored.layout = dbd::findLayout(*stored.dbdTable, stored.file.header.layoutHash);
    if (!stored.layout) {
        err << "husk: db2 cache: no matching WoWDBDefs layout for '" << path << "' (layout_hash=0x" << std::hex
            << stored.file.header.layoutHash << std::dec << ")\n";
        cache().erase(insertedIt);
        return nullptr;
    }
    stored.inlineColumns = dbd::resolveFieldNames(*stored.dbdTable, *stored.layout, stored.file.fieldStorageInfo);
    return &stored;
}

void clearDb2CacheForTests() { cache().clear(); }

}  // namespace husk::sources
