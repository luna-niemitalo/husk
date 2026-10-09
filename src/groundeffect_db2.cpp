#include "groundeffect_db2.hpp"

#include <filesystem>
#include <stdexcept>

#include "db2table.hpp"

namespace husk::groundeffect {

namespace {

uint32_t requireValue(const std::optional<uint32_t>& v, const char* column, size_t row) {
    if (!v) throw std::runtime_error(std::string(column) + ": expected a value in row " + std::to_string(row) + ", got none");
    return *v;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string texturePath = (std::filesystem::path(db2Dir) / "groundeffecttexture.db2").string();
    std::string doodadPath = (std::filesystem::path(db2Dir) / "groundeffectdoodad.db2").string();
    auto textures = db2table::readNamedColumns(texturePath, dbdDir, {"ID", "Density"}, err);
    auto textureArrays = db2table::readNamedArrayColumns(texturePath, dbdDir, {"DoodadID", "DoodadWeight"}, err);
    auto doodads = db2table::readNamedColumns(doodadPath, dbdDir, {"ID", "ModelFileID", "Flags"}, err);
    if (!textures || !textureArrays || !doodads) return std::nullopt;
    if (textures->size() != textureArrays->size()) {
        err << "groundeffecttexture.db2: scalar and array column reads disagree on row count (" << textures->size()
            << " vs " << textureArrays->size() << ")\n";
        return std::nullopt;
    }

    Data out;
    for (size_t r = 0; r < doodads->size(); ++r) {
        const auto& row = (*doodads)[r];
        out.doodads[requireValue(row[0], "GroundEffectDoodad.ID", r)] = {row[1].value_or(0), row[2].value_or(0)};
    }
    for (size_t r = 0; r < textures->size(); ++r) {
        TextureRow row;
        row.id = requireValue((*textures)[r][0], "GroundEffectTexture.ID", r);
        row.density = (*textures)[r][1];
        const auto& ids = (*textureArrays)[r][0];
        const auto& weights = (*textureArrays)[r][1];
        if (!ids || !weights || ids->size() != weights->size()) {
            throw std::runtime_error("GroundEffectTexture row " + std::to_string(row.id) +
                                     ": expected DoodadID and DoodadWeight arrays of equal length");
        }
        for (size_t k = 0; k < ids->size(); ++k) {
            if ((*ids)[k] != 0) row.doodads.push_back({(*ids)[k], (*weights)[k]});
        }
        out.textures.push_back(std::move(row));
    }
    return out;
}

}  // namespace husk::groundeffect
