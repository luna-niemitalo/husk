#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "wmo_canon_input.hpp"

// What wmo_canon_input.cpp (the model) and wmo_effects_input.cpp (its fog,
// ambient and particulate effect instances) share; not for other callers.
namespace husk::wmoinput {

// Defined in wmo_canon_input.cpp.
canon::Vec3 toCanon(const wmo::Vec3& v);

// MOMT.blendMode is an EGxBlend index (Rendering.md), not M2's remapped
// M2BLEND; only the modes canon::FramebufferBlend names are mapped.
std::optional<canon::FramebufferBlend> framebufferBlendFor(uint32_t egxBlend);

// MOCV stores BGRA bytes; canon colour sets are RGBA.
std::array<uint8_t, 4> bgraToRgba(uint32_t bgra);

canon::TextureRef textureById(const TextureResolutions& textures, uint32_t fdid);

canon::Vec3 colorOf(uint32_t bgra);

// Defined in wmo_effects_input.cpp.

// Puts effect instances into a WMO's owned sets. Ids follow the doodads'
// MODD indices, so they stay unique across the model's sets. A model
// without doodad sets gets an always-on "effects" set as set 0, which is
// where an instance with no set of its own goes otherwise.
struct EffectPlacer {
    std::vector<canon::Model::OwnedSet>& sets;
    uint32_t nextId;

    canon::InstanceRef place(uint32_t set, canon::PlacedInstance instance) {
        if (sets.empty()) {
            canon::Model::OwnedSet effects;
            effects.set.ref = canon::Ref{canon::RecordIndex{0}, "effects", canon::NameSource::Synthesized};
            effects.alwaysOn = true;
            sets.push_back(std::move(effects));
        }
        instance.id = nextId;
        sets[set].set.instances.push_back(std::move(instance));
        return {set, nextId++};
    }
};

// Every MFOG record but the empty entry, in the set it applies with (MFED),
// else set 0. Returns each record's instance, nullopt for the empty entry.
std::vector<std::optional<canon::InstanceRef>> placeFogs(const WmoInputs& in, EffectPlacer& placer);

// MAVG/MAVD/MBVD, each in the set it applies with. Returns the MAVD and
// MBVD instances, which group refs name; nothing names an MAVG.
struct AmbientRefs {
    std::vector<canon::InstanceRef> points;
    std::vector<canon::InstanceRef> boxes;
};

AmbientRefs placeAmbient(const WmoInputs& in, EffectPlacer& placer);

// MPVD has no doodad set: every volume is in set 0.
std::vector<canon::InstanceRef> placeParticulate(const WmoInputs& in, EffectPlacer& placer);

// A group's refs into a list of instances: MAVR, MBVR, MPVR.
void appendRefs(uint32_t part, const char* tag, const std::vector<uint16_t>& refs,
                const std::vector<canon::InstanceRef>& instances, std::vector<canon::InstanceRef>& out);

// A group's fog list: MFVR, else MOGP fogIds. A 0 names the model's default
// fog, not a list entry: fogIds are left-packed (2,1,0,0) and MFVR never
// lists 0, in every sampled root.
std::vector<canon::InstanceRef> partFogs(uint32_t part, const wmo::GroupFile& g,
                                         const std::vector<std::optional<canon::InstanceRef>>& fogs);

}  // namespace husk::wmoinput
