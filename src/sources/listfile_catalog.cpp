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

}  // namespace husk::sources
