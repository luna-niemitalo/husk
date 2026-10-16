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

#include <nlohmann/json.hpp>

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
    if (!ref.name.empty()) {
        w.key("name");
        w.value(ref.name);
        w.key("name_source");
        w.value(nameSourceName(ref.source));
    }
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
    w.value("0.2.1");
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

// Writes an image input's texture index and a mesh input's asset ref; the
// caller holds the document's texture table and knows where bundles are.
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

void writeZoneFogValues(json::Writer& w, const canon::ZoneFogValues& f) {
    w.beginObject();
    for (auto [key, value] : {std::pair{"end", f.end}, std::pair{"scaler", f.scaler}, std::pair{"density", f.density},
                              std::pair{"start_offset", f.startOffset}, std::pair{"height", f.height},
                              std::pair{"height_scaler", f.heightScaler}, std::pair{"height_density", f.heightDensity},
                              std::pair{"z_scalar", f.zScalar}, std::pair{"curve_start", f.curveStart},
                              std::pair{"curve_end", f.curveEnd}, std::pair{"sun_angle", f.sunAngle},
                              std::pair{"sun_strength", f.sunStrength},
                              std::pair{"end_color_distance", f.endColorDistance}}) {
        writeFloatField(w, key, value);
    }
    for (auto [key, color] : {std::pair{"sky_color", &f.skyColor}, std::pair{"end_color", &f.endColor},
                              std::pair{"sun_color", &f.sunColor}, std::pair{"height_color", &f.heightColor},
                              std::pair{"end_height_color", &f.endHeightColor}}) {
        writeVec3Field(w, key, *color);
    }
    for (auto [key, cubic] : {std::pair{"height_coefficients", &f.heightCoefficients},
                              std::pair{"main_coefficients", &f.mainCoefficients},
                              std::pair{"height_density_coefficients", &f.heightDensityCoefficients}}) {
        writeFloatArrayField(w, key, cubic->data(), cubic->size());
    }
    w.endObject();
}

void writeBoolField(json::Writer& w, const char* key, bool v) {
    w.key(key);
    w.value(v);
}

void writeLightKey(json::Writer& w, const canon::LightKey& k, const InputAssets& assets) {
    w.beginObject();
    writeFloatField(w, "time", k.time);
    for (auto [key, color] : {std::pair{"direct_color", &k.directColor}, std::pair{"ambient_color", &k.ambientColor},
                              std::pair{"horizon_ambient_color", &k.horizonAmbientColor},
                              std::pair{"ground_ambient_color", &k.groundAmbientColor}, std::pair{"sun_color", &k.sunColor},
                              std::pair{"shadow_opacity", &k.shadowOpacity}}) {
        writeVec3Field(w, key, *color);
    }
    w.key("sky");
    w.beginObject();
    for (auto [key, color] : {std::pair{"top_color", &k.skyTopColor}, std::pair{"middle_color", &k.skyMiddleColor},
                              std::pair{"band1_color", &k.skyBand1Color}, std::pair{"band2_color", &k.skyBand2Color},
                              std::pair{"smog_color", &k.skySmogColor}}) {
        writeVec3Field(w, key, *color);
    }
    w.endObject();
    w.key("clouds");
    w.beginObject();
    for (auto [key, color] : {std::pair{"sun_color", &k.cloudSunColor}, std::pair{"emissive_color", &k.cloudEmissiveColor},
                              std::pair{"layer1_ambient_color", &k.cloudLayer1AmbientColor},
                              std::pair{"layer2_ambient_color", &k.cloudLayer2AmbientColor}}) {
        writeVec3Field(w, key, *color);
    }
    writeFloatField(w, "density", k.cloudDensity);
    w.endObject();
    w.key("water");
    w.beginObject();
    for (auto [key, color] : {std::pair{"ocean_close_color", &k.oceanCloseColor}, std::pair{"ocean_far_color", &k.oceanFarColor},
                              std::pair{"river_close_color", &k.riverCloseColor}, std::pair{"river_far_color", &k.riverFarColor}}) {
        writeVec3Field(w, key, *color);
    }
    w.endObject();
    for (auto [key, texture] : {std::pair{"color_grading", &k.colorGrading}, std::pair{"darker_color_grading", &k.darkerColorGrading}}) {
        if (!*texture) continue;
        w.key(key);
        assets.texture(**texture);
    }
    w.key("fog");
    writeZoneFogValues(w, k.fog);
    if (!k.unresolved.empty()) {
        w.key("unresolved");
        writeUnresolved(w, k.unresolved);
    }
    w.endObject();
}

void writeLightSettings(json::Writer& w, const canon::LightSettings& s, const InputAssets& assets) {
    w.beginObject();
    writeBoolField(w, "highlight_sky", s.highlightSky);
    if (s.skybox) {
        w.key("skybox");
        w.beginObject();
        w.key("ref");
        writeRef(w, s.skybox->ref);
        w.key("model");
        assets.mesh(s.skybox->model);
        if (s.skybox->celestialModel) {
            w.key("celestial_model");
            assets.mesh(*s.skybox->celestialModel);
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(s.skybox->flags));
        w.endObject();
        w.endObject();
    }
    writeFloatField(w, "glow", s.glow);
    w.key("water_alpha");
    w.beginObject();
    for (auto [key, value] : {std::pair{"river_shallow", s.riverShallowAlpha}, std::pair{"river_deep", s.riverDeepAlpha},
                              std::pair{"ocean_shallow", s.oceanShallowAlpha}, std::pair{"ocean_deep", s.oceanDeepAlpha}}) {
        writeFloatField(w, key, value);
    }
    w.endObject();
    w.key("sun");
    w.beginObject();
    for (auto [key, value] : {std::pair{"polar", s.sunPolar}, std::pair{"azimuth", s.sunAzimuth},
                              std::pair{"attenuation_start", s.sunAttenuationStart},
                              std::pair{"attenuation_end", s.sunAttenuationEnd}}) {
        writeFloatField(w, key, value);
    }
    w.endObject();
    // wowdev.wiki DB/LightParams: the only bits whose two name lists agree.
    writeBoolField(w, "hide_sun", s.flags & 0x4);
    writeBoolField(w, "hide_moon", s.flags & 0x8);
    writeBoolField(w, "hide_stars", s.flags & 0x10);
    if (s.celestialSphereOverride) writeVec3Field(w, "celestial_sphere_override", *s.celestialSphereOverride);
    if (s.sunPositionOverride) writeVec3Field(w, "sun_position_override", *s.sunPositionOverride);
    writeIntField(w, "cloud_kind", s.cloudType);
    writeIntField(w, "ssao_settings", s.ssaoSettings);
    w.key("source");
    w.beginObject();
    w.key("flags");
    w.value(hexFlags(s.flags));
    w.endObject();
    if (!s.unresolved.empty()) {
        w.key("unresolved");
        writeUnresolved(w, s.unresolved);
    }
    w.endObject();
}

// resources.light_params: each parameter set once, as zone_light styles
// index it.
void writeLightParams(json::Writer& w, const std::vector<canon::LightParams>& table, const InputAssets& assets) {
    w.beginArray();
    for (const canon::LightParams& params : table) {
        w.beginObject();
        w.key("ref");
        writeRef(w, params.ref);
        if (params.settings) {
            w.key("settings");
            writeLightSettings(w, *params.settings, assets);
        }
        w.key("keys");
        w.beginArray();
        for (const canon::LightKey& k : params.keys) writeLightKey(w, k, assets);
        w.endArray();
        w.endObject();
    }
    w.endArray();
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
    if (style.zoneLight) {
        static constexpr const char* kSlots[] = {"clear", "clear_underwater", "storm", "storm_underwater", "death"};
        w.key("zone_light");
        w.beginObject();
        for (size_t slot = 0; slot < canon::ZoneLight::SlotCount; ++slot) {
            if (!style.zoneLight->params[slot]) continue;
            w.key(kSlots[slot]);
            w.value(static_cast<int64_t>(*style.zoneLight->params[slot]));
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
// manifest); `uris` already carry it. Image inputs index the document's
// own resources.textures; their payloads go under `bundleDir/textures/`.
std::string placementSetText(const canon::PlacementSet& set, const std::string& prefix, const AssetUris& uris,
                             const Provenance& provenance, const std::filesystem::path& bundleDir,
                             std::unordered_set<std::string>& writtenTextures, const AssetUris& textureUris) {
    std::vector<const canon::PlacedInstance*> assets;
    std::unordered_map<std::string, size_t> assetIndex;
    std::vector<size_t> instanceAsset;
    for (const canon::PlacedInstance& instance : set.instances) {
        std::string key = (instance.effect ? "effect/" : "model/") + identityKey(instance.asset);
        auto [it, added] = assetIndex.emplace(key, assets.size());
        if (added) assets.push_back(&instance);
        instanceAsset.push_back(it->second);
    }

    TextureTable textures(bundleDir, writtenTextures, prefix, &textureUris);
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
                {[&](const canon::TextureRef& texture) { w.value(static_cast<int64_t>(textures.add(texture))); },
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
        if (instance.distant) {
            const canon::DistantListing& d = *instance.distant;
            w.key("distant");
            w.beginObject();
            w.key("bounds_min");
            writeFloats(w, {d.boundsMin.x, d.boundsMin.y, d.boundsMin.z});
            w.key("bounds_max");
            writeFloats(w, {d.boundsMax.x, d.boundsMax.y, d.boundsMax.z});
            w.key("source");
            w.beginObject();
            w.key("radius");
            w.value(static_cast<double>(d.radius));
            if (d.band) {
                w.key("band");
                w.value(static_cast<int64_t>(*d.band));
            }
            w.endObject();
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
    // Rendered before the texture table is written: their colour-grading
    // textures join it.
    std::ostringstream lightParams;
    if (!set.lightParams.empty()) {
        json::Writer lw(lightParams);
        writeLightParams(lw, set.lightParams,
                         {[&](const canon::TextureRef& texture) { lw.value(static_cast<int64_t>(textures.add(texture))); },
                          [&](const canon::Ref& model) { writeAssetRef(lw, model, uris); }});
    }
    if (!textures.empty() || !set.lightParams.empty()) {
        w.key("resources");
        w.beginObject();
        if (!textures.empty()) textures.write(w);
        if (!set.lightParams.empty()) {
            w.key("light_params");
            w.rawValue(lightParams.str());
        }
        w.endObject();
    }
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
                                                    const Provenance& provenance, const AssetUris& textureUris) {
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
            doc.inlineText = placementSetText(*entry.set, "", assetUris, provenance, bundleDir, writtenTextures, textureUris);
        } else {
            doc.uri = "sets/" + std::to_string(i) + ".json";
            std::filesystem::create_directories(bundleDir / "sets");
            std::string text = placementSetText(*entry.set, "../", setUris, provenance, bundleDir, writtenTextures, textureUris);
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

namespace {

const bundle_schema::EmbeddedSchema* embeddedTechnique(const std::string& name) {
    const auto& techniques = bundle_schema::embeddedTechniques();
    auto it = std::find_if(techniques.begin(), techniques.end(), [&](const auto& t) { return t.name == name; });
    return it == techniques.end() ? nullptr : &*it;
}

}  // namespace

std::string techniqueUri(const std::string& name) { return "techniques/" + name + ".json"; }

std::vector<std::string> materialTechniques(const canon::Material& material) {
    std::vector<std::string> candidates;
    if (material.pixelShader) {
        candidates.push_back("m2_pixel_shader/" + *material.pixelShader);
    } else if (material.shader) {
        const auto* id = std::get_if<canon::RecordIndex>(&material.shader->id);
        std::string name = !material.shader->name.empty() ? material.shader->name
                           : id                           ? "shader_" + std::to_string(id->value)
                                                          : "";
        if (!name.empty()) candidates.push_back("wmo_shader/" + name);
    }
    if (material.framebufferBlend) candidates.push_back("framebuffer_blend/" + framebufferBlendName(*material.framebufferBlend));
    std::vector<std::string> out;
    for (const std::string& name : candidates) {
        if (embeddedTechnique(name)) out.push_back(name);
    }
    return out;
}

void writeTechniqueDocuments(const std::vector<std::string>& names, const std::filesystem::path& bundleDir,
                             const Provenance& provenance) {
    std::set<std::string> written;
    std::vector<std::string> pending(names.begin(), names.end());
    while (!pending.empty()) {
        std::string name = pending.back();
        pending.pop_back();
        if (!written.insert(name).second) continue;
        const bundle_schema::EmbeddedSchema* body = embeddedTechnique(name);
        if (!body) throw std::runtime_error("bundle writer: no technique document '" + name + "' in techniques/");
        for (const nlohmann::json& ref : nlohmann::json::parse(body->text)["references"]) {
            if (ref["kind"] == "technique") pending.push_back(ref["asset"]["name"]);
        }
        std::ostringstream text;
        json::Writer w(text);
        w.beginObject();
        writeContractHeader(w, "technique", canon::Ref{canon::None{}, name, canon::NameSource::Synthesized}, provenance);
        w.rawMembers(std::string(body->text));
        w.endObject();
        std::filesystem::path path = bundleDir / techniqueUri(name);
        std::filesystem::create_directories(path.parent_path());
        std::string bytes = text.str();
        writeFile(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
    }
}

void writeSurfaceTypes(json::Writer& w, const std::vector<canon::SurfaceType>& types) {
    w.beginArray();
    for (const canon::SurfaceType& s : types) {
        w.beginObject();
        w.key("ref");
        writeRef(w, s.ref);
        if (s.soundClass) {
            w.key("sound_class");
            writeRef(w, *s.soundClass);
        }
        for (auto [key, spray] : {std::pair{"footstep_spray_run", &s.footstepSprayRun},
                                  std::pair{"footstep_spray_walk", &s.footstepSprayWalk}}) {
            if (!*spray) continue;
            w.key(key);
            writeRef(w, **spray);
        }
        w.key("source");
        w.beginObject();
        w.key("flags");
        w.value(hexFlags(s.flags));
        w.endObject();
        w.endObject();
    }
    w.endArray();
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
                                         const std::function<void(json::Writer&)>& moreSelection,
                                         const std::function<void(json::Writer&)>& moreReferences) {
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
    if (moreReferences) moreReferences(w);
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
        case canon::FramebufferBlend::ModAdd: return "mod_add";
    }
    return "opaque";
}

uint32_t TextureTable::add(const std::function<void(json::Writer&)>& writeEntry) {
    std::ostringstream text;
    json::Writer w(text);
    writeEntry(w);
    auto [it, added] = index_.emplace(text.str(), static_cast<uint32_t>(entries_.size()));
    if (added) entries_.push_back(text.str());
    return it->second;
}

uint32_t TextureTable::add(const canon::TextureRef& texture) {
    auto fdid = std::get_if<canon::FileDataId>(&texture.resolved.id);
    bool hasPayload = texture.payload || texture.rawPayload;
    if (texture.state != canon::TextureRef::State::Resolved || hasPayload || !fdid || !shared_ || !shared_->count(fdid->value)) {
        return add([&](json::Writer& w) { writeTextureEntry(w, texture, bundleDir_, writtenTextures_, uriPrefix_); });
    }
    std::string uri = uriPrefix_ + shared_->at(fdid->value);
    return add([&](json::Writer& w) {
        w.beginObject();
        w.key("state");
        w.value("resolved");
        w.key("identity");
        writeRef(w, texture.resolved);
        w.key("payload");
        w.beginObject();
        w.key("uri");
        w.value(uri);
        w.key("media_type");
        w.value(textureMediaType(std::filesystem::path(uri).extension().string().substr(1)));
        w.endObject();
        w.endObject();
    });
}

void TextureTable::write(json::Writer& w) const {
    w.key("textures");
    w.beginArray();
    for (const std::string& entry : entries_) w.rawValue(entry);
    w.endArray();
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
            if (!std::holds_alternative<canon::None>(texture.resolved.id)) {
                w.key("identity");
                writeRef(w, texture.resolved);
            }
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
