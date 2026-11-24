#include "itemappearance_db2.hpp"

#include <algorithm>
#include <filesystem>

#include "db2table.hpp"

namespace husk::itemappearance {

namespace {

uint32_t orZero(const std::optional<uint32_t>& v) { return v.value_or(0); }

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

std::vector<ModifiedAppearance> loadModifiedAppearances(const std::string& db2Dir, const std::string& dbdDir,
                                                          std::ostream& err) {
    std::vector<ModifiedAppearance> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "itemmodifiedappearance.db2"), dbdDir,
                                            {"ID", "ItemAppearanceID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) result.push_back({orZero(row[0]), orZero(row[1])});
    return result;
}

std::vector<Appearance> loadAppearances(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<Appearance> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "itemappearance.db2"), dbdDir,
                                            {"ID", "ItemDisplayInfoID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) result.push_back({orZero(row[0]), orZero(row[1])});
    return result;
}

std::vector<DisplayInfo> loadDisplayInfos(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<DisplayInfo> result;
    auto rows = db2table::readNamedArrayColumns(joinPath(db2Dir, "itemdisplayinfo.db2"), dbdDir,
                                                 {"ID", "ModelResourcesID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        DisplayInfo info;
        info.id = row[0] && !row[0]->empty() ? row[0]->front() : 0;
        if (row[1]) {
            for (size_t i = 0; i < std::min<size_t>(2, row[1]->size()); ++i) info.modelResourcesIds[i] = (*row[1])[i];
        }
        result.push_back(info);
    }
    return result;
}

std::vector<ModelMatRes> loadModelMatRes(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<ModelMatRes> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "itemdisplayinfomodelmatres.db2"), dbdDir,
                                            {"ItemDisplayInfoID", "MaterialResourcesID", "TextureType", "ModelIndex"},
                                            err);
    if (!rows) return result;
    for (const auto& row : *rows) result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2]), orZero(row[3])});
    return result;
}

std::vector<MaterialRes> loadMaterialRes(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::vector<MaterialRes> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "itemdisplayinfomaterialres.db2"), dbdDir,
                                            {"ItemDisplayInfoID", "ComponentSection", "MaterialResourcesID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    return result;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.modifiedAppearances = loadModifiedAppearances(db2Dir, dbdDir, err);
    data.appearances = loadAppearances(db2Dir, dbdDir, err);
    data.displayInfos = loadDisplayInfos(db2Dir, dbdDir, err);
    data.modelMatRes = loadModelMatRes(db2Dir, dbdDir, err);
    data.materialRes = loadMaterialRes(db2Dir, dbdDir, err);
    if (data.modifiedAppearances.empty() && data.appearances.empty() && data.displayInfos.empty() &&
        data.modelMatRes.empty() && data.materialRes.empty()) {
        return std::nullopt;
    }
    return data;
}

Resolution resolve(const Data& data, uint32_t itemModifiedAppearanceId, std::ostream& err) {
    Resolution result;

    const ModifiedAppearance* modifiedAppearance = nullptr;
    for (const auto& ma : data.modifiedAppearances) {
        if (ma.id == itemModifiedAppearanceId) {
            modifiedAppearance = &ma;
            break;
        }
    }
    if (!modifiedAppearance) {
        err << "husk: note: ItemModifiedAppearanceID " << itemModifiedAppearanceId
            << " matched no row in ItemModifiedAppearance -- skipping\n";
        return result;
    }

    const Appearance* appearance = nullptr;
    for (const auto& a : data.appearances) {
        if (a.id == modifiedAppearance->itemAppearanceId) {
            appearance = &a;
            break;
        }
    }
    if (!appearance) {
        err << "husk: note: ItemModifiedAppearanceID " << itemModifiedAppearanceId << "'s ItemAppearanceID "
            << modifiedAppearance->itemAppearanceId << " matched no row in ItemAppearance -- dangling reference\n";
        return result;
    }
    result.itemDisplayInfoId = appearance->itemDisplayInfoId;

    // A real, common sentinel, not a dangling reference: ItemAppearance's
    // own ItemDisplayInfoID column literally stores 0 when the player has
    // transmogrified this slot to Hidden (confirmed directly against real
    // data -- e.g. a real "Hidden Shoulder"/"Hidden Tabard" appearance's
    // ItemAppearance row has ItemDisplayInfoID=0 verbatim, not an absent
    // row husk failed to find). Same "0 is a real value, not unresolved"
    // convention assembleEquippedItems' ModelResourcesID 0 check applies
    // one hop downstream (canon_item_builder.cpp). Returning here
    // (materials/sectionMaterials/modelResourcesIds left unset) gives the
    // item no components -- correct, since Hidden has nothing to attach --
    // without the misleading "dangling reference" log.
    if (appearance->itemDisplayInfoId == 0) return result;

    const DisplayInfo* displayInfo = nullptr;
    for (const auto& d : data.displayInfos) {
        if (d.id == appearance->itemDisplayInfoId) {
            displayInfo = &d;
            break;
        }
    }
    if (!displayInfo) {
        err << "husk: note: ItemModifiedAppearanceID " << itemModifiedAppearanceId << "'s ItemDisplayInfoID "
            << appearance->itemDisplayInfoId << " matched no row in ItemDisplayInfo -- dangling reference\n";
        return result;
    }
    result.modelResourcesIds = displayInfo->modelResourcesIds;

    for (const auto& m : data.modelMatRes) {
        if (m.itemDisplayInfoId == displayInfo->id) result.materials.push_back(m);
    }
    for (const auto& m : data.materialRes) {
        if (m.itemDisplayInfoId == displayInfo->id) result.sectionMaterials.push_back(m);
    }

    return result;
}

}  // namespace husk::itemappearance
