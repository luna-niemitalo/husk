#include "character_texture_context.hpp"

#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "chrcustomization_db2.hpp"
#include "chrmodel_db2.hpp"
#include "sources/listfile_catalog.hpp"
#include "texturefiledata_db2.hpp"

namespace husk::chartexture {

namespace {

std::optional<uint32_t> parseId(const std::string& text) {
    try {
        size_t used = 0;
        unsigned long value = std::stoul(text, &used);
        if (used != text.size()) return std::nullopt;
        return static_cast<uint32_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The ChrModelID --chr-model-id names: explicit, derived (unset or
// "auto"), or none ("none", or nothing derivable).
std::optional<uint32_t> chrModelFor(const Args& args, const std::string& modelPath, const ListfileIndex& listfile,
                                    const std::string& listfileRoot, std::ostream& err,
                                    std::optional<chrrace::Data>* raceData) {
    if (args.chrModelId == "none") return std::nullopt;
    if (args.chrModelId.empty() || args.chrModelId == "auto") {
        return deriveChrModelId(args.db2Dir, args.dbdDir, modelPath, listfile, listfileRoot, err, raceData);
    }
    std::optional<uint32_t> id = parseId(args.chrModelId);
    if (!id) {
        err << "husk: note: --chr-model-id '" << args.chrModelId
            << "' isn't a non-negative integer, 'auto', or 'none' -- no character textures\n";
    }
    return id;
}

std::optional<uint32_t> layoutFor(const Args& args, std::optional<uint32_t> chrModelId,
                                  std::optional<chrrace::Data>& raceData, std::ostream& err) {
    if (!args.charLayoutId.empty()) {
        std::optional<uint32_t> id = parseId(args.charLayoutId);
        if (!id) err << "husk: note: --char-layout-id '" << args.charLayoutId << "' isn't a non-negative integer\n";
        return id;
    }
    if (!chrModelId) return std::nullopt;
    if (!raceData) raceData = chrrace::load(args.db2Dir, args.dbdDir, err);
    if (!raceData) return std::nullopt;
    for (const chrrace::ChrModelDisplay& m : raceData->chrModels) {
        if (m.chrModelId == *chrModelId && m.charComponentTextureLayoutId != 0) return m.charComponentTextureLayoutId;
    }
    err << "husk: note: ChrModelID " << *chrModelId << " has no CharComponentTextureLayoutID\n";
    return std::nullopt;
}

// Texture types the layout draws with exactly one ChrModelTextureLayer: the
// only types where one choice texture can stand for the whole slot.
void addSingleLayerTargets(const chrmodel::Data& layouts, uint32_t layoutId, sources::CharacterTextureContext& ctx) {
    std::unordered_map<uint32_t, uint32_t> count;
    std::unordered_map<uint32_t, uint32_t> target;
    for (const chrmodel::ChrModelTextureLayer& layer : layouts.textureLayers) {
        if (layer.charComponentTextureLayoutsId != layoutId) continue;
        ++count[layer.textureType];
        target[layer.textureType] = layer.chrModelTextureTargetId;
    }
    for (const auto& [type, n] : count) {
        if (n == 1) ctx.singleLayerTargetByTextureType[type] = target[type];
    }
}

std::vector<uint32_t> choicesFor(const Args& args, const chrcustomization::Data& data,
                                 std::optional<uint32_t> chrModelId, std::ostream& err) {
    if (args.choiceIds.empty()) {
        if (!chrModelId) return {};
        // Lowest OrderIndex per option: husk's heuristic, not a client default.
        return chrcustomization::defaultChoiceIdsForModel(data, *chrModelId);
    }
    std::vector<uint32_t> ids;
    std::stringstream list(args.choiceIds);
    for (std::string token; std::getline(list, token, ',');) {
        std::optional<uint32_t> id = parseId(token);
        if (!id) {
            err << "husk: note: --customization-choice-ids entry '" << token << "' isn't a non-negative integer\n";
            return {};
        }
        ids.push_back(*id);
    }
    return ids;
}

// A material whose element names a related choice applies only when that
// choice is selected too (a hairstyle's tiara, one texture per hair colour).
// A target two selected materials claim keeps the last; fileDataId 0 is a
// material TextureFileData doesn't resolve, kept as a known miss.
void addChoiceTextures(const Args& args, const chrcustomization::Data& data, const std::vector<uint32_t>& choiceIds,
                       sources::CharacterTextureContext& ctx, std::ostream& err) {
    std::optional<texturefiledata::Data> files = texturefiledata::load(args.db2Dir, args.dbdDir, err);
    std::unordered_set<uint32_t> selected(choiceIds.begin(), choiceIds.end());
    for (uint32_t choiceId : choiceIds) {
        for (const auto& material : chrcustomization::resolveChoice(data, choiceId, err).materials) {
            if (material.relatedChoiceId != 0 && !selected.count(material.relatedChoiceId)) continue;
            uint32_t fileDataId = 0;
            if (files) {
                auto it = files->find(material.materialResourcesId);
                if (it != files->end()) fileDataId = it->second;
            }
            ctx.fileDataIdByTarget[material.chrModelTextureTargetId] = fileDataId;
        }
    }
}

}  // namespace

std::optional<uint32_t> deriveChrModelId(const std::string& db2Dir, const std::string& dbdDir,
                                         const std::string& modelPath, const ListfileIndex& listfile,
                                         const std::string& listfileRoot, std::ostream& err,
                                         std::optional<chrrace::Data>* raceDataOut) {
    std::optional<chrrace::Data> raceData = chrrace::load(db2Dir, dbdDir, err);
    if (!raceData) {
        err << "husk: note: --chr-model-id auto: no ChrRaces/ChrModel chain loaded from '" << db2Dir << "'\n";
        return std::nullopt;
    }
    if (raceDataOut) *raceDataOut = raceData;

    // The FileDataID is exact; race+sex can be ambiguous (Dracthyr male is
    // both forms). Once the file has a FileDataID its answer stands, even
    // an ambiguous one: the filename fallback is only for files without.
    std::optional<uint32_t> modelFdid = sources::fileDataIdForPath(listfile, modelPath, listfileRoot);
    if (modelFdid) {
        std::optional<uint32_t> derived = chrrace::deriveChrModelIdFromFileDataId(*raceData, *modelFdid, err);
        if (derived) err << "husk: note: ChrModelID " << *derived << " from FileDataID " << *modelFdid << "\n";
        return derived;
    }
    std::optional<chrrace::ParsedName> parsed = chrrace::parseModelBasename(modelPath);
    if (!parsed) return std::nullopt;
    std::optional<uint32_t> derived = chrrace::deriveChrModelId(*raceData, *parsed, err);
    if (derived) {
        err << "husk: note: ChrModelID " << *derived << " from the filename (race '" << parsed->raceToken
            << "', sex " << parsed->sex << "): no listfile FileDataID for '" << modelPath << "'\n";
    }
    return derived;
}

std::optional<sources::CharacterTextureContext> build(const Args& args, const std::string& modelPath,
                                                      const ListfileIndex& listfile,
                                                      const std::string& listfileRoot, std::ostream& err) {
    if (args.db2Dir.empty() && args.dbdDir.empty() && args.charLayoutId.empty() && args.chrModelId.empty() &&
        args.choiceIds.empty()) {
        return std::nullopt;
    }
    if (args.db2Dir.empty() || args.dbdDir.empty()) {
        err << "husk: note: character textures need --db2-dir and --dbd-dir -- skipping\n";
        return std::nullopt;
    }

    std::optional<chrrace::Data> raceData;
    std::optional<uint32_t> chrModelId = chrModelFor(args, modelPath, listfile, listfileRoot, err, &raceData);
    sources::CharacterTextureContext ctx;

    if (std::optional<uint32_t> layoutId = layoutFor(args, chrModelId, raceData, err)) {
        if (std::optional<chrmodel::Data> layouts = chrmodel::load(args.db2Dir, args.dbdDir, err)) {
            addSingleLayerTargets(*layouts, *layoutId, ctx);
        }
    }
    if (std::optional<chrcustomization::Data> data = chrcustomization::load(args.db2Dir, args.dbdDir, err)) {
        std::vector<uint32_t> choiceIds = choicesFor(args, *data, chrModelId, err);
        addChoiceTextures(args, *data, choiceIds, ctx, err);
    }

    if (ctx.singleLayerTargetByTextureType.empty() && ctx.fileDataIdByTarget.empty()) return std::nullopt;
    return ctx;
}

}  // namespace husk::chartexture
