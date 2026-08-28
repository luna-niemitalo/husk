#include "catalog.hpp"

#include <algorithm>
#include <sstream>
#include <tuple>

#include "../export_texture_resolution.hpp"
#include "listfile_catalog.hpp"
#include "texture_catalog.hpp"

namespace husk::sources {

Catalog::Catalog(std::string texturesDir, std::unordered_map<uint32_t, std::string> listfile,
                  std::string listfileRoot, std::string texturesOutDir)
    : texturesDir_(std::move(texturesDir)),
      listfile_(std::move(listfile)),
      listfileRoot_(listfileRoot.empty() ? texturesDir_ : std::move(listfileRoot)),
      texturesOutDir_(std::move(texturesOutDir)) {}

void Catalog::registerPathOverride(uint32_t fdid, std::string realContentPath) {
    pathOverrides_[fdid] = std::move(realContentPath);
}

std::optional<std::filesystem::path> Catalog::listfileTargetPath(uint32_t fdid) const {
    if (auto ov = pathOverrides_.find(fdid); ov != pathOverrides_.end()) {
        // Same join `pathForFileDataId` uses (listfile_catalog.cpp): not
        // requiring listfileRoot_ non-empty, an empty root acts as an
        // identity join -- matches the pre-Catalog behavior of the
        // `listfile.emplace(...)` hack this replaces, which fed straight
        // into the same `pathForFileDataId` call every other fdid uses.
        if (listfileRoot_.empty()) return std::filesystem::path(ov->second);
        return std::filesystem::path(listfileRoot_) / ov->second;
    }
    return pathForFileDataId(listfile_, listfileRoot_, fdid);
}

Catalog::ModelState& Catalog::stateForModel(const TextureModelContext& model) {
    auto& state = modelStates_[model.modelPath];
    if (state.poolInitialized) return state;

    state.pool = husk::commands::scanFuzzyTexturePool(texturesDir_, model.modelPath);
    // A same-basename fuzzy candidate whose own trailing FileDataID matches
    // one of this exact M2's own texture-array entries is never a
    // plausible guess for an unrelated hardcoded slot -- it's already a
    // real, specifically-identified texture belonging to some *other* M2
    // texture-array index (e.g. a particle/ribbon-emitter sprite,
    // referenced by array index rather than by any material batch), not an
    // unknown file a human just happened to name after this model. Real
    // bug this fixes: `ethereal2_f.m2`'s monster_1/monster_2/monster_3/
    // environment slots (no FileDataID of their own) were picking up this
    // same model's own particle-effect sprite textures purely because they
    // share the model's basename under the community listfile's naming
    // convention -- moved verbatim out of export_materials.cpp's own
    // pre-Catalog pool setup, same exclusion, same reasoning.
    if (!model.ownTextureFileDataIds.empty()) {
        std::string basenameLower = husk::commands::lowercaseModelBasename(model.modelPath);
        state.pool.files.erase(
            std::remove_if(state.pool.files.begin(), state.pool.files.end(),
                            [&](const std::filesystem::path& p) {
                                auto fdid = husk::commands::fuzzyCandidateFileDataId(p, basenameLower);
                                return fdid && std::find(model.ownTextureFileDataIds.begin(),
                                                          model.ownTextureFileDataIds.end(),
                                                          *fdid) != model.ownTextureFileDataIds.end();
                            }),
            state.pool.files.end());
    }
    state.poolInitialized = true;
    return state;
}

size_t Catalog::remainingTexturePoolSize(const std::string& modelPath) const {
    auto it = modelStates_.find(modelPath);
    return it == modelStates_.end() ? 0 : it->second.pool.files.size();
}

Resolved<EncodedTexture> Catalog::resolveLiteralTier(uint32_t fdid) const {
    auto r = resolveLiteralTextureBytes(fdid, texturesDir_, texturesOutDir_);
    if (!r.found()) return Resolved<EncodedTexture>::miss(ResolutionTier::Literal, r.reason);
    EncodedTexture et;
    et.bytes = std::move(*r.value);
    et.imageName = std::to_string(fdid);
    return Resolved<EncodedTexture>::hit(std::move(et), ResolutionTier::Literal, r.reason);
}

Resolved<EncodedTexture> Catalog::resolveListfileTier(uint32_t fdid) const {
    auto target = listfileTargetPath(fdid);
    if (!target) {
        return Resolved<EncodedTexture>::miss(
            ResolutionTier::Listfile, "no listfile row (or knowledge-base override) for FileDataID " +
                                           std::to_string(fdid));
    }
    auto stem = *target;
    stem.replace_extension();
    auto bytes = husk::commands::resolveTextureBytes(stem, listfileRoot_, texturesOutDir_);
    if (!bytes) {
        return Resolved<EncodedTexture>::miss(
            ResolutionTier::Listfile, "listfile names '" + stem.string() + "' for FileDataID " +
                                           std::to_string(fdid) + " but neither .png nor .blp exists there");
    }
    EncodedTexture et;
    et.bytes = std::move(*bytes);
    et.imageName = stem.filename().string();
    return Resolved<EncodedTexture>::hit(std::move(et), ResolutionTier::Listfile, stem.string() + ".{png,blp}");
}

// Tier 3, the real orchestration `RESOURCE_CATALOG.md`'s Settled section
// describes: `filterCandidatesForType` is run exactly once (unlike the
// pre-Catalog call site, which ran it twice -- once inside
// `claimSoleFuzzyTextureCandidate`, once again in the caller's own
// ambiguity branch, always producing the identical set since nothing
// mutates the pool between the two calls). Exactly one match: claim
// (remove from the pool) and read it -- a read failure here is a genuine
// miss, the pool stays depleted, no re-scan. Zero matches: miss, pool
// untouched. Two or more: genuinely ambiguous -- ordered via
// `orderCandidatesForDefault`, every one embedded as an `alternates` entry
// (front() is also the chosen `value`), pool left untouched (an ambiguous
// pool stays available for the next unresolved slot too, same as before
// this object existed).
Resolved<EncodedTexture> Catalog::resolveFuzzyTier(ModelState& state, uint32_t textureType,
                                                    const std::string& modelPath, bool preferGlowVariant) {
    std::string basenameLower = husk::commands::lowercaseModelBasename(modelPath);
    auto matching = husk::commands::filterCandidatesForType(state.pool.files, textureType, basenameLower);

    if (matching.empty()) {
        return Resolved<EncodedTexture>::miss(
            ResolutionTier::FuzzySameBasenamePool,
            "no same-basename pool candidates compatible with texture type " + std::to_string(textureType));
    }

    if (matching.size() == 1) {
        auto claimed = matching.front();
        state.pool.files.erase(std::find(state.pool.files.begin(), state.pool.files.end(), claimed));
        auto bytes = husk::commands::readTextureFileBytes(claimed, texturesDir_, texturesOutDir_);
        if (!bytes) {
            return Resolved<EncodedTexture>::miss(
                ResolutionTier::FuzzySameBasenamePool,
                "claimed '" + claimed.string() + "' from the pool but failed to read/decode it");
        }
        EncodedTexture et;
        et.bytes = std::move(*bytes);
        et.imageName = claimed.stem().string();
        et.matchedFilename = claimed.filename().string();
        return Resolved<EncodedTexture>::hit(
            std::move(et), ResolutionTier::FuzzySameBasenamePool,
            "claimed '" + claimed.string() + "' as the pool's sole type-compatible candidate");
    }

    husk::commands::orderCandidatesForDefault(matching, texturesDir_, texturesOutDir_, basenameLower,
                                               ambiguousCandidateCache_, preferGlowVariant);
    std::vector<Alternate> alternates;
    alternates.reserve(matching.size());
    for (const auto& candidatePath : matching) {
        auto cached = ambiguousCandidateCache_.find(candidatePath);
        if (cached == ambiguousCandidateCache_.end()) {
            auto bytes = husk::commands::readTextureFileBytes(candidatePath, texturesDir_, texturesOutDir_);
            if (!bytes) continue;  // unreadable candidate -- skip, don't fail the whole slot
            cached = ambiguousCandidateCache_.emplace(candidatePath, std::move(*bytes)).first;
        }
        Alternate alt;
        alt.filename = candidatePath.filename().string();
        alt.category =
            husk::commands::classifyCandidateCategory(candidatePath, basenameLower).value_or(std::string());
        std::tie(alt.width, alt.height) = husk::commands::pngDimensions(cached->second);
        alt.imagePng = cached->second;
        alternates.push_back(std::move(alt));
    }
    if (alternates.empty()) {
        return Resolved<EncodedTexture>::miss(ResolutionTier::FuzzySameBasenamePool,
                                               std::to_string(matching.size()) +
                                                   " type-compatible pool candidates, but all failed to "
                                                   "read/decode");
    }

    EncodedTexture et;
    et.bytes = alternates.front().imagePng;
    et.imageName = std::filesystem::path(alternates.front().filename).stem().string();
    et.matchedFilename = alternates.front().filename;
    auto result = Resolved<EncodedTexture>::hit(
        std::move(et), ResolutionTier::FuzzySameBasenamePool,
        std::to_string(alternates.size()) + " type-compatible pool candidates, ambiguous -- defaulted to '" +
            alternates.front().filename + "' via orderCandidatesForDefault");
    result.alternates = std::move(alternates);
    return result;
}

Resolved<EncodedTexture> Catalog::texture(uint32_t fdid, uint32_t textureType, const TextureModelContext& model,
                                           bool preferGlowVariant) {
    auto& state = stateForModel(model);
    if (auto cached = state.slotCache.find(model.textureSlotIndex); cached != state.slotCache.end()) {
        return cached->second;
    }

    // Tier 1 -> tier 2, fdid-driven, tried only when there's an fdid to try
    // at all -- fdid == 0 skips straight to tier 3, same as the pre-Catalog
    // call site (which never asked resolveLiteralTextureBytes/
    // resolveListfileTextureBytes to look up a literal "0.png").
    Resolved<EncodedTexture> result =
        Resolved<EncodedTexture>::miss(ResolutionTier::Miss, "fdid == 0, no deterministic tier to try");
    if (fdid != 0) {
        result = resolveLiteralTier(fdid);
        if (!result.found()) result = resolveListfileTier(fdid);
    }
    if (!result.found()) {
        result = resolveFuzzyTier(state, textureType, model.modelPath, preferGlowVariant);
    }
    // Tier 4 (parent-directory same-basename) -- deliberate gap, see
    // texture()'s own header doc comment. Tier 5 (knowledge base) --
    // deliberately not called from here at all, same doc comment.

    ledger_.push_back(LedgerEntry{model.modelPath, model.textureSlotIndex, fdid, textureType, result.found(),
                                   result.tier, result.reason, result.value ? result.value->bytes.size() : 0,
                                   result.alternates.size(),
                                   result.value ? result.value->imageName : std::string()});
    state.slotCache.emplace(model.textureSlotIndex, result);
    return result;
}

std::string Catalog::describe() const {
    std::ostringstream out;
    out << "sources::Catalog: texturesDir='" << texturesDir_ << "' listfileRoot='" << listfileRoot_
        << "' (" << listfile_.size() << " listfile rows, " << pathOverrides_.size() << " path overrides)\n";
    for (const auto& e : ledger_) {
        out << "  " << e.modelPath << " slot " << e.textureSlotIndex << " fdid=" << e.fileDataId
            << " type=" << e.textureType << " -> " << tierName(e.tier) << (e.found ? " HIT" : " MISS");
        if (e.alternateCount > 0) out << " (" << e.alternateCount << " alternates, ambiguous)";
        if (e.found) out << " (" << e.byteCount << " bytes)";
        out << " -- " << e.reason << "\n";
    }
    return out.str();
}

}  // namespace husk::sources
