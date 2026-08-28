#include "texture_catalog.hpp"

#include <filesystem>
#include <utility>

#include "../export_texture_resolution.hpp"

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

}  // namespace husk::sources
