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
        {"ChrCustomizationChoiceID", "ChrCustomizationGeosetID", "ChrCustomizationBoneSetID"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
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

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.elements = loadElements(db2Dir, dbdDir, err);
    data.geosets = loadGeosets(db2Dir, dbdDir, err);
    data.boneSets = loadBoneSets(db2Dir, dbdDir, err);
    if (data.elements.empty() && data.geosets.empty() && data.boneSets.empty()) return std::nullopt;
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
    bool anyRow = false;
    for (const auto& e : data.elements) {
        if (e.choiceId != choiceId) continue;
        anyRow = true;
        if (e.geosetId != 0) geosetElementId = e.geosetId;
        if (e.boneSetId != 0) boneSetElementId = e.boneSetId;
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

    return result;
}

}  // namespace husk::chrcustomization
