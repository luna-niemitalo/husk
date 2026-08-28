#include "listfile_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace husk::sources {

std::optional<uint32_t> fileDataIdForPath(const std::unordered_map<uint32_t, std::string>& listfile,
                                           const std::string& modelPath, const std::string& listfileRoot) {
    if (listfile.empty() || listfileRoot.empty()) return std::nullopt;
    std::error_code ec;
    auto rel = std::filesystem::relative(std::filesystem::path(modelPath), listfileRoot, ec);
    if (ec) return std::nullopt;
    std::string relPath = rel.generic_string();
    std::transform(relPath.begin(), relPath.end(), relPath.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    for (const auto& [fdid, path] : listfile) {
        if (path == relPath) return fdid;
    }
    return std::nullopt;
}

}  // namespace husk::sources
