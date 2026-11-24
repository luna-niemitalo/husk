#include "anim_names.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace husk::animnames {

namespace {

constexpr std::string_view kOpen = "const ANIM_NAMES = [";
constexpr std::string_view kClose = "];";

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool isCommit(std::string_view s) {
    if (s.size() != 40) return false;
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// `refs/heads/main` from the loose ref file, else from packed-refs.
std::string resolveRef(const std::filesystem::path& gitDir, std::string_view ref) {
    std::string loose(trim(readText(gitDir / std::string(ref))));
    if (isCommit(loose)) return loose;
    std::istringstream packed(readText(gitDir / "packed-refs"));
    for (std::string line; std::getline(packed, line);) {
        std::string_view entry = trim(line);
        if (entry.size() < 42 || entry[40] != ' ') continue;
        if (entry.substr(41) == ref && isCommit(entry.substr(0, 40))) return std::string(entry.substr(0, 40));
    }
    return {};
}

}  // namespace

std::unordered_map<uint32_t, std::string> parseAnimMapper(std::string_view text, const std::string& fileForErrors) {
    std::unordered_map<uint32_t, std::string> names;
    std::istringstream lines{std::string(text)};
    size_t lineNumber = 0;
    bool inList = false;
    uint32_t id = 0;
    auto fail = [&](const std::string& expected, std::string_view actual) {
        throw std::runtime_error(fileForErrors + ":" + std::to_string(lineNumber) + ": expected " + expected +
                                 ", got '" + std::string(actual) + "'");
    };
    for (std::string raw; std::getline(lines, raw);) {
        ++lineNumber;
        std::string_view line = trim(raw);
        if (!inList) {
            inList = line == kOpen;
            continue;
        }
        if (line == kClose) {
            if (id == 0) fail("at least one name before '];'", line);
            return names;
        }
        if (!line.empty() && line.back() == ',') line.remove_suffix(1);
        if (line.size() < 2 || line.front() != '"' || line.back() != '"') fail("one double-quoted name per line", line);
        std::string_view name = line.substr(1, line.size() - 2);
        if (name.find_first_of("\"\\") != std::string_view::npos) {
            fail("a name without quotes or escapes", name);
        }
        if (!name.empty()) names.emplace(id, std::string(name));
        ++id;
    }
    if (!inList) fail("a line '" + std::string(kOpen) + "'", "end of file");
    fail("'" + std::string(kClose) + "' closing the list", "end of file");
    return names;
}

std::string checkoutCommit(const std::filesystem::path& checkout) {
    std::filesystem::path gitDir = checkout / ".git";
    std::string head(trim(readText(gitDir / "HEAD")));
    if (isCommit(head)) return head;
    constexpr std::string_view kRefPrefix = "ref: ";
    if (head.rfind(kRefPrefix, 0) != 0) return {};
    return resolveRef(gitDir, trim(std::string_view(head).substr(kRefPrefix.size())));
}

Table load(const std::filesystem::path& checkout) {
    std::filesystem::path file = checkout / std::string(kFile);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("--anim-names: expected a wow.export checkout with " + std::string(kFile) +
                                 ", got no readable " + file.string());
    }
    std::ostringstream text;
    text << in.rdbuf();
    Table table;
    table.names = parseAnimMapper(text.str(), file.string());
    std::string commit = checkoutCommit(checkout);
    table.detail = "github.com/Kruithne/wow.export " + std::string(kFile) + " at " +
                   (commit.empty() ? std::string("unknown commit") : commit) + " (" + checkout.string() + ")";
    return table;
}

}  // namespace husk::animnames
