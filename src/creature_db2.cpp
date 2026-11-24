#include "creature_db2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <stdexcept>

#include "db2table.hpp"

namespace husk::creaturedb {

namespace {

std::string tablePath(const std::string& db2Dir, const char* file) {
    return (std::filesystem::path(db2Dir) / file).string();
}

uint32_t requiredId(const std::optional<std::vector<uint32_t>>& v, const char* table, size_t row) {
    if (!v || v->empty()) throw std::runtime_error(std::string(table) + ".ID: expected a value in row " + std::to_string(row) + ", got none");
    return v->front();
}

uint32_t first(const std::optional<std::vector<uint32_t>>& v) { return v && !v->empty() ? v->front() : 0; }

float finite(uint32_t bits, const char* column, uint32_t id) {
    float value = std::bit_cast<float>(bits);
    if (!std::isfinite(value)) {
        throw std::runtime_error(std::string(column) + ": expected a finite number in row " + std::to_string(id) + ", got " +
                                 std::to_string(value));
    }
    return value;
}

// An ID -> Name_lang table (CreatureType, CreatureFamily).
std::optional<std::unordered_map<uint32_t, std::string>> loadNames(const std::string& path, const char* table,
                                                                   const std::string& dbdDir, std::ostream& err) {
    auto ids = db2table::readNamedArrayColumns(path, dbdDir, {"ID"}, err);
    auto names = db2table::readNamedStringColumns(path, dbdDir, {"Name_lang"}, err);
    if (!ids || !names) return std::nullopt;
    if (ids->size() != names->size()) {
        err << path << ": column reads disagree on row count (" << ids->size() << ", " << names->size() << ")\n";
        return std::nullopt;
    }
    std::unordered_map<uint32_t, std::string> out;
    for (size_t r = 0; r < ids->size(); ++r) out[requiredId((*ids)[r][0], table, r)] = (*names)[r][0].value_or("");
    return out;
}

}  // namespace

std::optional<Data> load(const std::string& db2Dir, const std::string& dbdDir, std::ostream& err) {
    std::string creaturePath = tablePath(db2Dir, "creature.db2");
    auto creatures = db2table::readNamedArrayColumns(
        creaturePath, dbdDir, {"ID", "CreatureType", "CreatureFamily", "DisplayID", "DisplayProbability", "Classification"}, err);
    auto creatureNames = db2table::readNamedStringColumns(creaturePath, dbdDir, {"Name_lang", "Title_lang"}, err);
    auto types = loadNames(tablePath(db2Dir, "creaturetype.db2"), "CreatureType", dbdDir, err);
    auto families = loadNames(tablePath(db2Dir, "creaturefamily.db2"), "CreatureFamily", dbdDir, err);
    auto displays = db2table::readNamedArrayColumns(tablePath(db2Dir, "creaturedisplayinfo.db2"), dbdDir,
                                                    {"ID", "ModelID", "CreatureModelScale"}, err);
    auto models = db2table::readNamedArrayColumns(tablePath(db2Dir, "creaturemodeldata.db2"), dbdDir,
                                                  {"ID", "FileDataID", "ModelScale"}, err);
    if (!creatures || !creatureNames || !types || !families || !displays || !models) return std::nullopt;
    if (creatures->size() != creatureNames->size()) {
        err << creaturePath << ": column reads disagree on row count (" << creatures->size() << ", "
            << creatureNames->size() << ")\n";
        return std::nullopt;
    }

    Data out;
    out.types = std::move(*types);
    out.families = std::move(*families);
    for (size_t r = 0; r < creatures->size(); ++r) {
        const auto& row = (*creatures)[r];
        Creature creature;
        creature.id = requiredId(row[0], "Creature", r);
        creature.type = first(row[1]);
        creature.family = first(row[2]);
        creature.classification = first(row[5]);
        for (size_t i = 0; i < 4; ++i) {
            if (row[3] && i < row[3]->size()) creature.displays[i] = (*row[3])[i];
            if (row[4] && i < row[4]->size()) creature.weights[i] = finite((*row[4])[i], "Creature.DisplayProbability", creature.id);
        }
        creature.name = (*creatureNames)[r][0].value_or("");
        creature.title = (*creatureNames)[r][1].value_or("");
        out.creatures.push_back(std::move(creature));
    }
    std::sort(out.creatures.begin(), out.creatures.end(), [](const Creature& a, const Creature& b) { return a.id < b.id; });
    for (size_t r = 0; r < displays->size(); ++r) {
        const auto& row = (*displays)[r];
        uint32_t id = requiredId(row[0], "CreatureDisplayInfo", r);
        out.displays[id] = {first(row[1]), finite(first(row[2]), "CreatureDisplayInfo.CreatureModelScale", id)};
    }
    for (size_t r = 0; r < models->size(); ++r) {
        const auto& row = (*models)[r];
        uint32_t id = requiredId(row[0], "CreatureModelData", r);
        out.models[id] = {first(row[1]), finite(first(row[2]), "CreatureModelData.ModelScale", id)};
    }
    return out;
}

}  // namespace husk::creaturedb
