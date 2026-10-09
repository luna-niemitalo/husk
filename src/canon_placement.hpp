#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_primitives.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// REFACTOR/PLACEMENT_SETS.md: a thin list of "put this asset here", owned or
// referenced by anything (a WMO's furniture, a terrain tile, a housing
// layout). Its invariants are what keep runtime-movable objects possible:
// instances are never baked into their surroundings, ids are stable,
// transforms are local to the set's parent frame, and per-instance overrides
// live on the instance, never on the asset.
namespace husk::canon {

// Light colour sampled at an instance's authored spot (WMO MODD color, MDDI
// multiplier). Stale once the instance moves; a runtime may recompute it.
struct InstanceTint {
    std::array<uint8_t, 4> rgba{};  // alpha as stored: WMO uses 1..254 as an index into the building's lights
    float multiplier = 1.0f;
};

struct PlacedInstance {
    uint32_t id = 0;  // unique within its set; what a runtime addresses
    Ref asset;        // the placed asset's identity
    Vec3 translation;
    Quat rotation;  // parent-from-asset
    float scale = 1.0f;
    std::optional<InstanceTint> tint;
    uint32_t flags = 0;  // source-format placement flags, raw
    // For an asset that owns placement sets: which of them this instance
    // turns on, by set ref. nullopt = the asset's defaults (its always-on sets).
    std::optional<std::vector<Ref>> activeSets;
};

struct PlacementSet {
    Ref ref;  // e.g. a WMO MODS name ("Set_$DefaultGlobal")
    std::vector<PlacedInstance> instances;
};

}  // namespace husk::canon
