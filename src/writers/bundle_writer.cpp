#include "writers/bundle_writer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include "canon_terrain.hpp"  // terrainTileCorner, the world grid
#include "json_writer.hpp"
#include "writers/bundle_bin_sections.hpp"
#include "writers/bundle_common.hpp"
#include "writers/material_writer.hpp"
#include "writers/writer_common.hpp"

namespace husk::writers {

namespace {

std::string knownRoleName(canon::KnownRole role) {
    switch (role) {
        case canon::KnownRole::Unknown: return "unknown";
        case canon::KnownRole::Diffuse: return "diffuse";
        case canon::KnownRole::Specular: return "specular";
        case canon::KnownRole::Emission: return "emission";
        case canon::KnownRole::Alpha: return "alpha";
        case canon::KnownRole::Detail: return "detail";
        case canon::KnownRole::Env: return "env";
    }
    return "unknown";
}

void writeLayerRole(json::Writer& w, const canon::LayerRole& role) {
    std::visit(
        [&w](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, canon::KnownRole>) {
                w.value(knownRoleName(r));
            } else {
                w.value(r);
            }
        },
        role);
}

void writeRgba(json::Writer& w, const std::array<float, 4>& rgba) {
    w.beginArray();
    for (float c : rgba) w.value(static_cast<double>(c));
    w.endArray();
}

// A choice's bound textures, by (option, choice) in the flat option list.
using ChoiceTextures = std::map<std::pair<uint32_t, uint32_t>, std::vector<const canon::ChoiceTexture*>>;

// `materialBase`: the reference index of material document 0.
void writeCustomizationOption(json::Writer& w, uint32_t chrModelId, const canon::Option& option, uint32_t optionIndex,
                              const ChoiceTextures& choiceTextures, uint32_t materialBase, TextureTable& textures) {
    w.beginObject();
    w.key("ref");
    writeRef(w, option.ref);
    w.key("chr_model");
    writeRef(w, canon::Ref{canon::Db2Row{"ChrModel", chrModelId}, "", canon::NameSource::None});
    w.key("order");
    w.value(static_cast<int64_t>(option.orderIndex));
    w.key("category");
    w.beginObject();
    w.key("ref");
    writeRef(w, option.category.ref);
    w.key("order");
    w.value(static_cast<int64_t>(option.category.orderIndex));
    w.endObject();
    w.key("choices");
    w.beginArray();
    for (const canon::Choice& choice : option.choices) {
        w.beginObject();
        w.key("ref");
        writeRef(w, choice.ref);
        w.key("order");
        w.value(static_cast<int64_t>(choice.orderIndex));
        if (choice.geoset) {
            w.key("geoset_id");
            w.value(static_cast<int64_t>(choice.geoset->group * 100 + choice.geoset->variant));
        }
        auto bound = choiceTextures.find({optionIndex, static_cast<uint32_t>(&choice - option.choices.data())});
        if (bound != choiceTextures.end()) {
            w.key("textures");
            w.beginArray();
            for (const canon::ChoiceTexture* t : bound->second) {
                w.beginObject();
                w.key("material");
                w.value(static_cast<int64_t>(materialBase + t->material));
                w.key("input");
                w.value(t->input);
                w.key("texture");
                w.value(static_cast<int64_t>(textures.add(t->texture)));
                if (t->onlyWith) {
                    w.key("only_with");
                    w.beginObject();
                    w.key("option");
                    w.value(static_cast<int64_t>(t->onlyWith->option));
                    w.key("choice");
                    w.value(static_cast<int64_t>(t->onlyWith->choice));
                    w.endObject();
                }
                w.endObject();
            }
            w.endArray();
        }
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

void writeOptions(json::Writer& w, const canon::Model& model, TextureTable& textures, uint32_t materialBase) {
    if (!model.textureSlots.empty()) {
        w.key("texture_slots");
        w.beginArray();
        for (const canon::TextureSlot& slot : model.textureSlots) {
            w.beginObject();
            w.key("name");
            w.value(slot.name);
            if (slot.sourceType) {
                w.key("source");
                w.beginObject();
                w.key("texture_type");
                w.value(static_cast<int64_t>(*slot.sourceType));
                w.endObject();
            }
            if (slot.inferredFrom) {
                w.key("inferred_from");
                w.value(*slot.inferredFrom);
            }
            w.key("options");
            w.beginArray();
            for (const canon::TextureRef& option : slot.options) w.value(static_cast<int64_t>(textures.add(option)));
            w.endArray();
            if (slot.composite) {
                w.key("composite");
                w.value(static_cast<int64_t>(materialBase + *slot.composite));
            }
            w.endObject();
        }
        w.endArray();
    }
    bool anyOption = std::any_of(model.customization.begin(), model.customization.end(),
                                 [](const canon::Definition& d) { return !d.options.empty(); });
    if (anyOption) {
        w.key("customization");
        w.beginObject();
        w.key("options");
        w.beginArray();
        ChoiceTextures choiceTextures;
        for (const canon::ChoiceTexture& t : model.choiceTextures) choiceTextures[{t.choice.option, t.choice.choice}].push_back(&t);
        uint32_t optionIndex = 0;
        for (const canon::Definition& definition : model.customization) {
            for (const canon::Option& option : definition.options) {
                writeCustomizationOption(w, definition.chrModelId, option, optionIndex++, choiceTextures, materialBase, textures);
            }
        }
        w.endArray();
        w.endObject();
    }
    if (!model.items.empty()) {
        w.key("items");
        w.beginArray();
        for (const canon::Ref& item : model.items) writeRef(w, item);
        w.endArray();
    }
    if (model.particleColorOptions.empty()) return;
    w.key("particle_colors");
    w.beginArray();
    for (const canon::ParticleColorOption& option : model.particleColorOptions) {
        w.beginObject();
        w.key("ref");
        writeRef(w, option.ref);
        w.key("ramps");
        w.beginArray();
        for (const canon::ParticleColorRamp& ramp : option.ramps) {
            w.beginObject();
            w.key("start");
            writeRgba(w, ramp.start);
            w.key("mid");
            writeRgba(w, ramp.mid);
            w.key("end");
            writeRgba(w, ramp.end);
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
}

void writePresets(json::Writer& w, const std::vector<canon::Preset>& presets) {
    if (presets.empty()) return;
    w.key("presets");
    w.beginArray();
    for (const canon::Preset& preset : presets) {
        w.beginObject();
        w.key("textures");
        w.beginArray();
        for (const canon::SlotChoice& choice : preset.textures) {
            w.beginObject();
            w.key("slot");
            w.value(static_cast<int64_t>(choice.slot));
            w.key("option");
            w.value(static_cast<int64_t>(choice.option));
            w.endObject();
        }
        w.endArray();
        if (preset.shownGeosets) {
            w.key("shown_geosets");
            w.beginArray();
            for (uint32_t id : *preset.shownGeosets) w.value(static_cast<int64_t>(id));
            w.endArray();
        }
        if (preset.particleColors) {
            w.key("particle_colors");
            w.value(static_cast<int64_t>(*preset.particleColors));
        }
        if (preset.opacity) writeFloatField(w, "opacity", *preset.opacity);
        if (!preset.customization.empty()) {
            w.key("customization");
            w.beginArray();
            for (const canon::CustomizationPick& pick : preset.customization) {
                w.beginObject();
                w.key("option");
                w.value(static_cast<int64_t>(pick.option));
                w.key("choice");
                w.value(static_cast<int64_t>(pick.choice));
                w.endObject();
            }
            w.endArray();
        }
        if (!preset.items.empty()) {
            w.key("items");
            w.beginArray();
            for (const canon::PresetItem& item : preset.items) {
                w.beginObject();
                if (item.slot) {
                    w.key("slot");
                    w.value(*item.slot);
                }
                w.key("item");
                w.value(static_cast<int64_t>(item.item));
                w.key("source");
                w.beginObject();
                w.key("item_slot");
                w.value(static_cast<int64_t>(item.sourceSlot));
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        w.key("sources");
        w.beginArray();
        for (const canon::PresetSource& source : preset.sources) {
            w.beginObject();
            w.key("ref");
            writeRef(w, source.ref);
            writeFloatField(w, "scale", source.scale);
            writeFloatField(w, "base_scale", source.baseScale);
            if (!source.users.empty()) {
                w.key("users");
                w.beginArray();
                for (const canon::PresetUser& user : source.users) {
                    w.beginObject();
                    w.key("ref");
                    writeRef(w, user.ref);
                    if (!user.title.empty()) {
                        w.key("title");
                        w.value(user.title);
                    }
                    w.endObject();
                }
                w.endArray();
            }
            w.endObject();
        }
        w.endArray();
        if (!preset.unresolved.empty()) {
            w.key("unresolved");
            writeUnresolved(w, preset.unresolved);
        }
        w.endObject();
    }
    w.endArray();
}

// resources.uv_maps: what a layer samples its texture with (a coordinate
// set or environment mapping, plus its own animation and scroll), one entry
// per distinct combination, so layers sampling alike share an index.
class UvMapTable {
public:
    uint32_t add(const canon::MaterialLayer& layer) {
        std::ostringstream text;
        json::Writer w(text);
        w.beginObject();
        w.key("set");
        if (auto set = std::get_if<canon::UvSetIndex>(&layer.uv)) {
            w.value(static_cast<int64_t>(set->index));
        } else {
            w.value("environment");
        }
        if (layer.uvAnimation) {
            w.key("transform");
            w.beginObject();
            if (layer.uvAnimation->translation) {
                w.key("translation");
                writeInlineCurve(w, *layer.uvAnimation->translation, writeVec3Value);
            }
            if (layer.uvAnimation->rotation) {
                w.key("rotation");
                writeInlineCurve(w, *layer.uvAnimation->rotation, writeQuatValue);
            }
            if (layer.uvAnimation->scaling) {
                w.key("scaling");
                writeInlineCurve(w, *layer.uvAnimation->scaling, writeVec3Value);
            }
            w.endObject();
        }
        if (layer.uvScroll) {
            w.key("scroll");
            w.beginArray();
            w.value(static_cast<double>(layer.uvScroll->x));
            w.value(static_cast<double>(layer.uvScroll->y));
            w.endArray();
        }
        w.endObject();
        auto [it, added] = index_.try_emplace(text.str(), static_cast<uint32_t>(entries_.size()));
        if (added) entries_.push_back(text.str());
        return it->second;
    }

    // The "uv_maps" member of an open resources object.
    void write(json::Writer& w) const {
        w.key("uv_maps");
        w.beginArray();
        for (const std::string& entry : entries_) w.rawValue(entry);
        w.endArray();
    }

private:
    std::vector<std::string> entries_;
    std::unordered_map<std::string, uint32_t> index_;
};

void writeMaterialLayer(json::Writer& w, const canon::MaterialLayer& layer, TextureTable& textures, UvMapTable& uvMaps) {
    w.beginObject();
    w.key("identity");
    writeRef(w, layer.identity);
    w.key("role");
    writeLayerRole(w, layer.role);
    w.key("uv_map");
    w.value(static_cast<int64_t>(uvMaps.add(layer)));
    w.key("texture");
    w.value(static_cast<int64_t>(textures.add(layer.texture)));
    if (layer.slot) {
        w.key("slot");
        w.value(static_cast<int64_t>(*layer.slot));
    }
    if (layer.textureUnit) {
        w.key("texture_unit");
        w.value(static_cast<int64_t>(*layer.textureUnit));
    }

    if (layer.tint) {
        w.key("tint");
        writeInlineCurve(w, *layer.tint, writeVec3Value);
    }
    if (layer.alphaFade) {
        w.key("alpha_fade");
        writeInlineCurve(w, *layer.alphaFade, writeScalarValue);
    }
    w.endObject();
}

// `techniques[i]`: material i's technique reference indices.
void writeMaterialsSection(json::Writer& w, const std::vector<canon::Material>& materials, TextureTable& textures,
                           UvMapTable& uvMaps, const std::vector<std::vector<uint32_t>>& techniques) {
    w.beginArray();
    for (size_t m = 0; m < materials.size(); ++m) {
        const canon::Material& material = materials[m];
        w.beginObject();
        if (!techniques[m].empty()) {
            w.key("techniques");
            w.beginArray();
            for (uint32_t t : techniques[m]) w.value(static_cast<int64_t>(t));
            w.endArray();
        }

        w.key("layers");
        w.beginArray();
        for (const canon::MaterialLayer& layer : material.layers) {
            writeMaterialLayer(w, layer, textures, uvMaps);
        }
        w.endArray();

        if (material.framebufferBlend) {
            w.key("framebuffer_blend");
            w.value(framebufferBlendName(*material.framebufferBlend));
        }
        if (material.surfaceType) {
            w.key("surface_type");
            w.value(static_cast<int64_t>(*material.surfaceType));
        }
        if (material.diffuseLayer) {
            w.key("diffuse_layer");
            writeRef(w, *material.diffuseLayer);
        }
        if (material.specularLayer) {
            w.key("specular_layer");
            writeRef(w, *material.specularLayer);
        }
        if (material.emissionLayer) {
            w.key("emission_layer");
            writeRef(w, *material.emissionLayer);
        }
        if (material.alphaLayer) {
            w.key("alpha_layer");
            writeRef(w, *material.alphaLayer);
        }
        w.key("unlit");
        w.value(material.unlit);
        w.key("unfogged");
        w.value(material.unfogged);
        w.key("two_sided");
        w.value(material.twoSided);
        if (material.shader) {
            w.key("shader");
            writeRef(w, *material.shader);
        }
        if (material.pixelShader) {
            w.key("pixel_shader");
            w.value(*material.pixelShader);
        }
        if (material.vertexShader) {
            w.key("vertex_shader");
            w.value(*material.vertexShader);
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(material.sourceFlags));
        w.endObject();

        w.endObject();
    }
    w.endArray();
}


void writeAttachments(json::Writer& w, const std::vector<canon::Attachment>& attachments) {
    w.beginArray();
    for (const canon::Attachment& a : attachments) {
        w.beginObject();
        w.key("ref");
        writeRef(w, a.ref);
        writeIntField(w, "point_id", a.pointId);
        w.key("bone");
        writeRef(w, a.bone);
        writeVec3Field(w, "position", a.position);
        writeAnimated(w, "animate_attached", a.animateAttached, writeScalarValue);
        w.endObject();
    }
    w.endArray();
}

void writeEvents(json::Writer& w, const std::vector<canon::Event>& events) {
    w.beginArray();
    for (const canon::Event& e : events) {
        w.beginObject();
        w.key("identifier");
        w.value(e.identifier);
        writeIntField(w, "data", e.data);
        w.key("bone");
        writeRef(w, e.bone);
        writeVec3Field(w, "position", e.position);
        w.endObject();
    }
    w.endArray();
}

void writeLights(json::Writer& w, const std::vector<canon::Light>& lights) {
    w.beginArray();
    for (const canon::Light& l : lights) {
        w.beginObject();
        w.key("kind");
        switch (l.type) {
            case 0: w.value("directional"); break;
            case 1: w.value("point"); break;
            default: w.value("unknown"); break;
        }
        if (l.bone) {
            w.key("bone");
            writeRef(w, *l.bone);
        }
        writeVec3Field(w, "position", l.position);
        writeAnimated(w, "ambient_color", l.ambientColor, writeVec3Value);
        writeAnimated(w, "ambient_intensity", l.ambientIntensity, writeScalarValue);
        writeAnimated(w, "diffuse_color", l.diffuseColor, writeVec3Value);
        writeAnimated(w, "diffuse_intensity", l.diffuseIntensity, writeScalarValue);
        writeAnimated(w, "attenuation_start", l.attenuationStart, writeScalarValue);
        writeAnimated(w, "attenuation_end", l.attenuationEnd, writeScalarValue);
        writeAnimated(w, "visibility", l.visibility, writeScalarValue);
        w.key("source");
        w.beginObject();
        writeIntField(w, "type", l.type);
        w.endObject();
        w.endObject();
    }
    w.endArray();
}

int64_t placementSetIndex(const canon::Ref& set) { return std::get<canon::RecordIndex>(set.id).value; }

// `source`: the raw flag word and the record the entry was decoded from.
void writeSourceBlock(json::Writer& w, uint32_t flags, const canon::SourceRecord& origin) {
    w.key("source");
    w.beginObject();
    w.key("flags");
    w.value(hexFlags(flags));
    w.key("record");
    writeSourceRecord(w, origin);
    w.endObject();
}

void writeLocalLights(json::Writer& w, const std::vector<canon::LocalLight>& lights, TextureTable& textures) {
    w.beginArray();
    for (const canon::LocalLight& l : lights) {
        w.beginObject();
        w.key("ref");
        writeRef(w, l.ref);
        w.key("kind");
        switch (l.kind) {
            case canon::LocalLight::Kind::Point: w.value("point"); break;
            case canon::LocalLight::Kind::Spot: w.value("spot"); break;
            case canon::LocalLight::Kind::Directional: w.value("directional"); break;
            case canon::LocalLight::Kind::Ambient: w.value("ambient"); break;
        }
        writeVec3Field(w, "position", l.position);
        if (l.rotation) {
            w.key("rotation");
            writeQuatValue(w, *l.rotation);
        }
        if (l.eulerRotation) writeVec3Field(w, "euler_rotation", *l.eulerRotation);
        writeVec3Field(w, "color", l.color);
        writeFloatField(w, "intensity", l.intensity);
        writeFloatField(w, "intensity_multiplier", l.intensityMultiplier);
        w.key("attenuated");
        w.value(l.attenuated);
        writeFloatField(w, "attenuation_start", l.attenuationStart);
        writeFloatField(w, "attenuation_end", l.attenuationEnd);
        if (l.gradient) {
            w.key("gradient");
            w.beginObject();
            writeVec3Field(w, "outer_color", l.gradient->outerColor);
            writeFloatField(w, "blend_start", l.gradient->blendStart);
            writeFloatField(w, "blend_end", l.gradient->blendEnd);
            w.endObject();
        }
        if (l.cone) {
            w.key("cone");
            w.beginObject();
            if (l.cone->falloffExponent) writeFloatField(w, "falloff_exponent", *l.cone->falloffExponent);
            writeFloatField(w, "inner_angle", l.cone->innerAngle);
            writeFloatField(w, "outer_angle", l.cone->outerAngle);
            w.endObject();
        }
        if (l.flicker) {
            w.key("flicker");
            w.beginObject();
            writeIntField(w, "mode", l.flicker->mode);
            writeFloatField(w, "intensity", l.flicker->intensity);
            writeFloatField(w, "speed", l.flicker->speed);
            w.endObject();
        }
        w.key("casts_shadows");
        w.value(l.castsShadows);
        writeFloatField(w, "shadow_scale", l.shadowScale);
        if (l.cookie) {
            w.key("cookie");
            w.value(static_cast<int64_t>(textures.add(*l.cookie)));
        }
        if (l.placementSet) writeIntField(w, "placement_set", placementSetIndex(*l.placementSet));
        if (l.part) writeIntField(w, "part", *l.part);
        if (!l.unresolved.empty()) {
            w.key("unresolved");
            writeUnresolved(w, l.unresolved);
        }
        writeSourceBlock(w, l.sourceFlags, l.origin);
        w.endObject();
    }
    w.endArray();
}

}  // namespace

void writeBundle(const canon::Model& model, const std::filesystem::path& bundleDir, const Provenance& provenance,
                 const AssetUris& assetUris, const AssetUris& textureUris) {
    std::error_code ec;
    std::filesystem::create_directories(bundleDir, ec);
    if (ec) {
        throw std::runtime_error("bundle writer: could not create directory " + bundleDir.string() + ": " +
                                  ec.message());
    }

    std::vector<uint8_t> meshBin;
    std::vector<uint8_t> skeletonBin;
    std::vector<uint8_t> animBin;
    std::vector<uint8_t> liquidBin;
    std::vector<uint8_t> collisionBin;

    std::vector<OwnedSetEntry> owned;
    for (const canon::Model::OwnedSet& set : model.placementSets) owned.push_back({&set.set, set.alwaysOn, set.inlineDocument});
    std::vector<SetDocument> sets = writePlacementSetDocuments(owned, bundleDir, assetUris, provenance, textureUris);

    std::ostringstream manifestStream;
    json::Writer w(manifestStream);

    w.beginObject();
    writeContractHeader(w, "model", model.subject, provenance);

    // Owned placement sets are the model's first references, so set i is
    // reference i.
    std::unordered_set<std::string> writtenTextures;
    TextureTable textures(bundleDir, writtenTextures, "", &textureUris);
    w.key("definition");
    w.beginObject();
    writeOwnedSetDefinitions(w, sets);
    uint32_t materialBase = static_cast<uint32_t>(sets.size());
    writeOptions(w, model, textures, materialBase);
    w.endObject();
    for (size_t i = 0; i < model.materialDocuments.size(); ++i) {
        writeMaterialDocument(model.materialDocuments[i], bundleDir / "materials" / (std::to_string(i) + ".json"), provenance,
                              bundleDir, "../", writtenTextures);
    }
    // Techniques follow the material documents in the references.
    std::vector<std::string> techniqueNames;
    std::vector<std::vector<uint32_t>> materialTechniqueRefs;
    uint32_t techniqueBase = materialBase + static_cast<uint32_t>(model.materialDocuments.size());
    for (const canon::Material& material : model.materials) {
        std::vector<uint32_t> refs;
        for (const std::string& name : materialTechniques(material)) {
            auto at = std::find(techniqueNames.begin(), techniqueNames.end(), name);
            if (at == techniqueNames.end()) at = techniqueNames.insert(techniqueNames.end(), name);
            refs.push_back(techniqueBase + static_cast<uint32_t>(at - techniqueNames.begin()));
        }
        materialTechniqueRefs.push_back(std::move(refs));
    }
    writeTechniqueDocuments(techniqueNames, bundleDir, provenance);
    writeOwnedSetSelectionAndReferences(
        w, sets, [&](json::Writer& sw) { writePresets(sw, model.presets); },
        [&](json::Writer& rw) {
            for (size_t i = 0; i < model.materialDocuments.size(); ++i) {
                rw.beginObject();
                rw.key("kind");
                rw.value("material");
                rw.key("asset");
                writeRef(rw, model.materialDocuments[i].subject, "materials/" + std::to_string(i) + ".json");
                rw.endObject();
            }
            for (const std::string& name : techniqueNames) {
                rw.beginObject();
                rw.key("kind");
                rw.value("technique");
                rw.key("asset");
                writeRef(rw, canon::Ref{canon::None{}, name, canon::NameSource::Synthesized}, techniqueUri(name));
                rw.endObject();
            }
        });

    w.key("resources");
    w.beginObject();

    w.key("geometry");
    w.beginObject();
    w.key("mesh");
    MeshSlices mesh0 = writeMeshSection(w, model, meshBin);
    w.key("lod");
    writeLodSection(w, model, mesh0, meshBin);
    w.key("liquids");
    writeLiquids(w, model, liquidBin);
    bool hasCollision = !model.collision.indices.empty();
    if (hasCollision) {
        w.key("collision");
        writeCollision(w, model.collision, collisionBin);
    }
    w.endObject();

    w.key("rig");
    w.beginObject();
    w.key("skeleton");
    writeSkeletonSection(w, model.skeleton, skeletonBin);
    w.key("animation");
    writeAnimationSection(w, model.skeleton, model.animations, animBin);
    w.endObject();

    UvMapTable uvMaps;
    w.key("materials");
    writeMaterialsSection(w, model.materials, textures, uvMaps, materialTechniqueRefs);
    uvMaps.write(w);

    w.key("scene");
    w.beginObject();
    w.key("attachments");
    writeAttachments(w, model.scene.attachments);
    w.key("events");
    writeEvents(w, model.scene.events);
    w.key("animated_lights");
    writeLights(w, model.scene.lights);
    w.key("static_lights");
    writeLocalLights(w, model.scene.localLights, textures);
    if (model.defaultFog) {
        w.key("default_fog");
        writeInstanceRef(w, *model.defaultFog);
    }
    w.endObject();

    if (!model.surfaceTypes.empty()) {
        w.key("surface_types");
        writeSurfaceTypes(w, model.surfaceTypes);
    }
    if (!model.liquidTypes.empty()) {
        w.key("liquid_types");
        writeLiquidTypes(w, model.liquidTypes, [&](const canon::TextureRef& frame) {
            w.value(static_cast<int64_t>(textures.add(frame)));
        });
    }
    if (!textures.empty()) textures.write(w);
    w.endObject();  // resources
    w.endObject();  // root

    writeFile(bundleDir / "mesh.bin", meshBin);
    writeFile(bundleDir / "skeleton.bin", skeletonBin);
    writeFile(bundleDir / "animation.bin", animBin);
    if (!model.liquids.empty()) writeFile(bundleDir / "liquid.bin", liquidBin);
    if (hasCollision) writeFile(bundleDir / "collision.bin", collisionBin);

    std::ofstream manifestFile(bundleDir / "manifest.json", std::ios::binary);
    if (!manifestFile) {
        throw std::runtime_error("bundle writer: could not open manifest.json for writing in " +
                                  bundleDir.string());
    }
    manifestFile << manifestStream.str();
}

void writeWorldBundle(const canon::World& world, const std::filesystem::path& bundleDir, const Provenance& provenance,
                      const std::vector<std::string>& tileUris, const AssetUris& assetUris) {
    if (tileUris.size() != world.cells.size()) {
        throw std::runtime_error("world writer: expected one tile uri per cell (" + std::to_string(world.cells.size()) +
                                 "), got " + std::to_string(tileUris.size()));
    }
    std::filesystem::create_directories(bundleDir);
    std::vector<OwnedSetEntry> owned;
    for (const canon::PlacementSet& set : world.placements) owned.push_back({&set, true});
    std::vector<SetDocument> sets = writePlacementSetDocuments(owned, bundleDir, assetUris, provenance);
    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "world", world.subject, provenance);
    w.key("definition");
    w.beginObject();
    w.endObject();
    w.key("selection");
    w.beginObject();
    w.endObject();

    // Cell i's tile is reference i; the off-grid sets follow.
    w.key("references");
    w.beginArray();
    for (size_t i = 0; i < world.cells.size(); ++i) {
        w.beginObject();
        w.key("kind");
        w.value("model");
        w.key("asset");
        writeRef(w, world.cells[i].tile, tileUris[i]);
        w.endObject();
    }
    for (const SetDocument& set : sets) {
        w.beginObject();
        w.key("kind");
        w.value("placement_set");
        w.key("asset");
        writeRef(w, set.ref, set.uri);
        w.endObject();
    }
    w.endArray();

    // canon_terrain.hpp's terrainTileCorner, as a grid: tileX steps -Y,
    // tileY steps -X, cell (0, 0) at the map's north-west corner.
    canon::Vec2 origin = canon::terrainTileCorner(0, 0);
    w.key("grid");
    w.beginObject();
    writeVec3Field(w, "origin", {origin.x, origin.y, 0});
    writeFloatField(w, "cell_size", canon::kTerrainTileSize);
    writeVec3Field(w, "axis_i", {0, -1, 0});
    writeVec3Field(w, "axis_j", {-1, 0, 0});
    w.key("cells");
    w.beginArray();
    for (size_t i = 0; i < world.cells.size(); ++i) {
        const canon::WorldCell& cell = world.cells[i];
        w.beginObject();
        writeIntField(w, "i", cell.tileX);
        writeIntField(w, "j", cell.tileY);
        writeIntField(w, "tile", static_cast<int64_t>(i));
        writeVec3Field(w, "bounds_min", cell.boundsMin);
        writeVec3Field(w, "bounds_max", cell.boundsMax);
        w.endObject();
    }
    w.endArray();
    w.endObject();

    w.key("placements");
    w.beginArray();
    for (size_t i = 0; i < sets.size(); ++i) w.value(static_cast<int64_t>(world.cells.size() + i));
    w.endArray();

    w.key("zones");
    w.beginArray();
    for (const canon::Zone& zone : world.zones) {
        w.beginObject();
        w.key("id");
        w.value(zone.id);
        writeName(w, zone.ref);
        if (zone.parent) {
            w.key("parent");
            w.value(world.zones.at(*zone.parent).id);
        }
        w.key("cells");
        w.beginArray();
        for (const canon::ZoneCell& cell : zone.cells) {
            w.beginObject();
            writeIntField(w, "i", cell.tileX);
            writeIntField(w, "j", cell.tileY);
            writeFloatField(w, "coverage", cell.coverage);
            w.endObject();
        }
        w.endArray();
        w.key("source");
        w.beginObject();
        w.key("id");
        writeIdentity(w, zone.ref.id);
        w.endObject();
        w.endObject();
    }
    w.endArray();

    w.endObject();

    std::string bytes = text.str();
    writeFile(bundleDir / "manifest.json", std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

}  // namespace husk::writers
