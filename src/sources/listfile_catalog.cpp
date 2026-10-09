#include "listfile_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace husk::sources {

std::optional<uint32_t> fileDataIdForPath(const husk::ListfileIndex& listfile, const std::string& modelPath,
                                           const std::string& listfileRoot) {
    if (listfile.empty() || listfileRoot.empty()) return std::nullopt;
    std::error_code ec;
    auto rel = std::filesystem::relative(std::filesystem::path(modelPath), listfileRoot, ec);
    if (ec) return std::nullopt;
    std::string relPath = rel.generic_string();
    std::transform(relPath.begin(), relPath.end(), relPath.begin(),
                    [](unsigned char c) { return std::tolower(c); });

    std::optional<uint32_t> found;
    listfile.forEach([&](uint32_t fdid, std::string_view path) {
        if (path != relPath) return true;  // keep scanning
        found = fdid;
        return false;  // match found -- stop
    });
    return found;
}

std::optional<std::filesystem::path> pathForFileDataId(const husk::ListfileIndex& listfile,
                                                         const std::string& listfileRoot, uint32_t fdid) {
    // Deliberately does NOT early-return on an empty listfileRoot: one of
    // the two original call sites (export_materials.cpp) never checked
    // that either, relying on std::filesystem::path("") / rel acting as an
    // identity join -- matching that exactly keeps this a verbatim move,
    // not a behavior change, for a caller that passes --listfile without
    // --listfile-root. The other call site (exportGearAuxItemModels)
    // already checks listfileRoot itself before ever reaching here.
    if (listfile.empty()) return std::nullopt;
    auto found = listfile.lookup(fdid);
    if (!found) return std::nullopt;
    return std::filesystem::path(listfileRoot) / std::filesystem::path(*found);
}

std::optional<std::string> contentNameForFileDataId(const husk::ListfileIndex& listfile, uint32_t fdid) {
    if (listfile.empty()) return std::nullopt;
    auto found = listfile.lookup(fdid);
    if (!found) return std::nullopt;
    return std::filesystem::path(*found).stem().string();
}

std::string listfileSpelling(const std::string& gamePath) {
    std::string out = gamePath;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return c == '\\' ? '/' : static_cast<char>(std::tolower(c));
    });
    for (const char* legacy : {".mdx", ".mdl"}) {
        if (out.size() >= 4 && out.compare(out.size() - 4, 4, legacy) == 0) {
            out.replace(out.size() - 4, 4, ".m2");
            break;
        }
    }
    return out;
}

std::unordered_map<std::string, uint32_t> fileDataIdsForGamePaths(const husk::ListfileIndex& listfile,
                                                                  const std::vector<std::string>& gamePaths) {
    std::unordered_map<std::string, std::vector<const std::string*>> wanted;
    for (const std::string& p : gamePaths) wanted[listfileSpelling(p)].push_back(&p);

    std::unordered_map<std::string, uint32_t> out;
    if (wanted.empty()) return out;
    size_t remaining = wanted.size();
    listfile.forEach([&](uint32_t fdid, std::string_view path) {
        auto it = wanted.find(std::string(path));
        if (it == wanted.end() || it->second.empty()) return true;
        for (const std::string* original : it->second) out.emplace(*original, fdid);
        it->second.clear();  // first listfile row wins, like fileDataIdForPath
        return --remaining > 0;
    });
    return out;
}

}  // namespace husk::sources
