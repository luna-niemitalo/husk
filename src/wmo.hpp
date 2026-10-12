#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// WMO (world map object: buildings, caves, dungeons) root and group files,
// per documentation/wowdev-wiki/md/WMO.md, checked against real 12.1 files.
// Layout facts and wiki corrections: WIKI_FINDINGS/WORLD.md. Like ADT, chunk
// tags are byte-reversed on disk.
//
// Positions and normals are read as stored: (X, Y, Z), Z up -- the same frame
// as M2. (WMO.md's "(X,Z,-Y)" note does not describe the bytes: a guard tower's
// groups are tallest on the third component, and doodad rotations turn about it.)
namespace husk::wmo {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using Vec2 = std::array<float, 2>;
using Vec3 = std::array<float, 3>;

struct Box {
    Vec3 min{};
    Vec3 max{};
};

// MOHD, 64 bytes.
struct RootHeader {
    uint32_t textureCount = 0;
    uint32_t groupCount = 0;
    uint32_t portalCount = 0;
    uint32_t lightCount = 0;
    uint32_t doodadNameCount = 0;  // not MODI's real length -- see RootFile::doodadFileDataIds
    uint32_t doodadDefCount = 0;
    uint32_t doodadSetCount = 0;
    uint32_t ambientColor = 0;  // CArgb, raw
    uint32_t wmoAreaTableId = 0;
    Box bounds;
    uint16_t flags = 0;
    uint16_t lodCount = 0;
};

// MOMT, 64 bytes.
struct Material {
    uint32_t flags = 0;
    uint32_t shader = 0;
    uint32_t blendMode = 0;  // EGxBlend
    std::array<uint32_t, 3> textures{};  // FileDataIDs, or MOTX byte offsets when the root has MOTX; 0 = none
    uint32_t sidnColor = 0;  // CImVector (BGRA), raw
    uint32_t diffuseColor = 0;
    uint32_t groundType = 0;  // TerrainType row
    uint32_t color2 = 0;
    uint32_t flags2 = 0;
};

// MOGI, 32 bytes.
struct GroupInfo {
    uint32_t flags = 0;
    Box bounds;
    int32_t nameOffset = -1;  // into RootFile::groupNames, -1 = unnamed
};

// MODS, 32 bytes.
struct DoodadSet {
    std::string name;
    uint32_t startIndex = 0;  // into RootFile::doodads
    uint32_t count = 0;
};

// MODD, 40 bytes.
struct Doodad {
    uint32_t nameIndex = 0;  // MODI index (or MODN byte offset on pre-8.1 files)
    uint8_t flags = 0;
    Vec3 position{};
    std::array<float, 4> rotation{};  // quaternion x, y, z, w
    float scale = 1.0f;
    uint32_t color = 0;  // CImVector (BGRA), raw
};

// MAVG (global) and MAVD (local), 48 bytes each: ambient-colour overrides
// (8.3+). MAVG's position/start/end are always zero (1,702 real records).
// Which colours flag 0x1 selects is contradictory on the wiki, so all three
// stay raw.
struct AmbientVolume {
    Vec3 position{};
    float start = 0;
    float end = 0;  // >= start in every real record
    std::array<uint32_t, 3> colors{};  // CImVector (BGRA), raw
    uint32_t flags = 0;
    uint16_t doodadSet = 0;  // MODS index
};

// MBVD, 128 bytes: a box-shaped ambient override. The six planes come in
// opposite pairs (0,1) (2,3) (4,5) with unit normals; the inside is where
// dot(normal, p) <= d for all six (54/54 real boxes non-empty that way,
// none the other way).
struct AmbientBox {
    std::array<std::array<float, 4>, 6> planes{};  // normal x, y, z, d
    float end = 0;  // <= the box's smallest half-size in 52/54: a fade distance in from the faces?
    std::array<uint32_t, 3> colors{};  // CImVector (BGRA), raw
    uint32_t flags = 0;
    uint16_t doodadSet = 0;  // MODS index
};

// MFOG, 48 bytes, with MFED's doodad set. On 4,000 real roots: every root
// has 1..10, most of them the wiki's "empty fog entry" placeholder (zero
// radii, ends 444.4445/222.2222); colour alpha is always 0xFF.
struct Fog {
    // 0x1 F_IEBLEND (wiki); 0x10, 0x100, 0x1000 and 0x10000 also occur,
    // undocumented.
    uint32_t flags = 0;
    Vec3 position{};
    float smallerRadius = 0;  // <= largerRadius in 4,526/4,534 real records
    float largerRadius = 0;
    struct Layer {
        float end = 0;
        float startScalar = 0;  // start = end * startScalar
        uint32_t color = 0;     // CImVector (BGRA), raw
    };
    Layer fog;
    Layer underwater;
    // MFED (SL+): the MODS index it applies with; nullopt without MFED.
    std::optional<uint16_t> doodadSet;
    // MFED's other 14 bytes, zero in all 300 real records: set only when a
    // record breaks that, so a build that starts using them shows up.
    std::optional<std::vector<uint8_t>> doodadSetReserved;
};

// MOPT, 20 bytes: a portal polygon, MOPV[startVertex, +count). On 800 real
// roots with portals: dot(normal, p) + distance == 0 for every vertex.
struct Portal {
    uint16_t startVertex = 0;
    uint16_t count = 0;
    Vec3 normal{};
    float distance = 0;
};

// MOPR, 8 bytes: a group's link through a portal. Each group owns the
// MOPR range its MOGP portalStart/portalCount names.
struct PortalRef {
    uint16_t portal = 0;  // MOPT index
    uint16_t group = 0;   // the group on the other side
    int16_t side = 0;     // +1/-1
};

// MPVD emitter entry, 844 bytes; four per record (WIKI_FINDINGS/WORLD.md
// "WMO MPVD"). Names below are value-characterized from the corpus, not
// from any symbol.
struct ParticulateEmitter {
    uint32_t slot = 0;  // 0..3 within the record
    std::array<float, 32> parameters{};  // emission parameters, per-field semantics unresolved
    std::array<float, 4> color{};        // RGBA 0..1, byte-quantized
    uint32_t textureFileDataId = 0;      // particle texture; 0 = slot unused
    uint32_t blendMode = 0;              // EGxBlend: 2 alpha, 3 additive seen
    uint32_t tilesX = 0;                 // flipbook grid
    uint32_t tilesY = 0;
    uint32_t frames = 0;  // frame count: the vertex shader takes frameIndex % frames
    float unknown90 = 0;  // entry +0x90, mostly 0
    float unknownA8 = 0;  // entry +0xA8, 1..35
};

// A byte range a record's layout doesn't describe, kept only when it isn't
// all zero (in the corpus it is), so a build that starts using it shows up.
struct UnknownRange {
    uint32_t offset = 0;          // from the record's start
    std::vector<uint32_t> words;  // the range's dwords, raw
};

// Appends [begin, end) of `record` (dword-aligned, inside the record) to
// `out` when any dword in it is nonzero. The one place both WMO and map
// sidecar records apply that rule.
void keepIfNonzero(const uint8_t* record, size_t begin, size_t end, std::vector<UnknownRange>& out);

// MPVD, 4,272 bytes: a box volume of ambient particles (dust, snow, ash).
struct ParticulateVolume {
    uint32_t flags = 0;  // bit 0 always set, bits 2..4 vary; meaning unresolved
    Vec3 position{};
    Vec3 extent{};
    Vec3 rotation{};  // radians, axis order unverified
    uint32_t presetId = 0;       // +0x30: shared across unrelated files, not a FileDataID
    uint32_t particleCount = 0;  // +0x34: count/density-like, 25..14,000
    float opacity = 0;           // +0x3C: density/opacity-like, 0.09..1
    // +0x40: (value, value2, time in seconds), time ascending; count at +0x298.
    std::vector<std::array<float, 3>> envelope;
    std::vector<ParticulateEmitter> emitters;  // used slots only (texture != 0)
    // Characterized by value range only (WIKI_FINDINGS/WORLD.md "Particulate
    // volumes").
    uint32_t unknown04 = 0;               // {0, 2}
    float unknown2C = 0;                  // 0.5..10, often 1
    uint32_t unknown38 = 0;               // {0, 1}
    std::array<float, 20> unknown220{};   // 10 records set the first 5, all 1.0
    float unknown29C = 0;                 // 0.1..18.8, usually 1
    // Every other range the layout leaves undescribed: +0x270..+0x298,
    // +0x2A0..+0x2D0, envelope slots past the key count, unused emitter
    // slots, each used emitter's +0xAC.. tail, and +0x1000..+0x10AC.
    std::vector<UnknownRange> unknownRanges;
};

inline constexpr size_t kParticulateVolumeSize = 4272;

// One 4,272-byte particulate-volume record at `offset` -- WMO MPVD's
// records and the head of every _mpv.wdt PVMI blob. `what` names it in
// errors. Throws ParseError when the record runs past `size`, the envelope
// count exceeds 40, or the +0x10AC emitter count disagrees with the slots
// that have a texture.
ParticulateVolume parseParticulateVolume(const uint8_t* data, size_t size, size_t offset, const std::string& what);

// MOLT, 48 bytes. The wiki suspects these only ever fed offline MOCV
// baking; every populated MOLT in a 4,000-root sample is type 0 (omni).
struct LegacyLight {
    uint8_t type = 0;  // 0 omni, 1 spot, 2 direct, 3 ambient
    uint8_t useAttenuation = 0;
    std::array<uint8_t, 2> unknown{};  // wiki "pad", "not padding as of v16"; real values 01 00 / 01 01
    uint32_t color = 0;  // CImVector (BGRA), raw
    Vec3 position{};
    float intensity = 0;
    std::array<float, 4> rotation{};  // quaternion x, y, z, w; spot/direct only
    float attenuationStart = 0;
    float attenuationEnd = 0;
};

// MNLD, 184 bytes (Shadowlands+ dynamic lights). Offsets checked on 30,044
// real records: every gap and unused field is zero, lightIndex is the
// record's own index, and attenuation start <= end throughout.
struct NewLight {
    int32_t type = 0;  // 0 point, 1 spot
    int32_t lightIndex = 0;
    int32_t flags = 0;      // 0x1 blend innerColor -> outerColor, 0x2 casts shadows
    int32_t doodadSet = 0;  // MODS index this light belongs to
    uint32_t innerColor = 0;  // CImVector (BGRA), raw
    Vec3 position{};
    Vec3 rotation{};  // Euler radians, axis order undocumented
    float attenuationStart = 0;
    float attenuationEnd = 0;
    float intensity = 0;
    uint32_t outerColor = 0;  // CImVector (BGRA), raw; used with flag 0x1
    float blendStart = 0;
    float blendEnd = 0;
    float flickerIntensity = 0;
    float flickerSpeed = 0;
    int32_t flickerMode = 0;  // 0 off, 1 sine, 2 noise, 3 noise step
    uint32_t cookieFileDataId = 0;  // 0 = none; a cube map for point lights
    float falloff = 0;     // spot: falloff exponent
    float innerAngle = 0;  // spot, radians
    float outerAngle = 0;  // spot, radians
    float scale = 0;                // stored as a half float; used with flag 0x2
    float intensityMultiplier = 0;  // stored as a half float; the client treats 0 as 1
};

// MOLS, 56 bytes: a group's spot lights, for its light sets (BfA+). Layout
// corpus-verified on all 378 records (WIKI_FINDINGS/WORLD.md "WMO lightset
// lights"); the field names are inferred from value classes.
struct GroupSpotLight {
    uint32_t lightIndex = 0;
    uint32_t color = 0;  // CImVector (BGRA), raw
    Vec3 position{};
    float attenuationStart = 0;
    float attenuationEnd = 0;
    float intensity = 0;
    Vec3 rotation{};      // Euler radians, axis order undocumented
    float unknown = 0;    // 1.0 in every real record
    float innerAngle = 0; // radians
    float outerAngle = 0; // radians, at most ~pi/4 in real records
};

// MOLP, 44 bytes: a group's point lights, for its light sets. The wiki's
// attenStart/attenEnd labels are swapped (start > end in 97.4% of 57,584
// records under its reading): +0x18 is the end, +0x1C the start.
struct GroupPointLight {
    uint32_t unknown0 = 0;
    uint32_t color = 0;  // CImVector (BGRA), raw
    Vec3 position{};
    float intensity = 0;
    float attenuationEnd = 0;
    float attenuationStart = 0;
    float unknown20 = 0;  // 0 in every real record
    float unknown24 = 0;  // a float, default -0.0
    float unknown28 = 0;  // a float, default -1.0
};

// MLSS/MLSP: one light set's window into the group's MOLS/MOLP records.
struct LightSetWindow {
    uint32_t offset = 0;
    uint32_t count = 0;
};

struct RootFile {
    uint32_t version = 0;
    RootHeader header;
    std::vector<Material> materials;
    std::string textureNames;  // raw MOTX blob; empty: material textures are FileDataIDs
    std::string groupNames;    // raw MOGN blob
    std::vector<GroupInfo> groups;
    // GFID: row-major [lodTier][group], 0 = no file for that cell. Empty when
    // the root has no GFID chunk.
    std::vector<uint32_t> groupFileDataIds;
    std::vector<DoodadSet> doodadSets;
    std::vector<uint32_t> doodadFileDataIds;  // MODI, sized off the chunk, never header.doodadNameCount
    std::string doodadNames;                  // raw MODN blob (pre-8.1)
    std::vector<Doodad> doodads;
    std::vector<float> doodadColorMultipliers;  // MDDI, parallel to doodads; empty when absent
    // MOUV: per material, two layers' UV scroll speed. Empty when absent (all zero).
    std::vector<std::array<Vec2, 2>> uvScrollSpeeds;
    std::vector<LegacyLight> legacyLights;  // MOLT
    std::vector<NewLight> newLights;        // MNLD
    std::vector<ParticulateVolume> particulateVolumes;  // MPVD
    std::vector<AmbientVolume> globalAmbientVolumes;  // MAVG
    std::vector<AmbientVolume> ambientVolumes;        // MAVD
    std::vector<AmbientBox> ambientBoxes;             // MBVD
    std::vector<Fog> fogs;                            // MFOG + MFED
    std::vector<Vec3> portalVertices;                 // MOPV
    std::vector<Portal> portals;                      // MOPT
    std::vector<PortalRef> portalRefs;                // MOPR
    // MOPE (11.1+), one raw 16-byte record per portal; empty when absent.
    // Real records hold ASCII fragments and "MOP" bytes in the documented
    // index field, so nothing in them is typed.
    std::vector<std::array<uint8_t, 16>> portalExtras;
    uint32_t skyboxFileDataId = 0;  // MOSI; 0 = none
    std::string skyboxName;         // MOSB; empty = none
};

// MOBA, 24 bytes (Legion+ layout).
struct Batch {
    uint32_t startIndex = 0;  // into GroupFile::indices
    uint32_t indexCount = 0;
    uint16_t minVertex = 0;
    uint16_t maxVertex = 0;
    uint32_t material = 0;  // index into RootFile::materials (material_id or material_id_large, per the flag)
};

// MPY2 flag: the face's ground type is its MOQG entry, not its material's.
inline constexpr uint16_t kFaceUsesQueryGroundType = 0x100;

// MOPY (2 bytes) or MPY2 (4 bytes) per triangle.
struct TriangleInfo {
    uint16_t flags = 0;
    uint16_t material = 0;  // 0xFF (MOPY) / 0xFFFF (MPY2): collision-only, not rendered
};

// MLIQ: a liquid height grid. Vertices and tiles are row-major, x fastest
// (the wiki's one "column major" note is wrong for real files: on 122 real
// non-square grids, row-major gives the smoother surface and the vertex
// usage that matches the tiles); vertex (x, y) sits at corner + (x, y) *
// kLiquidTileSize, +X/+Y.
struct Liquid {
    uint32_t vertsX = 0;
    uint32_t vertsY = 0;  // tiles are (vertsX - 1) * (vertsY - 1)
    Vec3 corner{};
    uint16_t material = 0;  // MOMT index
    // Per vertex, the 4 bytes before the height: flow1/flow2/flow1Pct/filler
    // for water, int16 s/t (UV * 256) for magma; which one depends on the
    // liquid type's material, outside this file.
    std::vector<std::array<uint8_t, 4>> vertexData;
    std::vector<float> heights;
    // SMOLTile: low nibble legacy liquid type (0xF = no liquid here),
    // 0x40 fishable, 0x80 shared, 0x10/0x20 unknown.
    std::vector<uint8_t> tiles;
};

// An ADT unit: a map chunk is 8 of these, a map tile 128 (wiki MLIQ).
inline constexpr float kLiquidTileSize = 25.0f / 6.0f;

// One group file: the MOGP header plus its sub-chunks.
struct GroupFile {
    uint32_t version = 0;
    uint32_t nameOffset = 0;
    uint32_t descriptiveNameOffset = 0;
    uint32_t flags = 0;
    Box bounds;
    uint16_t portalStart = 0;
    uint16_t portalCount = 0;
    uint16_t transparentBatchCount = 0;
    uint16_t interiorBatchCount = 0;
    uint16_t exteriorBatchCount = 0;
    std::array<uint8_t, 4> fogIds{};  // MFOG indices, unused slots 0
    uint32_t liquidType = 0;
    uint32_t wmoAreaTableId = 0;
    uint32_t flags2 = 0;

    std::vector<TriangleInfo> triangles;
    std::vector<uint32_t> indices;  // MOVI (u16) or MOVX (u32), widened
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::vector<Vec2>> uvSets;            // every MOTV, in file order
    std::vector<std::vector<uint32_t>> colorSets;     // every MOCV (BGRA, raw), in file order
    std::vector<Batch> batches;
    // Per-face ground types (DF+): MOQG[i] is the TerrainType row of the
    // face queryFaceStart + i (MOGX's first word), used where that face's
    // MPY2 flags have 0x100 (wowdev.wiki WMO#MOQG; checked on 278 sampled
    // groups: the flagged faces span exactly that window).
    uint32_t queryFaceStart = 0;
    std::vector<uint32_t> queryFaceGroundTypes;
    std::optional<Liquid> liquid;  // MLIQ
    // The root volumes this group uses (8.3+/9.0+): MPVR into MPVD, MAVR
    // into MAVD, MBVR into MBVD, MFVR into MFOG. Empty when absent.
    std::vector<uint16_t> particulateVolumeRefs;
    std::vector<uint16_t> ambientVolumeRefs;
    std::vector<uint16_t> ambientBoxRefs;
    std::vector<uint16_t> fogRefs;
    // The root lights reaching this group: MOLR into MOLT, MNLR into MNLD.
    std::vector<uint16_t> legacyLightRefs;
    std::vector<uint16_t> newLightRefs;
    // The group's own light-set lights (MOLS, MOLP) and each set's window
    // into them (MLSS, MLSP; set k pairs the k-th window of each).
    std::vector<GroupSpotLight> spotLights;
    std::vector<GroupPointLight> pointLights;
    std::vector<LightSetWindow> spotLightSets;
    std::vector<LightSetWindow> pointLightSets;
};

// Throws ParseError when a required chunk (MVER, MOHD) is missing, a chunk's
// size doesn't fit its record size, or a record runs past its chunk.
RootFile parseRoot(const std::vector<uint8_t>& fileBytes);

// Throws ParseError when MVER or MOGP is missing, MOGP is shorter than its
// 68-byte header, a per-vertex chunk's count differs from MOVT's, a batch
// runs past the index buffer, or MLIQ's counts don't match its size or each
// other (tiles = verts - 1 per axis), or an MLSS/MLSP window runs past its
// MOLS/MOLP records.
GroupFile parseGroup(const std::vector<uint8_t>& fileBytes);

// GFID cell for (lodTier, group); 0 when the cell is empty or out of range.
uint32_t groupFileDataId(const RootFile& root, uint32_t lodTier, uint32_t group);

// The zero-terminated string starting at `offset` in a MOGN/MOTX/MODN blob.
// Throws ParseError when `offset` is past the blob.
std::string stringAt(const std::string& blob, uint32_t offset);

}  // namespace husk::wmo
