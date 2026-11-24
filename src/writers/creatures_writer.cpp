#include "writers/creatures_writer.hpp"

#include <unordered_map>
#include <sstream>

#include "json_writer.hpp"

namespace husk::writers {

std::string creatureCatalogDocument(const canon::CreatureCatalog& catalog, const Provenance& provenance,
                                    const AssetUris& modelUris) {
    std::unordered_map<uint32_t, const std::string*> modelId;
    for (const canon::CreatureModel& model : catalog.models) {
        modelId.emplace(std::get<canon::FileDataId>(model.ref.id).value, &model.id);
    }

    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "creatures", catalog.subject, provenance);
    w.key("definition");
    w.beginObject();
    w.endObject();
    w.key("selection");
    w.beginObject();
    w.endObject();

    w.key("references");
    w.beginArray();
    for (const canon::CreatureModel& model : catalog.models) {
        w.beginObject();
        w.key("id");
        w.value(model.id);
        w.key("kind");
        w.value("model");
        w.key("asset");
        writeAssetRef(w, model.ref, modelUris);
        w.endObject();
    }
    w.endArray();

    w.key("creatures");
    w.beginArray();
    for (const canon::Creature& creature : catalog.creatures) {
        w.beginObject();
        w.key("id");
        w.value(creature.id);
        writeName(w, creature.ref);
        if (!creature.title.empty()) {
            w.key("title");
            w.value(creature.title);
        }
        if (creature.type) {
            w.key("type");
            writeRef(w, *creature.type);
        }
        if (creature.family) {
            w.key("family");
            writeRef(w, *creature.family);
        }
        w.key("displays");
        w.beginArray();
        for (const canon::CreatureDisplayUse& use : creature.displays) {
            w.beginObject();
            w.key("display");
            w.value(catalog.displays.at(use.display).id);
            writeFloatField(w, "weight", use.weight);
            w.endObject();
        }
        w.endArray();
        w.key("source");
        w.beginObject();
        w.key("id");
        writeIdentity(w, creature.ref.id);
        writeIntField(w, "classification", creature.classification);
        w.endObject();
        w.endObject();
    }
    w.endArray();

    w.key("displays");
    w.beginArray();
    for (const canon::CreatureDisplay& display : catalog.displays) {
        w.beginObject();
        w.key("id");
        w.value(display.id);
        if (display.model) {
            w.key("model");
            w.value(*modelId.at(*display.model));
        }
        if (display.preset) {
            w.key("preset");
            w.value(*display.preset);
        }
        if (display.scale) writeFloatField(w, "scale", *display.scale);
        if (display.baseScale) writeFloatField(w, "base_scale", *display.baseScale);
        for (auto [key, ref] : {std::pair{"object_effect_package", &display.objectEffectPackage},
                                std::pair{"state_spell_visual_kit", &display.stateSpellVisualKit},
                                std::pair{"anim_replacement_set", &display.animReplacementSet},
                                std::pair{"dissolve_effect", &display.dissolveEffect},
                                std::pair{"mount_poof_spell_visual_kit", &display.mountPoofSpellVisualKit}}) {
            if (!*ref) continue;
            w.key(key);
            writeRef(w, **ref);
        }
        w.key("source");
        w.beginObject();
        w.key("id");
        writeIdentity(w, display.ref.id);
        if (display.sizeClass) writeIntField(w, "size_class", *display.sizeClass);
        w.endObject();
        w.endObject();
    }
    w.endArray();

    w.endObject();
    return text.str();
}

}  // namespace husk::writers
