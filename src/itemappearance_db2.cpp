#include "itemappearance_db2.hpp"

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
    // "ModelResourcesID" resolves to the real field's element 0 when the
    // current live layout stores it as a 2-entry array (db2table.cpp's own
    // decodeField-first-element behavior, same one chrmodel_db2.hpp's
    // texture-layer reader already relies on) -- see this file's own doc
    // comment for why only element 0 is used.
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "itemdisplayinfo.db2"), dbdDir,
                                            {"ID", "ModelResourcesID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) result.push_back({orZero(row[0]), orZero(row[1])});
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

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.modifiedAppearances = loadModifiedAppearances(db2Dir, dbdDir, err);
    data.appearances = loadAppearances(db2Dir, dbdDir, err);
    data.displayInfos = loadDisplayInfos(db2Dir, dbdDir, err);
    data.modelMatRes = loadModelMatRes(db2Dir, dbdDir, err);
    if (data.modifiedAppearances.empty() && data.appearances.empty() && data.displayInfos.empty() &&
        data.modelMatRes.empty()) {
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
    result.modelResourcesId = displayInfo->modelResourcesId;

    for (const auto& m : data.modelMatRes) {
        if (m.itemDisplayInfoId == displayInfo->id) result.materials.push_back(m);
    }

    return result;
}

}  // namespace husk::itemappearance
