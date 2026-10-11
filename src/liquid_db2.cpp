#include "liquid_db2.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <map>
#include <stdexcept>

#include "db2table.hpp"

namespace husk::liquiddb {

namespace {

uint32_t requireValue(const std::optional<uint32_t>& v, const char* column, size_t row) {
    if (!v) throw std::runtime_error(std::string(column) + ": expected a value in row " + std::to_string(row) + ", got none");
    return *v;
}

std::string tablePath(const std::string& db2Dir, const char* file) {
    return (std::filesystem::path(db2Dir) / file).string();
}

struct TextureRow {
    uint32_t order = 0;
    uint32_t fileDataId = 0;
    int32_t kind = -1;
};

// LiquidTypeXTexture lists a type's slots back to back: slot k takes
// max(FrameCountTexture[k], 1) rows, and the slots end where the rows do
// (all 197 types of 12.1.0 split this way exactly; WIKI_FINDINGS/ADT.md).
std::vector<TextureSlot> splitSlots(uint32_t typeId, std::vector<TextureRow> rows, const std::vector<uint32_t>& frameCounts) {
    std::sort(rows.begin(), rows.end(), [](const TextureRow& a, const TextureRow& b) { return a.order < b.order; });
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].order != i) {
            throw std::runtime_error("LiquidTypeXTexture rows of LiquidType " + std::to_string(typeId) +
                                     ": expected OrderIndex " + std::to_string(i) + ", got " +
                                     std::to_string(rows[i].order));
        }
    }
    std::vector<TextureSlot> slots;
    size_t used = 0;
    for (uint32_t count : frameCounts) {
        if (used == rows.size()) break;
        size_t frames = std::max<uint32_t>(count, 1);
        if (used + frames > rows.size()) {
            throw std::runtime_error("LiquidType " + std::to_string(typeId) + ": expected " + std::to_string(frames) +
                                     " texture rows for slot " + std::to_string(slots.size()) + ", got " +
                                     std::to_string(rows.size() - used));
        }
        TextureSlot slot;
        slot.kind = rows[used].kind;
        for (size_t i = 0; i < frames; ++i) slot.frames.push_back(rows[used + i].fileDataId);
        slots.push_back(std::move(slot));
        used += frames;
    }
    if (used != rows.size()) {
        throw std::runtime_error("LiquidType " + std::to_string(typeId) + ": expected its slots' frame counts to cover " +
                                 std::to_string(rows.size()) + " texture rows, they cover " + std::to_string(used));
    }
    return slots;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string objectPath = tablePath(db2Dir, "liquidobject.db2");
    std::string typePath = tablePath(db2Dir, "liquidtype.db2");
    std::string materialPath = tablePath(db2Dir, "liquidmaterial.db2");
    std::string texturePath = tablePath(db2Dir, "liquidtypextexture.db2");
    auto objects = db2table::readNamedColumns(objectPath, dbdDir,
                                              {"ID", "LiquidTypeID", "FlowDirection", "FlowSpeed", "Fishable", "Reflection"}, err);
    auto types = db2table::readNamedColumns(typePath, dbdDir, {"ID", "Flags", "MaterialID"}, err);
    auto typeNames = db2table::readNamedStringColumns(typePath, dbdDir, {"Name"}, err);
    auto frameCounts = db2table::readNamedArrayColumns(typePath, dbdDir, {"FrameCountTexture"}, err);
    auto materials = db2table::readNamedColumns(materialPath, dbdDir, {"ID", "LVF", "Flags"}, err);
    auto textures = db2table::readNamedColumns(texturePath, dbdDir, {"LiquidTypeID", "OrderIndex", "FileDataID", "Type"}, err);
    if (!objects || !types || !typeNames || !frameCounts || !materials || !textures) return std::nullopt;
    if (types->size() != typeNames->size() || types->size() != frameCounts->size()) {
        err << "liquidtype.db2: column reads disagree on row count (" << types->size() << ", " << typeNames->size()
            << ", " << frameCounts->size() << ")\n";
        return std::nullopt;
    }

    Data out;
    for (size_t r = 0; r < objects->size(); ++r) {
        const auto& row = (*objects)[r];
        out.objects[requireValue(row[0], "LiquidObject.ID", r)] = {
            row[1].value_or(0), std::bit_cast<float>(row[2].value_or(0)), std::bit_cast<float>(row[3].value_or(0)),
            row[4].value_or(0), row[5].value_or(0)};
    }
    for (size_t r = 0; r < materials->size(); ++r) {
        const auto& row = (*materials)[r];
        out.materials[requireValue(row[0], "LiquidMaterial.ID", r)] = {row[1].value_or(0), row[2].value_or(0)};
    }
    std::map<uint32_t, std::vector<TextureRow>> texturesByType;
    for (size_t r = 0; r < textures->size(); ++r) {
        const auto& row = (*textures)[r];
        texturesByType[requireValue(row[0], "LiquidTypeXTexture.LiquidTypeID", r)].push_back(
            {row[1].value_or(0), row[2].value_or(0), static_cast<int32_t>(row[3].value_or(0))});
    }
    for (size_t r = 0; r < types->size(); ++r) {
        uint32_t id = requireValue((*types)[r][0], "LiquidType.ID", r);
        TypeRow row;
        row.name = (*typeNames)[r][0].value_or("");
        row.flags = (*types)[r][1].value_or(0);
        row.materialId = (*types)[r][2].value_or(0);
        auto rows = texturesByType.find(id);
        if (rows != texturesByType.end()) {
            row.slots = splitSlots(id, std::move(rows->second), (*frameCounts)[r][0].value_or(std::vector<uint32_t>{}));
        }
        out.types[id] = std::move(row);
    }
    return out;
}

}  // namespace husk::liquiddb
