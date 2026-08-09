#include "dbd.hpp"

#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>

namespace husk::dbd {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

std::string stripComment(const std::string& line) {
    size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

ColumnType parseBaseType(const std::string& base) {
    if (base == "int") return ColumnType::Int;
    if (base == "float") return ColumnType::Float;
    if (base == "string") return ColumnType::String;
    if (base == "locstring") return ColumnType::LocString;
    throw ParseError("dbd: unknown column type '" + base + "'");
}

// A foreign-key column looks like "int<Table::Col>" -- the base type is the
// part before '<', the relation target (if any) is "Table::Col" inside the
// angle brackets.
std::pair<ColumnType, std::optional<RelationTarget>> parseColumnType(const std::string& token) {
    size_t open = token.find('<');
    ColumnType type = parseBaseType(token.substr(0, open));
    if (open == std::string::npos) return {type, std::nullopt};
    size_t close = token.find('>', open);
    if (close == std::string::npos) {
        throw ParseError("dbd: unterminated '<...>' relation target in '" + token + "'");
    }
    std::string inner = token.substr(open + 1, close - open - 1);
    size_t sep = inner.find("::");
    if (sep == std::string::npos) return {type, std::nullopt};  // e.g. array-length markers, no "::"
    RelationTarget rel;
    rel.targetTable = inner.substr(0, sep);
    rel.targetColumn = inner.substr(sep + 2);
    return {type, rel};
}

std::vector<uint32_t> parseHashList(const std::string& rest) {
    std::vector<uint32_t> hashes;
    std::istringstream iss(rest);
    std::string tok;
    while (std::getline(iss, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) continue;
        hashes.push_back(static_cast<uint32_t>(std::stoul(tok, nullptr, 16)));
    }
    return hashes;
}

// One field line inside a LAYOUT block, e.g. "$noninline,id$ID<32>",
// "$relation$CharComponentTextureLayoutsID<32>", "TextureType<32>",
// "Name[3]", "Name<32>[3] // comment". The annotations/name were always
// needed for naming; <Size>/[Length] are parsed too now (declaredBits/
// arrayLength), needed by resolveFieldNames' field_storage_info
// cross-check -- see WoWDBDefs README.md's "Columns" section for the
// grammar ("Size (8, 16, 32 or 64, prefixed by u if unsigned int):
// ColName<Size>", "Array: ColName[Length]", "Both: ColName<Size>[Length]").
Field parseFieldLine(std::string line) {
    line = trim(stripComment(line));
    Field field;
    while (!line.empty() && line[0] == '$') {
        size_t close = line.find('$', 1);
        if (close == std::string::npos) {
            throw ParseError("dbd: unterminated '$...$' annotation in '" + line + "'");
        }
        std::string annotations = line.substr(1, close - 1);
        std::istringstream annStream(annotations);
        std::string tag;
        while (std::getline(annStream, tag, ',')) {
            if (tag == "noninline") field.nonInline = true;
            if (tag == "id") field.isId = true;
        }
        line = line.substr(close + 1);
    }
    size_t end = line.find_first_of("<[");
    field.name = trim(end == std::string::npos ? line : line.substr(0, end));
    std::string rest = end == std::string::npos ? "" : line.substr(end);

    if (!rest.empty() && rest[0] == '<') {
        size_t close = rest.find('>');
        if (close == std::string::npos) {
            throw ParseError("dbd: unterminated '<...>' size annotation in '" + line + "'");
        }
        std::string sizeToken = rest.substr(1, close - 1);
        // "prefixed by u if unsigned int" (README.md) -- signedness isn't
        // needed for a bit-width check, only the numeric width itself.
        if (!sizeToken.empty() && (sizeToken[0] == 'u' || sizeToken[0] == 'U')) {
            sizeToken = sizeToken.substr(1);
        }
        try {
            field.declaredBits = static_cast<uint32_t>(std::stoul(sizeToken));
        } catch (const std::exception&) {
            throw ParseError("dbd: non-numeric '<" + sizeToken + ">' size annotation in '" + line + "'");
        }
        rest = rest.substr(close + 1);
    }
    if (!rest.empty() && rest[0] == '[') {
        size_t close = rest.find(']');
        if (close == std::string::npos) {
            throw ParseError("dbd: unterminated '[...]' array annotation in '" + line + "'");
        }
        std::string lenToken = rest.substr(1, close - 1);
        try {
            field.arrayLength = static_cast<uint32_t>(std::stoul(lenToken));
        } catch (const std::exception&) {
            throw ParseError("dbd: non-numeric '[" + lenToken + "]' array annotation in '" + line + "'");
        }
    }
    return field;
}

// The per-element bit width a .dbd column's declared type implies, per
// WoWDBDefs README.md's "Columns" grammar: floats/(loc)strings never carry
// a <Size> annotation because they're always one fixed native width -- a
// raw 32-bit float, or a 32-bit string-table offset -- so their width is
// implied by the type alone, not read from `declaredBits`. Only `int`
// columns declare an explicit size; nullopt propagates through when a real
// .dbd layout line has no size at all (shouldn't happen for a real int
// field per the grammar, but if it does, there's nothing to check against,
// not a guessed 32).
std::optional<uint32_t> expectedElementBits(ColumnType type, const std::optional<uint32_t>& declaredBits) {
    switch (type) {
        case ColumnType::Float:
        case ColumnType::String:
        case ColumnType::LocString:
            return 32;
        case ColumnType::Int:
            return declaredBits;
    }
    return std::nullopt;
}

}  // namespace

Table parseDbd(const std::string& text, std::string tableName) {
    Table table;
    table.tableName = std::move(tableName);

    std::istringstream stream(text);
    std::string rawLine;
    bool inColumns = false;
    std::optional<Layout> currentLayout;

    auto flushLayout = [&]() {
        if (currentLayout && !currentLayout->hashes.empty()) {
            table.layouts.push_back(std::move(*currentLayout));
        }
        currentLayout.reset();
    };

    while (std::getline(stream, rawLine)) {
        std::string trimmed = trim(rawLine);
        if (trimmed.empty()) {
            inColumns = false;
            flushLayout();
            continue;
        }
        if (trimmed == "COLUMNS") {
            inColumns = true;
            continue;
        }
        if (inColumns) {
            std::string noComment = trim(stripComment(trimmed));
            if (noComment.empty()) continue;
            size_t sp = noComment.find(' ');
            if (sp == std::string::npos) continue;  // malformed column line, skip rather than throw
            std::string typeToken = noComment.substr(0, sp);
            std::string name = trim(noComment.substr(sp + 1));
            if (!name.empty() && name.back() == '?') name.pop_back();
            auto [type, relation] = parseColumnType(typeToken);
            table.columns.push_back({name, type, relation});
            continue;
        }
        if (trimmed.rfind("LAYOUT ", 0) == 0) {
            if (!currentLayout) currentLayout.emplace();
            std::vector<uint32_t> hashes = parseHashList(trimmed.substr(7));
            currentLayout->hashes.insert(currentLayout->hashes.end(), hashes.begin(), hashes.end());
            continue;
        }
        if (trimmed.rfind("BUILD", 0) == 0 || trimmed.rfind("COMMENT", 0) == 0) {
            continue;  // version-range/human-comment metadata, not needed for name resolution
        }
        if (!currentLayout) continue;  // stray line before any LAYOUT block -- ignore, don't throw
        currentLayout->fields.push_back(parseFieldLine(trimmed));
    }
    flushLayout();
    return table;
}

std::optional<Table> loadTableForHash(const std::string& dbdDir, uint32_t tableHash) {
    std::ifstream manifestFile(dbdDir + "/manifest.json");
    if (!manifestFile) return std::nullopt;
    std::string manifestText((std::istreambuf_iterator<char>(manifestFile)),
                              std::istreambuf_iterator<char>());

    static const std::regex kNameRe(R"re("tableName"\s*:\s*"([^"]*)")re");
    static const std::regex kHashRe(R"re("tableHash"\s*:\s*"([^"]*)")re");

    std::string foundName;
    size_t depth = 0;
    size_t objStart = 0;
    for (size_t i = 0; i < manifestText.size() && foundName.empty(); ++i) {
        char c = manifestText[i];
        if (c == '{') {
            if (depth == 0) objStart = i;
            ++depth;
        } else if (c == '}') {
            if (depth == 0) continue;
            --depth;
            if (depth != 0) continue;
            std::string obj = manifestText.substr(objStart, i - objStart + 1);
            std::smatch hashMatch;
            if (!std::regex_search(obj, hashMatch, kHashRe)) continue;
            uint32_t hash = static_cast<uint32_t>(std::stoul(hashMatch[1].str(), nullptr, 16));
            if (hash != tableHash) continue;
            std::smatch nameMatch;
            if (std::regex_search(obj, nameMatch, kNameRe)) {
                foundName = nameMatch[1].str();
            }
        }
    }
    if (foundName.empty()) return std::nullopt;

    std::ifstream dbdFile(dbdDir + "/definitions/" + foundName + ".dbd");
    if (!dbdFile) return std::nullopt;
    std::string dbdText((std::istreambuf_iterator<char>(dbdFile)), std::istreambuf_iterator<char>());
    return parseDbd(dbdText, foundName);
}

const Layout* findLayout(const Table& table, uint32_t layoutHash) {
    for (const Layout& layout : table.layouts) {
        for (uint32_t h : layout.hashes) {
            if (h == layoutHash) return &layout;
        }
    }
    return nullptr;
}

std::optional<std::string> findIdFieldName(const Layout& layout) {
    for (const Field& f : layout.fields) {
        if (f.isId && f.nonInline) return f.name;
    }
    return std::nullopt;
}

std::vector<std::string> findNonInlineNonIdFieldNames(const Layout& layout) {
    std::vector<std::string> names;
    for (const Field& f : layout.fields) {
        if (f.nonInline && !f.isId) names.push_back(f.name);
    }
    return names;
}

std::optional<std::vector<Column>> resolveFieldNames(const Table& table, const Layout& layout,
                                                      const std::vector<db2::FieldStorageInfo>&
                                                          fieldStorageInfo) {
    std::vector<const Field*> inlineFields;
    std::vector<Column> result;
    for (const Field& f : layout.fields) {
        if (f.nonInline) continue;
        Column column{f.name, ColumnType::Int, std::nullopt};
        for (const Column& c : table.columns) {
            if (c.name == f.name) {
                column = c;
                break;
            }
        }
        inlineFields.push_back(&f);
        result.push_back(std::move(column));
    }
    // Field *count* mismatch -- the coarse, original safety net: a wrong
    // layout hash or a stale WoWDBDefs definition can't even claim the
    // right number of slots.
    if (result.size() != fieldStorageInfo.size()) return std::nullopt;

    // Field *shape* mismatch -- see this function's own doc comment
    // (dbd.hpp) for exactly what's checked per db2::FieldCompression
    // storage type and why.
    for (size_t i = 0; i < result.size(); ++i) {
        std::optional<uint32_t> elementBits = expectedElementBits(result[i].type, inlineFields[i]->declaredBits);
        if (!elementBits) continue;  // nothing checkable for this field
        uint32_t expectedTotalBits = *elementBits * inlineFields[i]->arrayLength;
        const db2::FieldStorageInfo& info = fieldStorageInfo[i];

        switch (info.storageType) {
            case db2::FieldCompression::None:
                if (info.fieldSizeBits != expectedTotalBits) return std::nullopt;
                break;
            case db2::FieldCompression::Bitpacked:
            case db2::FieldCompression::BitpackedSigned:
                if (info.fieldSizeBits > expectedTotalBits) return std::nullopt;
                break;
            case db2::FieldCompression::BitpackedIndexedArray:
                if (info.arrayCount != inlineFields[i]->arrayLength) return std::nullopt;
                break;
            case db2::FieldCompression::CommonData:
            case db2::FieldCompression::BitpackedIndexed:
                break;  // no fact comparable to a declared type/size, see doc comment
        }
    }
    return result;
}

}  // namespace husk::dbd
