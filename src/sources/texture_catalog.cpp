#include "texture_catalog.hpp"

#include <filesystem>
#include <utility>

#include "../export_texture_resolution.hpp"
#include "listfile_catalog.hpp"

namespace husk::sources {

Resolved<std::vector<uint8_t>> resolveLiteralTextureBytes(uint32_t fdid, const std::string& texturesDir,
                                                            const std::string& texturesOutDir) {
    if (texturesDir.empty()) {
        return Resolved<std::vector<uint8_t>>::miss(ResolutionTier::Literal, "texturesDir is empty");
    }
    auto stem = std::filesystem::path(texturesDir) / std::to_string(fdid);
    if (auto bytes = husk::commands::resolveTextureBytes(stem, texturesDir, texturesOutDir)) {
        return Resolved<std::vector<uint8_t>>::hit(std::move(*bytes), ResolutionTier::Literal,
                                                     stem.string() + ".{png,blp}");
    }
    return Resolved<std::vector<uint8_t>>::miss(
        ResolutionTier::Literal, "neither '" + stem.string() + ".png' nor '" + stem.string() + ".blp' exists");
}

Resolved<ListfileTextureResult> resolveListfileTextureBytes(
    uint32_t fdid, const std::unordered_map<uint32_t, std::string>& listfile, const std::string& listfileRoot,
    const std::string& texturesOutDir) {
    auto found = pathForFileDataId(listfile, listfileRoot, fdid);
    if (!found) {
        return Resolved<ListfileTextureResult>::miss(
            ResolutionTier::Listfile, "no listfile row for FileDataID " + std::to_string(fdid));
    }
    auto stem = found->replace_extension();
    if (auto bytes = husk::commands::resolveTextureBytes(stem, listfileRoot, texturesOutDir)) {
        ListfileTextureResult result{std::move(*bytes), stem.filename().string()};
        return Resolved<ListfileTextureResult>::hit(std::move(result), ResolutionTier::Listfile,
                                                      stem.string() + ".{png,blp}");
    }
    return Resolved<ListfileTextureResult>::miss(
        ResolutionTier::Listfile,
        "listfile names '" + stem.string() + "' for FileDataID " + std::to_string(fdid) +
            " but neither .png nor .blp exists there");
}

Resolved<FuzzyPoolTextureResult> resolveClaimedFuzzyPoolTextureBytes(const std::filesystem::path& claimedPath,
                                                                       const std::string& texturesDir,
                                                                       const std::string& texturesOutDir) {
    if (auto bytes = husk::commands::readTextureFileBytes(claimedPath, texturesDir, texturesOutDir)) {
        FuzzyPoolTextureResult result{std::move(*bytes), claimedPath.stem().string(),
                                       claimedPath.filename().string()};
        return Resolved<FuzzyPoolTextureResult>::hit(std::move(result), ResolutionTier::FuzzySameBasenamePool,
                                                       "claimed '" + claimedPath.string() + "' from the pool");
    }
    return Resolved<FuzzyPoolTextureResult>::miss(
        ResolutionTier::FuzzySameBasenamePool,
        "claimed '" + claimedPath.string() + "' from the pool but failed to read/decode it");
}

}  // namespace husk::sources
