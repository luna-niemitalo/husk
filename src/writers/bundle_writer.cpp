#include "writers/bundle_writer.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include "canon_terrain.hpp"  // terrainTileCorner, the world grid
#include "json_writer.hpp"
#include "writers/bundle_common.hpp"
#include "writers/material_writer.hpp"
#include "writers/writer_common.hpp"

namespace husk::writers {

namespace {

std::string billboardName(canon::BillboardMode mode) {
    switch (mode) {
        case canon::BillboardMode::None: return "none";
        case canon::BillboardMode::Spherical: return "spherical";
        case canon::BillboardMode::CylindricalLockX: return "cylindrical_lock_x";
        case canon::BillboardMode::CylindricalLockY: return "cylindrical_lock_y";
        case canon::BillboardMode::CylindricalLockZ: return "cylindrical_lock_z";
    }
    return "none";
}

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

void writeInstanceRef(json::Writer& w, const canon::InstanceRef& ref) {
    w.beginObject();
    w.key("set");
    w.value(static_cast<int64_t>(ref.set));
    w.key("id");
    w.value(static_cast<int64_t>(ref.id));
    w.endObject();
}

// resources.mesh -- appends every mesh BufferSlice to `meshBin` and writes
// the section (including the "primitives" inline array) to `w`. Takes the
// whole `model`, not just `model.mesh`, so each primitive's own
// "material_index" can be resolved via canon::resolveMaterialIndex (I2) --
// `model.materials` is deduped and no longer 1:1 with `model.mesh
// .primitives`, so a primitive's material must be looked up through
// `model.primitiveMaterials`, not assumed to share its own index.
template <typename T>
const char* componentTypeName() {
    if constexpr (std::is_same_v<T, uint8_t>) return "u8";
    if constexpr (std::is_same_v<T, uint16_t>) return "u16";
    if constexpr (std::is_same_v<T, uint32_t>) return "u32";
    if constexpr (std::is_same_v<T, int32_t>) return "i32";
    if constexpr (std::is_same_v<T, float>) return "f32";
}

const char* domainName(canon::AttributeDomain domain) {
    switch (domain) {
        case canon::AttributeDomain::Vertex: return "vertex";
        case canon::AttributeDomain::Face: return "face";
        case canon::AttributeDomain::Part: return "part";
    }
    throw std::logic_error("unreachable AttributeDomain");
}

void writeAttributes(json::Writer& w, const std::vector<canon::AttributeChannel>& attributes,
                     std::vector<uint8_t>& meshBin) {
    w.beginArray();
    for (const canon::AttributeChannel& a : attributes) {
        w.beginObject();
        w.key("name");
        w.value(a.name);
        w.key("domain");
        w.value(domainName(a.domain));
        w.key("data");
        writeBufferSlice(w, std::visit(
                                [&](const auto& values) {
                                    using T = typename std::decay_t<decltype(values)>::value_type;
                                    return appendComponents(meshBin, "mesh.bin", values, a.components,
                                                            componentTypeName<T>());
                                },
                                a.values));
        w.endObject();
    }
    w.endArray();
}

void writeMeshSection(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& meshBin) {
    const canon::Mesh& mesh = model.mesh;
    w.beginObject();

    w.key("positions");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.positions, 3, "f32"), "POSITION");

    w.key("normals");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.normals, 3, "f32"), "NORMAL");

    w.key("uv_sets");
    w.beginArray();
    for (size_t k = 0; k < mesh.uvSets.size(); ++k) {
        std::string semantic = "TEXCOORD_" + std::to_string(k);
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.uvSets[k], 2, "f32"), semantic.c_str());
    }
    w.endArray();

    if (!mesh.colorSets.empty()) {
        w.key("colors");
        w.beginArray();
        for (size_t k = 0; k < mesh.colorSets.size(); ++k) {
            std::string semantic = "COLOR_" + std::to_string(k);
            writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.colorSets[k], 4, "u8"), semantic.c_str());
        }
        w.endArray();
    }

    if (!mesh.skinning.empty()) {
        std::vector<std::array<uint8_t, 4>> joints;
        std::vector<std::array<float, 4>> weights;
        joints.reserve(mesh.skinning.size());
        weights.reserve(mesh.skinning.size());
        for (const auto& s : mesh.skinning) {
            joints.push_back(s.joints);
            weights.push_back(s.weights);
        }
        w.key("joints0");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", joints, 4, "u8"), "JOINTS_0");
        w.key("weights0");
        writeBufferSlice(w, appendArray(meshBin, "mesh.bin", weights, 4, "f32"), "WEIGHTS_0");
    }

    w.key("indices");
    writeBufferSlice(w, appendArray(meshBin, "mesh.bin", mesh.indices, 1, "u32"), "INDICES");

    w.key("primitives");
    w.beginArray();
    for (size_t i = 0; i < mesh.primitives.size(); ++i) {
        const canon::PrimitiveGeoset& prim = mesh.primitives[i];
        w.beginObject();
        w.key("geoset_id");
        w.value(static_cast<int64_t>(prim.geoset.group * 100 + prim.geoset.variant));
        w.key("geoset_group");
        w.value(static_cast<int64_t>(prim.geoset.group));
        w.key("geoset_variant");
        w.value(static_cast<int64_t>(prim.geoset.variant));
        w.key("index_start");
        w.value(static_cast<int64_t>(prim.indexStart));
        w.key("index_count");
        w.value(static_cast<int64_t>(prim.indexCount));
        // Resolved via canon::resolveMaterialIndex, not assumed to equal
        // `i` -- model.materials is deduped (canon_model.hpp's own doc
        // comment); see this function's own doc comment above and
        // tests/test_writers_bundle.cpp's explicit regression test. A
        // nullopt here means `model` holds a `primitiveMaterials` entry
        // this bundle format has no way to represent (a bundle-external
        // material reference, not implemented anywhere yet) -- an interior
        // invariant violation for this writer's own current scope, so it
        // throws rather than silently writing a wrong index (FOREIGN_DATA
        // policy: the interior trusts an already-validated canon::Model,
        // it doesn't limp past a shape it can't handle).
        auto matIdx = canon::resolveMaterialIndex(model, i);
        if (!matIdx) {
            throw std::runtime_error("writeBundle: primitive " + std::to_string(i) +
                                      "'s material reference isn't a local RecordIndex -- bundle writer has no "
                                      "way to represent a bundle-external material reference yet");
        }
        w.key("material_index");
        w.value(static_cast<int64_t>(*matIdx));
        if (prim.part) {
            w.key("part");
            w.value(static_cast<int64_t>(*prim.part));
        }
        w.endObject();
    }
    w.endArray();

    w.key("parts");
    w.beginArray();
    for (const canon::MeshPart& part : mesh.parts) {
        w.beginObject();
        w.key("ref");
        writeRef(w, part.ref);
        w.key("bounds_min");
        writeVec3Value(w, part.boundsMin);
        w.key("bounds_max");
        writeVec3Value(w, part.boundsMax);
        for (auto [key, refs] : {std::pair{"fog_volumes", &part.fogs}, std::pair{"ambient_color_volumes", &part.ambientVolumes},
                                 std::pair{"ambient_particle_volumes", &part.particulateVolumes}}) {
            if (refs->empty()) continue;
            w.key(key);
            w.beginArray();
            for (const canon::InstanceRef& ref : *refs) writeInstanceRef(w, ref);
            w.endArray();
        }
        if (!part.lights.empty()) {
            w.key("lights");
            w.beginArray();
            for (uint32_t light : part.lights) w.value(static_cast<int64_t>(light));
            w.endArray();
        }
        if (!part.lightSets.empty()) {
            w.key("light_sets");
            w.beginArray();
            for (const std::vector<uint32_t>& set : part.lightSets) {
                w.beginArray();
                for (uint32_t light : set) w.value(static_cast<int64_t>(light));
                w.endArray();
            }
            w.endArray();
        }
        if (!part.portals.empty()) {
            w.key("portals");
            w.beginArray();
            for (const canon::PortalLink& link : part.portals) {
                w.beginObject();
                w.key("portal");
                w.value(static_cast<int64_t>(link.portal));
                w.key("to_part");
                w.value(static_cast<int64_t>(link.toPart));
                w.key("unresolved");
                w.beginObject();
                w.key("side");
                w.value(static_cast<int64_t>(link.side));
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(part.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();

    w.key("portals");
    w.beginArray();
    for (const canon::Portal& portal : mesh.portals) {
        w.beginObject();
        w.key("vertices");
        w.beginArray();
        for (const canon::Vec3& v : portal.vertices) writeVec3Value(w, v);
        w.endArray();
        w.key("normal");
        writeVec3Value(w, portal.normal);
        w.key("distance");
        w.value(static_cast<double>(portal.distance));
        if (!portal.sourceExtra.empty()) {
            w.key("unresolved");
            w.beginObject();
            w.key("extra_record");
            w.beginArray();
            for (uint8_t b : portal.sourceExtra) w.value(static_cast<int64_t>(b));
            w.endArray();
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();

    w.key("attributes");
    writeAttributes(w, mesh.attributes, meshBin);

    w.endObject();
}

void writeSkeletonSection(json::Writer& w, const canon::Skeleton& skeleton, std::vector<uint8_t>& skeletonBin) {
    w.beginObject();

    w.key("joint_count");
    w.value(static_cast<int64_t>(skeleton.joints.size()));

    std::vector<int32_t> parents;
    std::vector<canon::Vec3> bindTranslations;
    parents.reserve(skeleton.joints.size());
    bindTranslations.reserve(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        parents.push_back(skeleton.joints[i].parent);
        bindTranslations.push_back(localBindTranslation(skeleton, i));
    }

    w.key("parents");
    writeBufferSlice(w, appendArray(skeletonBin, "skeleton.bin", parents, 1, "i32"));
    w.key("bind_translation");
    writeBufferSlice(w, appendArray(skeletonBin, "skeleton.bin", bindTranslations, 3, "f32"));

    w.key("joints");
    w.beginArray();
    for (const canon::Joint& joint : skeleton.joints) {
        w.beginObject();
        w.key("billboard");
        w.value(billboardName(joint.billboard));
        w.key("ref");
        writeRef(w, joint.ref);
        w.key("structural_label");
        w.value(joint.structuralLabel);
        w.endObject();
    }
    w.endArray();

    w.endObject();
}

// One channel (translation/rotation/scale) of one joint's ComposedJointCurves.
template <typename T>
void writeAnimChannel(json::Writer& w, const canon::Curve<T>& curve, std::vector<uint8_t>& animBin) {
    std::vector<float> times;
    std::vector<T> values;
    times.reserve(curve.keyframes.size());
    values.reserve(curve.keyframes.size());
    for (const auto& [t, v] : curve.keyframes) {
        times.push_back(t);
        values.push_back(v);
    }

    uint32_t componentCount;
    if constexpr (std::is_same_v<T, canon::Vec3>) {
        componentCount = 3;
    } else if constexpr (std::is_same_v<T, canon::Quat>) {
        componentCount = 4;
    } else {
        componentCount = 1;
    }

    w.beginObject();
    w.key("times");
    writeBufferSlice(w, appendArray(animBin, "animation.bin", times, 1, "f32"));
    w.key("values");
    writeBufferSlice(w, appendArray(animBin, "animation.bin", values, componentCount, "f32"));
    w.key("interpolation");
    w.value(interpolationName(curve.interpolation));
    w.endObject();
}

void writeAnimationSection(json::Writer& w, const canon::Skeleton& skeleton,
                            const std::vector<canon::AnimationClip>& clips, std::vector<uint8_t>& animBin) {
    w.beginArray();
    for (const canon::AnimationClip& clip : clips) {
        w.beginObject();
        w.key("sequence_index");
        w.value(static_cast<int64_t>(clip.sequence.index));
        w.key("sequence_kind");
        w.value(sequenceKindName(clip.sequence.kind));

        w.key("joints");
        w.beginArray();
        for (size_t jointIndex = 0; jointIndex < clip.boneCurves.size(); ++jointIndex) {
            std::optional<ComposedJointCurves> composed = composeJointCurves(skeleton, clip, jointIndex);
            if (!composed) continue;  // sparse: only real curve data gets an entry

            w.beginObject();
            w.key("joint_index");
            w.value(static_cast<int64_t>(jointIndex));
            w.key("translation");
            writeAnimChannel(w, composed->translation, animBin);
            w.key("rotation");
            writeAnimChannel(w, composed->rotation, animBin);
            w.key("scale");
            writeAnimChannel(w, composed->scale, animBin);
            w.endObject();
        }
        w.endArray();

        w.endObject();
    }
    w.endArray();
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

void writeMaterialsSection(json::Writer& w, const std::vector<canon::Material>& materials, TextureTable& textures,
                           UvMapTable& uvMaps) {
    w.beginArray();
    for (const canon::Material& material : materials) {
        w.beginObject();

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




// resources.liquids -- appends every liquid's arrays to `liquidBin`.
void writeLiquids(json::Writer& w, const canon::Model& model, std::vector<uint8_t>& liquidBin) {
    w.beginArray();
    for (const canon::LiquidGrid& l : model.liquids) {
        w.beginObject();
        writeIntField(w, "part", l.part);
        w.key("material_index");
        w.value(static_cast<int64_t>(std::get<canon::RecordIndex>(l.material).value));
        w.key("liquid_type");
        writeRef(w, l.liquidType);
        writeVec3Field(w, "corner", l.corner);
        writeFloatField(w, "tile_size", l.tileSize);
        writeIntField(w, "verts_x", l.vertsX);
        writeIntField(w, "verts_y", l.vertsY);
        w.key("heights");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.heights, 1, "f32"));
        w.key("tile_exists");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.tileExists, 1, "u8"));
        w.key("source");
        w.beginObject();
        writeIntField(w, "liquid_type", l.sourceLiquidType);
        w.key("vertex_data");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.vertexData, 4, "u8"));
        w.key("tile_flags");
        writeBufferSlice(w, appendArray(liquidBin, "liquid.bin", l.tileFlags, 1, "u8"));
        w.endObject();
        w.endObject();
    }
    w.endArray();
}

void writeCollision(json::Writer& w, const canon::CollisionMesh& c, std::vector<uint8_t>& collisionBin) {
    constexpr const char* kBin = "collision.bin";
    w.beginObject();
    w.key("positions");
    writeBufferSlice(w, appendArray(collisionBin, kBin, c.positions, 3, "f32"));
    w.key("indices");
    writeBufferSlice(w, appendArray(collisionBin, kBin, c.indices, 1, "u32"));
    if (!c.triangleNormals.empty()) {
        w.key("triangle_normals");
        writeBufferSlice(w, appendArray(collisionBin, kBin, c.triangleNormals, 3, "f32"));
    }
    w.endObject();
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
        });

    w.key("resources");
    w.beginObject();

    w.key("geometry");
    w.beginObject();
    w.key("mesh");
    writeMeshSection(w, model, meshBin);
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
    writeMaterialsSection(w, model.materials, textures, uvMaps);
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

    w.endObject();

    std::string bytes = text.str();
    writeFile(bundleDir / "manifest.json", std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

}  // namespace husk::writers
