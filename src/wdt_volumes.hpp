#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "wmo.hpp"  // ParticulateVolume

// A map's volume sidecars: <map>_fogs.wdt (VFOG/VFEX/VFE2) and <map>_mpv.wdt
// (PVMI/PVPD/PVBD groups), per documentation/wowdev-wiki/md/WDT.md plus
// WIKI_FINDINGS/WORLD.md for VFE2 and PVMI. Positions are world-frame
// (+X north, +Y west, Z up, origin at the map centre): every real PVMI
// position and 1,503/1,542 VFOG positions land on an existing tile of their
// map that way. Chunk tags are byte-reversed on disk.
namespace husk::wdt {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Vec3 = std::array<float, 3>;

// VFOG, 104 bytes.
struct Fog {
    Vec3 color{};  // 0..1
    std::array<float, 3> intensity{};
    float unknown18 = 0;
    Vec3 position{};
    std::array<float, 4> rotation{};  // quaternion x, y, z, w
    std::array<float, 3> radius{};    // wiki: fog start radius (unverified)
    std::array<int32_t, 4> animationPeriods{};
    uint32_t flags = 0;
    uint32_t modelFileDataId = 0;  // 0: the client falls back to 166046 (spells/errorcube.m2)
    uint32_t fogLevel = 0;         // 0..2, compared against the client's fog-level setting
    uint32_t id = 0;               // unique across the files
    // VFEX (96 bytes) and VFE2 (176 bytes, undocumented on the wiki), each
    // naming its VFOG by id, as fields; nullopt when absent.
    //
    // VFE2 (WIKI_FINDINGS/WORLD.md "_fogs.wdt VFE2"): three byte-quantized
    // RGB colours at +0x00/+0x20/+0x2C and a mode float at +0x3C, only ever
    // 1.0 or 2.0; their fog roles are candidates only. The other words up to
    // +0x70 are floats characterized by range only; +0x70.. is zero in all
    // 1,542 records, kept in `unknownRanges` only if not.
    struct Palette {
        std::array<Vec3, 3> colors{};
        float mode = 0;
        std::array<float, 5> scalars0C{};  // +0x0C..+0x1F
        float scalar38 = 0;
        std::array<float, 3> scalars40{};  // +0x40..+0x4B
        std::array<float, 8> scalars50{};  // +0x50..+0x6F
        std::vector<wmo::UnknownRange> unknownRanges;
    };
    std::optional<Palette> palette;
    // VFEX's characterized fields (WIKI_FINDINGS/WORLD.md "_fogs.wdt VFEX"),
    // from 1,542 real records. Its +0x48..+0x5F tail is zero in all of them,
    // kept in `unknownRanges` only if not.
    struct Extra {
        uint32_t enabled = 0;  // 1 in 1,539, 0 in 3
        Vec3 extent{};         // +0x04, positive, 10..hundreds
        // +0x10..+0x43; 1.0 is the common default of each. Co-variation
        // doesn't split them into clean triples, so they stay one array.
        std::array<float, 13> parameters{};
        std::vector<wmo::UnknownRange> unknownRanges;
    };
    std::optional<Extra> extraFields;
};

struct FogsFile {
    uint32_t version = 0;
    std::vector<Fog> fogs;
};

// PVPD, 16 bytes. Semantics unresolved: a unit 2D vector (4,467/4,467 real
// records), a field only ever -0.0, and a world-coordinate-sized float. Not
// a vertical half-plane with PVBD's boxes on one side (tested, fails).
struct ParticulatePoint {
    std::array<float, 2> direction{};
    float unknown08 = 0;
    float unknown0C = 0;
};

// PVBD, 64 bytes: a node of a box hierarchy. The wiki's "indices into PVPD"
// is wrong: they name boxes in the same group's PVBD (indices up to the box
// count, past the point count, in 143/619 real groups). In all 619 groups
// boxes 0..P-1 (P = the group's PVPD count) are leaves, one per PVPD
// record, every later box has children, and exactly one box is no box's
// child (the root). A root with complete = 0 (143/619) has sub-boxes that
// share leaves.
struct ParticulateBox {
    Vec3 min{};
    Vec3 max{};
    std::vector<uint32_t> children;  // into the group's PVBD, the first num_unk1C of 8
    uint32_t complete = 0;  // wiki: 0 = joined with the next entry (unverified)
};

// One PVMI blob: the shared particulate-volume record plus the map trailer.
struct MapParticulateVolume {
    wmo::ParticulateVolume volume;
    Vec3 boundsMin{};
    Vec3 boundsMax{};
    uint32_t id = 0;  // unique across the files, same convention as VFOG.id
    std::vector<ParticulatePoint> points;  // the group's PVPD records
    std::vector<ParticulateBox> boxes;     // the group's PVBD records
};

struct MpvFile {
    uint32_t version = 0;
    std::vector<MapParticulateVolume> volumes;
};

// Throws ParseError when MVER is missing, a record size doesn't divide its
// chunk, or a VFEX/VFE2 names no VFOG or a VFOG twice.
FogsFile parseFogs(const std::vector<uint8_t>& fileBytes);

// Throws ParseError when MVER is missing, a PVMI appears under an MVER other
// than 3 (the only layout in real files), a PVPD/PVBD comes before any PVMI, a PVMI isn't 4,312 bytes,
// its trailer's PVPD count disagrees with the group, or a box's child count
// is above 8 or names a box past the group's PVBD.
MpvFile parseMpv(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::wdt
