// Command-layer tests: exercise husk::commands::info/exportGlb by spawning
// the real compiled binary (see run_husk.hpp) against small, synthetic,
// on-disk fixtures. Unlike tests/test_integration.cpp, none of these need
// real game files or HUSK_TEST_* env vars -- they always run.
//
// This file exists because of a real, confirmed gap (see FAILURES.md #5):
// cmd_info.cpp/cmd_export.cpp -- the only place several bugs actually lived
// (FAILURES.md #1-#4) -- had zero committed test coverage that didn't
// require a personal WoW install. Every fixture below targets one specific,
// previously-confirmed-broken behavior; if any of these start failing again,
// it's a real regression, not a flake.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "run_husk.hpp"

namespace {

using husk::test::runHusk;
namespace fs = std::filesystem;

fs::path tempPath(const std::string& name) {
    return fs::temp_directory_path() / ("husk-cli-test-" + name);
}

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v >> 16));
    b.push_back(static_cast<uint8_t>(v >> 24));
}

void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}

void putTag(std::vector<uint8_t>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

// A minimal valid-shaped MD20 blob: every field husk::m2::parseBlob reads,
// zeroed, through particleEmitters (minHeaderSize = 0x130 = 304 bytes --
// see src/m2.cpp's offset table, or tests/test_m2.cpp's independent
// transcription of the same wowdev.wiki spec for the authoritative
// field-by-field layout). Field *values* don't matter here -- these tests
// are about cmd_info.cpp/cmd_export.cpp's own exception handling, not
// M2 field semantics -- only that parseHeader gets past the header
// successfully so a test can target one specific field beyond it. The
// REQUIRE guards against silently building the wrong-sized fixture (the
// exact mistake this generator's ad-hoc predecessor made during manual
// testing -- see FAILURES.md's history).
std::vector<uint8_t> minimalMd20(uint32_t version = 274) {
    std::vector<uint8_t> b;
    putTag(b, "MD20");
    putU32(b, version);
    for (int i = 0; i < 74; ++i) putU32(b, 0);
    REQUIRE(b.size() == 0x130);
    return b;
}

// A minimalMd20 with one real, zeroed M2Vertex (48 bytes) appended right
// after the header, `vertices` pointing at it -- enough to get an export
// past the vertex-parsing and model/.skin cross-check stages so a test can
// target something further along the pipeline (bones, skinning).
std::vector<uint8_t> tinyValidM2() {
    auto b = minimalMd20();
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(b.size());
    std::memcpy(b.data() + 0x03C, &count, 4);
    std::memcpy(b.data() + 0x040, &off, 4);
    b.resize(b.size() + 0x30, 0);
    return b;
}

// Wraps `md20` (e.g. tinyValidM2()'s output) in an MD21 chunk, plus an
// SFID chunk holding `skinFileDataIds` (empty means no SFID chunk at all)
// -- used by the LOD-auto-selection ('auto' + --skin-dir) tests below.
std::vector<uint8_t> chunkedM2WithSfid(const std::vector<uint8_t>& md20,
                                        const std::vector<uint32_t>& skinFileDataIds) {
    std::vector<uint8_t> b;
    putTag(b, "MD21");
    putU32(b, static_cast<uint32_t>(md20.size()));
    b.insert(b.end(), md20.begin(), md20.end());
    if (!skinFileDataIds.empty()) {
        putTag(b, "SFID");
        putU32(b, static_cast<uint32_t>(skinFileDataIds.size() * 4));
        for (uint32_t id : skinFileDataIds) putU32(b, id);
    }
    return b;
}

// Pairs with tinyValidM2(): one global-vertex slot (-> M2 vertex 0) and one
// degenerate triangle (all three corners = that same slot). submeshes/
// batches are left empty (count 0) -- these fixtures exercise unrelated
// failure paths (bone cycles, NaN vertices, huge counts), not materials, so
// "no submeshes" is fine as long as the header itself parses, which now
// requires those two array descriptors to physically be present (real
// .skin files always have them -- see src/skin.hpp's Header).
std::vector<uint8_t> tinyMatchingSkin() {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 1);
    putU32(b, 44);  // vertices: count=1, offset=44
    putU32(b, 3);
    putU32(b, 46);  // indices: count=3, offset=46
    putU32(b, 0);
    putU32(b, 0);  // bones: count=0, offset=0 (unread, see skin.hpp)
    putU32(b, 0);
    putU32(b, 0);  // submeshes: count=0, offset=0
    putU32(b, 0);
    putU32(b, 0);  // batches: count=0, offset=0
    REQUIRE(b.size() == 44);
    b.push_back(0);
    b.push_back(0);  // vertices[0] = 0
    for (int i = 0; i < 3; ++i) {
        b.push_back(0);
        b.push_back(0);  // indices = [0, 0, 0]
    }
    return b;
}

// A minimalMd20 with two zeroed M2Vertex records (global vertices 0 and 1)
// and one zeroed M2Material (blendMode 0 = Opaque, flags 0), for the
// FAILURES2.md #1 (skinSectionId/geoset) regression test below -- needs two
// real vertices (one per submesh) and at least one material for two batches
// to resolve against.
std::vector<uint8_t> twoVertexOneMaterialM2() {
    auto b = minimalMd20();
    uint32_t vertCount = 2;
    uint32_t vertOff = static_cast<uint32_t>(b.size());
    std::memcpy(b.data() + 0x03C, &vertCount, 4);
    std::memcpy(b.data() + 0x040, &vertOff, 4);
    b.resize(b.size() + 2 * 0x30, 0);

    uint32_t matCount = 1;
    uint32_t matOff = static_cast<uint32_t>(b.size());
    std::memcpy(b.data() + 0x070, &matCount, 4);
    std::memcpy(b.data() + 0x074, &matOff, 4);
    b.resize(b.size() + 4, 0);
    return b;
}

// A .skin file with two submeshes/batches carrying *different*
// skinSectionId values (0 and 401 -- a plausible base-body vs. cloak-geoset
// pairing per Character_Customization's own group numbering) each with one
// degenerate triangle, referencing material 0 -- the minimal shape that
// exercises cmd_export.cpp's new distinct-geoset-count note (FAILURES2.md
// #1) without needing any texture/color/weight combo tables.
std::vector<uint8_t> twoGeosetSkin() {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 2);
    putU32(b, 44);  // vertices: count=2, offset=44 (local slot -> global vtx)
    putU32(b, 6);
    putU32(b, 48);  // indices: count=6, offset=48
    putU32(b, 0);
    putU32(b, 0);  // bones: unread
    putU32(b, 2);
    putU32(b, 60);  // submeshes: count=2, offset=60
    putU32(b, 2);
    putU32(b, 60 + 2 * 0x30);  // batches: count=2, right after submeshes
    REQUIRE(b.size() == 44);

    putU16(b, 0);
    putU16(b, 1);  // vertices[0]=global 0, vertices[1]=global 1
    REQUIRE(b.size() == 48);
    for (int i = 0; i < 3; ++i) putU16(b, 0);  // indices[0..3) = local slot 0
    for (int i = 0; i < 3; ++i) putU16(b, 1);  // indices[3..6) = local slot 1
    REQUIRE(b.size() == 60);

    // Submesh 0: skinSectionId=0 ("base"), covers indices[0..3).
    putU16(b, 0);
    putU16(b, 0);  // skinSectionId, Level
    putU16(b, 0);
    putU16(b, 1);  // vertexStart, vertexCount
    putU16(b, 0);
    putU16(b, 3);  // indexStart, indexCount
    b.resize(b.size() + 0x30 - 12, 0);

    // Submesh 1: skinSectionId=401 ("cloak"-range), covers indices[3..6).
    size_t sm1 = b.size();
    putU16(b, 401);
    putU16(b, 0);
    putU16(b, 1);
    putU16(b, 1);
    putU16(b, 3);
    putU16(b, 3);
    b.resize(sm1 + 0x30, 0);
    REQUIRE(b.size() == 60 + 2 * 0x30);

    // Batch 0 -> submesh 0, batch 1 -> submesh 1, both -> material 0,
    // no textures (textureCount=0 skips the texture-combo lookups
    // entirely). Byte layout per src/skin.cpp's parseBatches: flags(u8,
    // 0x00), priorityPlane(i8, 0x01, unread), shader_id(u16, 0x02, unread),
    // skinSectionIndex(u16, 0x04), geosetIndex(u16, 0x06, unread),
    // colorIndex(u16, 0x08), materialIndex(u16, 0x0A), materialLayer(u16,
    // 0x0C, unread), textureCount(u16, 0x0E), textureComboIndex(u16, 0x10),
    // textureCoordComboIndex(u16, 0x12), textureWeightComboIndex(u16, 0x14),
    // textureTransformComboIndex(u16, 0x16) -- 0x18 bytes total.
    for (uint16_t skinSectionIndex : {uint16_t{0}, uint16_t{1}}) {
        b.push_back(0);  // flags
        b.push_back(0);  // priorityPlane
        putU16(b, 0);    // shader_id
        putU16(b, skinSectionIndex);
        putU16(b, 0);       // geosetIndex
        putU16(b, 0xFFFF);  // colorIndex = none
        putU16(b, 0);       // materialIndex = 0
        putU16(b, 0);       // materialLayer
        putU16(b, 0);       // textureCount = 0
        putU16(b, 0);       // textureComboIndex
        putU16(b, 0);       // textureCoordComboIndex
        putU16(b, 0);       // textureWeightComboIndex
        putU16(b, 0);       // textureTransformComboIndex
    }
    REQUIRE(b.size() == 60 + 2 * 0x30 + 2 * 0x18);

    return b;
}

// Configurable single-batch fixture for the buildMaterialsAndPrimitives
// out-of-range tests below (FINDINGS.md §4.2): one submesh (skinSectionId
// = 0, indexStart = 0, indexCount = submeshIndexCount) and one batch
// built from `fields`. Every earlier check in buildMaterialsAndPrimitives's
// own chain (skinSectionIndex -> submesh index range -> materialIndex ->
// colorIndex -> textureWeightComboIndex[+resolved weightIndex] ->
// textureComboIndex[+resolved textureIndex] -> textureCoordComboIndex)
// must pass for a later one to even be reached, so each test below keeps
// every field before the one under test valid and deliberately breaks
// only that one.
struct BatchFields {
    uint16_t skinSectionIndex = 0;
    uint16_t colorIndex = 0xFFFF;
    uint16_t materialIndex = 0;
    uint16_t textureCount = 0;
    uint16_t textureComboIndex = 0;
    uint16_t textureCoordComboIndex = 0;
    uint16_t textureWeightComboIndex = 0;
    uint16_t textureTransformComboIndex = 0xFFFF;
};

std::vector<uint8_t> oneBatchSkin(const BatchFields& fields, uint16_t submeshIndexCount = 3) {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 1);
    putU32(b, 44);  // vertices: count=1, offset=44
    putU32(b, 3);
    putU32(b, 46);  // indices: count=3, offset=46
    putU32(b, 0);
    putU32(b, 0);  // bones: unread
    putU32(b, 1);
    putU32(b, 52);  // submeshes: count=1, offset=52
    putU32(b, 1);
    putU32(b, 52 + 0x30);  // batches: count=1, right after the submesh
    REQUIRE(b.size() == 44);

    putU16(b, 0);  // vertices[0] = global vertex 0
    REQUIRE(b.size() == 46);
    for (int i = 0; i < 3; ++i) putU16(b, 0);  // indices = [0, 0, 0]
    REQUIRE(b.size() == 52);

    // Submesh 0: skinSectionId=0, claims indexStart=0/indexCount=
    // submeshIndexCount -- normally 3 (matching the 3 real indices
    // above), overridable to something larger to exercise the
    // "corrupted .skin?" range check specifically.
    putU16(b, 0);
    putU16(b, 0);  // skinSectionId, Level
    putU16(b, 0);
    putU16(b, 1);  // vertexStart, vertexCount
    putU16(b, 0);
    putU16(b, submeshIndexCount);  // indexStart, indexCount
    b.resize(b.size() + 0x30 - 12, 0);
    REQUIRE(b.size() == 52 + 0x30);

    b.push_back(0);  // flags
    b.push_back(0);  // priorityPlane
    putU16(b, 0);    // shader_id
    putU16(b, fields.skinSectionIndex);
    putU16(b, 0);  // geosetIndex
    putU16(b, fields.colorIndex);
    putU16(b, fields.materialIndex);
    putU16(b, 0);  // materialLayer
    putU16(b, fields.textureCount);
    putU16(b, fields.textureComboIndex);
    putU16(b, fields.textureCoordComboIndex);
    putU16(b, fields.textureWeightComboIndex);
    putU16(b, fields.textureTransformComboIndex);
    REQUIRE(b.size() == 52 + 0x30 + 0x18);

    return b;
}

// tinyValidM2() (1 vertex) plus zeroed M2Material/M2Color/
// M2TextureWeight/uint16-combo-table records at the given counts --
// content-irrelevant (all zeroed) except combo-table entry 0, when a
// `*Combo0` override is given, which lets a test target the
// "resolved-via-combo" half of a check (weightIndex/textureIndex) rather
// than the combo-table index bound itself.
std::vector<uint8_t> materialsFixtureM2(uint32_t materialCount, uint32_t colorCount,
                                         uint32_t textureWeightCount,
                                         uint32_t textureWeightComboCount, uint32_t textureCount,
                                         uint32_t textureComboCount, uint32_t textureCoordComboCount,
                                         std::optional<uint16_t> textureWeightCombo0 = std::nullopt,
                                         std::optional<uint16_t> textureCombo0 = std::nullopt) {
    auto b = tinyValidM2();

    auto appendArray = [&](size_t headerOff, uint32_t count, size_t recordSize) {
        uint32_t off = static_cast<uint32_t>(b.size());
        std::memcpy(b.data() + headerOff, &count, 4);
        std::memcpy(b.data() + headerOff + 4, &off, 4);
        b.resize(b.size() + count * recordSize, 0);
        return off;
    };

    appendArray(0x070, materialCount, 0x04);                          // materials
    appendArray(0x048, colorCount, 0x28);                              // colors
    appendArray(0x058, textureWeightCount, 0x14);                      // textureWeights
    uint32_t twcOff = appendArray(0x090, textureWeightComboCount, 2);  // textureWeightCombos
    appendArray(0x050, textureCount, 0x10);                            // textures
    uint32_t tcOff = appendArray(0x080, textureComboCount, 2);         // textureCombos
    appendArray(0x088, textureCoordComboCount, 2);                     // textureCoordCombos

    if (textureWeightCombo0 && textureWeightComboCount > 0) {
        uint16_t v = *textureWeightCombo0;
        std::memcpy(b.data() + twcOff, &v, 2);
    }
    if (textureCombo0 && textureComboCount > 0) {
        uint16_t v = *textureCombo0;
        std::memcpy(b.data() + tcOff, &v, 2);
    }

    return b;
}

// Patches materialsFixtureM2's first M2Color record (colors.offset, read
// back from the header rather than recomputed by hand, so this can't
// silently drift from materialsFixtureM2's own layout) so its color track
// is genuinely per-sequence animated -- values outer M2Array count=2, not
// the single-constant-value shape constantTrackValueOffset requires (see
// m2::trackHasAnimatedData's outer.count > 1 case, FINDINGS.md §3.2).
// Content of the two claimed sub-arrays is irrelevant; trackHasAnimatedData
// only inspects outer.count once it's already > 1. Requires colorCount >= 1.
void patchColorTrackAnimated(std::vector<uint8_t>& b) {
    uint32_t colorOff;
    std::memcpy(&colorOff, b.data() + 0x048 + 4, 4);
    uint32_t two = 2;
    uint32_t innerOff = static_cast<uint32_t>(b.size());
    std::memcpy(b.data() + colorOff + 0x0C, &two, 4);
    std::memcpy(b.data() + colorOff + 0x0C + 4, &innerOff, 4);
    b.resize(b.size() + 2 * 8, 0);  // 2 zeroed inner-array descriptors
}

// Same as patchColorTrackAnimated, but for materialsFixtureM2's first
// M2TextureWeight record (a single fixed16 track at record offset 0x00,
// so its values-outer array is at weights.offset + 0x0C). Requires
// textureWeightCount >= 1.
void patchWeightTrackAnimated(std::vector<uint8_t>& b) {
    uint32_t weightOff;
    std::memcpy(&weightOff, b.data() + 0x058 + 4, 4);
    uint32_t two = 2;
    uint32_t innerOff = static_cast<uint32_t>(b.size());
    std::memcpy(b.data() + weightOff + 0x0C, &two, 4);
    std::memcpy(b.data() + weightOff + 0x0C + 4, &innerOff, 4);
    b.resize(b.size() + 2 * 8, 0);
}

// tinyValidM2() (1 vertex) plus 1 M2Material (so a batch's default
// materialIndex=0 resolves) and 1 M2TextureTransform record whose
// translation track is a real, unambiguously constant value (rotation/
// scaling left at their "no data" default) and a 1-entry
// textureTransformCombos table pointing at it -- the FINDINGS.md §3.1
// export-side regression test fixture below. Pair with
// oneBatchSkin({.textureTransformComboIndex = 0}).
std::vector<uint8_t> oneTextureTransformM2() {
    auto b = tinyValidM2();

    uint32_t matOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 0x04, 0);  // M2Material: flags=0, blendMode=0
    uint32_t matCount = 1;
    std::memcpy(b.data() + 0x070, &matCount, 4);
    std::memcpy(b.data() + 0x074, &matOff, 4);

    uint32_t xfOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 0x3C, 0);

    // translation track (xfOff + 0x00): one sub-array, one keyframe --
    // (0.1, 0.2, 0.0), written directly rather than via vec3Bytes (defined
    // later in this file) to avoid a forward-declaration dependency.
    uint32_t innerOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 8, 0);
    uint32_t valueOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 12, 0);
    float tx = 0.1f, ty = 0.2f, tz = 0.0f;
    std::memcpy(b.data() + valueOff + 0, &tx, 4);
    std::memcpy(b.data() + valueOff + 4, &ty, 4);
    std::memcpy(b.data() + valueOff + 8, &tz, 4);
    uint32_t one = 1;
    std::memcpy(b.data() + innerOff + 0, &one, 4);
    std::memcpy(b.data() + innerOff + 4, &valueOff, 4);
    std::memcpy(b.data() + xfOff + 0x0C, &one, 4);
    std::memcpy(b.data() + xfOff + 0x0C + 4, &innerOff, 4);

    uint32_t xfCount = 1;
    std::memcpy(b.data() + 0x060, &xfCount, 4);
    std::memcpy(b.data() + 0x064, &xfOff, 4);

    uint32_t comboOff = static_cast<uint32_t>(b.size());
    putU16(b, 0);  // textureTransformCombos[0] = transform index 0
    uint32_t comboCount = 1;
    std::memcpy(b.data() + 0x098, &comboCount, 4);
    std::memcpy(b.data() + 0x09C, &comboOff, 4);

    return b;
}

// One M2CompBone (88 bytes, see tests/test_m2.cpp for the full field
// layout) with only keyBoneId/parentBone set -- the only fields a bone
// parent-chain test cares about.
std::vector<uint8_t> oneCompBone(int32_t keyBoneId, int16_t parentBone) {
    std::vector<uint8_t> b(0x58, 0);
    std::memcpy(b.data() + 0x00, &keyBoneId, 4);
    std::memcpy(b.data() + 0x08, &parentBone, 2);
    return b;
}

// A .skel file's SKB1 chunk containing exactly the given bones (see
// src/skel.hpp/skel.cpp).
std::vector<uint8_t> buildSkel(const std::vector<std::pair<int32_t, int16_t>>& bones) {
    std::vector<uint8_t> payload;
    putU32(payload, static_cast<uint32_t>(bones.size()));  // bones.count
    putU32(payload, 16);                                   // bones.offset, right after this header
    putU32(payload, 0);                                    // key_bone_lookup.count
    putU32(payload, 0);                                    // key_bone_lookup.offset
    for (const auto& [keyBoneId, parentBone] : bones) {
        auto boneBytes = oneCompBone(keyBoneId, parentBone);
        payload.insert(payload.end(), boneBytes.begin(), boneBytes.end());
    }
    std::vector<uint8_t> b;
    putTag(b, "SKB1");
    putU32(b, static_cast<uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

// Writes an M2Array<T>-shaped (count, offset) pair at the absolute offset
// `off` in `buf`, resizing if needed. Unlike fillTrack's payload (appended
// wherever `buf.size()` currently is), `arrOffset` here is caller-chosen --
// used below to point a track's inner array descriptor at an offset that's
// only meaningful in a *separate* buffer (an external .anim file's own
// blob), proving resolveVec3TrackSequence's externalDataBlob parameter is
// really being used, not coincidentally reading the right bytes from `buf`
// itself.
void putArrayAt(std::vector<uint8_t>& buf, size_t off, uint32_t count, uint32_t arrOffset) {
    if (buf.size() < off + 8) buf.resize(off + 8, 0);
    std::memcpy(buf.data() + off, &count, 4);
    std::memcpy(buf.data() + off + 4, &arrOffset, 4);
}

// Fills in one M2Track<T>'s already-reserved 20-byte slot at `trackOff`
// with a *single* animation sub-array (index 0, matching sequence index 0
// -- the only sequence tinyAnimatedM2 defines), appended at the current end
// of `buf`. `rawValueBytes[i]` is keyframe i's value bytes (e.g. 12 for a
// C3Vector, 8 for an M2CompQuat); `timestampsMs[i]` its timestamp -- same
// length required. Mirrors tests/test_m2.cpp's putFullTrack, just in this
// file's own append-as-you-go style rather than that one's offset-and-
// resize style.
void fillTrack(std::vector<uint8_t>& buf, size_t trackOff, const std::vector<uint32_t>& timestampsMs,
               const std::vector<std::vector<uint8_t>>& rawValueBytes) {
    uint32_t tsOuterOff = static_cast<uint32_t>(buf.size());
    buf.resize(buf.size() + 8, 0);
    uint32_t valOuterOff = static_cast<uint32_t>(buf.size());
    buf.resize(buf.size() + 8, 0);

    uint32_t tsDataOff = static_cast<uint32_t>(buf.size());
    for (uint32_t ts : timestampsMs) putU32(buf, ts);
    uint32_t valDataOff = static_cast<uint32_t>(buf.size());
    for (const auto& v : rawValueBytes) buf.insert(buf.end(), v.begin(), v.end());

    uint32_t n = static_cast<uint32_t>(timestampsMs.size());
    std::memcpy(buf.data() + tsOuterOff, &n, 4);
    std::memcpy(buf.data() + tsOuterOff + 4, &tsDataOff, 4);
    std::memcpy(buf.data() + valOuterOff, &n, 4);
    std::memcpy(buf.data() + valOuterOff + 4, &valDataOff, 4);

    // M2TrackBase's own header: interpolation_type=1 (linear) and
    // global_sequence=0xFFFF ("none" -- see m2::TrackMeta::kNoGlobalSequence).
    // Every track this helper builds is a real per-sequence-indexed one, so
    // it must not look like a global-sequence track (0xFFFF is the "none"
    // sentinel; leaving this zero-filled would make resolveVec3TrackSequence/
    // resolveQuatTrackSequence correctly, but unhelpfully for this fixture's
    // purposes, refuse to resolve it by sequence index at all).
    uint16_t interpType = 1;
    uint16_t noGlobalSeq = 0xFFFF;
    std::memcpy(buf.data() + trackOff + 0x00, &interpType, 2);
    std::memcpy(buf.data() + trackOff + 0x02, &noGlobalSeq, 2);

    uint32_t one = 1;
    std::memcpy(buf.data() + trackOff + 0x04, &one, 4);
    std::memcpy(buf.data() + trackOff + 0x08, &tsOuterOff, 4);
    std::memcpy(buf.data() + trackOff + 0x0C, &one, 4);
    std::memcpy(buf.data() + trackOff + 0x10, &valOuterOff, 4);
}

std::vector<uint8_t> vec3Bytes(float x, float y, float z) {
    std::vector<uint8_t> b(12);
    std::memcpy(b.data() + 0, &x, 4);
    std::memcpy(b.data() + 4, &y, 4);
    std::memcpy(b.data() + 8, &z, 4);
    return b;
}

// Raw M2CompQuat wire bytes for the identity quaternion (0,0,0,1) -- wire
// value (32767,32767,32767,65535), see husk::m2::Quat's doc comment.
std::vector<uint8_t> identityQuatBytes() {
    std::vector<uint8_t> b(8);
    uint16_t vals[4] = {32767, 32767, 32767, 65535};
    std::memcpy(b.data(), vals, 8);
    return b;
}

// tinyValidM2() (1 vertex) plus exactly 1 M2Sequence (id=100,
// flags=0x20 -- "stored inline") and exactly 1 inline bone whose
// translation/rotation/scale tracks all carry real keyframe data for that
// one sequence -- enough to prove `husk export` produces an actual glTF
// animation clip end-to-end (see test_m2.cpp/test_gltf.cpp for the same
// logic tested in isolation, one layer at a time).
std::vector<uint8_t> tinyAnimatedM2() {
    auto b = tinyValidM2();

    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t duration = 1000;
    std::memcpy(b.data() + seqOff + 0x04, &duration, 4);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(b, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    return b;
}

// tinyValidM2() (1 vertex) plus 1 M2Sequence with flags=0 (neither 0x20
// nor 0x40 -- "external .anim file" per wowdev.wiki's Flags table) and 1
// inline bone whose translation track's descriptors (outer + one inner
// M2Array, both still physically inside the M2 blob) claim keyframe data
// at offsets that are only meaningful in a *separate* .anim blob -- proving
// `husk export --anim-dir` really reads the payload cross-blob end to end,
// not by coincidentally finding valid-looking bytes inside the M2 itself.
// Rotation/scale tracks are left with 0 sub-arrays (no data at all, for
// either blob) -- this sequence's translation channel alone is enough to
// prove the wiring works.
std::vector<uint8_t> tinyExternalAnimM2() {
    auto b = tinyValidM2();

    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 200;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    // flags left at 0 -- external, not an alias.

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    size_t trackOff = boneOff + 0x10;  // translation
    uint16_t noGlobalSeq = 0xFFFF;  // see fillTrack's identical comment
    std::memcpy(b.data() + trackOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(b.size());
    b.resize(tsOuterOff + 8, 0);
    putArrayAt(b, tsOuterOff, 1, 0);  // sequence 0: 1 timestamp at *anim-blob* offset 0
    uint32_t valOuterOff = static_cast<uint32_t>(b.size());
    b.resize(valOuterOff + 8, 0);
    putArrayAt(b, valOuterOff, 1, 4);  // sequence 0: 1 C3Vector at *anim-blob* offset 4
    putArrayAt(b, trackOff + 0x04, 1, tsOuterOff);
    putArrayAt(b, trackOff + 0x0C, 1, valOuterOff);
    // rotation (+0x24) / scale (+0x38) tracks stay all-zero: 0 sub-arrays.

    return b;
}

// The .anim file tinyExternalAnimM2()'s translation track descriptor
// actually points into: a single (timestamp=5000ms, C3Vector(9,9,9))
// keyframe, laid out at exactly the offsets tinyExternalAnimM2() claims
// (0 and 4).
std::vector<uint8_t> tinyAnimFile() {
    std::vector<uint8_t> b;
    putU32(b, 5000);            // timestamp, at offset 0
    auto pos = vec3Bytes(9, 9, 9);
    b.insert(b.end(), pos.begin(), pos.end());  // C3Vector, at offset 4
    return b;
}

void appendChunkTo(std::vector<uint8_t>& file, const char* tag, const std::vector<uint8_t>& payload) {
    putTag(file, tag);
    putU32(file, static_cast<uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

// An SKB1 payload (see src/skel.cpp) with exactly one M2CompBone
// (keyBoneId/parentBone = -1, everything else zeroed, tracks left at "0
// sub-arrays" for the caller to fill via fillTrack/putArrayAt -- same
// pattern tinyAnimatedM2/tinyExternalAnimM2 use for an inline M2's bones).
// `boneOff` (out param) is the resulting SKB1-payload-relative offset of
// that bone, i.e. the base fillTrack's trackOff is relative to.
std::vector<uint8_t> buildSkb1PayloadForTracks(size_t* boneOff) {
    std::vector<uint8_t> payload(16, 0);
    putArrayAt(payload, 0x00, 1, 16);  // bones: count=1, offset=16
    // key_bone_lookup (0x08) left at {0, 0} -- unread.
    payload.resize(16 + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(payload.data() + 16 + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(payload.data() + 16 + 0x08, &parentBone, 2);
    if (boneOff) *boneOff = 16;
    return payload;
}

// An SKS1 payload (see src/skel.cpp) with exactly one M2Sequence -- same
// 0x40-byte record shape tests/test_m2.cpp's putSequence uses, transcribed
// fresh here since this file builds fixtures byte-by-byte rather than
// sharing helpers across test binaries.
std::vector<uint8_t> buildSks1Payload(uint16_t seqId, uint32_t seqFlags) {
    std::vector<uint8_t> payload(32, 0);
    // global_loops (0x00) and sequence_lookups (0x10) left at {0, 0}.
    putArrayAt(payload, 0x08, 1, 32);  // sequences: count=1, offset=32
    payload.resize(32 + 0x40, 0);
    std::memcpy(payload.data() + 32 + 0x00, &seqId, 2);
    uint32_t duration = 1000;
    std::memcpy(payload.data() + 32 + 0x04, &duration, 4);
    std::memcpy(payload.data() + 32 + 0x0C, &seqFlags, 4);
    return payload;
}

}  // namespace

TEST_CASE("husk export: an inline bone with a flags&0x20 sequence produces a real glTF "
          "animation, end to end") {
    auto m2Path = tempPath("animated.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("animated.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("animated.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim-dir resolves an external (flags without 0x20/0x40) sequence's "
          "bone keyframes from a real .anim file, via AFID, end to end") {
    auto md20 = tinyExternalAnimM2();

    // Wrap in MD21 + an AFID chunk mapping (animId=200, subAnimId=0) ->
    // fileId=777 -- AFID only exists in the chunked container (wowdev.wiki:
    // "This section only applies to versions >= 7.0.1.20740").
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);  // anim_id
    putU16(file, 0);    // sub_anim_id
    putU32(file, 777);  // file_id

    auto m2Path = tempPath("external-anim.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("external-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("external-anim.glb").string() + " --anim-dir " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: an external sequence with no matching --anim-dir file produces no "
          "animation clip, not an error") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);

    auto m2Path = tempPath("external-anim-missing.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("external-anim-missing.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-dir-empty";
    fs::create_directories(animDir);  // no 777.anim inside

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("external-anim-missing.glb").string() + " --anim-dir " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a sequence without flags&0x20 (external .anim data) produces no "
          "animation clip, even with real inline bone track data") {
    auto m2 = tinyAnimatedM2();
    // Clear the inline-storage flag -- same shape as a genuinely low-
    // priority sequence whose real keyframes live in a .anim file husk
    // doesn't parse the content of; the M2's own inline track data (if
    // any) shouldn't be trusted for a sequence that claims it isn't here.
    uint32_t seqOff = 0;
    std::memcpy(&seqOff, m2.data() + 0x020, 4);
    uint32_t noFlags = 0;
    std::memcpy(m2.data() + seqOff + 0x0C, &noFlags, 4);

    auto m2Path = tempPath("not-inline.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("not-inline.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("not-inline.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a .skel with no SKS1 chunk at all gets no animation clips, not an "
          "error (a .skel isn't required to carry sequences, same as skel::findAnimFileIds "
          "already tolerating a missing AFID)") {
    // tinyValidM2() (not tinyAnimatedM2()) -- no inline bones at all, so
    // bones come entirely from the .skel file below, same as a real
    // Legion+ SKID-linked model. The M2's *own* sequences array is
    // irrelevant here regardless -- a .skel-sourced skeleton's animations
    // come from the .skel's own SKS1/AFID, never the owning M2's.
    auto m2 = tinyValidM2();

    auto m2Path = tempPath("skel-no-sks1.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-no-sks1.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-no-sks1.skel");
    writeFile(skelPath, buildSkel({{-1, -1}}));  // SKB1 only, no SKS1

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("skel-no-sks1.glb").string() + " " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a .skel with an inline (flags&0x20) SKS1 sequence and real SKB1 track "
          "data produces a real glTF animation, end to end") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    fillTrack(skb1Payload, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(skb1Payload, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(skb1Payload, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(300, 0x20));

    auto m2Path = tempPath("skel-inline-anim.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("skel-inline-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-inline-anim.skel");
    writeFile(skelPath, skel);

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("skel-inline-anim.glb").string() + " " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a .skel external (flags without 0x20/0x40) SKS1 sequence resolves via "
          "the .skel's own AFID + --anim-dir, cross-blob (own AFID table, not the owning M2's)") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;  // see fillTrack's identical comment
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);  // seq 0: 1 timestamp at *anim-blob* offset 0
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);  // seq 0: 1 C3Vector at *anim-blob* offset 4
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(400, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 400);  // anim_id
    putU16(afid, 0);    // sub_anim_id
    putU32(afid, 777);  // file_id -- this .skel's own AFID, unrelated to the M2's
    appendChunkTo(skel, "AFID", afid);

    auto m2Path = tempPath("skel-external-anim.m2");
    writeFile(m2Path, tinyValidM2());  // globalFlags=0 -- .anim is flat, matching tinyAnimFile()
    auto skinPath = tempPath("skel-external-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-external-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-anim-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result =
        runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                tempPath("skel-external-anim.glb").string() + " " + skelPath.string() +
                " --anim-dir " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence whose --anim-dir file is AFSB-tagged (not "
          "AFM2) produces no animation clip, not an error -- AFSB has no documented byte "
          "layout husk can parse yet") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);  // tracks left empty -- never reached

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(500, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 500);
    putU16(afid, 0);
    putU32(afid, 888);
    appendChunkTo(skel, "AFID", afid);

    // globalFlags |= 0x200000 -- "chunked .anim files" (wowdev.wiki's
    // flag_unk_0x200000) -- needed for the AFM2-vs-AFSB sniff to run at
    // all (see buildAnimations's doc comment); a flat-.anim model has no
    // AFSB shape to begin with.
    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-afsb-anim.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-afsb-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-afsb-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-afsb-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> afsbFile;
    appendChunkTo(afsbFile, "AFSB", {1, 2, 3, 4});  // content never parsed -- just needs the tag
    writeFile(animDir / "888.anim", afsbFile);

    auto result =
        runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                tempPath("skel-afsb-anim.glb").string() + " " + skelPath.string() +
                " --anim-dir " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence's --anim-dir file with BOTH a small AFM2 "
          "chunk and a trailing AFSB chunk still produces no animation clip, not a bounds-check "
          "crash -- real bloodelffemale_hd .anim files have exactly this shape (a tiny AFM2 "
          "stub alongside the real AFSB data), and using the stub's payload as if it were the "
          "full flat-format content throws a real 'claims more keyframes than this blob holds' "
          "error, so AFSB's mere presence has to override AFM2's") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);  // tracks left empty -- never reached

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(600, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 600);
    putU16(afid, 0);
    putU32(afid, 999);
    appendChunkTo(skel, "AFID", afid);

    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-afm2-stub-afsb-anim.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-afm2-stub-afsb-anim.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-afm2-stub-afsb-anim.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-afm2-stub-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> mixedFile;
    appendChunkTo(mixedFile, "AFM2", {0, 0, 0, 0});  // tiny stub, not real track data
    appendChunkTo(mixedFile, "AFSB", {1, 2, 3, 4});  // the real (unparsed) data
    writeFile(animDir / "999.anim", mixedFile);

    auto result =
        runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                tempPath("skel-afm2-stub-afsb-anim.glb").string() + " " + skelPath.string() +
                " --anim-dir " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk info: directory as path fails cleanly, not a crash (FAILURES.md #1)") {
    auto result = runHusk("info " + fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);
}

TEST_CASE("husk info: chunked file with a truncated trailing chunk header fails cleanly, not a "
          "crash (FAILURES.md #1)") {
    // Valid MD21 wrapper around a minimal MD20 blob, followed by a
    // truncated second chunk header (a tag with no size field) -- used to
    // throw husk::ChunkError straight through info()'s then-too-narrow
    // catch and abort the whole process.
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SKID");
    bytes.push_back(0xAA);  // 1 of 4 size bytes -- truncated

    auto path = tempPath("chunked-truncated.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: generic non-M2 garbage fails cleanly, not a crash (FAILURES.md #1)") {
    // Not MD20, and not a well-formed chunk stream either -- exercises
    // "falls through to 'maybe this is chunked', then runs out of buffer
    // mid-header" from FAILURES.md #1, using nothing more exotic than a
    // wrong magic and zero-filled padding. This is the realistic case:
    // pointing husk at the wrong file entirely, not a hand-crafted one.
    std::vector<uint8_t> bytes;
    putTag(bytes, "XXXX");
    bytes.resize(300, 0);

    auto path = tempPath("garbage.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: couldn't read") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk export: corrupted huge vertex count fails with a real message, not "
          "std::bad_alloc (FAILURES.md #2)") {
    auto m2 = minimalMd20();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x03C, &count, 4);
    std::memcpy(m2.data() + 0x040, &off, 4);
    auto m2Path = tempPath("huge-vertices.m2");
    writeFile(m2Path, m2);

    // m2::parseVertices runs (and, pre-fix, would have OOM'd) before
    // cmd_export.cpp ever opens the .skin file, so this path doesn't need
    // to exist or be valid.
    auto result = runHusk("export " + m2Path.string() + " /nonexistent.skin " +
                           tempPath("huge-vertices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("vertices array claims") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: corrupted huge bone count fails with a real message, not "
          "std::bad_alloc (FAILURES.md #2)") {
    auto m2 = tinyValidM2();
    uint32_t count = 0xFFFFFFF0;
    uint32_t off = 0;
    std::memcpy(m2.data() + 0x02C, &count, 4);
    std::memcpy(m2.data() + 0x030, &off, 4);
    auto m2Path = tempPath("huge-bones.m2");
    writeFile(m2Path, m2);

    auto skinPath = tempPath("huge-bones.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("huge-bones.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("bones array claims") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: .skin file with a corrupted huge indices count fails with a real "
          "message, not std::bad_alloc (FAILURES.md #2)") {
    auto m2Path = tempPath("for-huge-skin-indices.m2");
    writeFile(m2Path, minimalMd20());  // 0 vertices -- never reached, indices fails first

    std::vector<uint8_t> skin;
    putTag(skin, "SKIN");
    putU32(skin, 0);
    putU32(skin, 0);           // vertices: count=0, offset=0
    putU32(skin, 0xFFFFFFF0);  // indices: corrupted huge count
    putU32(skin, 8);
    putU32(skin, 0);
    putU32(skin, 0);  // bones: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // submeshes: count=0, offset=0
    putU32(skin, 0);
    putU32(skin, 0);  // batches: count=0, offset=0
    auto skinPath = tempPath("huge-indices.skin");
    writeFile(skinPath, skin);

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("huge-skin-indices.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(result.output.find("uint16 entries") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a 2-cycle in the bones' parent chain is rejected, not silently exported "
          "(FAILURES.md #3)") {
    auto m2Path = tempPath("cycle.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("cycle.skin");
    writeFile(skinPath, tinyMatchingSkin());
    // Bone 0's parent is bone 1, bone 1's parent is bone 0: every
    // individual parentBone value is in-range and non-self-referential, so
    // only chain-walking (not a plain range check) can catch this.
    auto skelPath = tempPath("cycle.skel");
    writeFile(skelPath, buildSkel({{-1, 1}, {-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("cycle.glb").string() + " " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a bone that is its own parent (a 1-node cycle) is still rejected, "
          "guarded against regressing while fixing the longer-cycle case above") {
    // A self-parent is the degenerate 1-node case of FAILURES.md #3's
    // cycle. It was already caught before this fix, by a dedicated check
    // in gltf::writeGlb -- cmd_export.cpp's new checkNoBoneCycles() (in
    // buildSkeleton(), which runs first) now catches it too and throws
    // its own "loops back on itself" first, so writeGlb's check never
    // gets a chance to fire for this particular caller. That's fine --
    // writeGlb's check is still live for any caller that skips
    // buildSkeleton() -- but it does mean this test should expect the
    // newer message, not the older one.
    auto m2Path = tempPath("self-parent.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("self-parent.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("self-parent.skel");
    writeFile(skelPath, buildSkel({{-1, 0}}));

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("self-parent.glb").string() + " " + skelPath.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("loops back") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk info: prints collision_box/collision_sphere_radius/collision_indices/"
          "collision_face_normals, not just collision_positions (FINDINGS.md §3.3)") {
    auto path = tempPath("collision.m2");
    writeFile(path, tinyValidM2());

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("collision_box: min=") != std::string::npos);
    CHECK(result.output.find("collision_sphere_radius: ") != std::string::npos);
    CHECK(result.output.find("collision_positions: ") != std::string::npos);
    CHECK(result.output.find("collision_indices: ") != std::string::npos);
    CHECK(result.output.find("collision_face_normals: ") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: flags a chunk tag that isn't in husk's known M2 chunk list") {
    // "ZZZZ" stands in for whatever chunk a future client build adds that
    // isn't yet in cmd_info.cpp's documentedM2ChunkTags -- see that file's
    // comment and README.md's Design notes for why this format keeps
    // growing new top-level chunks. The point of this test isn't "ZZZZ"
    // itself, it's proving the diagnostic path actually fires end-to-end
    // through the real CLI, not just at the parser level (see
    // tests/test_m2.cpp's chunkTags tests for that half).
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "ZZZZ");
    putU32(bytes, 4);
    putU32(bytes, 0xDEADBEEF);

    auto path = tempPath("undocumented-chunk.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("ZZZZ") != std::string::npos);
    CHECK(result.output.find("not in husk's known M2 chunk list") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: a real, fully-documented chunk set gets no undocumented-chunk note") {
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "SFID");  // a real, documented tag -- see cmd_info.cpp
    putU32(bytes, 0);

    auto path = tempPath("documented-chunk.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("SFID") != std::string::npos);  // still listed under "chunks:"
    CHECK(result.output.find("not in husk's known M2 chunk list") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk export: non-finite (NaN/Inf) vertex position is rejected, not silently baked "
          "into the glb (FAILURES.md #4)") {
    auto m2 = tinyValidM2();
    size_t vertexOff = m2.size() - 0x30;
    uint32_t nanBits = 0x7FC00000;  // quiet NaN
    uint32_t infBits = 0x7F800000;  // +Infinity
    std::memcpy(m2.data() + vertexOff + 0x00, &nanBits, 4);
    std::memcpy(m2.data() + vertexOff + 0x04, &infBits, 4);
    auto m2Path = tempPath("nan-vertex.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("nan-vertex.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("nan-vertex.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: 'auto' .skin path without --skin-dir defaults to the model's own "
          "directory, and still fails cleanly when the FileDataID-named .skin isn't there") {
    auto m2Path = tempPath("auto-no-skindir.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("auto-no-skindir.glb").string());
    CHECK(result.exitCode == 1);
    // --skin-dir now defaults to the model's own directory (same one
    // tempPath() writes m2Path into) rather than being required -- since no
    // '12345.skin' actually exists there, this fails on the file itself,
    // not on a missing flag.
    CHECK(result.output.find("12345.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: 'auto' on a model with no SFID chunk fails cleanly, naming the reason") {
    auto m2Path = tempPath("auto-no-sfid.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {}));  // no SFID chunk at all

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("auto-no-sfid.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no SFID chunk") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --skin-dir given without 'auto' as the .skin path fails cleanly") {
    auto m2Path = tempPath("skindir-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("skindir-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("skindir-without-auto.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--skin-dir") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: 'auto' + --skin-dir resolves SFID entry 0 (highest-detail LOD) and "
          "exports successfully") {
    auto m2Path = tempPath("auto-resolve.m2");
    uint32_t fileDataId = 555111;
    // Entry 0 (555111) is the one that should get used -- entry 1 (555112)
    // deliberately has no matching file on disk, so this only passes if
    // resolution actually picked entry 0, not "some" entry.
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {fileDataId, 555112}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath = skinDir / (std::to_string(fileDataId) + ".skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto outPath = tempPath("auto-resolve.glb");
    auto result = runHusk("export " + m2Path.string() + " auto " + outPath.string() +
                           " --skin-dir " + skinDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("resolved 'auto'") != std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: 'auto' with --skin-dir pointing at a directory missing the resolved "
          "FileDataID's .skin fails cleanly") {
    auto m2Path = tempPath("auto-missing-file.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {999999999}));

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("auto-missing-file.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("couldn't open") != std::string::npos);
    CHECK(result.output.find("999999999") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod given without 'auto' as the .skin path fails cleanly") {
    auto m2Path = tempPath("lod-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("lod-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("lod-without-auto.glb").string() + " --lod 1");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --lod <n> resolves SFID entry n instead of always 0") {
    auto m2Path = tempPath("lod-n.m2");
    uint32_t entry1FileDataId = 777222;
    // Entry 0 (777111) deliberately has no matching file on disk -- this
    // only passes if --lod 1 actually picked entry 1, not entry 0.
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {777111, entry1FileDataId}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath = skinDir / (std::to_string(entry1FileDataId) + ".skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto outPath = tempPath("lod-n.glb");
    auto result = runHusk("export " + m2Path.string() + " auto " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod 1");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find(std::to_string(entry1FileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: --lod <n> out of range for the SFID chunk fails cleanly, naming the "
          "entry count") {
    auto m2Path = tempPath("lod-oor.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111, 222222}));  // 2 entries

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("lod-oor.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string() + " --lod 5");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("out of range") != std::string::npos);
    CHECK(result.output.find("2") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod given a non-numeric, non-'all' value fails cleanly") {
    auto m2Path = tempPath("lod-nan.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111}));

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("lod-nan.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string() + " --lod banana");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod all resolves every SFID entry and exports one named node per LOD "
          "tier") {
    auto m2Path = tempPath("lod-all.m2");
    uint32_t id0 = 888001, id1 = 888002;
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {id0, id1}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath0 = skinDir / (std::to_string(id0) + ".skin");
    auto skinPath1 = skinDir / (std::to_string(id1) + ".skin");
    writeFile(skinPath0, tinyMatchingSkin());
    writeFile(skinPath1, tinyMatchingSkin());

    auto outPath = tempPath("lod-all.glb");
    auto result = runHusk("export " + m2Path.string() + " auto " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod all");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 LOD tier(s)") != std::string::npos);
    CHECK(result.output.find("lod0") != std::string::npos);
    CHECK(result.output.find("lod1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath0);
    fs::remove(skinPath1);
    fs::remove(outPath);
}

TEST_CASE("husk info: prints skin_file_data_ids/lod_count/bone_file_data_ids/anim_file_ids when "
          "their chunks are present") {
    auto md20 = minimalMd20();
    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());

    putTag(bytes, "SFID");
    putU32(bytes, 8);
    putU32(bytes, 469824);
    putU32(bytes, 469830);

    putTag(bytes, "LDV1");
    putU32(bytes, 4);
    putU16(bytes, 8);  // unk0
    putU16(bytes, 3);  // lodCount

    putTag(bytes, "BFID");
    putU32(bytes, 4);
    putU32(bytes, 777001);

    putTag(bytes, "AFID");
    putU32(bytes, 8);
    putU16(bytes, 120);      // anim_id
    putU16(bytes, 0);        // sub_anim_id
    putU32(bytes, 469839);   // file_id

    auto path = tempPath("full-sidecars.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("skin_file_data_ids: 469824, 469830") != std::string::npos);
    CHECK(result.output.find("lod_count: 3") != std::string::npos);
    CHECK(result.output.find("bone_file_data_ids: 1") != std::string::npos);
    CHECK(result.output.find("anim_file_ids: 1") != std::string::npos);

    fs::remove(path);
}

// Regression tests for FAILURES2.md #3: parseBones/parseSequences/
// parseRibbons' fixed record strides are only documented/verified for
// Wrath+ (version 264), but nothing warned when a file below that version
// went through them anyway -- expansionForVersion happily recognizes and
// labels Classic (256-257)/TBC (260-263) already, so the information needed
// to catch this was already on hand and simply unused.
TEST_CASE("husk info: a version below Wrath (264) prints a loud warning") {
    auto path = tempPath("pre-wrath.m2");
    writeFile(path, minimalMd20(/*version=*/260));  // The Burning Crusade

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") != std::string::npos);
    CHECK(result.output.find("260") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: a Wrath+ version prints no such warning") {
    auto path = tempPath("post-wrath.m2");
    writeFile(path, minimalMd20(/*version=*/274));

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") == std::string::npos);

    fs::remove(path);
}

// Regression test for FAILURES2.md #4: `husk info` used to print only the
// raw `textures`/`materials` array *counts* (parseTextures/parseMaterials
// were never called at all in cmd_info.cpp), unlike attachments/events/
// lights/ribbons, which all get per-record detail -- and Header::
// textureFileDataIds (the TXID chunk, already resolved and used internally
// by `husk export --textures`) was never printed anywhere in `info`'s
// output at all, unlike every other sidecar FileDataID list.
TEST_CASE("husk info: prints per-texture type/flags/filename, per-material flags/blend_mode, and "
          "texture_file_data_ids when TXID is present") {
    auto md20 = minimalMd20();

    // textures[0]: type=0 (real embedded filename), flags=0, name "foo.blp".
    // textures[1]: type=1 (runtime-substituted -- filename shouldn't print).
    uint32_t nameOff = static_cast<uint32_t>(md20.size());
    std::string name = "foo.blp";
    md20.insert(md20.end(), name.begin(), name.end());
    md20.push_back(0);  // trailing NUL, trimmed by readName

    uint32_t texOff = static_cast<uint32_t>(md20.size());
    putU32(md20, 0);                                     // textures[0].type
    putU32(md20, 0);                                      // textures[0].flags
    putU32(md20, static_cast<uint32_t>(name.size() + 1));  // textures[0].filename.count
    putU32(md20, nameOff);                                 // textures[0].filename.offset
    putU32(md20, 1);                                       // textures[1].type
    putU32(md20, 0);                                       // textures[1].flags
    putU32(md20, 0);                                       // textures[1].filename.count
    putU32(md20, 0);                                       // textures[1].filename.offset
    uint32_t two = 2;
    std::memcpy(md20.data() + 0x050, &two, 4);   // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset

    // materials[0]: flags=0x04 (two-sided), blendMode=2 (Alpha).
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0x04);
    putU16(md20, 2);
    uint32_t one = 1;
    std::memcpy(md20.data() + 0x070, &one, 4);
    std::memcpy(md20.data() + 0x074, &matOff, 4);

    std::vector<uint8_t> bytes;
    putTag(bytes, "MD21");
    putU32(bytes, static_cast<uint32_t>(md20.size()));
    bytes.insert(bytes.end(), md20.begin(), md20.end());
    putTag(bytes, "TXID");
    putU32(bytes, 8);
    putU32(bytes, 1034713);  // textures[0]'s FileDataID
    putU32(bytes, 0);        // textures[1]: not file-based (type != 0)

    auto path = tempPath("textures-materials.m2");
    writeFile(path, bytes);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("texture 0: type=0 flags=0x0 filename=foo.blp file_data_id=1034713") !=
          std::string::npos);
    // texture 1 has type != 0 -- filename must NOT print (it's a runtime
    // substitution slot, not a real path -- see m2::Texture's doc comment),
    // and file_data_id must not print either (its TXID entry is 0, "none").
    CHECK(result.output.find("texture 1: type=1 flags=0x0") != std::string::npos);
    CHECK(result.output.find("texture 1: type=1 flags=0x0 filename") == std::string::npos);
    CHECK(result.output.find("texture 1: type=1 flags=0x0 file_data_id") == std::string::npos);
    CHECK(result.output.find("material 0: flags=0x4 blend_mode=2") != std::string::npos);
    CHECK(result.output.find("texture_file_data_ids: 1034713, 0") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: prints attachments/events/lights/cameras/ribbon_emitters/particle_emitters "
          "counts") {
    auto md20 = minimalMd20();
    auto path = tempPath("counts-only.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attachments: 0") != std::string::npos);
    CHECK(result.output.find("events: 0") != std::string::npos);
    CHECK(result.output.find("lights: 0") != std::string::npos);
    CHECK(result.output.find("cameras: 0") != std::string::npos);
    CHECK(result.output.find("ribbon_emitters: 0") != std::string::npos);
    CHECK(result.output.find("particle_emitters: 0") != std::string::npos);

    fs::remove(path);
}

// CLI default-resolution tests: `husk export <file.m2>` alone (no .skin,
// output, .skel, or --textures/--skin-dir/--anim-dir) should resolve
// everything it reasonably can from what's already sitting next to the
// model, rather than requiring every argument spelled out even when it's
// exactly where the tool could have found it itself. Each fixture below
// gets its own dedicated subdirectory (not the shared system temp root
// tempPath() otherwise writes into) so directory-scan-based defaulting has
// a clean, isolated view -- no risk of an unrelated file from a different
// test case being picked up.

fs::path defaultsDir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("husk-cli-test-defaults-" + name);
    fs::create_directories(dir);
    return dir;
}

// Regression test for FAILURES2.md #3 (export side -- see the two `husk
// info` versions of this test above for the info-command side).
TEST_CASE("husk export: a version below Wrath (264) prints a loud warning") {
    auto dir = defaultsDir("prewrathexport");
    auto md20 = minimalMd20(/*version=*/256);  // Classic
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x03C, &count, 4);
    std::memcpy(md20.data() + 0x040, &off, 4);
    md20.resize(md20.size() + 0x30, 0);
    writeFile(dir / "classic.m2", md20);
    writeFile(dir / "classic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "classic.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Wrath") != std::string::npos);
    CHECK(result.output.find("256") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: model path alone resolves a same-basename .skin and defaults the output "
          "path, end to end") {
    auto dir = defaultsDir("basic");
    writeFile(dir / "basic.m2", tinyValidM2());
    writeFile(dir / "basic00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "basic.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no .skin path given") != std::string::npos);
    CHECK(result.output.find("no output path given") != std::string::npos);
    CHECK(fs::exists(dir / "basic.glb"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: default .skin resolution never matches a different model's file that "
          "merely extends this one's name as a string (the _hd-variant trap)") {
    auto dir = defaultsDir("hdtrap");
    writeFile(dir / "hero.m2", tinyValidM2());
    writeFile(dir / "hero00.skin", tinyMatchingSkin());
    // A real, unrelated, much-higher-poly model -- "hero_hd" starts with
    // "hero" as a plain string, but the character right after the basename
    // is '_', not a digit, so it must never be picked for hero.m2.
    writeFile(dir / "hero_hd00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "hero.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("hero00.skin") != std::string::npos);
    CHECK(result.output.find("hero_hd00.skin") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: multiple same-basename .skin candidates resolves to the lowest-numbered "
          "one, and says so") {
    auto dir = defaultsDir("ambiguous");
    writeFile(dir / "multi.m2", tinyValidM2());
    writeFile(dir / "multi01.skin", tinyMatchingSkin());
    writeFile(dir / "multi00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "multi.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("multi00.skin") != std::string::npos);
    CHECK(result.output.find("2 same-basename .skin files") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: no .skin path given and none found next to the model fails cleanly, "
          "naming what was expected") {
    auto dir = defaultsDir("noskin");
    writeFile(dir / "lonely.m2", tinyValidM2());

    auto result = runHusk("export " + (dir / "lonely.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no same-named") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a 0-inline-bone model with a same-basename .skel next to it resolves the "
          "skeleton automatically, end to end") {
    auto dir = defaultsDir("skel");
    writeFile(dir / "rigged.m2", tinyValidM2());  // inline bones empty
    writeFile(dir / "rigged00.skin", tinyMatchingSkin());
    writeFile(dir / "rigged.skel", buildSkel({{-1, -1}}));  // one root bone

    auto result = runHusk("export " + (dir / "rigged.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("found and using") != std::string::npos);
    CHECK(result.output.find("rigged.skel") != std::string::npos);
    CHECK(result.output.find("1 bones") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures defaults to the model's own directory -- a FileDataID-named "
          "file already sitting there is embedded without passing the flag") {
    auto dir = defaultsDir("textures");
    auto md20 = tinyValidM2();
    // One M2Texture (type=0, i.e. hardcoded/filename-based -- fine, only
    // the TXID-resolved FileDataID matters for embedding) plus a TXID chunk
    // giving it FileDataID 555, and one .skin batch referencing material 0
    // (itself referencing texture 0 via a trivial 1-entry combo table) --
    // the minimum shape buildMaterialsAndPrimitives needs to try resolving
    // an actual image.
    uint32_t one = 1;
    uint32_t texOff = static_cast<uint32_t>(md20.size());
    md20.resize(texOff + 16, 0);  // M2Texture: type/flags/filename(M2Array<char>)
    std::memcpy(md20.data() + 0x050, &one, 4);    // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    md20.resize(matOff + 4, 0);  // M2Material: flags(u16)=0, blendMode(u16)=0
    std::memcpy(md20.data() + 0x070, &one, 4);    // materials.count
    std::memcpy(md20.data() + 0x074, &matOff, 4);  // materials.offset
    uint32_t comboOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0);  // textureCombos[0] = texture index 0
    std::memcpy(md20.data() + 0x080, &one, 4);      // textureCombos.count
    std::memcpy(md20.data() + 0x084, &comboOff, 4);  // textureCombos.offset

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "TXID");
    putU32(file, 4);
    putU32(file, 555);  // texture 0's FileDataID

    // .skin with one submesh + one batch (materialIndex=0,
    // textureComboIndex=0) -- header offsets/sizes per src/skin.cpp
    // (vertices=0x04, indices=0x0C, bones=0x14, submeshes=0x1C,
    // batches=0x24; kSubmeshSize=0x30, kBatchSize=0x18). Built by appending
    // each section and patching its header descriptor from the running
    // size, rather than hand-computed offsets, so a struct-size mistake
    // shows up as a wrong read immediately instead of silently compiling.
    std::vector<uint8_t> skin;
    putTag(skin, "SKIN");
    skin.resize(44, 0);  // 5 M2Array descriptors, patched below
    auto patchArray = [&](size_t off, uint32_t count, uint32_t offset) {
        std::memcpy(skin.data() + off, &count, 4);
        std::memcpy(skin.data() + off + 4, &offset, 4);
    };

    uint32_t submeshOff = static_cast<uint32_t>(skin.size());
    skin.resize(skin.size() + 0x30, 0);
    uint16_t indexStart = 0, indexCount = 3;
    std::memcpy(skin.data() + submeshOff + 0x08, &indexStart, 2);
    std::memcpy(skin.data() + submeshOff + 0x0A, &indexCount, 2);
    patchArray(0x1C, 1, submeshOff);

    uint32_t batchOff = static_cast<uint32_t>(skin.size());
    skin.resize(skin.size() + 0x18, 0);
    uint16_t zero16 = 0;
    uint16_t one16 = 1;
    uint16_t noColor = 0xFFFF;  // colorIndex's "none" sentinel (skin.hpp's Batch::colorIndex)
    std::memcpy(skin.data() + batchOff + 0x04, &zero16, 2);   // skinSectionIndex
    std::memcpy(skin.data() + batchOff + 0x08, &noColor, 2);  // colorIndex: none
    std::memcpy(skin.data() + batchOff + 0x0A, &zero16, 2);   // materialIndex
    std::memcpy(skin.data() + batchOff + 0x0E, &one16, 2);    // textureCount: 1 (gates resolution)
    std::memcpy(skin.data() + batchOff + 0x10, &zero16, 2);   // textureComboIndex
    patchArray(0x24, 1, batchOff);

    // vertices lookup table: 1 entry -> global vertex 0.
    uint32_t vertOff = static_cast<uint32_t>(skin.size());
    putU16(skin, 0);
    patchArray(0x04, 1, vertOff);

    // indices: 3 entries, all pointing at vertices-lookup slot 0 (a
    // degenerate triangle onto the model's one vertex -- same as
    // tinyMatchingSkin's own shape).
    uint32_t idxOff = static_cast<uint32_t>(skin.size());
    putU16(skin, 0);
    putU16(skin, 0);
    putU16(skin, 0);
    patchArray(0x0C, 3, idxOff);

    patchArray(0x14, 0, 0);  // bones: unread, left empty

    auto dirModel = dir / "textured.m2";
    writeFile(dirModel, file);
    writeFile(dir / "textured00.skin", skin);
    writeFile(dir / "555.png", {1, 2, 3, 4});  // fake PNG bytes -- husk embeds raw, doesn't decode

    auto result = runHusk("export " + dirModel.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --anim-dir defaults to the model's own directory -- an external "
          "sequence's .anim file already sitting there resolves without passing the flag") {
    auto dir = defaultsDir("animdir");
    auto md20 = tinyExternalAnimM2();

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);  // anim_id, matches tinyExternalAnimM2's sequence id
    putU16(file, 0);    // sub_anim_id
    putU32(file, 999);  // file_id

    writeFile(dir / "extanim.m2", file);
    writeFile(dir / "extanim00.skin", tinyMatchingSkin());
    writeFile(dir / "999.anim", tinyAnimFile());

    auto result = runHusk("export " + (dir / "extanim.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// A tinyValidM2() (1 vertex) with one material, one texture (type=0, empty
// filename), and a 1-entry textureCombos table pointing at it -- the
// minimum an M2 needs for a batch's textureComboIndex to resolve at all.
// Used by the FAILURES2.md #6 (multi-texture batch) regression test below.
std::vector<uint8_t> oneTextureOneMaterialM2() {
    auto b = tinyValidM2();

    uint32_t texOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 16, 0);  // M2Texture: type=0, flags=0, filename={0,0}
    uint32_t one = 1;
    std::memcpy(b.data() + 0x050, &one, 4);
    std::memcpy(b.data() + 0x054, &texOff, 4);

    uint32_t matOff = static_cast<uint32_t>(b.size());
    b.resize(b.size() + 4, 0);  // M2Material: flags=0, blendMode=0
    std::memcpy(b.data() + 0x070, &one, 4);
    std::memcpy(b.data() + 0x074, &matOff, 4);

    uint32_t comboOff = static_cast<uint32_t>(b.size());
    putU16(b, 0);  // textureCombos[0] = texture index 0
    std::memcpy(b.data() + 0x080, &one, 4);
    std::memcpy(b.data() + 0x084, &comboOff, 4);
    return b;
}

// A .skin with one submesh/batch whose textureCount is 2 -- per
// wowdev.wiki M2/.skin#Texture_units, a real second texture layer (e.g. an
// env-mapped "shine" pass), which husk only ever resolves the first of
// (FAILURES2.md #6). Pairs with oneTextureOneMaterialM2().
std::vector<uint8_t> oneBatchTwoTexturesSkin() {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 1);
    putU32(b, 44);  // vertices: count=1, offset=44
    putU32(b, 3);
    putU32(b, 46);  // indices: count=3, offset=46
    putU32(b, 0);
    putU32(b, 0);  // bones: unread
    putU32(b, 1);
    putU32(b, 52);  // submeshes: count=1, offset=52
    putU32(b, 1);
    putU32(b, 52 + 0x30);  // batches: count=1, right after submeshes
    REQUIRE(b.size() == 44);

    putU16(b, 0);  // vertices[0] = global vertex 0
    REQUIRE(b.size() == 46);
    for (int i = 0; i < 3; ++i) putU16(b, 0);  // indices = [0, 0, 0]
    REQUIRE(b.size() == 52);

    putU16(b, 0);
    putU16(b, 0);  // skinSectionId, Level
    putU16(b, 0);
    putU16(b, 1);  // vertexStart, vertexCount
    putU16(b, 0);
    putU16(b, 3);  // indexStart, indexCount
    b.resize(52 + 0x30, 0);
    REQUIRE(b.size() == 52 + 0x30);

    b.push_back(0);  // flags
    b.push_back(0);  // priorityPlane
    putU16(b, 0);    // shader_id
    putU16(b, 0);    // skinSectionIndex
    putU16(b, 0);    // geosetIndex
    putU16(b, 0xFFFF);  // colorIndex = none
    putU16(b, 0);        // materialIndex = 0
    putU16(b, 0);        // materialLayer
    putU16(b, 2);        // textureCount = 2 -- the field under test
    putU16(b, 0);        // textureComboIndex = 0
    putU16(b, 0);        // textureCoordComboIndex
    putU16(b, 0);        // textureWeightComboIndex
    putU16(b, 0);        // textureTransformComboIndex
    REQUIRE(b.size() == 52 + 0x30 + 0x18);

    return b;
}

// Regression tests for FAILURES2.md #9: FAILURES.md #4 fixed non-finite
// (NaN/Inf) vertex positions/normals, but the identical exposure existed,
// unfixed, for animation keyframe data -- neither the resolved value nor
// the timestamp ordering was ever checked before this fix. Both fixtures
// below are otherwise identical to tinyAnimatedM2() (see its own doc
// comment), just with the translation track's second keyframe corrupted one
// way at a time.
TEST_CASE("husk export: a non-finite (NaN) translation keyframe value fails with a real message, "
          "not a silently-invalid .glb") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    float nan = std::numeric_limits<float>::quiet_NaN();
    fillTrack(b, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(nan, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("nan-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("nan-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("nan-keyframe.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite (NaN/Inf) value") != std::string::npos);
    CHECK(result.output.find("bone 0's translation keyframe 1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a non-monotonic (out-of-order) translation keyframe timestamp fails with "
          "a real message") {
    auto b = tinyValidM2();
    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = 1;
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40, 0);
    uint16_t seqId = 100;
    std::memcpy(b.data() + seqOff + 0x00, &seqId, 2);
    uint32_t seqFlags = 0x20;
    std::memcpy(b.data() + seqOff + 0x0C, &seqFlags, 4);

    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);

    // Keyframe 1's timestamp (500) is *before* keyframe 0's (1000) -- a
    // corrupted/truncated read, not a valid ascending keyframe sequence.
    fillTrack(b, boneOff + 0x10, {1000, 500}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    fillTrack(b, boneOff + 0x24, {0, 1000}, {identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("nonmonotonic-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("nonmonotonic-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " " + skinPath.string() + " " +
                           tempPath("nonmonotonic-keyframe.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("isn't strictly greater than") != std::string::npos);
    CHECK(result.output.find("bone 0's translation keyframe 1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// The ordinary (finite, ascending) case -- tinyAnimatedM2() itself -- must
// keep working; already covered by other tests in this file (e.g. "husk
// export: end-to-end animated model produces a real glTF animation clip"),
// not repeated here.

// Regression test for FAILURES2.md #1: a .skin file whose submeshes carry
// different skinSectionId ("geoset ID") values -- the normal shape for a
// real character model bundling multiple selectable hairstyles/gear geosets
// in one file -- used to be exported completely unfiltered with zero
// indication anything unusual happened. husk still doesn't filter geosets
// (that's a separate, bigger feature), but it must now say so loudly.
TEST_CASE("husk export: batches spanning more than one distinct skinSectionId (geoset ID) print a "
          "loud note naming them") {
    auto dir = defaultsDir("geosets");
    writeFile(dir / "geoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "geoset00.skin", twoGeosetSkin());

    auto result = runHusk("export " + (dir / "geoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 distinct geoset IDs") != std::string::npos);
    CHECK(result.output.find("skinSectionId: 0, 401") != std::string::npos);
    CHECK(result.output.find("doesn't filter geosets yet") != std::string::npos);

    fs::remove_all(dir);
}

// The same-skinSectionId case (the ordinary shape -- every submesh really is
// part of one consistent mesh) must stay quiet: no note at all.
TEST_CASE("husk export: batches that all share one skinSectionId print no geoset note") {
    auto dir = defaultsDir("samegeoset");
    auto skin = twoGeosetSkin();
    // Patch submesh 1's skinSectionId (offset 60 + 0x30 + 0x00) from 401 to
    // 0, matching submesh 0 -- same fixture, single-geoset shape.
    uint16_t zero16 = 0;
    std::memcpy(skin.data() + 60 + 0x30 + 0x00, &zero16, 2);
    writeFile(dir / "onegeoset.m2", twoVertexOneMaterialM2());
    writeFile(dir / "onegeoset00.skin", skin);

    auto result = runHusk("export " + (dir / "onegeoset.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("distinct geoset IDs") == std::string::npos);

    fs::remove_all(dir);
}

// Regression test for FAILURES2.md #6: a batch with textureCount > 1 (a
// real second texture layer, e.g. an env-mapped "shine" pass) used to be
// silently reduced to a single texture with zero indication anything was
// dropped.
TEST_CASE("husk export: a batch with textureCount > 1 prints a note that extra texture layers "
          "are dropped") {
    auto dir = defaultsDir("multitex");
    writeFile(dir / "shiny.m2", oneTextureOneMaterialM2());
    writeFile(dir / "shiny00.skin", oneBatchTwoTexturesSkin());

    auto result = runHusk("export " + (dir / "shiny.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with more than one texture") != std::string::npos);
    CHECK(result.output.find("FAILURES2.md #6") != std::string::npos);

    fs::remove_all(dir);
}

// The ordinary (single-texture) case must stay quiet.
TEST_CASE("husk export: a batch with textureCount == 1 prints no multi-texture note") {
    auto dir = defaultsDir("singletex");
    writeFile(dir / "plain.m2", oneTextureOneMaterialM2());
    auto skin = oneBatchTwoTexturesSkin();
    uint16_t one16 = 1;
    // textureCount lives at submesh-block-end + batch offset 0x0E (see
    // oneBatchTwoTexturesSkin's own layout comment: 52 + 0x30 is the batch's
    // start).
    std::memcpy(skin.data() + 52 + 0x30 + 0x0E, &one16, 2);
    writeFile(dir / "plain00.skin", skin);

    auto result = runHusk("export " + (dir / "plain.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("more than one texture") == std::string::npos);

    fs::remove_all(dir);
}

// Regression test for FAILURES2.md #7: a bone track whose global_sequence
// field is set (continuous, M2Sequence-independent looping animation --
// glow pulses, idle sway) correctly refuses to misattribute its keyframes
// to whichever M2Sequence happens to occupy outer-array position 0 (fixed
// separately, see TODO_correctness.md item 1 and m2::TrackMeta's doc
// comment) -- but used to resolve to *no* animation at all as a result, not
// a real global-sequence clip. `m2::resolveVec3GlobalSequenceTrack`/
// `buildGlobalSequenceAnimations` (src/m2.cpp, src/cmd_export.cpp) fix that:
// this checks it end to end through the real CLI, not just the underlying
// parser (see tests/test_m2.cpp for that).
TEST_CASE("husk export: a global-sequence-driven bone track resolves to a real animation clip") {
    auto m2 = tinyValidM2();
    uint32_t boneOff = static_cast<uint32_t>(m2.size());
    uint32_t boneCount = 1;
    std::memcpy(m2.data() + 0x02C, &boneCount, 4);
    std::memcpy(m2.data() + 0x030, &boneOff, 4);
    m2.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(m2.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(m2.data() + boneOff + 0x08, &parentBone, 2);

    fillTrack(m2, boneOff + 0x10, {0, 1000}, {vec3Bytes(0, 0, 0), vec3Bytes(1, 2, 3)});
    // fillTrack always writes global_sequence = 0xFFFF ("none") at the
    // track's own header -- patch it to a real global-sequence index (3),
    // marking this specific track as global-sequence-driven instead of
    // per-M2Sequence (see fillTrack's own doc comment).
    uint16_t globalSeq = 3;
    std::memcpy(m2.data() + boneOff + 0x10 + 0x02, &globalSeq, 2);
    // Rotation/scale get a trivial single keyframe each, left "none" (
    // fillTrack's default) -- isolates this test to exactly the translation
    // track actually under test.
    fillTrack(m2, boneOff + 0x24, {0}, {identityQuatBytes()});
    fillTrack(m2, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto dir = defaultsDir("globalseq");
    writeFile(dir / "glow.m2", m2);
    writeFile(dir / "glow00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "glow.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// The ordinary case (tinyAnimatedM2's per-M2Sequence-only bone -- fillTrack's
// default global_sequence is "none") must not gain a phantom extra clip.
TEST_CASE("husk export: a model with no global-sequence-driven tracks gains no extra clip") {
    auto dir = defaultsDir("noglobalseq");
    writeFile(dir / "normal.m2", tinyAnimatedM2());
    writeFile(dir / "normal00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "normal.m2").string());
    CHECK(result.exitCode == 0);
    // tinyAnimatedM2 resolves to exactly 1 real (per-sequence) clip already
    // (see its own doc comment/other tests using it) -- must stay exactly 1,
    // not 2, confirming no spurious global-sequence clip appears when no
    // track is actually global-sequence-driven.
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove_all(dir);
}

// Regression coverage for a real bug: `--help`/`-h` used to only be
// special-cased in main.cpp, before a subcommand name was even read --
// `husk export --help` fell through to treating "--help" as a literal
// model path (args[0] is always the first positional in exportGlb, taken
// unconditionally), producing a confusing "couldn't open '--help'" error
// instead of the usage text each cmd_*.cpp already had. Every subcommand
// now checks isHelpFlag (commands.hpp) before doing any real argument
// parsing.
TEST_CASE("husk export --help prints usage and exits 0, not a file-not-found error") {
    auto result = runHusk("export --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk export -h (shorthand) prints usage and exits 0") {
    auto result = runHusk("export -h");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export <file.m2> --help (help after a positional) still prints usage, "
          "without ever trying to open the (nonexistent) model path") {
    auto result = runHusk("export nonexistent-model.m2 --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk info --help prints usage and exits 0, not a file-not-found error") {
    auto result = runHusk("info --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk dump-chunks --help prints usage and exits 0, not a file-not-found error") {
    auto result = runHusk("dump-chunks --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk --help prints the top-level command list and exits 0") {
    auto result = runHusk("--help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("usage: husk <command>") != std::string::npos);
    CHECK(result.output.find("export") != std::string::npos);
    CHECK(result.output.find("dump-chunks") != std::string::npos);
}

TEST_CASE("husk --version prints a non-empty version string and exits 0 (FINDINGS.md §2.3)") {
    // HUSK_VERSION is baked in at CMake configure time (a git describe,
    // see CMakeLists.txt) and varies build to build (e.g. a "-dirty"
    // suffix) -- this only checks the command works and says *something*
    // real, not any specific string.
    auto result = runHusk("--version");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk ") != std::string::npos);
    CHECK(result.output.find("husk unknown") == std::string::npos);
}

TEST_CASE("husk -V (shorthand) prints the same thing as --version") {
    auto result = runHusk("-V");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk ") != std::string::npos);
}

TEST_CASE("husk with no command prints usage and exits 1") {
    auto result = runHusk("");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk <command>") != std::string::npos);
}

TEST_CASE("husk with an unknown command fails cleanly, not a crash") {
    auto result = runHusk("frobnicate");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("unknown command") != std::string::npos);
}

// Remaining CLI argv edge cases (FINDINGS.md §4.3): each subcommand's
// argc guard, and export's four "flag given with no value" branches.
// None of these need a real M2 -- they're all rejected before any file is
// ever opened.

TEST_CASE("husk export with no arguments at all prints usage and exits 1") {
    auto result = runHusk("export");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export --textures with no value prints usage and exits 1") {
    auto result = runHusk("export some.m2 --textures");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export --skin-dir with no value prints usage and exits 1") {
    auto result = runHusk("export some.m2 --skin-dir");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export --anim-dir with no value prints usage and exits 1") {
    auto result = runHusk("export some.m2 --anim-dir");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export --lod with no value prints usage and exits 1") {
    auto result = runHusk("export some.m2 --lod");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk export with more than 4 positionals (model + 3 trailing-optional) prints usage "
          "and exits 1") {
    auto result = runHusk("export some.m2 a.skin out.glb some.skel one-too-many");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk export") != std::string::npos);
}

TEST_CASE("husk info with no arguments at all prints usage and exits 1") {
    auto result = runHusk("info");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
}

TEST_CASE("husk info with more than one argument prints usage and exits 1") {
    auto result = runHusk("info a.m2 b.m2");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk info") != std::string::npos);
}

TEST_CASE("husk dump-chunks with no arguments at all prints usage and exits 1") {
    auto result = runHusk("dump-chunks");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
}

TEST_CASE("husk dump-chunks with more than one argument prints usage and exits 1") {
    auto result = runHusk("dump-chunks a.m2 b.m2");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("usage: husk dump-chunks") != std::string::npos);
}

// Adversarial/out-of-range coverage for buildMaterialsAndPrimitives
// (cmd_export.cpp, FINDINGS.md §4.2): six real, well-written bounds
// checks chaining batch -> submesh -> material -> color/textureWeight/
// texture/textureCoord, previously exercised only with in-range synthetic
// fixtures. A real mismatched .skin/.m2 pairing hits exactly these paths.

TEST_CASE("husk export: batch skinSectionIndex out of range for submeshes fails cleanly") {
    auto dir = defaultsDir("badskinsection");
    writeFile(dir / "m.m2", tinyValidM2());
    writeFile(dir / "m.skin", oneBatchSkin({.skinSectionIndex = 1}));  // only submesh 0 exists

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("skinSectionIndex (1) is out of range for 1 submeshes") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: submesh index range past the resolved triangle-index buffer fails "
          "cleanly (corrupted .skin?)") {
    auto dir = defaultsDir("badindexrange");
    writeFile(dir / "m.m2", tinyValidM2());
    // Submesh claims 10 indices; the .skin's own indices array (and thus
    // the resolved triangle-index buffer) only has 3.
    writeFile(dir / "m.skin", oneBatchSkin({}, /*submeshIndexCount=*/10));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("corrupted .skin?") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch materialIndex out of range for materials fails cleanly") {
    auto dir = defaultsDir("badmaterial");
    writeFile(dir / "m.m2", tinyValidM2());       // 0 materials
    writeFile(dir / "m.skin", oneBatchSkin({}));  // materialIndex defaults to 0

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("materialIndex (0) is out of range for 0 materials") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch colorIndex out of range for colors fails cleanly") {
    auto dir = defaultsDir("badcolor");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 1 material, 0 colors
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("colorIndex (0) is out of range for 0 colors") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureWeightComboIndex out of range for textureWeightCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badweightcombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0));  // 1 textureWeightCombos entry
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureWeightComboIndex (5) is out of range for 1 "
                              "textureWeightCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture weight, resolved via textureWeightCombos, out of range "
          "for textureWeights fails cleanly") {
    auto dir = defaultsDir("badweightresolved");
    // textureWeightCombos[0] = 99, but there are 0 real textureWeights.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0, /*textureWeightCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture weight (index 99 via textureWeightCombos[0]) is out of "
                              "range for 0 textureWeights entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureComboIndex out of range for textureCombos fails cleanly") {
    auto dir = defaultsDir("badtexturecombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 0 textureCombos
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureComboIndex (0) is out of range for 0 textureCombos "
                              "entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch texture, resolved via textureCombos, out of range for textures "
          "fails cleanly") {
    auto dir = defaultsDir("badtextureresolved");
    // textureCombos[0] = 99, but there are 0 real textures.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 0, 1, 0, std::nullopt, /*textureCombo0=*/99));
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture (index 99 via textureCombos[0]) is out of range for 0 "
                              "textures") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureCoordComboIndex out of range for textureCoordCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badtexcoordcombo");
    // 1 real texture, textureCombos[0]=0 (valid, points at it), but only
    // 1 textureCoordCombos entry even though the batch claims index 5.
    writeFile(dir / "m.m2",
              materialsFixtureM2(1, 0, 0, 0, 1, 1, 1, std::nullopt, /*textureCombo0=*/0));
    writeFile(dir / "m.skin",
              oneBatchSkin({.textureCount = 1, .textureComboIndex = 0, .textureCoordComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("textureCoordComboIndex (5) is out of range for 1 "
                              "textureCoordCombos entries") != std::string::npos);

    fs::remove_all(dir);
}

// FINDINGS.md §3.2: a batch's M2Color/M2TextureWeight can be genuinely
// animated (per-sequence or global-sequence keyframes), the same track
// shape a bone's translation/rotation/scale can be -- but unlike a bone
// track (a real, animatable glTF node property, see FAILURES2.md #7),
// core glTF has no way to animate a material's baseColorFactor at all, so
// there's no real clip to build. These tests confirm husk says so instead
// of silently exporting the batch as if the track were the ordinary
// constant-value case.

TEST_CASE("husk export: an animated (non-constant) M2Color track is dropped with a note, not "
          "silently treated as constant") {
    auto dir = defaultsDir("animatedcolor");
    auto m2 = materialsFixtureM2(1, 1, 0, 0, 0, 0, 0);  // 1 material, 1 color
    patchColorTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);
    CHECK(result.output.find("core glTF has no way to animate a material's baseColorFactor") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an animated (non-constant) M2TextureWeight track is dropped with a "
          "note, not silently treated as constant") {
    auto dir = defaultsDir("animatedweight");
    // 1 material, 1 textureWeight, 1 textureWeightCombos entry (its
    // zeroed default value, 0, already points at textureWeights[0]).
    auto m2 = materialsFixtureM2(1, 0, 1, 1, 0, 0, 0);
    patchWeightTrackAnimated(m2);
    writeFile(dir / "m.m2", m2);
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) whose color tint (M2Color) or transparency fade") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a constant (non-animated) M2Color track gets no animated-tint note") {
    auto dir = defaultsDir("constantcolor");
    // materialsFixtureM2's zeroed color record has outer.count=0 (empty,
    // not animated) -- the ordinary, already-well-tested case; this just
    // confirms it doesn't spuriously trigger the new note.
    writeFile(dir / "m.m2", materialsFixtureM2(1, 1, 0, 0, 0, 0, 0));
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("color tint") == std::string::npos);

    fs::remove_all(dir);
}

// FINDINGS.md §3.1: a batch's textureTransformComboIndex resolving to a
// real M2TextureTransform gets noted and exported as inert extras, not
// silently dropped -- see gltf.hpp's TextureTransform doc comment for
// why it's never applied to the actual render.

TEST_CASE("husk export: a batch referencing a texture transform gets a note, and husk info "
          "counts texture_transforms") {
    auto dir = defaultsDir("texturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    writeFile(dir / "m.skin", oneBatchSkin({.textureTransformComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 batch(es) with a UV transform (M2TextureTransform)") !=
          std::string::npos);

    auto infoResult = runHusk("info " + (dir / "m.m2").string());
    CHECK(infoResult.exitCode == 0);
    CHECK(infoResult.output.find("texture_transforms: 1 ") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a batch with textureTransformComboIndex = 0xFFFF (none) gets no "
          "UV-transform note") {
    auto dir = defaultsDir("notexturetransform");
    writeFile(dir / "m.m2", oneTextureTransformM2());
    // Default BatchFields::textureTransformComboIndex is 0xFFFF -- the
    // model has a real texture_transforms entry, but this batch doesn't
    // reference it.
    writeFile(dir / "m.skin", oneBatchSkin({}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("UV transform") == std::string::npos);

    fs::remove_all(dir);
}
