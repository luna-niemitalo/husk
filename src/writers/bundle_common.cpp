#include "writers/bundle_common.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include "bundle_schema_embedded.hpp"

namespace husk::writers {

namespace {

std::string nameSourceName(canon::NameSource source) {
    switch (source) {
        case canon::NameSource::M2Embedded: return "m2_embedded";
        case canon::NameSource::AdtEmbedded: return "adt_embedded";
        case canon::NameSource::WmoEmbedded: return "wmo_embedded";
        case canon::NameSource::Listfile: return "listfile";
        case canon::NameSource::Db2: return "db2";
        case canon::NameSource::Synthesized: return "synthesized";
        case canon::NameSource::None: return "none";
    }
    return "none";
}

}  // namespace

void writeBufferSlice(json::Writer& w, const BufferSlice& slice, const char* semantic) {
    w.beginObject();
    w.key("file");
    w.value(slice.file);
    w.key("byte_offset");
    w.value(static_cast<int64_t>(slice.byteOffset));
    w.key("byte_length");
    w.value(static_cast<int64_t>(slice.byteLength));
    w.key("component_type");
    w.value(slice.componentType);
    w.key("component_count");
    w.value(static_cast<int64_t>(slice.componentCount));
    w.key("count");
    w.value(static_cast<int64_t>(slice.count));
    if (semantic != nullptr) {
        w.key("semantic");
        w.value(semantic);
    }
    w.endObject();
}

void writeRef(json::Writer& w, const canon::Ref& ref, const std::string& uri) {
    w.beginObject();
    w.key("id");
    w.beginObject();
    std::visit(
        [&w](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, canon::None>) {
                w.key("kind");
                w.value("none");
            } else if constexpr (std::is_same_v<T, canon::FileDataId>) {
                w.key("kind");
                w.value("file_data_id");
                w.key("value");
                w.value(static_cast<int64_t>(id.value));
            } else if constexpr (std::is_same_v<T, canon::Db2Row>) {
                w.key("kind");
                w.value("db2_row");
                w.key("table");
                w.value(id.table);
                w.key("row");
                w.value(static_cast<int64_t>(id.row));
            } else if constexpr (std::is_same_v<T, canon::RecordIndex>) {
                w.key("kind");
                w.value("record_index");
                w.key("value");
                w.value(static_cast<int64_t>(id.value));
            }
        },
        ref.id);
    w.endObject();
    w.key("name");
    w.value(ref.name);
    w.key("name_source");
    w.value(nameSourceName(ref.source));
    if (!uri.empty()) {
        w.key("uri");
        w.value(uri);
    }
    w.endObject();
}

void writeAssetRef(json::Writer& w, const canon::Ref& ref, const AssetUris& uris) {
    std::string uri;
    if (auto fdid = std::get_if<canon::FileDataId>(&ref.id)) {
        auto it = uris.find(fdid->value);
        if (it != uris.end()) uri = it->second;
    }
    writeRef(w, ref, uri);
}

canon::Ref subjectRef(const std::filesystem::path& file, std::optional<uint32_t> fileDataId) {
    canon::Identity id = fileDataId ? canon::Identity{canon::FileDataId{*fileDataId}} : canon::Identity{canon::None{}};
    std::string name = file.stem().string();
    bool nameIsId = fileDataId && name == std::to_string(*fileDataId);
    return canon::Ref{id, name, nameIsId ? canon::NameSource::Synthesized : canon::NameSource::Listfile};
}

void writeContractHeader(json::Writer& w, const char* kind, const canon::Ref& subject, const Provenance& provenance) {
    w.key("format");
    w.value("husk-bundle");
    w.key("kind");
    w.value(kind);
    w.key("schema_version");
    // Every schema/*.schema.json pins this as its schema_version const;
    // bump them together. Below 1.0, placeholder effects are allowed
    // (REFACTOR/EFFECTS.md).
    w.value("0.1.0");
    w.key("subject");
    writeRef(w, subject);
    w.key("space");
    w.beginObject();
    w.key("length_unit");
    w.value("yard");
    w.key("angle_unit");
    w.value("radian");
    w.key("time_unit");
    w.value("second");
    w.key("up_axis");
    w.value("+z");
    w.key("handedness");
    w.value("right");
    w.endObject();
    w.key("provenance");
    w.beginObject();
    w.key("producer");
    w.value(provenance.producer);
    w.key("exported_at");
    w.value(isoTimestampUtc());
    w.key("input_format");
    w.value(provenance.inputFormat);
    w.key("sources");
    w.beginArray();
    for (const Provenance::Source& source : provenance.sources) {
        w.beginObject();
        w.key("name");
        w.value(source.name);
        w.key("role");
        w.value(source.role);
        w.key("available");
        w.value(source.available);
        if (!source.detail.empty()) {
            w.key("detail");
            w.value(source.detail);
        }
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

std::string hexFlags(uint32_t flags) {
    std::ostringstream out;
    out << "0x" << std::hex << flags;
    return out.str();
}

void writeSourceRecord(json::Writer& w, const canon::SourceRecord& record) {
    w.beginObject();
    w.key("table");
    w.value(record.table);
    w.key("index");
    w.value(static_cast<int64_t>(record.index));
    w.endObject();
}

namespace {

void writeFloats(json::Writer& w, std::initializer_list<float> values) {
    w.beginArray();
    for (float v : values) w.value(static_cast<double>(v));
    w.endArray();
}

// Identity key for deduplicating a set's referenced assets.
std::string identityKey(const canon::Ref& ref) {
    std::ostringstream key;
    std::visit(
        [&key](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, canon::None>) {
                key << "none";
            } else if constexpr (std::is_same_v<T, canon::Db2Row>) {
                key << "db2:" << id.table << ":" << id.row;
            } else if constexpr (std::is_same_v<T, canon::FileDataId>) {
                key << "fdid:" << id.value;
            } else {
                key << "record:" << id.value;
            }
        },
        ref.id);
    key << "/" << ref.name;
    return key.str();
}

void writeVec3(json::Writer& w, const canon::Vec3& v) { writeFloats(w, {v.x, v.y, v.z}); }

// Writes an image input's texture entry and a mesh input's asset ref; the
// caller knows where payloads and bundles are relative to the document.
struct InputAssets {
    std::function<void(const canon::TextureRef&)> texture;
    std::function<void(const canon::Ref&)> mesh;
};

void writeInputValue(json::Writer& w, const canon::InputValue& value, const InputAssets& assets) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, canon::Vec3>) {
                writeVec3(w, v);
            } else if constexpr (std::is_same_v<T, canon::TextureRef>) {
                assets.texture(v);
            } else if constexpr (std::is_same_v<T, canon::Ref>) {
                assets.mesh(v);
            } else if constexpr (std::is_same_v<T, std::vector<canon::InputValue>>) {
                w.beginArray();
                for (const canon::InputValue& element : v) writeInputValue(w, element, assets);
                w.endArray();
            } else if constexpr (std::is_same_v<T, std::vector<canon::NamedInput>>) {
                w.beginObject();
                for (const canon::NamedInput& field : v) {
                    w.key(field.name);
                    writeInputValue(w, field.value, assets);
                }
                w.endObject();
            } else {
                w.value(v);
            }
        },
        value.value);
}

void writeDistanceFog(json::Writer& w, const canon::DistanceFog& fog) {
    w.beginObject();
    if (fog.linear) {
        w.key("linear");
        w.beginObject();
        w.key("start");
        w.value(static_cast<double>(fog.linear->start));
        w.key("end");
        w.value(static_cast<double>(fog.linear->end));
        w.key("color");
        writeVec3(w, fog.linear->color);
        w.endObject();
    }
    w.endObject();
}

void writeRgba(json::Writer& w, const std::array<float, 4>& c) { writeFloats(w, {c[0], c[1], c[2], c[3]}); }

void writeFogCycleKey(json::Writer& w, const canon::FogCycleKey& k) {
    w.beginObject();
    for (auto [key, value] : {std::pair{"time", k.time}, std::pair{"end", k.end}, std::pair{"scaler", k.scaler},
                              std::pair{"density", k.density}, std::pair{"start_offset", k.startOffset},
                              std::pair{"height", k.height}, std::pair{"height_scaler", k.heightScaler},
                              std::pair{"height_density", k.heightDensity}, std::pair{"z_scalar", k.zScalar},
                              std::pair{"curve_start", k.curveStart}, std::pair{"curve_end", k.curveEnd},
                              std::pair{"sun_angle", k.sunAngle}, std::pair{"sun_strength", k.sunStrength},
                              std::pair{"end_color_distance", k.endColorDistance}}) {
        writeFloatField(w, key, value);
    }
    for (auto [key, color] : {std::pair{"sky_color", &k.skyColor}, std::pair{"end_color", &k.endColor},
                              std::pair{"sun_color", &k.sunColor}, std::pair{"height_color", &k.heightColor},
                              std::pair{"end_height_color", &k.endHeightColor}}) {
        writeVec3Field(w, key, *color);
    }
    for (auto [key, cubic] : {std::pair{"height_coefficients", &k.heightCoefficients},
                              std::pair{"main_coefficients", &k.mainCoefficients},
                              std::pair{"height_density_coefficients", &k.heightDensityCoefficients}}) {
        writeFloatArrayField(w, key, cubic->data(), cubic->size());
    }
    w.endObject();
}

void writeRibbonSystem(json::Writer& w, const canon::RibbonSystem& r) {
    w.beginObject();
    w.key("materials");
    w.beginArray();
    for (const canon::RenderState& state : r.materials) {
        w.beginObject();
        if (state.blend) {
            w.key("framebuffer_blend");
            w.value(framebufferBlendName(*state.blend));
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(state.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();
    writeAnimated(w, "color", r.color, writeVec3Value);
    writeAnimated(w, "alpha", r.alpha, writeScalarValue);
    writeAnimated(w, "height_above", r.heightAbove, writeScalarValue);
    writeAnimated(w, "height_below", r.heightBelow, writeScalarValue);
    writeAnimated(w, "texture_slot", r.textureSlot, writeScalarValue);
    writeAnimated(w, "visibility", r.visibility, writeScalarValue);
    writeFloatField(w, "edges_per_second", r.edgesPerSecond);
    writeFloatField(w, "edge_lifetime", r.edgeLifetime);
    writeFloatField(w, "gravity", r.gravity);
    writeIntField(w, "priority_plane", r.priorityPlane);
    writeIntField(w, "ribbon_color_index", r.ribbonColorIndex);
    writeIntField(w, "texture_transform_lookup_index", r.textureTransformLookupIndex);
    w.endObject();
}

void writeParticleSystem(json::Writer& w, const canon::ParticleSystem& p) {
    auto writeUint16 = [](json::Writer& w2, uint16_t v) { w2.value(static_cast<int64_t>(v)); };
    w.beginObject();
    if (p.childEmittersModel) {
        w.key("child_emitters_model");
        writeRef(w, *p.childEmittersModel);
    }
    writeIntField(w, "particle_color_index", p.particleColorIndex);
    writeFloatArrayField(w, "multi_texture_scale", p.multiTextureScale, 2);
    writeIntField(w, "priority_plane", p.priorityPlane);

    writeAnimated(w, "emission_speed", p.emissionSpeed, writeScalarValue);
    writeAnimated(w, "speed_variation", p.speedVariation, writeScalarValue);
    writeAnimated(w, "vertical_range", p.verticalRange, writeScalarValue);
    writeAnimated(w, "horizontal_range", p.horizontalRange, writeScalarValue);
    writeAnimated(w, "gravity", p.gravity, writeScalarValue);
    writeAnimated(w, "lifespan", p.lifespan, writeScalarValue);
    writeAnimated(w, "emission_rate", p.emissionRate, writeScalarValue);
    writeAnimated(w, "emission_area_length", p.emissionAreaLength, writeScalarValue);
    writeAnimated(w, "emission_area_width", p.emissionAreaWidth, writeScalarValue);
    writeAnimated(w, "z_source", p.zSource, writeScalarValue);
    writeAnimated(w, "enabled_in", p.enabledIn, writeScalarValue);
    writeFloatField(w, "lifespan_variation", p.lifespanVariation);
    writeFloatField(w, "emission_rate_variation", p.emissionRateVariation);

    writeLifetimeCurve(w, "color", p.color, writeVec3Value);
    writeLifetimeCurve(w, "alpha", p.alpha, writeScalarValue);
    writeLifetimeCurve(w, "scale", p.scale, writeVec2Value);
    writeLifetimeCurve(w, "head_cell", p.headCell, writeUint16);
    writeLifetimeCurve(w, "tail_cell", p.tailCell, writeUint16);
    w.key("scale_variation");
    writeVec2Value(w, p.scaleVariation);

    writeFloatField(w, "tail_length", p.tailLength);
    writeFloatField(w, "twinkle_speed", p.twinkleSpeed);
    writeFloatField(w, "twinkle_percent", p.twinklePercent);
    writeFloatField(w, "twinkle_scale_min", p.twinkleScaleMin);
    writeFloatField(w, "twinkle_scale_max", p.twinkleScaleMax);
    writeFloatField(w, "inherit_velocity_scale", p.inheritVelocityScale);
    writeFloatField(w, "drag", p.drag);
    writeFloatField(w, "base_spin", p.baseSpin);
    writeFloatField(w, "base_spin_variation", p.baseSpinVariation);
    writeFloatField(w, "spin_speed", p.spinSpeed);
    writeFloatField(w, "spin_speed_variation", p.spinSpeedVariation);
    writeVec3Field(w, "tumble_min", p.tumbleMin);
    writeVec3Field(w, "tumble_max", p.tumbleMax);
    writeVec3Field(w, "wind_vector", p.windVector);
    writeFloatField(w, "wind_time", p.windTime);
    writeFloatField(w, "follow_speed1", p.followSpeed1);
    writeFloatField(w, "follow_scale1", p.followScale1);
    writeFloatField(w, "follow_speed2", p.followSpeed2);
    writeFloatField(w, "follow_scale2", p.followScale2);
    w.key("spline_points");
    w.beginArray();
    for (const canon::Vec3& point : p.splinePoints) writeVec3Value(w, point);
    w.endArray();
    writeFloatArrayField(w, "multi_texture_scroll_mid", p.multiTextureScrollMid, 4);
    writeFloatArrayField(w, "multi_texture_scroll_range", p.multiTextureScrollRange, 4);
    w.key("source");
    w.beginObject();
    writeIntField(w, "blending_type", p.blendingType);
    writeIntField(w, "emitter_type", p.emitterType);
    w.endObject();
    w.endObject();
}

void writeStyleBlock(json::Writer& w, const canon::StyleBlock& style) {
    w.beginObject();
    if (style.distanceFog) {
        w.key("distance_fog");
        writeDistanceFog(w, *style.distanceFog);
    }
    if (style.exitBlend) {
        w.key("exit_blend");
        w.value(true);
    }
    if (style.underwater) {
        w.key("looks");
        w.beginObject();
        w.key("underwater");
        writeDistanceFog(w, *style.underwater);
        w.endObject();
    }
    if (style.ambientColors) {
        w.key("ambient_color");
        w.beginObject();
        w.key("colors");
        w.beginArray();
        for (const auto& color : *style.ambientColors) writeRgba(w, color);
        w.endArray();
        w.endObject();
    }
    if (style.volumetricFog) {
        w.key("volumetric_fog");
        w.beginObject();
        w.key("color");
        writeVec3(w, style.volumetricFog->color);
        w.key("min_quality");
        w.value(static_cast<int64_t>(style.volumetricFog->minQuality));
        w.endObject();
    }
    if (style.zoneFog) {
        static constexpr const char* kSlots[] = {"clear", "clear_underwater", "storm", "storm_underwater", "death"};
        w.key("zone_fog");
        w.beginObject();
        for (size_t slot = 0; slot < canon::ZoneFog::SlotCount; ++slot) {
            const std::vector<canon::FogCycleKey>& cycle = style.zoneFog->cycles[slot];
            if (cycle.empty()) continue;
            w.key(kSlots[slot]);
            w.beginArray();
            for (const canon::FogCycleKey& k : cycle) writeFogCycleKey(w, k);
            w.endArray();
        }
        w.endObject();
    }
    if (style.particleSystem) {
        w.key("particle_system");
        writeParticleSystem(w, *style.particleSystem);
    }
    if (style.ribbonSystem) {
        w.key("ribbon_system");
        writeRibbonSystem(w, *style.ribbonSystem);
    }
    if (style.maxParticles) {
        w.key("particulate");
        w.beginObject();
        w.key("max_particles");
        w.value(static_cast<int64_t>(*style.maxParticles));
        w.endObject();
    }
    if (style.draw) {
        w.key("draw");
        w.beginObject();
        if (style.draw->blend) {
            w.key("blend");
            w.value(framebufferBlendName(*style.draw->blend));
        }
        w.key("color");
        writeRgba(w, style.draw->color);
        w.endObject();
    }
    w.endObject();
}


void writeEffectInstanceFields(json::Writer& w, const canon::EffectPlacement& effect, const InputAssets& assets) {
    w.key("inputs");
    w.beginObject();
    for (const auto& [name, value] : effect.inputs) {
        w.key(name);
        writeInputValue(w, value, assets);
    }
    w.endObject();
    if (!effect.style.empty()) {
        w.key("style");
        w.beginObject();
        for (const canon::SourceStyle& style : effect.style) {
            w.key(style.source);
            if (!style.perElement) {
                writeStyleBlock(w, style.blocks.at(0));
                continue;
            }
            w.beginArray();
            for (const canon::StyleBlock& block : style.blocks) writeStyleBlock(w, block);
            w.endArray();
        }
        w.endObject();
    }
    if (effect.priority) {
        w.key("priority");
        switch (*effect.priority) {
            case canon::Priority::Essential: w.value("essential"); break;
            case canon::Priority::Ambient: w.value("ambient"); break;
            case canon::Priority::Decorative: w.value("decorative"); break;
        }
    }
    if (!effect.derived.empty()) {
        w.key("provenance");
        w.beginObject();
        for (const canon::Derived& d : effect.derived) {
            w.key(d.path);
            w.beginObject();
            w.key("kind");
            w.value("derived");
            w.key("rule");
            w.value(d.rule);
            w.endObject();
        }
        w.endObject();
    }
    if (!effect.unresolved.empty()) {
        w.key("unresolved");
        writeUnresolved(w, effect.unresolved);
    }
}

std::string effectUri(const canon::Ref& effect) { return "effects/" + effect.name + ".json"; }

// One placement_set document as text. `prefix` leads from where the
// document sits to `bundleDir` ("../" from sets/, "" inline in the
// manifest); `uris` already carry it. Image inputs' payloads go under
// `bundleDir/textures/`.
std::string placementSetText(const canon::PlacementSet& set, const std::string& prefix, const AssetUris& uris,
                             const Provenance& provenance, const std::filesystem::path& bundleDir,
                             std::unordered_set<std::string>& writtenTextures) {
    std::vector<const canon::PlacedInstance*> assets;
    std::unordered_map<std::string, size_t> assetIndex;
    std::vector<size_t> instanceAsset;
    for (const canon::PlacedInstance& instance : set.instances) {
        std::string key = (instance.effect ? "effect/" : "model/") + identityKey(instance.asset);
        auto [it, added] = assetIndex.emplace(key, assets.size());
        if (added) assets.push_back(&instance);
        instanceAsset.push_back(it->second);
    }

    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "placement_set", set.ref, provenance);
    w.key("definition");
    w.beginObject();
    w.endObject();
    w.key("selection");
    w.beginObject();
    w.endObject();
    w.key("references");
    w.beginArray();
    for (const canon::PlacedInstance* asset : assets) {
        w.beginObject();
        w.key("kind");
        w.value(asset->effect ? "effect" : "model");
        w.key("asset");
        if (asset->effect) {
            writeRef(w, asset->asset, prefix + effectUri(asset->asset));
        } else {
            writeAssetRef(w, asset->asset, uris);
        }
        w.endObject();
    }
    w.endArray();
    w.key("instances");
    w.beginArray();
    for (size_t i = 0; i < set.instances.size(); ++i) {
        const canon::PlacedInstance& instance = set.instances[i];
        w.beginObject();
        w.key("id");
        w.value(static_cast<int64_t>(instance.id));
        w.key("reference");
        w.value(static_cast<int64_t>(instanceAsset[i]));
        if (instance.anchor) {
            w.key("anchor");
            writeRef(w, *instance.anchor);
        }
        w.key("translation");
        writeFloats(w, {instance.translation.x, instance.translation.y, instance.translation.z});
        w.key("rotation");
        writeFloats(w, {instance.rotation.x, instance.rotation.y, instance.rotation.z, instance.rotation.w});
        w.key("scale");
        w.value(static_cast<double>(instance.scale));
        if (instance.effect) {
            writeEffectInstanceFields(
                w, *instance.effect,
                {[&](const canon::TextureRef& texture) { writeTextureEntry(w, texture, bundleDir, writtenTextures, prefix); },
                 [&](const canon::Ref& model) { writeAssetRef(w, model, uris); }});
            w.key("source");
            w.beginObject();
            w.key("flags");
            w.value(hexFlags(instance.flags));
            w.key("record");
            writeSourceRecord(w, instance.effect->origin);
            w.endObject();
            w.endObject();
            continue;
        }
        if (instance.tint) {
            w.key("tint");
            w.beginObject();
            w.key("rgba");
            w.beginArray();
            for (uint8_t c : instance.tint->rgba) w.value(c / 255.0);
            w.endArray();
            w.key("multiplier");
            w.value(static_cast<double>(instance.tint->multiplier));
            w.endObject();
        }
        w.key("selection");
        w.beginObject();
        w.key("placement_sets");
        w.beginArray();
        if (instance.activeSets) {
            for (const canon::Ref& ref : *instance.activeSets) {
                w.value(static_cast<int64_t>(std::get<canon::RecordIndex>(ref.id).value));
            }
        }
        w.endArray();
        w.endObject();
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(instance.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();
    w.endObject();
    return text.str();
}

}  // namespace

namespace {

const char* unresolvedKindName(canon::UnresolvedField::Kind kind) {
    switch (kind) {
        case canon::UnresolvedField::Kind::Scalar: return "scalar";
        case canon::UnresolvedField::Kind::Scalars: return "scalars";
        case canon::UnresolvedField::Kind::Bits: return "bits";
        case canon::UnresolvedField::Kind::Color: return "color";
        case canon::UnresolvedField::Kind::Count: return "count";
        case canon::UnresolvedField::Kind::Id: return "id";
        case canon::UnresolvedField::Kind::Angle: return "angle";
        case canon::UnresolvedField::Kind::UnitVector: return "unit_vector";
        case canon::UnresolvedField::Kind::Words: return "words";
    }
    return "scalar";
}

}  // namespace

void writeUnresolved(json::Writer& w, const std::vector<canon::UnresolvedField>& fields) {
    w.beginObject();
    w.key("fields");
    w.beginArray();
    for (const canon::UnresolvedField& f : fields) {
        w.beginObject();
        w.key("field");
        w.value(f.field);
        w.key("kind");
        w.value(unresolvedKindName(f.kind));
        w.key("value");
        std::visit(
            [&w](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, uint32_t>) {
                    w.value(hexFlags(v));
                } else if constexpr (std::is_same_v<T, std::vector<uint32_t>>) {
                    w.beginArray();
                    for (uint32_t word : v) w.value(hexFlags(word));
                    w.endArray();
                } else if constexpr (std::is_same_v<T, std::vector<double>>) {
                    w.beginArray();
                    for (double d : v) w.value(d);
                    w.endArray();
                } else {
                    w.value(v);
                }
            },
            f.value);
        if (f.candidateRole) {
            w.key("candidate_role");
            w.value(*f.candidateRole);
        }
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

std::vector<SetDocument> writePlacementSetDocuments(const std::vector<OwnedSetEntry>& sets,
                                                    const std::filesystem::path& bundleDir, const AssetUris& assetUris,
                                                    const Provenance& provenance) {
    // Set files sit one level down, in sets/; inline sets at the bundle root.
    AssetUris setUris;
    for (const auto& [fdid, uri] : assetUris) {
        setUris.emplace(fdid, std::filesystem::path(uri).is_absolute() ? uri : "../" + uri);
    }
    std::vector<SetDocument> out;
    std::set<std::string> effects;
    std::unordered_set<std::string> writtenTextures;
    for (size_t i = 0; i < sets.size(); ++i) {
        const OwnedSetEntry& entry = sets[i];
        SetDocument doc{entry.set->ref, "", entry.alwaysOn, entry.set->instances.size(), ""};
        if (entry.inlineDocument) {
            doc.inlineText = placementSetText(*entry.set, "", assetUris, provenance, bundleDir, writtenTextures);
        } else {
            doc.uri = "sets/" + std::to_string(i) + ".json";
            std::filesystem::create_directories(bundleDir / "sets");
            std::string text = placementSetText(*entry.set, "../", setUris, provenance, bundleDir, writtenTextures);
            writeFile(bundleDir / doc.uri, std::vector<uint8_t>(text.begin(), text.end()));
        }
        out.push_back(std::move(doc));
        for (const canon::PlacedInstance& instance : sets[i].set->instances) {
            if (instance.effect) effects.insert(instance.asset.name);
        }
    }
    if (!effects.empty()) std::filesystem::create_directories(bundleDir / "effects");
    for (const std::string& name : effects) writeEffectDocument(name, bundleDir / ("effects/" + name + ".json"), provenance);
    return out;
}

void writeEffectDocument(const std::string& name, const std::filesystem::path& path, const Provenance& provenance) {
    const auto& effects = bundle_schema::embeddedEffects();
    auto body = std::find_if(effects.begin(), effects.end(), [&](const auto& e) { return e.name == name; });
    if (body == effects.end()) throw std::runtime_error("bundle writer: no effect document '" + name + "' in effects/");
    std::ostringstream text;
    json::Writer w(text);
    w.beginObject();
    writeContractHeader(w, "effect", canon::Ref{canon::None{}, name, canon::NameSource::Synthesized}, provenance);
    w.rawMembers(std::string(body->text));
    w.endObject();
    std::string bytes = text.str();
    writeFile(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

void writeLiquidTypes(json::Writer& w, const std::vector<canon::LiquidType>& types,
                      const std::function<void(const canon::TextureRef&)>& writeTexture) {
    w.beginArray();
    for (const canon::LiquidType& t : types) {
        w.beginObject();
        w.key("identity");
        writeRef(w, t.ref);
        if (!std::holds_alternative<canon::None>(t.material.id)) {
            w.key("material");
            w.beginObject();
            w.key("identity");
            writeRef(w, t.material);
            if (t.materialVertexFormat) {
                w.key("vertex_format");
                w.value(static_cast<int64_t>(*t.materialVertexFormat));
                w.key("source");
                w.beginObject();
                w.key("flags");
                w.value(hexFlags(*t.materialFlags));
                w.endObject();
            }
            w.endObject();
        }
        if (!t.textures.empty()) {
            w.key("textures");
            w.beginArray();
            for (const canon::LiquidTextureSlot& slot : t.textures) {
                w.beginObject();
                w.key("frames");
                w.beginArray();
                for (const canon::TextureRef& frame : slot.frames) writeTexture(frame);
                w.endArray();
                w.key("source");
                w.beginObject();
                w.key("kind");
                w.value(static_cast<int64_t>(slot.sourceKind));
                w.endObject();
                w.endObject();
            }
            w.endArray();
        }
        if (t.flags) {
            w.key("source");
            w.beginObject();
            w.key("flags");
            w.value(hexFlags(*t.flags));
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();
}

std::string isoTimestampUtc() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

void writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("bundle writer: could not open " + path.string() + " for writing");
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

// BUNDLE_FORMAT.md's settled texture-encoding vocabulary, extension half --
// only Png is reachable today (canon::TextureEncoding's own doc comment:
// every real producer still decodes BLP -> PNG before canon:: ever sees
// the bytes), the rest exist so this switch doesn't need revisiting once
// that changes.
void writeOwnedSetDefinitions(json::Writer& w, const std::vector<SetDocument>& sets) {
    w.key("placement_sets");
    w.beginArray();
    for (size_t i = 0; i < sets.size(); ++i) {
        w.beginObject();
        w.key("reference");
        w.value(static_cast<int64_t>(i));
        w.key("always_on");
        w.value(sets[i].alwaysOn);
        w.key("instance_count");
        w.value(static_cast<int64_t>(sets[i].instanceCount));
        w.endObject();
    }
    w.endArray();
}

void writeOwnedSetSelectionAndReferences(json::Writer& w, const std::vector<SetDocument>& sets,
                                         const std::function<void(json::Writer&)>& moreSelection) {
    w.key("selection");
    w.beginObject();
    w.key("placement_sets");
    w.beginArray();
    w.endArray();
    if (moreSelection) moreSelection(w);
    w.endObject();

    w.key("references");
    w.beginArray();
    for (const SetDocument& set : sets) {
        w.beginObject();
        w.key("kind");
        w.value("placement_set");
        w.key("asset");
        writeRef(w, set.ref, set.uri);
        if (!set.inlineText.empty()) {
            w.key("document");
            w.beginObject();
            w.rawMembers(set.inlineText);
            w.endObject();
        }
        w.endObject();
    }
    w.endArray();
}

std::string textureFileExtension(canon::TextureEncoding encoding) {
    switch (encoding) {
        case canon::TextureEncoding::Png: return "png";
        case canon::TextureEncoding::Bc1:
        case canon::TextureEncoding::Bc2:
        case canon::TextureEncoding::Bc3:
        case canon::TextureEncoding::Bc5:
        case canon::TextureEncoding::Bgra:
        case canon::TextureEncoding::Palettized:
            return "dds";  // BUNDLE_FORMAT.md's "Texture encoding -- settled": DDS houses the compressed blocks verbatim
    }
    return "png";
}

std::string textureMediaType(const std::string& extension) {
    if (extension == "png") return "image/png";
    if (extension == "dds") return "image/vnd-ms.dds";
    throw std::logic_error("textureMediaType: no media type for extension '" + extension + "'");
}

// Writes `texture`'s resolved payload (when it carries one, AUDIT.md §7.4)
// to `bundleDir/textures/<name>.<ext>` and records it as the entry's
// `payload` {uri, media_type} beside its identity -- BUNDLE_FORMAT.md's
// "Embed or reference" shape. A Resolved identity with no payload (a
// future reference-only producer) writes the identity with no payload, the
// documented "husk knows what this is" case, not an error. `writtenTextures` dedupes by
// filename across the whole bundle (more than one material layer can
// resolve to the same real texture) so repeated identical bytes are
// written to disk once, not once per referencing layer.
//
// `rawPayload` (the DDS-housed source blocks, BUNDLE_FORMAT.md's "Texture
// encoding -- settled") wins over `payload` (the PNG projection) when both
// are present: this bundle is exactly the archival/engine-facing format
// that section says should carry the lossless source payload, not the
// glTF-facing PNG. `payload` stays as the fallback so a texture whose
// source was never a compressed BLP in the first place (already a `.png`
// on disk, or DDS extraction declined for a Palette-encoded source) still
// gets a real file in the bundle instead of being silently dropped just
// because the DDS variant wasn't available for it.
void writeVec2Value(json::Writer& w, const canon::Vec2& v) {
    w.beginArray();
    w.value(static_cast<double>(v.x));
    w.value(static_cast<double>(v.y));
    w.endArray();
}

void writeVec3Value(json::Writer& w, const canon::Vec3& v) {
    w.beginArray();
    w.value(static_cast<double>(v.x));
    w.value(static_cast<double>(v.y));
    w.value(static_cast<double>(v.z));
    w.endArray();
}

void writeQuatValue(json::Writer& w, const canon::Quat& q) {
    w.beginArray();
    w.value(static_cast<double>(q.x));
    w.value(static_cast<double>(q.y));
    w.value(static_cast<double>(q.z));
    w.value(static_cast<double>(q.w));
    w.endArray();
}

void writeScalarValue(json::Writer& w, float v) { w.value(static_cast<double>(v)); }

void writeFloatField(json::Writer& w, const char* key, float v) {
    w.key(key);
    w.value(static_cast<double>(v));
}

void writeIntField(json::Writer& w, const char* key, int64_t v) {
    w.key(key);
    w.value(v);
}

void writeVec3Field(json::Writer& w, const char* key, const canon::Vec3& v) {
    w.key(key);
    writeVec3Value(w, v);
}

void writeFloatArrayField(json::Writer& w, const char* key, const float* values, size_t count) {
    w.key(key);
    w.beginArray();
    for (size_t i = 0; i < count; ++i) w.value(static_cast<double>(values[i]));
    w.endArray();
}

std::string sequenceKindName(canon::SequenceRef::Kind kind) {
    return kind == canon::SequenceRef::Kind::GlobalSequence ? "global_sequence" : "sequence";
}

std::string interpolationName(canon::Interpolation interp) {
    return interp == canon::Interpolation::Step ? "step" : "linear";
}

std::string framebufferBlendName(canon::FramebufferBlend blend) {
    switch (blend) {
        case canon::FramebufferBlend::Opaque: return "opaque";
        case canon::FramebufferBlend::AlphaKey: return "alpha_key";
        case canon::FramebufferBlend::Alpha: return "alpha";
        case canon::FramebufferBlend::NoAlphaAdd: return "no_alpha_add";
        case canon::FramebufferBlend::Add: return "add";
        case canon::FramebufferBlend::Mod: return "mod";
        case canon::FramebufferBlend::Mod2x: return "mod2x";
        case canon::FramebufferBlend::BlendAdd: return "blend_add";
    }
    return "opaque";
}

void writeTextureEntry(json::Writer& w, const canon::TextureRef& texture, const std::filesystem::path& bundleDir,
                       std::unordered_set<std::string>& writtenTextures, const std::string& uriPrefix) {
    w.beginObject();
    switch (texture.state) {
        case canon::TextureRef::State::Resolved: {
            w.key("state");
            w.value("resolved");
            w.key("identity");
            writeRef(w, texture.resolved);
            const canon::TextureRef::Payload* chosen =
                texture.rawPayload ? &*texture.rawPayload : (texture.payload ? &*texture.payload : nullptr);
            if (chosen) {
                std::string stem = texture.resolved.name;
                if (stem.empty()) {
                    if (auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id)) {
                        stem = std::to_string(fdid->value);
                    } else {
                        stem = "texture";
                    }
                }
                std::string extension = textureFileExtension(chosen->encoding);
                std::string filename = stem + "." + extension;
                if (writtenTextures.insert(filename).second) {
                    std::filesystem::path texturesDir = bundleDir / "textures";
                    std::error_code ec;
                    std::filesystem::create_directories(texturesDir, ec);
                    if (ec) {
                        throw std::runtime_error("bundle writer: could not create directory " +
                                                  texturesDir.string() + ": " + ec.message());
                    }
                    writeFile(texturesDir / filename, chosen->bytes);
                }
                w.key("payload");
                w.beginObject();
                w.key("uri");
                w.value(uriPrefix + "textures/" + filename);
                w.key("media_type");
                w.value(textureMediaType(extension));
                w.endObject();
            }
            break;
        }
        case canon::TextureRef::State::KnownUnresolved:
            w.key("state");
            w.value("known_unresolved");
            w.key("unresolved_reason");
            w.value(texture.unresolvedReason);
            break;
        case canon::TextureRef::State::Ambiguous:
            w.key("state");
            w.value("ambiguous");
            w.key("candidates");
            w.beginArray();
            for (const auto& candidate : texture.candidates) {
                w.beginObject();
                w.key("identity");
                writeRef(w, candidate.identity);
                w.key("category");
                w.value(candidate.category);
                w.key("width");
                w.value(static_cast<int64_t>(candidate.width));
                w.key("height");
                w.value(static_cast<int64_t>(candidate.height));
                w.endObject();
            }
            w.endArray();
            break;
    }
    w.endObject();
}

}  // namespace husk::writers
