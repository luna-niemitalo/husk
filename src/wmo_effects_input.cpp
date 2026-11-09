// A WMO's fog (MFOG), ambient (MAVG/MAVD/MBVD) and particulate (MPVD)
// records as effect instances (REFACTOR/EFFECTS.md), and the group refs
// that name them.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "wmo_canon_input.hpp"
#include "wmo_canon_input_internal.hpp"

namespace husk::wmoinput {

namespace {

std::array<float, 4> rgbaOf(uint32_t bgra) {
    std::array<uint8_t, 4> rgba = bgraToRgba(bgra);
    return {rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f};
}

// What every ambient record shares as an effect instance: its three colours
// as style, its flags (all of them undocumented) as unresolved bits.
canon::PlacedInstance ambientInstance(const char* effect, const char* table, uint32_t index,
                                      const std::array<uint32_t, 3>& colors, uint32_t flags) {
    canon::PlacedInstance instance;
    instance.asset = canon::Ref{canon::None{}, effect, canon::NameSource::Synthesized};
    instance.flags = flags;
    canon::EffectPlacement placement;
    placement.origin = {table, index};
    canon::StyleBlock style;
    style.ambientColors.emplace();
    for (size_t k = 0; k < 3; ++k) (*style.ambientColors)[k] = rgbaOf(colors[k]);
    placement.style = {{"ambient", {style}}};
    if (flags != 0) placement.unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, flags, std::nullopt});
    instance.effect = std::move(placement);
    return instance;
}

// MAVG: the model-wide override. Its position and distances have no region
// to describe, so any set ones are carried unresolved.
canon::PlacedInstance globalAmbientInstance(const wmo::AmbientVolume& v, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_global", "MAVG", index, v.colors, v.flags);
    std::vector<canon::UnresolvedField>& unresolved = instance.effect->unresolved;
    if (v.position != wmo::Vec3{}) {
        unresolved.push_back({"position", canon::UnresolvedField::Kind::Scalars,
                              std::vector<double>(v.position.begin(), v.position.end()), std::nullopt});
    }
    if (v.start != 0) unresolved.push_back({"start", canon::UnresolvedField::Kind::Scalar, double{v.start}, std::nullopt});
    if (v.end != 0) unresolved.push_back({"end", canon::UnresolvedField::Kind::Scalar, double{v.end}, std::nullopt});
    return instance;
}

// MAVD: an override that fades out between two distances from a point.
canon::PlacedInstance pointAmbientInstance(const wmo::AmbientVolume& v, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_point", "MAVD", index, v.colors, v.flags);
    instance.translation = toCanon(v.position);
    instance.effect->inputs = {{"start", {double{v.start}}}, {"end", {double{v.end}}}};
    return instance;
}

// MBVD: an override inside six planes. Its `end` reads as a fade distance in
// from the faces (<= the smallest half-size in 52 of 54 boxes), not confirmed.
canon::PlacedInstance boxAmbientInstance(const wmo::AmbientBox& b, uint32_t index) {
    canon::PlacedInstance instance = ambientInstance("classic_ambient_box", "MBVD", index, b.colors, b.flags);
    std::vector<canon::InputValue> planes;
    for (const auto& [x, y, z, d] : b.planes) {
        planes.push_back({std::vector<canon::NamedInput>{{"normal", {canon::Vec3{x, y, z}}}, {"distance", {double{d}}}}});
    }
    instance.effect->inputs = {{"planes", {std::move(planes)}}};
    instance.effect->unresolved.push_back({"end", canon::UnresolvedField::Kind::Scalar, double{b.end}, "region.edge"});
    return instance;
}

// The wiki's F_IEBLEND: radius ignored; not drawn, but scales the area fog
// down near the model's exits.
constexpr uint32_t kFogExitBlend = 0x1;

// The wiki's "empty fog entry", byte for byte (a -0.0 position included):
// what record 0 holds when the model has no default fog of its own. It is
// record 0 in 3,631 of 4,000 sampled roots and never a later record.
bool isEmptyFog(const wmo::Fog& f) {
    return f.flags == 0 && f.position[0] == 0 && f.position[1] == 0 && f.position[2] == 0 && f.smallerRadius == 0 &&
           f.largerRadius == 0 && f.fog.end == 444.4444580078125f && f.fog.startScalar == 0.25f &&
           f.fog.color == 0xFFFFFFFF && f.underwater.end == 222.22222900390625f && f.underwater.startScalar == -0.5f &&
           f.underwater.color == 0xFF0000FF;
}

void unresolvedFloat(const char* field, float value, std::vector<canon::UnresolvedField>& out) {
    if (std::isfinite(value)) {
        out.push_back({field, canon::UnresolvedField::Kind::Scalar, static_cast<double>(value), std::nullopt});
    } else {
        out.push_back({field, canon::UnresolvedField::Kind::Bits, std::bit_cast<uint32_t>(value), std::nullopt});
    }
}

// The wiki's MoP fog: thickens from end * startScalar to end. A layer whose
// numbers aren't finite is carried raw instead.
std::optional<canon::DistanceFog> distanceFog(const wmo::Fog::Layer& layer, const std::string& name,
                                              std::vector<canon::UnresolvedField>& unresolved) {
    if (std::isfinite(layer.end) && std::isfinite(layer.startScalar)) {
        return canon::DistanceFog{canon::LinearFog{layer.end * layer.startScalar, layer.end, colorOf(layer.color)}};
    }
    unresolvedFloat((name + ".end").c_str(), layer.end, unresolved);
    unresolvedFloat((name + ".start_scalar").c_str(), layer.startScalar, unresolved);
    return std::nullopt;
}

// Alpha is 0xFF in all 4,534 sampled records.
void requireOpaque(const wmo::Fog::Layer& layer, const char* field, std::vector<canon::UnresolvedField>& out) {
    uint32_t alpha = layer.color >> 24;
    if (alpha != 0xFF) out.push_back({field, canon::UnresolvedField::Kind::Scalar, static_cast<double>(alpha), std::nullopt});
}

// One MFOG record as an effect instance (REFACTOR/EFFECTS.md, literal tier).
canon::PlacedInstance buildFogInstance(const WmoInputs& in, uint32_t index) {
    const wmo::Fog& f = in.root->fogs[index];
    canon::PlacedInstance instance;
    instance.translation = toCanon(f.position);
    instance.flags = f.flags;
    canon::EffectPlacement effect;
    effect.origin = {"MFOG", index};
    canon::StyleBlock style;
    style.distanceFog = distanceFog(f.fog, "fog", effect.unresolved);
    style.underwater = distanceFog(f.underwater, "underwater", effect.unresolved);
    // Radii that aren't finite numbers occur (a Torghast record holds a NaN
    // and the integer 3): no radius is stated then, and the bits are kept.
    bool radiiFinite = std::isfinite(f.smallerRadius) && std::isfinite(f.largerRadius);
    if (f.flags & kFogExitBlend) {
        instance.asset = canon::Ref{canon::None{}, "classic_fog_exit_blend", canon::NameSource::Synthesized};
        style.exitBlend = true;
        effect.style = {{"exit_blend", {style}}};
        unresolvedFloat("smaller_radius", f.smallerRadius, effect.unresolved);
        unresolvedFloat("larger_radius", f.largerRadius, effect.unresolved);
    } else {
        instance.asset = canon::Ref{canon::None{}, "classic_local_fog", canon::NameSource::Synthesized};
        if (radiiFinite) {
            effect.inputs = {{"inner_radius", static_cast<double>(f.smallerRadius)},
                             {"outer_radius", static_cast<double>(f.largerRadius)}};
        } else {
            unresolvedFloat("smaller_radius", f.smallerRadius, effect.unresolved);
            unresolvedFloat("larger_radius", f.largerRadius, effect.unresolved);
        }
        // linear_fog_to_extinction: the classic band [start, end] read as a
        // medium that is 95% opaque across it (e^-3 ~ 0.05).
        if (style.distanceFog && style.distanceFog->linear) {
            const canon::LinearFog& fog = *style.distanceFog->linear;
            if (fog.end > fog.start) {
                effect.inputs.push_back({"extinction", {3.0 / (double{fog.end} - fog.start)}});
                effect.derived.push_back({"inputs.extinction", "linear_fog_to_extinction"});
            }
            effect.inputs.push_back({"albedo", {fog.color}});
            effect.derived.push_back({"inputs.albedo", "fog_color_to_albedo"});
        }
        effect.style = {{"fog", {style}}};
    }
    if (uint32_t unknown = f.flags & ~kFogExitBlend; unknown != 0) {
        effect.unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, unknown, std::nullopt});
    }
    requireOpaque(f.fog, "fog.color.alpha", effect.unresolved);
    requireOpaque(f.underwater, "underwater.color.alpha", effect.unresolved);
    if (f.doodadSetReserved) {
        effect.unresolved.push_back({"doodad_set.reserved", canon::UnresolvedField::Kind::Scalars,
                                     std::vector<double>(f.doodadSetReserved->begin(), f.doodadSetReserved->end()),
                                     std::nullopt});
    }
    instance.effect = std::move(effect);
    return instance;
}

std::string fieldAt(const std::string& base, uint32_t offset) {
    char text[16];
    std::snprintf(text, sizeof(text), "+0x%X", offset);
    return base + text;
}

void unresolvedScalar(std::vector<canon::UnresolvedField>& out, std::string field, double value, bool keepZero = false) {
    if (value != 0 || keepZero) out.push_back({std::move(field), canon::UnresolvedField::Kind::Scalar, value, std::nullopt});
}

template <size_t N>
void unresolvedScalars(std::vector<canon::UnresolvedField>& out, std::string field, const std::array<float, N>& values) {
    if (std::all_of(values.begin(), values.end(), [](float v) { return v == 0; })) return;
    out.push_back({std::move(field), canon::UnresolvedField::Kind::Scalars, std::vector<double>(values.begin(), values.end()),
                   std::nullopt});
}

}  // namespace

std::vector<std::optional<canon::InstanceRef>> placeFogs(const WmoInputs& in, EffectPlacer& placer) {
    const wmo::RootFile& root = *in.root;
    std::vector<std::optional<canon::InstanceRef>> out(root.fogs.size());
    for (uint32_t i = 0; i < root.fogs.size(); ++i) {
        if (isEmptyFog(root.fogs[i])) continue;
        out[i] = placer.place(root.fogs[i].doodadSet.value_or(0), buildFogInstance(in, i));
    }
    return out;
}

canon::PlacedInstance particulateInstance(const wmo::ParticulateVolume& v, const TextureResolutions& textures,
                                          canon::SourceRecord origin) {
    canon::PlacedInstance instance;
    instance.asset = canon::Ref{canon::None{}, "classic_particulate_volume", canon::NameSource::Synthesized};
    // Every _mpv.wdt volume's world box is centred on its position and its
    // extent's size (619/619), so the extent is the full size.
    instance.translation = toCanon(v.position);
    instance.flags = v.flags;
    canon::EffectPlacement effect;
    effect.origin = std::move(origin);

    std::vector<canon::InputValue> emitters;
    canon::SourceStyle emitterStyle{"emitter", {}, true};
    std::vector<canon::UnresolvedField>& unresolved = effect.unresolved;
    for (size_t k = 0; k < v.emitters.size(); ++k) {
        const wmo::ParticulateEmitter& e = v.emitters[k];
        std::vector<canon::NamedInput> fields{{"texture", {textureById(textures, e.textureFileDataId)}},
                                              {"tiles_x", {int64_t{e.tilesX}}},
                                              {"tiles_y", {int64_t{e.tilesY}}},
                                              {"frames", {int64_t{e.frames}}}};
        canon::StyleBlock block;
        block.draw = canon::StyleBlock::Draw{framebufferBlendFor(e.blendMode), e.color};
        // blend_to_light_role: an additive particle adds light, an
        // alpha-blended one reflects it (EGxBlend, documented, unlike M2's
        // particle blend ids).
        if (block.draw->blend == canon::FramebufferBlend::Add || block.draw->blend == canon::FramebufferBlend::Alpha) {
            bool additive = block.draw->blend == canon::FramebufferBlend::Add;
            fields.push_back({"light_role", {std::string(additive ? "emission" : "albedo")}});
            effect.derived.push_back({"inputs.emitters." + std::to_string(k) + ".light_role", "blend_to_light_role"});
        }
        emitters.push_back({std::move(fields)});
        emitterStyle.blocks.push_back(block);
        std::string base = "emitters[" + std::to_string(k) + "]";
        unresolvedScalars(unresolved, base + ".parameters", e.parameters);
        if (!block.draw->blend) {
            unresolved.push_back({base + ".blend", canon::UnresolvedField::Kind::Id, double(e.blendMode), std::nullopt});
        }
        unresolvedScalar(unresolved, fieldAt(base, 0x90), e.unknown90);
        unresolvedScalar(unresolved, fieldAt(base, 0xA8), e.unknownA8);
    }
    effect.inputs = {{"size", {toCanon(v.extent)}}, {"emitters", {std::move(emitters)}}};
    canon::StyleBlock volumeStyle;
    // The particle cap: the shader bounds its simulation threads by it.
    volumeStyle.maxParticles = v.particleCount;
    effect.style = {{"volume", {volumeStyle}, false}, std::move(emitterStyle)};
    // count_to_spawn_density: the cap spread over the box, for each emitter
    // (whether the client splits it between emitters is unknown).
    double volume = double{v.extent[0]} * v.extent[1] * v.extent[2];
    if (volume > 0) {
        effect.inputs.push_back({"density", {v.particleCount / volume}});
        effect.derived.push_back({"inputs.density", "count_to_spawn_density"});
    }

    // Neither the rotation's axis order nor its sign convention is settled
    // (2 of 193 records turn about two axes): carried, not applied.
    if (v.rotation != wmo::Vec3{}) {
        unresolved.push_back({"rotation", canon::UnresolvedField::Kind::Angle,
                              std::vector<double>(v.rotation.begin(), v.rotation.end()), std::nullopt});
    }
    if (v.flags != 0) unresolved.push_back({"flags", canon::UnresolvedField::Kind::Bits, v.flags, std::nullopt});
    unresolvedScalar(unresolved, fieldAt("record", 0x04), v.unknown04);
    unresolvedScalar(unresolved, fieldAt("record", 0x2C), v.unknown2C);
    unresolved.push_back({"preset", canon::UnresolvedField::Kind::Id, double(v.presetId), std::nullopt});
    unresolvedScalar(unresolved, fieldAt("record", 0x38), v.unknown38);
    unresolvedScalar(unresolved, "opacity", v.opacity, true);
    if (!v.envelope.empty()) {
        std::vector<double> keys;
        for (const auto& [value, value2, time] : v.envelope) keys.insert(keys.end(), {time, value, value2});
        unresolved.push_back({"envelope (time, value, value2)", canon::UnresolvedField::Kind::Scalars, keys, std::nullopt});
    }
    unresolvedScalars(unresolved, fieldAt("record", 0x220), v.unknown220);
    unresolvedScalar(unresolved, fieldAt("record", 0x29C), v.unknown29C);
    for (const wmo::UnknownRange& range : v.unknownRanges) {
        unresolved.push_back({fieldAt("record", range.offset), canon::UnresolvedField::Kind::Words, range.words, std::nullopt});
    }
    instance.effect = std::move(effect);
    return instance;
}

AmbientRefs placeAmbient(const WmoInputs& in, EffectPlacer& placer) {
    const wmo::RootFile& root = *in.root;
    for (uint32_t i = 0; i < root.globalAmbientVolumes.size(); ++i) {
        const wmo::AmbientVolume& v = root.globalAmbientVolumes[i];
        placer.place(v.doodadSet, globalAmbientInstance(v, i));
    }
    AmbientRefs out;
    for (uint32_t i = 0; i < root.ambientVolumes.size(); ++i) {
        const wmo::AmbientVolume& v = root.ambientVolumes[i];
        out.points.push_back(placer.place(v.doodadSet, pointAmbientInstance(v, i)));
    }
    for (uint32_t i = 0; i < root.ambientBoxes.size(); ++i) {
        const wmo::AmbientBox& b = root.ambientBoxes[i];
        out.boxes.push_back(placer.place(b.doodadSet, boxAmbientInstance(b, i)));
    }
    return out;
}

std::vector<canon::InstanceRef> placeParticulate(const WmoInputs& in, EffectPlacer& placer) {
    std::vector<canon::InstanceRef> out;
    for (uint32_t i = 0; i < in.root->particulateVolumes.size(); ++i) {
        out.push_back(placer.place(0, particulateInstance(in.root->particulateVolumes[i], in.textures, {"MPVD", i})));
    }
    return out;
}

void appendRefs(uint32_t part, const char* tag, const std::vector<uint16_t>& refs,
                const std::vector<canon::InstanceRef>& instances, std::vector<canon::InstanceRef>& out) {
    for (uint16_t r : refs) {
        if (r >= instances.size()) {
            throw std::runtime_error("WMO group " + std::to_string(part) + "'s " + tag + ": expected refs < " +
                                     std::to_string(instances.size()) + " root records, got " + std::to_string(r));
        }
        out.push_back(instances[r]);
    }
}

std::vector<canon::InstanceRef> partFogs(uint32_t part, const wmo::GroupFile& g,
                                         const std::vector<std::optional<canon::InstanceRef>>& fogs) {
    std::vector<uint16_t> refs = g.fogRefs.empty() ? std::vector<uint16_t>(g.fogIds.begin(), g.fogIds.end()) : g.fogRefs;
    const char* tag = g.fogRefs.empty() ? "fogIds" : "MFVR";
    std::vector<canon::InstanceRef> out;
    for (uint16_t r : refs) {
        if (r >= fogs.size()) {
            throw std::runtime_error("WMO group " + std::to_string(part) + "'s " + tag + ": expected refs < " +
                                     std::to_string(fogs.size()) + " MFOG records, got " + std::to_string(r));
        }
        if (r != 0 && fogs[r]) out.push_back(*fogs[r]);
    }
    return out;
}

}  // namespace husk::wmoinput
