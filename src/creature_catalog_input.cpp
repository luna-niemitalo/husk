#include "creature_catalog_input.hpp"

#include <map>
#include <set>

#include "uuid.hpp"

namespace husk::creaturecatalog {

namespace {

canon::Ref db2Ref(const char* table, uint32_t row, const std::string& name) {
    return {canon::Db2Row{table, row}, name, name.empty() ? canon::NameSource::None : canon::NameSource::Db2};
}

// Row 0 is "none" in every table this names rows of; a row the table lacks
// keeps its identity without a name.
std::optional<canon::Ref> namedRow(const char* table, uint32_t row, const std::unordered_map<uint32_t, std::string>& names) {
    if (row == 0) return std::nullopt;
    auto it = names.find(row);
    return db2Ref(table, row, it == names.end() ? std::string() : it->second);
}

}  // namespace

canon::CreatureCatalog assemble(const creaturedb::Data& data) {
    canon::CreatureCatalog out;
    out.subject = {canon::None{}, "creatures", canon::NameSource::Synthesized};

    std::map<uint32_t, size_t> displayIndex;
    for (const creaturedb::Creature& c : data.creatures) {
        for (uint32_t display : c.displays) {
            if (display != 0) displayIndex.emplace(display, 0);
        }
    }
    for (auto& [row, index] : displayIndex) {
        index = out.displays.size();
        canon::CreatureDisplay display;
        display.id = nameUuid("creature_display/CreatureDisplayInfo/" + std::to_string(row));
        display.ref = db2Ref("CreatureDisplayInfo", row, "");
        // A display the client's table lacks (an encrypted section) keeps
        // only its identity.
        if (auto info = data.displays.find(row); info != data.displays.end()) {
            const creaturedb::Display& row = info->second;
            display.scale = row.scale;
            display.sizeClass = row.sizeClass;
            static const std::unordered_map<uint32_t, std::string> kUnnamed;
            display.objectEffectPackage = namedRow("ObjectEffectPackage", row.objectEffectPackage, kUnnamed);
            display.stateSpellVisualKit = namedRow("SpellVisualKit", row.stateSpellVisualKit, kUnnamed);
            display.animReplacementSet = namedRow("AnimReplacementSet", row.animReplacementSet, kUnnamed);
            display.dissolveEffect = namedRow("DissolveEffect", row.dissolveEffect, kUnnamed);
            display.mountPoofSpellVisualKit = namedRow("SpellVisualKit", row.mountPoofSpellVisualKit, kUnnamed);
            if (auto model = data.models.find(row.modelData); model != data.models.end()) {
                if (model->second.fileDataId != 0) display.model = model->second.fileDataId;
                display.baseScale = model->second.scale;
            }
        }
        out.displays.push_back(std::move(display));
    }

    std::set<uint32_t> models;
    for (const canon::CreatureDisplay& display : out.displays) {
        if (display.model) models.insert(*display.model);
    }
    // The key is writers::referenceId's for a model by file id, so a model
    // has the same id here as in every other document's references.
    for (uint32_t fdid : models) {
        out.models.push_back({nameUuid("model/file_data_id/" + std::to_string(fdid)),
                              {canon::FileDataId{fdid}, "", canon::NameSource::None}});
    }

    for (const creaturedb::Creature& c : data.creatures) {
        canon::Creature creature;
        creature.id = nameUuid("creature/Creature/" + std::to_string(c.id));
        creature.ref = db2Ref("Creature", c.id, c.name);
        creature.title = c.title;
        creature.type = namedRow("CreatureType", c.type, data.types);
        creature.family = namedRow("CreatureFamily", c.family, data.families);
        creature.classification = c.classification;
        for (size_t i = 0; i < c.displays.size(); ++i) {
            if (c.displays[i] != 0) creature.displays.push_back({displayIndex.at(c.displays[i]), c.weights[i]});
        }
        out.creatures.push_back(std::move(creature));
    }
    return out;
}

}  // namespace husk::creaturecatalog
