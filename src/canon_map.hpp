#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "canon_primitives.hpp"
#include "canon_ref.hpp"
#include "canon_scene.hpp"  // ParticulateVolume

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// What a map places as a whole rather than per tile: volumes that span
// tiles freely. World frame, the same as canon::Terrain (+X north, +Y west,
// Z up, origin at the map centre).
namespace husk::canon {

// A volumetric fog (WDT _fogs VFOG, with its VFEX/VFE2 extras).
struct FogVolume {
    Ref ref;  // id = RecordIndex in the source table; name = "fog_<id>", Synthesized
    SourceRecord origin;
    uint32_t id = 0;  // the source's own id, unique across a map's files
    Vec3 color;  // RGB 0..1
    std::array<float, 3> intensity{};
    float unknown18 = 0;
    Vec3 position;
    Quat rotation;
    std::array<float, 3> radius{};  // wiki: fog start radius (unverified)
    std::array<int32_t, 4> animationPeriods{};
    uint32_t flags = 0;
    // The fog's shape model (FileDataID). Ref{} when the source holds 0, for
    // which the client substitutes spells/errorcube.m2.
    Ref model;
    uint32_t fogLevel = 0;  // 0..2: drawn when the client's fog level is at least this

    // VFE2's characterized part; candidate fog roles only.
    struct Palette {
        std::array<Vec3, 3> colors{};  // RGB 0..1
        float mode = 0;                // 1 or 2
    };
    std::optional<Palette> palette;
    // VFEX's characterized part (see wdt::Fog::Extra).
    struct Extra {
        bool enabled = false;
        Vec3 extent;  // extent-like, positive
        std::array<float, 13> parameters{};  // semantics unresolved, default 1.0
    };
    std::optional<Extra> extra;
    std::vector<uint8_t> sourceExtra;   // VFEX record, raw; empty when absent
    std::vector<uint8_t> sourceExtra2;  // VFE2 record, raw; empty when absent
};

// A particulate volume a map places (WDT _mpv PVMI group). `volume` is the
// same record WMOs use, here in the world frame.
struct MapParticulateVolume {
    ParticulateVolume volume;
    uint32_t id = 0;  // unique across a map's files
    Vec3 boundsMin;
    Vec3 boundsMax;
    // PVPD, semantics unresolved: a unit 2D vector, a field only ever -0.0
    // and a world-coordinate-sized float per point.
    struct Point {
        Vec2 direction;
        float unknown08 = 0;
        float unknown0C = 0;
    };
    std::vector<Point> points;
    // PVBD: a box hierarchy. boxes[i] for i < points.size() is the leaf of
    // points[i]; every later box has children (indices into `boxes`); one
    // box is the root. A root with complete == 0 has sub-boxes that share
    // leaves.
    struct Box {
        Vec3 min;
        Vec3 max;
        std::vector<uint32_t> children;
        uint32_t complete = 0;
    };
    std::vector<Box> boxes;
};

struct MapScene {
    Ref map;  // the map's directory name
    std::vector<FogVolume> fogs;
    std::vector<MapParticulateVolume> particulateVolumes;
};

}  // namespace husk::canon
