#include "export_skin_resolution.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <tuple>

#include "export_materials.hpp"  // scanDirOrWarn

namespace husk::commands {

const std::vector<uint32_t>& requireSkinFileDataIds(const m2::Header& header,
                                                      const std::string& modelPath) {
    if (!header.skinFileDataIds || header.skinFileDataIds->empty()) {
        throw std::runtime_error("'" + modelPath +
                                  "' has no SFID chunk (or it's empty) -- this M2 doesn't carry "
                                  "skin FileDataIDs to auto-select from (pre-Legion M2s never do) "
                                  "-- pass an explicit .skin path instead of 'auto'");
    }
    return *header.skinFileDataIds;
}

std::vector<std::pair<std::string, std::string>> resolveAutoSkinPaths(const m2::Header& header,
                                                                        const std::string& skinDir,
                                                                        const std::string& modelPath,
                                                                        const std::string& lodArg) {
    if (skinDir.empty()) {
        // Only reachable via --lod/an indexed 'auto' resolution with
        // --skin-dir explicitly 'none' -- exportGlb's caller already
        // rejects this combination before parsing the model at all (see
        // its own "--lod ... --skin-dir 'none'" check), so this is a
        // belt-and-suspenders guard, not the primary error path.
        throw std::runtime_error(
            "'auto' needs a --skin-dir to search (got 'none') -- pass --skin-dir <dir> pointing at "
            "a directory of already-extracted '<FileDataID>.skin' files, or drop --lod so 'auto' "
            "can fall back to the same-basename numbered scan instead");
    }
    const auto& ids = requireSkinFileDataIds(header, modelPath);
    auto pathFor = [&](size_t index) {
        return (std::filesystem::path(skinDir) / (std::to_string(ids[index]) + ".skin")).string();
    };

    // Same same-basename-numbered-scan fallback resolveSkin already has for
    // its own (only ever entry-0) case, generalized to any SFID index --
    // --lod's behavior shouldn't depend on whether it was passed explicitly
    // (TODO/CLEANUP_TODO.md's former item 4, reproduced 2026-08-19 on a real
    // 'bloodelffemale_hd.m2': '--lod 0' failed, no '--lod' succeeded, same
    // directory, same files -- a real local extraction commonly has
    // '<basename><N>.skin' files but no FileDataID-named ones at all). Also
    // makes 'auto'/'--lod' finally check the SFID candidate actually exists
    // before returning it, rather than deferring that discovery to whatever
    // later stage tries to open the file.
    auto resolveOneIndex = [&](size_t index) -> std::string {
        std::string sfidCandidate = pathFor(index);
        std::error_code ec;
        if (std::filesystem::exists(sfidCandidate, ec) && !ec) return sfidCandidate;

        for (const auto& [lod, path] : findSameBasenameSkins(modelPath)) {
            if (static_cast<size_t>(lod) == index) {
                std::cerr << "husk: note: 'auto' resolved '" << path
                          << "' via the same-basename numbered scan (SFID entry " << index
                          << "'s own '" << sfidCandidate << "' wasn't found locally)\n";
                return path;
            }
        }
        std::string lodCountNote;
        if (header.lodCount) {
            lodCountNote = " (LDV1 lod_count: " + std::to_string(*header.lodCount) + ")";
        }
        throw std::runtime_error("'auto' couldn't resolve SFID entry " + std::to_string(index) + " for '" +
                                  modelPath + "': expected '" + sfidCandidate +
                                  "', and no matching '<model-basename>" + std::to_string(index) +
                                  ".skin' exists next to it either" + lodCountNote);
    };

    if (lodArg == "all") {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            result.emplace_back("lod" + std::to_string(i), resolveOneIndex(i));
        }
        return result;
    }

    size_t index = 0;
    if (!lodArg.empty()) {
        // Deliberately strict: std::stoul silently accepts leading
        // whitespace/a leading sign and stops at the first non-digit rather
        // than requiring the whole argument to be numeric -- reject
        // anything it would otherwise quietly half-parse (e.g. "3abc"),
        // same "foreign data that doesn't fit its own claims is an error"
        // policy as every parser in this codebase (--lod counts as one,
        // even though it's a CLI argument rather than file bytes).
        if (!std::all_of(lodArg.begin(), lodArg.end(), [](unsigned char c) { return std::isdigit(c); })) {
            throw std::runtime_error("--lod '" + lodArg + "' isn't 'all' or a non-negative integer");
        }
        index = static_cast<size_t>(std::stoul(lodArg));
    }
    if (index >= ids.size()) {
        std::string lodCountNote;
        if (header.lodCount) {
            lodCountNote = " (LDV1 lod_count: " + std::to_string(*header.lodCount) + ")";
        }
        throw std::runtime_error("--lod " + std::to_string(index) + " is out of range -- '" +
                                  modelPath + "'s SFID chunk only has " + std::to_string(ids.size()) +
                                  " skin FileDataID(s)" + lodCountNote);
    }
    return {{lodArg.empty() ? "" : "lod" + std::to_string(index), resolveOneIndex(index)}};
}

std::vector<std::pair<int, std::string>> findSameBasenameSkins(const std::string& modelPath) {
    std::filesystem::path model(modelPath);
    std::string baseName = model.stem().string();  // e.g. "bloodelffemale"
    std::filesystem::path dir = model.parent_path();
    if (dir.empty()) dir = ".";

    // (lod, path, digit-suffix length) -- the length is only used to filter
    // for the "prefer 2 digits" rule below, then discarded.
    std::vector<std::tuple<int, std::string, size_t>> found;
    for (const auto& entry :
         scanDirOrWarn(dir.string(), "model's own directory (for same-basename .skin resolution)")) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() <= baseName.size() || name.compare(0, baseName.size(), baseName) != 0) {
            continue;
        }
        size_t digitsStart = baseName.size();
        size_t pos = digitsStart;
        while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) ++pos;
        if (pos == digitsStart) continue;  // no digit right after the basename
        if (name.compare(pos, name.size() - pos, ".skin") != 0) continue;
        int lod = std::stoi(name.substr(digitsStart, pos - digitsStart));
        found.emplace_back(lod, entry.path().string(), pos - digitsStart);
    }

    bool hasTwoDigitMatch =
        std::any_of(found.begin(), found.end(), [](const auto& t) { return std::get<2>(t) == 2; });

    std::vector<std::pair<int, std::string>> result;
    for (const auto& [lod, path, digitLen] : found) {
        if (hasTwoDigitMatch && digitLen != 2) continue;
        result.emplace_back(lod, path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::pair<std::string, std::string>> resolveSkin(const m2::Header& header,
                                                               const std::string& modelPath,
                                                               const std::string& skinDir,
                                                               bool skinDirNone) {
    bool sfidPresent = header.skinFileDataIds && !header.skinFileDataIds->empty();
    std::string sfidCandidate;

    if (!skinDirNone && sfidPresent) {
        sfidCandidate =
            (std::filesystem::path(skinDir) / (std::to_string((*header.skinFileDataIds)[0]) + ".skin"))
                .string();
        std::error_code ec;
        if (std::filesystem::exists(sfidCandidate, ec) && !ec) {
            std::cerr << "husk: note: 'auto' resolved '" << sfidCandidate
                      << "' (SFID entry 0, highest-detail LOD)\n";
            return {{"", sfidCandidate}};
        }
    }

    auto candidates = findSameBasenameSkins(modelPath);
    if (!candidates.empty()) {
        std::cerr << "husk: note: 'auto' resolved '" << candidates.front().second
                  << "' via the same-basename numbered scan";
        if (candidates.size() > 1) {
            std::cerr << " (lowest-numbered of " << candidates.size()
                      << " same-basename .skin files found next to the model)";
        }
        std::cerr << "\n";
        return {{"", candidates.front().second}};
    }

    std::string reason;
    if (skinDirNone) {
        reason = "--skin-dir is 'none', so the SFID stage was skipped entirely";
    } else if (!sfidPresent) {
        reason = "'" + modelPath + "' has no SFID chunk (or it's empty) -- this M2 doesn't carry "
                                    "skin FileDataIDs to auto-select from (pre-Legion M2s never do)";
    } else {
        reason = "the SFID-declared FileDataID's .skin wasn't found at the expected path '" +
                 sfidCandidate + "'";
    }
    throw std::runtime_error("'auto' couldn't resolve a .skin file for '" + modelPath + "': " + reason +
                              ", and no same-named '<model-basename><N>.skin' file exists next to it "
                              "either -- pass an explicit .skin path instead of 'auto'");
}

}  // namespace husk::commands
