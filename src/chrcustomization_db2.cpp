#include "chrcustomization_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::chrcustomization {

namespace {

uint32_t orZero(const std::optional<uint32_t>& v) { return v.value_or(0); }

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

std::vector<Element> loadElements(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Element> result;
    auto rows = db2table::readNamedColumns(
        joinPath(db2Dir, "chrcustomizationelement.db2"), dbdDir,
        {"ChrCustomizationChoiceID", "ChrCustomizationGeosetID", "ChrCustomizationBoneSetID",
         "ChrCustomizationMaterialID", "RelatedChrCustomizationChoiceID"},
        err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2]), orZero(row[3]), orZero(row[4])});
    }
    return result;
}

std::vector<Geoset> loadGeosets(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Geoset> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "chrcustomizationgeoset.db2"), dbdDir,
                                            {"ID", "GeosetType", "GeosetID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<BoneSet> loadBoneSets(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<BoneSet> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "chrcustomizationboneset.db2"), dbdDir,
                                            {"ID", "BoneFileDataID", "ModelFileDataID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<Option> loadOptions(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Option> result;
    auto intRows = db2table::readNamedColumns(joinPath(db2Dir, "chrcustomizationoption.db2"), dbdDir,
                                               {"ID", "ChrModelID", "OrderIndex"}, err);
    if (!intRows) return result;
    auto strRows = db2table::readNamedStringColumns(joinPath(db2Dir, "chrcustomizationoption.db2"), dbdDir,
                                                      {"Name_lang"}, err);
    for (size_t i = 0; i < intRows->size(); ++i) {
        const auto& row = (*intRows)[i];
        std::string name = (strRows && i < strRows->size()) ? (*strRows)[i][0].value_or("") : "";
        result.push_back({orZero(row[0]), orZero(row[1]), std::move(name), orZero(row[2])});
    }
    return result;
}

std::vector<Material> loadMaterials(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Material> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "chrcustomizationmaterial.db2"), dbdDir,
                                            {"ID", "ChrModelTextureTargetID", "MaterialResourcesID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<Choice> loadChoices(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Choice> result;
    auto intRows = db2table::readNamedColumns(joinPath(db2Dir, "chrcustomizationchoice.db2"), dbdDir,
                                               {"ID", "ChrCustomizationOptionID", "OrderIndex"}, err);
    if (!intRows) return result;
    auto strRows = db2table::readNamedStringColumns(joinPath(db2Dir, "chrcustomizationchoice.db2"), dbdDir,
                                                      {"Name_lang"}, err);
    for (size_t i = 0; i < intRows->size(); ++i) {
        const auto& row = (*intRows)[i];
        std::string name = (strRows && i < strRows->size()) ? (*strRows)[i][0].value_or("") : "";
        result.push_back({orZero(row[0]), orZero(row[1]), std::move(name), orZero(row[2])});
    }
    return result;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.elements = loadElements(db2Dir, dbdDir, err);
    data.geosets = loadGeosets(db2Dir, dbdDir, err);
    data.boneSets = loadBoneSets(db2Dir, dbdDir, err);
    data.options = loadOptions(db2Dir, dbdDir, err);
    data.choices = loadChoices(db2Dir, dbdDir, err);
    data.materials = loadMaterials(db2Dir, dbdDir, err);
    if (data.elements.empty() && data.geosets.empty() && data.boneSets.empty() && data.options.empty() &&
        data.choices.empty() && data.materials.empty()) {
        return std::nullopt;
    }
    return data;
}

Resolution resolveChoice(const Data& data, uint32_t choiceId, std::ostream& err) {
    Resolution result;

    // Real, verified against local data (real end-to-end CLI testing, not
    // just inspection -- see this file's own git history for the exact
    // bug this caught): one ChrCustomizationChoiceID owns *several*
    // ChrCustomizationElement rows, not one -- e.g. real choice 1758 has
    // 19 rows, only one of which
    // carries a nonzero ChrCustomizationGeosetID/BoneSetID, the rest zero
    // (presumably other elements' own fields, not read by this reader).
    // Matching only the first row per choice (an earlier, wrong version of
    // this function did exactly that) silently drops the real geoset/
    // boneset whenever it isn't that first row -- must scan every row for
    // this choice and take whichever one(s) carry a nonzero value, same as
    // reference/wow.export's own per-field `.set()` loop over every row.
    uint32_t geosetElementId = 0;
    uint32_t boneSetElementId = 0;
    // (materialId, relatedChoiceId) per Element row -- relatedChoiceId
    // travels with its own materialId, not collapsed into a single flag,
    // since a choice's several material rows can each carry a *different*
    // relatedChoiceId (see Element::relatedChoiceId's own doc comment).
    std::vector<std::pair<uint32_t, uint32_t>> materialElements;
    bool anyRow = false;
    for (const auto& e : data.elements) {
        if (e.choiceId != choiceId) continue;
        anyRow = true;
        if (e.geosetId != 0) geosetElementId = e.geosetId;
        if (e.boneSetId != 0) boneSetElementId = e.boneSetId;
        if (e.materialId != 0) materialElements.push_back({e.materialId, e.relatedChoiceId});
    }
    if (!anyRow) {
        err << "husk: note: ChrCustomizationChoiceID " << choiceId
            << " matched no row in ChrCustomizationElement -- skipping\n";
        return result;
    }

    if (geosetElementId != 0) {
        bool found = false;
        for (const auto& g : data.geosets) {
            if (g.id == geosetElementId) {
                result.geosetId = g.geosetType * 100 + g.geosetId;
                found = true;
                break;
            }
        }
        if (!found) {
            err << "husk: note: ChrCustomizationChoiceID " << choiceId << "'s ChrCustomizationGeosetID "
                << geosetElementId << " matched no row in ChrCustomizationGeoset -- dangling reference\n";
        }
    }

    if (boneSetElementId != 0) {
        bool found = false;
        for (const auto& b : data.boneSets) {
            if (b.id == boneSetElementId) {
                result.boneFileDataId = b.boneFileDataId;
                found = true;
                break;
            }
        }
        if (!found) {
            err << "husk: note: ChrCustomizationChoiceID " << choiceId << "'s ChrCustomizationBoneSetID "
                << boneSetElementId << " matched no row in ChrCustomizationBoneSet -- dangling reference\n";
        }
    }

    for (const auto& [materialElementId, relatedChoiceId] : materialElements) {
        bool found = false;
        for (const auto& m : data.materials) {
            if (m.id == materialElementId) {
                result.materials.push_back({m.chrModelTextureTargetId, m.materialResourcesId, relatedChoiceId});
                found = true;
                break;
            }
        }
        if (!found) {
            err << "husk: note: ChrCustomizationChoiceID " << choiceId << "'s ChrCustomizationMaterialID "
                << materialElementId << " matched no row in ChrCustomizationMaterial -- dangling reference\n";
        }
    }

    return result;
}

std::vector<NamedChoice> namedChoicesForModel(const Data& data, uint32_t chrModelId, std::ostream& err) {
    std::vector<NamedChoice> result;
    if (data.options.empty() || data.choices.empty()) return result;

    for (const auto& option : data.options) {
        if (option.chrModelId != chrModelId) continue;
        for (const auto& choice : data.choices) {
            if (choice.optionId != option.id) continue;
            NamedChoice nc;
            nc.optionId = option.id;
            nc.optionName = option.name;
            nc.optionOrderIndex = option.orderIndex;
            nc.choiceId = choice.id;
            nc.choiceName = choice.name;
            nc.choiceOrderIndex = choice.orderIndex;
            nc.resolution = resolveChoice(data, choice.id, err);
            result.push_back(std::move(nc));
        }
    }
    return result;
}

std::vector<uint32_t> defaultChoiceIdsForModel(const Data& data, uint32_t chrModelId) {
    std::vector<uint32_t> result;
    if (data.options.empty() || data.choices.empty()) return result;

    for (const auto& option : data.options) {
        if (option.chrModelId != chrModelId) continue;
        const Choice* best = nullptr;
        for (const auto& choice : data.choices) {
            if (choice.optionId != option.id) continue;
            if (!best || choice.orderIndex < best->orderIndex ||
                (choice.orderIndex == best->orderIndex && choice.id < best->id)) {
                best = &choice;
            }
        }
        if (best) result.push_back(best->id);
    }
    return result;
}

}  // namespace husk::chrcustomization
