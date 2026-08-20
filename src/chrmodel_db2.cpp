#include "chrmodel_db2.hpp"

#include <filesystem>

#include "db2table.hpp"

namespace husk::chrmodel {

namespace {

uint32_t orZero(const std::optional<uint32_t>& v) { return v.value_or(0); }

std::string joinPath(const std::string& dir, const char* filename) {
    return (std::filesystem::path(dir) / filename).string();
}

std::vector<CharComponentTextureLayout> loadLayouts(const std::string& db2Dir, const std::string& dbdDir,
                                                      std::ostream& err) {
    std::vector<CharComponentTextureLayout> result;
    auto rows = db2table::readNamedColumns(joinPath(db2Dir, "charcomponenttexturelayouts.db2"), dbdDir,
                                            {"ID", "Width", "Height"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2])});
    }
    return result;
}

std::vector<ChrModelMaterial> loadMaterials(const std::string& db2Dir, const std::string& dbdDir,
                                             std::ostream& err) {
    std::vector<ChrModelMaterial> result;
    auto rows = db2table::readNamedColumns(
        joinPath(db2Dir, "chrmodelmaterial.db2"), dbdDir,
        {"ID", "CharComponentTextureLayoutsID", "TextureType", "Width", "Height", "Flags"}, err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2]), orZero(row[3]), orZero(row[4]),
                           orZero(row[5])});
    }
    return result;
}

std::vector<CharComponentTextureSection> loadSections(const std::string& db2Dir, const std::string& dbdDir,
                                                        std::ostream& err) {
    std::vector<CharComponentTextureSection> result;
    auto rows = db2table::readNamedColumns(
        joinPath(db2Dir, "charcomponenttexturesections.db2"), dbdDir,
        {"ID", "CharComponentTextureLayoutID", "SectionType", "X", "Y", "Width", "Height",
         "OverlapSectionMask"},
        err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2]), orZero(row[3]), orZero(row[4]),
                           orZero(row[5]), orZero(row[6]), orZero(row[7])});
    }
    return result;
}

std::vector<ChrModelTextureLayer> loadTextureLayers(const std::string& db2Dir, const std::string& dbdDir,
                                                     std::ostream& err) {
    std::vector<ChrModelTextureLayer> result;
    auto rows = db2table::readNamedColumns(
        joinPath(db2Dir, "chrmodeltexturelayer.db2"), dbdDir,
        {"ID", "CharComponentTextureLayoutsID", "TextureType", "Layer", "Flags", "BlendMode",
         "TextureSectionTypeBitMask", "ChrModelTextureTargetID"},
        err);
    if (!rows) return result;
    for (const auto& row : *rows) {
        result.push_back({orZero(row[0]), orZero(row[1]), orZero(row[2]), orZero(row[3]), orZero(row[4]),
                           orZero(row[5]), orZero(row[6]), orZero(row[7])});
    }
    return result;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    Data data;
    data.layouts = loadLayouts(db2Dir, dbdDir, err);
    data.materials = loadMaterials(db2Dir, dbdDir, err);
    data.sections = loadSections(db2Dir, dbdDir, err);
    data.textureLayers = loadTextureLayers(db2Dir, dbdDir, err);
    if (data.layouts.empty() && data.materials.empty() && data.sections.empty() &&
        data.textureLayers.empty()) {
        return std::nullopt;
    }
    return data;
}

}  // namespace husk::chrmodel
