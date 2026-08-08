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

ColumnType parseColumnType(const std::string& token) {
    // A foreign-key column looks like "int<Table::Col>" -- the type itself
    // is always the part before '<'.
    std::string base = token.substr(0, token.find('<'));
    if (base == "int") return ColumnType::Int;
    if (base == "float") return ColumnType::Float;
    if (base == "string") return ColumnType::String;
    if (base == "locstring") return ColumnType::LocString;
    throw ParseError("dbd: unknown column type '" + token + "'");
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
// "Name[3]", "Name<32>[3] // comment". Only the annotations and the bare
// name matter here -- <size>/[length] aren't needed for naming.
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
        }
        line = line.substr(close + 1);
    }
    size_t end = line.find_first_of("<[");
    field.name = trim(end == std::string::npos ? line : line.substr(0, end));
    return field;
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
            table.columns.emplace_back(name, parseColumnType(typeToken));
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

std::optional<std::vector<std::pair<std::string, ColumnType>>> resolveFieldNames(
    const Table& table, const Layout& layout, size_t fieldCount) {
    std::vector<std::pair<std::string, ColumnType>> result;
    for (const Field& f : layout.fields) {
        if (f.nonInline) continue;
        ColumnType type = ColumnType::Int;
        for (const auto& [name, t] : table.columns) {
            if (name == f.name) {
                type = t;
                break;
            }
        }
        result.emplace_back(f.name, type);
    }
    if (result.size() != fieldCount) return std::nullopt;
    return result;
}

}  // namespace husk::dbd
