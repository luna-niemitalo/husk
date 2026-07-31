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

// A dedicated subdirectory (not the shared system temp root tempPath()
// otherwise writes into) so directory-scan-based defaulting (--skin
// auto's same-basename scan, --skin-dir/--textures/--anim/--skel all
// defaulting to "the model's own directory") gets a clean, isolated view --
// no risk of an unrelated file from a different test case being picked up.
fs::path defaultsDir(const std::string& name) {
    auto dir = fs::temp_directory_path() / ("husk-cli-test-defaults-" + name);
    fs::create_directories(dir);
    return dir;
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

// A .skin with every array (vertices/indices/submeshes/batches) genuinely
// empty (count 0) -- the dominant real-corpus shape (3,807 files): a
// pure particle/ribbon VFX model with no renderable geometry at all, not
// merely an empty batch table over real vertices (that's tinyMatchingSkin's
// case). Pairs with a 0-vertex M2 (minimalMd20() itself, unmodified).
std::vector<uint8_t> emptySkin() {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 0);
    putU32(b, 44);  // vertices: count=0, offset=44
    putU32(b, 0);
    putU32(b, 44);  // indices: count=0, offset=44
    putU32(b, 0);
    putU32(b, 0);  // bones: count=0, offset=0 (unread)
    putU32(b, 0);
    putU32(b, 0);  // submeshes: count=0, offset=0
    putU32(b, 0);
    putU32(b, 0);  // batches: count=0, offset=0
    REQUIRE(b.size() == 44);
    return b;
}

// minimalMd20() (0 vertices) plus one M2CompBone -- the real shape a
// geometry-less VFX model still has (the corpus's own
// particle-only .m2 files all carry at least one bone for emitter
// attachment), so a mesh-less export still has a skeleton to fall back to
// instead of hitting writeGlbMulti's "nothing to export at all" case.
std::vector<uint8_t> zeroVertexOneBoneM2() {
    auto b = minimalMd20();
    uint32_t boneOff = static_cast<uint32_t>(b.size());
    uint32_t boneCount = 1;
    std::memcpy(b.data() + 0x02C, &boneCount, 4);
    std::memcpy(b.data() + 0x030, &boneOff, 4);
    b.resize(boneOff + 0x58, 0);
    int32_t keyBoneId = -1;
    std::memcpy(b.data() + boneOff + 0x00, &keyBoneId, 4);
    int16_t parentBone = -1;
    std::memcpy(b.data() + boneOff + 0x08, &parentBone, 2);
    return b;
}

// Pairs with tinyValidM2() (1 M2 vertex) to reproduce the real "wrong .skin"
// prefix-collision shape: 2 local vertex slots resolving to global vertices 5
// and 6 (both out of range for a 1-vertex model), each referenced 3 times
// by `indices` -- 6 out-of-range triangleIndices entries total, the worst
// being 6, so the improved error message's count/max-offender fields have
// real, distinct values to assert on (not 1-and-1, which wouldn't tell
// count and max apart).
std::vector<uint8_t> outOfRangeVertexSkin() {
    std::vector<uint8_t> b;
    putTag(b, "SKIN");
    putU32(b, 2);
    putU32(b, 44);  // vertices: count=2, offset=44
    putU32(b, 6);
    putU32(b, 48);  // indices: count=6, offset=48
    putU32(b, 0);
    putU32(b, 0);  // bones: unread
    putU32(b, 0);
    putU32(b, 0);  // submeshes: count=0
    putU32(b, 0);
    putU32(b, 0);  // batches: count=0
    REQUIRE(b.size() == 44);
    putU16(b, 5);
    putU16(b, 6);  // vertices[0]=global 5, vertices[1]=global 6
    REQUIRE(b.size() == 48);
    for (int i = 0; i < 3; ++i) putU16(b, 0);  // indices[0..3) -> local slot 0 -> global 5
    for (int i = 0; i < 3; ++i) putU16(b, 1);  // indices[3..6) -> local slot 1 -> global 6
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
// `husk export --anim <dir>` really reads the payload cross-blob end to end,
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

// A minimal, valid .bone file (see src/bone.hpp/tests/test_bone.cpp): a
// leading version=1, then BIDA (one uint16 bone index) and BOMT (one
// identity-plus-translation 4x4 row-major matrix) in lockstep, one entry.
std::vector<uint8_t> buildBoneFile(uint16_t boneIndex, float tx, float ty, float tz) {
    std::vector<uint8_t> file;
    putU32(file, 1);  // version
    std::vector<uint8_t> bida;
    putU16(bida, boneIndex);
    appendChunkTo(file, "BIDA", bida);
    std::vector<uint8_t> bomt;
    float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1};
    for (float f : rows) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        putU32(bomt, bits);
    }
    appendChunkTo(file, "BOMT", bomt);
    return file;
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

// tinyValidM2() (1 vertex) plus 1 M2Texture (type=0)/1 M2Material/a 1-entry
// textureCombos table pointing at it, wrapped in MD21 with a TXID chunk
// giving that texture FileDataID `fileDataId` -- the minimum shape
// buildMaterialsAndPrimitives needs to try resolving a real embedded image.
// Shared by the --textures default-directory and --textures none tests
// below: both need the identical model, only the `--textures` argument
// differs.
std::vector<uint8_t> oneTexturedModel(uint32_t fileDataId) {
    auto md20 = tinyValidM2();
    uint32_t one = 1;
    uint32_t texOff = static_cast<uint32_t>(md20.size());
    md20.resize(texOff + 16, 0);  // M2Texture: type/flags/filename(M2Array<char>)
    std::memcpy(md20.data() + 0x050, &one, 4);     // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    md20.resize(matOff + 4, 0);  // M2Material: flags=0, blendMode=0
    std::memcpy(md20.data() + 0x070, &one, 4);     // materials.count
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
    putU32(file, fileDataId);
    return file;
}

// A .skin with one submesh/batch (materialIndex=0, textureComboIndex=0,
// textureCount=1) over the model oneTexturedModel() builds -- header
// offsets/sizes per src/skin.cpp (vertices=0x04, indices=0x0C, bones=0x14,
// submeshes=0x1C, batches=0x24; kSubmeshSize=0x30, kBatchSize=0x18). Built
// by appending each section and patching its header descriptor from the
// running size, rather than hand-computed offsets, so a struct-size mistake
// shows up as a wrong read immediately instead of silently compiling.
std::vector<uint8_t> oneTexturedModelSkin() {
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
    uint16_t zero16 = 0, one16 = 1, noColor = 0xFFFF;  // colorIndex's "none" sentinel
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
    // degenerate triangle onto the model's one vertex).
    uint32_t idxOff = static_cast<uint32_t>(skin.size());
    putU16(skin, 0);
    putU16(skin, 0);
    putU16(skin, 0);
    patchArray(0x0C, 3, idxOff);

    patchArray(0x14, 0, 0);  // bones: unread, left empty
    return skin;
}

}  // namespace

TEST_CASE("husk export: an inline bone with a flags&0x20 sequence produces a real glTF "
          "animation, end to end") {
    auto m2Path = tempPath("animated.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("animated.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("animated.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim <dir> resolves an external (flags without 0x20/0x40) sequence's "
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("external-anim.glb").string() + " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: an external sequence with no matching --anim <dir> file produces no "
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("external-anim-missing.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

// M2_GAPS_TODO.md's former Item 1 / WIKI_FINDINGS.md §12: a "pure alias"
// sequence (flags & 0x40 set, flags & 0x20 NOT set) has no keyframe data of
// its own -- buildAnimations now resolves aliasNext to the terminal
// non-alias sequence and reuses *that* sequence's own keyframe data,
// registered under the alias's own id. `spec` gives each sequence's own
// (id, variationIndex, flags, aliasNext); only sequence array index 0 gets
// real inline bone keyframe data (fillTrack's own single-outer-sub-array
// convention, same as tinyAnimatedM2), so any *other* sequence that
// produces a clip at all can only be getting there via alias-chain
// resolution borrowing index 0's data, not its own.
struct AliasSeqSpec {
    uint16_t id;
    uint16_t variationIndex;
    uint32_t flags;
    uint16_t aliasNext;
};

std::vector<uint8_t> aliasChainM2(const std::vector<AliasSeqSpec>& specs) {
    auto b = tinyValidM2();

    uint32_t seqOff = static_cast<uint32_t>(b.size());
    uint32_t seqCount = static_cast<uint32_t>(specs.size());
    std::memcpy(b.data() + 0x01C, &seqCount, 4);
    std::memcpy(b.data() + 0x020, &seqOff, 4);
    b.resize(seqOff + 0x40 * specs.size(), 0);
    for (size_t i = 0; i < specs.size(); ++i) {
        size_t off = seqOff + i * 0x40;
        uint16_t id = specs[i].id;
        uint16_t variationIndex = specs[i].variationIndex;
        uint32_t flags = specs[i].flags;
        uint16_t aliasNext = specs[i].aliasNext;
        std::memcpy(b.data() + off + 0x00, &id, 2);
        std::memcpy(b.data() + off + 0x02, &variationIndex, 2);
        std::memcpy(b.data() + off + 0x0C, &flags, 4);
        std::memcpy(b.data() + off + 0x3E, &aliasNext, 2);
    }

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

TEST_CASE("husk export: a pure-alias sequence (flags 0x40, no 0x20) resolves via aliasNext to a "
          "sibling sequence's real inline data, producing a real clip instead of none") {
    auto m2Path = tempPath("alias-2hop.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x40, 0}}));
    auto skinPath = tempPath("alias-2hop.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-2hop.glb").string());
    CHECK(result.exitCode == 0);
    // Pre-fix, sequence 1 (pure alias) was skipped outright -- only
    // sequence 0's own clip would exist ("1 animation(s)"). Both existing
    // is the real regression signal (verified via git stash: this line
    // reads "1 animation(s)" before the aliasNext-resolution fix, "2" after).
    CHECK(result.output.find("2 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a multi-hop alias chain (pure alias -> pure alias -> real inline data) "
          "resolves all the way to the terminal sequence") {
    auto m2Path = tempPath("alias-3hop.m2");
    // seq2 (id=300) -> aliasNext=1 -> seq1 (id=200, also pure alias) ->
    // aliasNext=0 -> seq0 (id=100, real inline data).
    writeFile(m2Path,
              aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x40, 0}, {300, 0, 0x40, 1}}));
    auto skinPath = tempPath("alias-3hop.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-3hop.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("3 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a sequence flagged both inline (0x20) AND alias (0x40) resolves against "
          "its own sequence index, not the (possibly invalid) alias chain (real data: 31/38 real "
          "alias sequences in bloodelffemale_hd.skel also carry 0x20 -- WIKI_FINDINGS.md §12's "
          "follow-up)") {
    auto m2Path = tempPath("alias-both-flags.m2");
    // seq1 has both 0x20 and 0x40 set, and a deliberately out-of-range
    // aliasNext (99, only 2 sequences exist) that would throw immediately
    // if the alias-resolution branch were ever taken for it -- since this
    // fixture's fillTrack only supplies real keyframe data at sequence-
    // array index 0 (seq0's own), seq1 produces no clip of its own either
    // way (no data at its own index 1); the real proof here is exit code 0
    // (no crash from the invalid aliasNext), which a broken priority order
    // would turn into a hard failure, not a silently-wrong clip count.
    writeFile(m2Path, aliasChainM2({{100, 0, 0x20, 0}, {200, 0, 0x20 | 0x40, 99}}));
    auto skinPath = tempPath("alias-both-flags.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-both-flags.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a self-referencing pure-alias sequence (aliasNext points to itself) "
          "fails cleanly with a cycle error, not an infinite loop") {
    auto m2Path = tempPath("alias-cycle.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x40, 0}}));  // seq 0 aliases to itself
    auto skinPath = tempPath("alias-cycle.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-cycle.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: export failed") != std::string::npos);
    CHECK(result.output.find("cycle") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: a pure-alias sequence's out-of-range aliasNext fails cleanly, not an "
          "out-of-bounds read") {
    auto m2Path = tempPath("alias-oob.m2");
    writeFile(m2Path, aliasChainM2({{100, 0, 0x40, 5}}));  // only 1 sequence, aliasNext=5
    auto skinPath = tempPath("alias-oob.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("alias-oob.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("husk: export failed") != std::string::npos);
    CHECK(result.output.find("aliasNext") != std::string::npos);
    CHECK(result.output.find("terminate called") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// --anim's resolution used to only ever look for '<FileDataID>.anim', but a
// real wow.export-style extraction names external .anim files
// '<model-basename><animId:04d>-<subAnimId:02d>.anim' instead (see
// findAnimFileByBasename, WIKI_FINDINGS.md §2, DESIGN.md's AFSB design note)
// -- these four cases cover its three-way priority (FileDataID file,
// basename file, neither) against the same tinyExternalAnimM2 fixture the
// two tests above already use.

TEST_CASE("husk export: --anim <dir> resolves via the basename convention when the model has no "
          "AFID chunk at all") {
    // Unwrapped (no MD21/AFID chunk) -- tinyExternalAnimM2's seqId=200,
    // subAnimId=0 has no FileDataID mapping to try in the first place, so
    // this exercises the animFileIds==nullopt path straight into the
    // basename fallback.
    auto m2 = tinyExternalAnimM2();

    auto m2Path = tempPath("anim-basename-no-afid.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("anim-basename-no-afid.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-no-afid-dir";
    fs::create_directories(animDir);
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-no-afid.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> falls back to the basename convention when an AFID entry "
          "exists but its FileDataID-named file is missing") {
    auto md20 = tinyExternalAnimM2();
    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "AFID");
    putU32(file, 8);
    putU16(file, 200);
    putU16(file, 0);
    putU32(file, 777);  // maps to 777.anim -- deliberately never written below

    auto m2Path = tempPath("anim-basename-afid-file-missing.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-afid-file-missing.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-afid-file-missing-dir";
    fs::create_directories(animDir);
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-afid-file-missing.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> prefers the FileDataID-named file over the basename-named "
          "one when both exist") {
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

    auto m2Path = tempPath("anim-basename-priority.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-priority.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-priority-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());  // real, resolvable data
    // Deliberately too small to hold the 1 timestamp (4 bytes) + 1 C3Vector
    // (12 bytes) tinyExternalAnimM2's track descriptors claim -- if the
    // basename file were read instead of 777.anim, resolving it would throw
    // a bounds error (a non-zero exit), not silently produce a wrong value.
    writeFile(animDir / (m2Path.stem().string() + "0200-00.anim"), {0, 0});

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-priority.glb").string() + " --anim " +
                           animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim <dir> produces no animation clip, not an error, when neither the "
          "FileDataID-named nor the basename-named file exists") {
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

    auto m2Path = tempPath("anim-basename-neither.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-basename-neither.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-basename-neither-dir";
    fs::create_directories(animDir);  // neither 777.anim nor the basename file exists

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-basename-neither.glb").string() + " --anim " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skel-no-sks1.glb").string() + " --skel " + skelPath.string());
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skel-inline-anim.glb").string() + " --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
}

TEST_CASE("husk export: a .skel external (flags without 0x20/0x40) SKS1 sequence resolves via "
          "the .skel's own AFID + --anim <dir>, cross-blob (own AFID table, not the owning M2's)") {
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
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-external-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence whose --anim <dir> file is AFSB-tagged "
          "resolves a real animation clip, end to end -- SKB1's own per-sequence (count,offset) "
          "descriptors point directly into the AFSB payload, same mechanism as an AFM2-shaped "
          "external file just pointed at a different blob (WIKI_FINDINGS.md §2's follow-up)") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;  // see fillTrack's identical comment
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);  // seq 0: 1 timestamp at *AFSB-blob* offset 0
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);  // seq 0: 1 C3Vector at *AFSB-blob* offset 4
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

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
    appendChunkTo(afsbFile, "AFSB", tinyAnimFile());  // real [timestamp][Vec3] payload
    writeFile(animDir / "888.anim", afsbFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-afsb-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence's --anim <dir> file with BOTH a small AFM2 "
          "stub chunk and a real AFSB chunk resolves the AFSB data, not the AFM2 stub -- real "
          "bloodelffemale_hd .anim files have exactly this shape (a tiny, all-near-zero AFM2 "
          "stub alongside the real AFSB data); using the stub's payload as if it were the full "
          "flat-format content throws a real 'claims more keyframes than this blob holds' error "
          "instead, so AFSB has to take priority whenever both are present") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);
    size_t transOff = boneOff + 0x10;
    uint16_t noGlobalSeq = 0xFFFF;
    std::memcpy(skb1Payload.data() + transOff + 0x02, &noGlobalSeq, 2);
    uint32_t tsOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(tsOuterOff + 8, 0);
    putArrayAt(skb1Payload, tsOuterOff, 1, 0);
    uint32_t valOuterOff = static_cast<uint32_t>(skb1Payload.size());
    skb1Payload.resize(valOuterOff + 8, 0);
    putArrayAt(skb1Payload, valOuterOff, 1, 4);
    putArrayAt(skb1Payload, transOff + 0x04, 1, tsOuterOff);
    putArrayAt(skb1Payload, transOff + 0x0C, 1, valOuterOff);

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
    appendChunkTo(mixedFile, "AFM2", {0, 0, 0, 0});    // tiny stub, not real track data
    appendChunkTo(mixedFile, "AFSB", tinyAnimFile());  // the real data
    writeFile(animDir / "999.anim", mixedFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-afm2-stub-afsb-anim.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: a .skel external sequence's chunked --anim <dir> file with neither an "
          "AFM2 nor an AFSB chunk produces no animation clip, not an error -- an unrecognized "
          "future .anim shape, same skip policy as a missing file") {
    size_t boneOff = 0;
    auto skb1Payload = buildSkb1PayloadForTracks(&boneOff);  // tracks left empty -- never reached

    std::vector<uint8_t> skel;
    appendChunkTo(skel, "SKB1", skb1Payload);
    appendChunkTo(skel, "SKS1", buildSks1Payload(700, 0));  // flags=0 -- external
    std::vector<uint8_t> afid;
    putU16(afid, 700);
    putU16(afid, 0);
    putU32(afid, 1111);
    appendChunkTo(skel, "AFID", afid);

    auto m2 = tinyValidM2();
    uint32_t globalFlags = 0x200000;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);

    auto m2Path = tempPath("skel-neither-afm2-afsb.m2");
    writeFile(m2Path, m2);
    auto skinPath = tempPath("skel-neither-afm2-afsb.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("skel-neither-afm2-afsb.skel");
    writeFile(skelPath, skel);
    auto animDir = fs::temp_directory_path() / "husk-cli-test-skel-neither-dir";
    fs::create_directories(animDir);
    std::vector<uint8_t> unknownFile;
    appendChunkTo(unknownFile, "ZZZZ", {1, 2, 3, 4});  // some future chunk shape, not AFM2/AFSB
    writeFile(animDir / "1111.anim", unknownFile);

    auto result =
        runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                tempPath("skel-neither-afm2-afsb.glb").string() + " --skel " + skelPath.string() +
                " --anim " + animDir.string());
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
    auto result = runHusk("export " + m2Path.string() + " --skin /nonexistent.skin -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("cycle.glb").string() + " --skel " + skelPath.string());
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("self-parent.glb").string() + " --skel " + skelPath.string());
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

TEST_CASE("husk info: global_flags prints named bits alongside the raw hex value") {
    auto m2 = minimalMd20();
    // tilt_x (0x1) | load_phys_data (0x20) | new_particle_record (0x200) --
    // three bits spanning the reserved-gap/version-gated boundaries in
    // GlobalFlag's own bit layout, not just the first bit.
    uint32_t globalFlags = 0x1 | 0x20 | 0x200;
    std::memcpy(m2.data() + 0x010, &globalFlags, 4);
    auto path = tempPath("global-flags.m2");
    writeFile(path, m2);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("global_flags: 0x221") != std::string::npos);
    CHECK(result.output.find("tilt_x") != std::string::npos);
    CHECK(result.output.find("load_phys_data") != std::string::npos);
    CHECK(result.output.find("new_particle_record") != std::string::npos);
    // A bit that isn't set must not show up in the name list.
    CHECK(result.output.find("camera_related") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: global_flags with no bits set prints \"(none set)\", not an empty parenthesis") {
    auto path = tempPath("no-global-flags.m2");
    writeFile(path, minimalMd20());  // globalFlags = 0

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("global_flags: 0x0 (none set)") != std::string::npos);

    fs::remove(path);
}

// textureCombinerCombos (wowdev.wiki M2#Header) only exists in the wire
// header at all when flag_use_texture_combiner_combos (0x8) is set --
// RO_COMPLETENESS_TODO.md's former Item 2b. Builds a real header past
// minimalMd20()'s own 0x130-byte end: the array descriptor at 0x130
// pointing at 3 real uint16 values appended right after it.
TEST_CASE("husk info: textureCombinerCombos is read and printed when "
          "flag_use_texture_combiner_combos is set") {
    auto b = minimalMd20();
    uint32_t globalFlags = 0x8;  // flag_use_texture_combiner_combos
    std::memcpy(b.data() + 0x010, &globalFlags, 4);
    uint32_t count = 3;
    uint32_t offset = static_cast<uint32_t>(b.size() + 8);
    b.resize(b.size() + 8);
    std::memcpy(b.data() + 0x130, &count, 4);
    std::memcpy(b.data() + 0x134, &offset, 4);
    putU16(b, 5);
    putU16(b, 6);
    putU16(b, 7);
    auto path = tempPath("texture-combiner-combos.m2");
    writeFile(path, b);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("textureCombinerCombos: 3") != std::string::npos);
    CHECK(result.output.find("5 6 7") != std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: textureCombinerCombos is absent (not printed) when the flag isn't set, "
          "even though the header is otherwise unchanged") {
    auto path = tempPath("no-texture-combiner-combos.m2");
    writeFile(path, minimalMd20());  // globalFlags = 0 -- flag unset

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("textureCombinerCombos") == std::string::npos);

    fs::remove(path);
}

TEST_CASE("husk info: flag_use_texture_combiner_combos set but the blob too short for the array "
          "fails cleanly, not a silent misread") {
    auto b = minimalMd20();
    uint32_t globalFlags = 0x8;
    std::memcpy(b.data() + 0x010, &globalFlags, 4);
    // No bytes appended past 0x130 -- the array descriptor itself doesn't fit.
    auto path = tempPath("truncated-texture-combiner-combos.m2");
    writeFile(path, b);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("textureCombinerCombos") != std::string::npos);

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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("nan-vertex.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("non-finite") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --skin auto (explicit) without --skin-dir defaults to the model's own "
          "directory, and still fails cleanly when the FileDataID-named .skin isn't there") {
    auto m2Path = tempPath("auto-no-skindir.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-no-skindir.glb").string());
    CHECK(result.exitCode == 1);
    // --skin-dir now defaults to the model's own directory (same one
    // tempPath() writes m2Path into) rather than being required -- since no
    // '12345.skin' actually exists there, this fails on the file itself,
    // not on a missing flag. resolveSkin's own "not found" reason names
    // the specific candidate path it checked, not just the directory.
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("12345.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --skin omitted resolves identically to --skin auto explicitly") {
    auto m2Path = tempPath("auto-omitted.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " -o " +
                           tempPath("auto-omitted.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("12345.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: 'auto' resolution order -- the SFID-declared FileDataID wins over a "
          "same-basename numbered scan match when both exist, not just 'some' resolution") {
    auto dir = defaultsDir("sfidwins");
    uint32_t fileDataId = 424242;
    writeFile(dir / "sfidwins.m2", chunkedM2WithSfid(tinyValidM2(), {fileDataId}));
    writeFile(dir / (std::to_string(fileDataId) + ".skin"), tinyMatchingSkin());
    // A same-basename numbered file also sits right next to the model --
    // if resolveSkin tried the fallback scan first (or instead of the SFID
    // stage), this file would get picked, and the assertions below would
    // fail.
    writeFile(dir / "sfidwins00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "sfidwins.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("SFID entry 0, highest-detail LOD") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") == std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId) + ".skin") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --skin-dir none skips the SFID stage entirely -- 'auto' falls straight "
          "to the same-basename scan even when a matching FileDataID-named file also sits in "
          "the model's own directory") {
    auto dir = defaultsDir("skindirnone");
    uint32_t fileDataId = 646464;
    writeFile(dir / "skindirnone.m2", chunkedM2WithSfid(tinyValidM2(), {fileDataId}));
    // Both the SFID-declared FileDataID's own file and a same-basename
    // numbered file sit in the model's directory -- --skin-dir none must
    // still pick the same-basename one, never even looking at the
    // FileDataID match.
    writeFile(dir / (std::to_string(fileDataId) + ".skin"), tinyMatchingSkin());
    writeFile(dir / "skindirnone00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "skindirnone.m2").string() + " --skin-dir none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
    CHECK(result.output.find("SFID entry 0") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: 'auto' on a model with no SFID chunk fails cleanly, naming the reason") {
    auto m2Path = tempPath("auto-no-sfid.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {}));  // no SFID chunk at all

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-no-sfid.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("no SFID chunk") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --skin none is rejected by CLI11 at parse time, naming the real "
          "expected values (a path, or 'auto') -- never silently accepted") {
    auto result = runHusk("export some.m2 --skin none");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("auto") != std::string::npos);
}

TEST_CASE("husk export: --skin-dir given while --skin is an explicit path (not 'auto') fails "
          "cleanly") {
    auto m2Path = tempPath("skindir-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("skindir-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("skindir-without-auto.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
    CHECK(result.output.find("'auto'") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --lod combined with --skin-dir none is its own explicit conflict -- "
          "--lod needs the SFID-based resolution stage --skin-dir 'none' disables") {
    auto m2Path = tempPath("lod-skindir-none-conflict.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {111111}));

    auto result = runHusk("export " + m2Path.string() + " --skin-dir none --lod 1");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
    CHECK(result.output.find("'none'") != std::string::npos);

    fs::remove(m2Path);
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
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string());
    CHECK(result.exitCode == 0);
    // resolveSkin's own success note is "'auto' resolved '<path>' (SFID
    // entry 0, ...)" -- note the word order (unlike the --lod-given path's
    // "resolved 'auto' -> ..." in exportGlb, these two success messages are
    // phrased differently for what's conceptually the same event).
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find(std::to_string(fileDataId)) != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: 'auto' with --skin-dir pointing at a directory missing the resolved "
          "FileDataID's .skin, and no same-basename fallback either, fails cleanly") {
    auto m2Path = tempPath("auto-missing-file.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {999999999}));

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
                           tempPath("auto-missing-file.glb").string() + " --skin-dir " +
                           fs::temp_directory_path().string());
    CHECK(result.exitCode == 1);
    // resolveSkin falls back to the same-basename scan (which also finds
    // nothing here) before giving up -- its "not found" reason names the
    // specific candidate path it checked (<skin-dir>/999999999.skin), not
    // just the directory.
    CHECK(result.output.find("'auto' couldn't resolve a .skin file") != std::string::npos);
    CHECK(result.output.find("wasn't found at the expected path") != std::string::npos);
    CHECK(result.output.find("999999999.skin") != std::string::npos);

    fs::remove(m2Path);
}

TEST_CASE("husk export: --lod given while --skin is an explicit path (not 'auto') fails "
          "cleanly") {
    auto m2Path = tempPath("lod-without-auto.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("lod-without-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("lod-without-auto.glb").string() + " --lod 1");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("'auto'") != std::string::npos);

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
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
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

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " +
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
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
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

// Regression coverage for kMinVerifiedParticleVersion: M2Particle's 0x1EC
// byte shape is only real-data-verified for Cataclysm+ (272) -- see
// m2::ParticleEmitter's doc comment. A file below that version with real
// particle_emitters must warn and stay count-only, same shape as the
// existing "below Wrath" bones/sequences/ribbons regression test above.
TEST_CASE("husk info: a particle_emitters array below Cataclysm (272) prints a loud warning and "
          "stays count-only") {
    auto md20 = minimalMd20(/*version=*/264);  // Wrath -- above the bones/ribbons floor, below
                                                 // the particle one
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x128, &count, 4);
    std::memcpy(md20.data() + 0x12C, &off, 4);
    md20.resize(md20.size() + 0x1EC, 0);

    auto path = tempPath("pre-cata-particles.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Cataclysm") != std::string::npos);
    CHECK(result.output.find("particle_emitters: 1") != std::string::npos);
    CHECK(result.output.find("particleId=") == std::string::npos);  // never parsed structurally

    fs::remove(path);
}

TEST_CASE("husk info: a Cata+ particle_emitters array prints no version warning and resolves "
          "real fields") {
    auto md20 = minimalMd20(/*version=*/274);
    uint32_t count = 1;
    uint32_t off = static_cast<uint32_t>(md20.size());
    std::memcpy(md20.data() + 0x128, &count, 4);
    std::memcpy(md20.data() + 0x12C, &off, 4);
    md20.resize(md20.size() + 0x1EC, 0);
    md20[off + 0x28] = 4;  // blendingType
    md20[off + 0x29] = 1;  // emitterType

    auto path = tempPath("cata-particles.m2");
    writeFile(path, md20);

    auto result = runHusk("info " + path.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("below Cataclysm") == std::string::npos);
    CHECK(result.output.find("particleId=") != std::string::npos);
    CHECK(result.output.find("blendingType=4 emitterType=1") != std::string::npos);

    fs::remove(path);
}

// CLI default-resolution tests: `husk export <file.m2>` alone (no .skin,
// output, .skel, or --textures/--skin-dir/--anim) should resolve
// everything it reasonably can from what's already sitting next to the
// model, rather than requiring every argument spelled out even when it's
// exactly where the tool could have found it itself. Each fixture below
// gets its own dedicated subdirectory (not the shared system temp root
// tempPath() otherwise writes into) so directory-scan-based defaulting has
// a clean, isolated view -- no risk of an unrelated file from a different
// test case being picked up.

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
    // --skin's default is "auto" (not a separate "no .skin path given" note
    // the old positional grammar printed) -- resolveSkin's own success note
    // covers this instead.
    CHECK(result.output.find("'auto' resolved") != std::string::npos);
    CHECK(result.output.find("same-basename numbered scan") != std::string::npos);
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

// Regression test: a real corpus scan found
// findSameBasenameSkins silently pairing "mogu_library_crate_10.m2" with
// "mogu_library_crate_100.skin" -- which actually belongs to the shorter
// sibling model "mogu_library_crate_1.m2" ("...crate_1" + "00" LOD suffix),
// not "...crate_10" ("...crate_10" + "0", a spurious 1-digit match that
// used to tie with the real "...crate_1000.skin" ("...crate_10" + "00")
// candidate and lose the std::sort tie-break (lexicographically, "0" <
// "00.skin"'s leading "0" but ".skin" < "0" in ASCII, so the *wrong* file
// sorted first). Reproduced here with a minimal same-shape pair: "crate_1"/
// "crate_10" siblings, each with its own real 2-digit-suffix .skin.
TEST_CASE("husk export: a model basename that's a numeric-suffix prefix of a sibling model's own "
          "basename resolves its own 2-digit-suffix .skin, not the sibling's colliding shorter/"
          "longer match") {
    auto dir = defaultsDir("basenamecollision");
    writeFile(dir / "crate_1.m2", tinyValidM2());
    writeFile(dir / "crate_10.m2", tinyValidM2());
    writeFile(dir / "crate_100.skin", tinyMatchingSkin());   // crate_1's real skin: "crate_1"+"00"
    writeFile(dir / "crate_1000.skin", tinyMatchingSkin());  // crate_10's real skin: "crate_10"+"00"

    auto shortResult = runHusk("export " + (dir / "crate_1.m2").string());
    CHECK(shortResult.exitCode == 0);
    CHECK(shortResult.output.find("crate_100.skin") != std::string::npos);

    auto longResult = runHusk("export " + (dir / "crate_10.m2").string());
    CHECK(longResult.exitCode == 0);
    CHECK(longResult.output.find("crate_1000.skin") != std::string::npos);

    fs::remove_all(dir);
}

// Regression test for the second fix above: a wrong-
// .skin pairing references hundreds of out-of-range vertex indices in real
// corpus files, not one -- the error message now names how many and the
// worst offender, not just the first index iteration happened to hit.
TEST_CASE("husk export: a .skin referencing multiple out-of-range M2 vertices reports the count "
          "and the worst offender, not just the first") {
    auto dir = defaultsDir("outofrangevtx");
    writeFile(dir / "mismatch.m2", tinyValidM2());
    writeFile(dir / "mismatch00.skin", outOfRangeVertexSkin());

    auto result = runHusk("export " + (dir / "mismatch.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("references 6 out-of-range M2 vertex index(es)") != std::string::npos);
    CHECK(result.output.find("up to 6") != std::string::npos);
    CHECK(result.output.find("only has 1 vertices") != std::string::npos);

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

// Shared by every --bones-dir test below: a 0-inline-bone model + a .skel
// with one root bone (index 0) and a BFID chunk declaring FileDataID
// 424242 -- the minimum shape needed to prove --bones-dir's resolution
// (model/.skel's BFID array -> '<dir>/<FileDataID>.bone').
std::vector<uint8_t> boneCorrectionSkel() {
    auto skel = buildSkel({{-1, -1}});
    std::vector<uint8_t> bfid;
    putU32(bfid, 424242);
    appendChunkTo(skel, "BFID", bfid);
    return skel;
}

TEST_CASE("husk export: --bones-dir resolves a .skel's BFID-declared FileDataID to a real "
          "'<FileDataID>.bone' file and attaches it as inert extras, end to end") {
    auto m2Path = tempPath("bonesdir.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir.skel");
    writeFile(skelPath, boneCorrectionSkel());
    auto dir = defaultsDir("bonesdir");
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);
    CHECK(result.output.find("'.bone' correction set(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

TEST_CASE("husk export: --bones-dir none never attaches corrections, even when a matching "
          "'<FileDataID>.bone' sits in the default (model's own) directory") {
    auto dir = defaultsDir("bonesdirnone");
    writeFile(dir / "bonesdirnone.m2", tinyValidM2());
    writeFile(dir / "bonesdirnone00.skin", tinyMatchingSkin());
    writeFile(dir / "bonesdirnone.skel", boneCorrectionSkel());
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "bonesdirnone.m2").string() + " --bones-dir none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("correction set(s)") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --bones-dir defaults to the model's own directory -- a FileDataID-named "
          "'.bone' file already sitting there is attached without passing the flag") {
    auto dir = defaultsDir("bonesdirdefault");
    writeFile(dir / "bonesdirdefault.m2", tinyValidM2());
    writeFile(dir / "bonesdirdefault00.skin", tinyMatchingSkin());
    writeFile(dir / "bonesdirdefault.skel", boneCorrectionSkel());
    writeFile(dir / "424242.bone", buildBoneFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "bonesdirdefault.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a .bone file correcting a bone index out of range for the model's "
          "skeleton fails the export with a clear message, naming the offending file/index") {
    auto m2Path = tempPath("bonesdir-oor.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir-oor.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir-oor.skel");
    writeFile(skelPath, boneCorrectionSkel());  // 1 bone (index 0) only
    auto dir = defaultsDir("bonesdiroor");
    writeFile(dir / "424242.bone", buildBoneFile(5, 0, 0, 0));  // bone 5 doesn't exist

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir-oor.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("424242.bone") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

// --lod all + multi-root and --bones-dir + multi-root are otherwise-untested
// combinations, since each half only had its own dedicated fixture before
// this. A 3-independent-
// root .skel (buildSkel's own multi-root shape, matching the real corpus
// finding that root count can be large) exercises both without a large real
// fixture: writeGlbMulti's synthesis is exercised alongside --lod's shared-
// skeleton-across-LOD-tiers path and --bones-dir's CorrectionSet::joint
// indices, neither of which this file otherwise checks against a multi-root
// skeleton at all.
TEST_CASE("husk export: --lod all combined with a multi-root .skel skeleton exports cleanly -- "
          "the shared skeleton/synthetic-root logic runs once, not per LOD tier") {
    auto m2Path = tempPath("lod-all-multiroot.m2");
    uint32_t id0 = 888101, id1 = 888102;
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {id0, id1}));

    auto skinDir = fs::temp_directory_path();
    auto skinPath0 = skinDir / (std::to_string(id0) + ".skin");
    auto skinPath1 = skinDir / (std::to_string(id1) + ".skin");
    writeFile(skinPath0, tinyMatchingSkin());
    writeFile(skinPath1, tinyMatchingSkin());

    auto skelPath = tempPath("lod-all-multiroot.skel");
    writeFile(skelPath, buildSkel({{-1, -1}, {-1, -1}, {-1, -1}}));  // 3 independent roots

    auto outPath = tempPath("lod-all-multiroot.glb");
    auto result = runHusk("export " + m2Path.string() + " --skin auto -o " + outPath.string() +
                           " --skin-dir " + skinDir.string() + " --lod all --skel " + skelPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 LOD tier(s)") != std::string::npos);
    CHECK(result.output.find("3 bones") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath0);
    fs::remove(skinPath1);
    fs::remove(skelPath);
    fs::remove(outPath);
}

TEST_CASE("husk export: --bones-dir combined with a multi-root .skel skeleton attaches "
          "corrections cleanly -- CorrectionSet::joint indices stay raw M2 bone indices, "
          "unaffected by the synthesized root node") {
    auto m2Path = tempPath("bonesdir-multiroot.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("bonesdir-multiroot.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("bonesdir-multiroot.skel");
    // 3 independent roots (indices 0, 1, 2), plus a BFID chunk so
    // --bones-dir has something to resolve -- same shape as
    // boneCorrectionSkel() above, just multi-root instead of single-root.
    auto skel = buildSkel({{-1, -1}, {-1, -1}, {-1, -1}});
    std::vector<uint8_t> bfid;
    putU32(bfid, 424242);
    appendChunkTo(skel, "BFID", bfid);
    writeFile(skelPath, skel);
    auto dir = defaultsDir("bonesdirmultiroot");
    // Corrects joint 2 -- the last of the 3 roots, not joint 0 -- so this
    // would fail loudly (an out-of-range or misattributed correction) if the
    // synthesized node's presence ever shifted a real joint's index.
    writeFile(dir / "424242.bone", buildBoneFile(2, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("bonesdir-multiroot.glb").string() + " --skel " + skelPath.string() +
                           " --bones-dir " + dir.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1/1") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove_all(dir);
}

// A minimal, real-shaped .phys file: PHYS (version) + a single BODY record
// (no shapes, so no SHAP/SHP2 chunk is needed) -- .phys's own chunk tags are
// byte-reversed on disk (WMO/ADT convention, opposite of M2's own inline
// chunks -- see src/phys.hpp's doc comment), so this appends reversed
// literals directly rather than reusing this file's own appendChunkTo
// (which assumes M2's non-reversed convention).
void appendChunkReversed(std::vector<uint8_t>& file, const char tag[4],
                          const std::vector<uint8_t>& payload) {
    file.push_back(tag[3]);
    file.push_back(tag[2]);
    file.push_back(tag[1]);
    file.push_back(tag[0]);
    putU32(file, static_cast<uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> buildPhysFile(uint16_t boneIndex, float x, float y, float z) {
    std::vector<uint8_t> file;
    std::vector<uint8_t> phys;
    putU16(phys, 1);  // version
    appendChunkReversed(file, "PHYS", phys);

    std::vector<uint8_t> body;
    putU16(body, 0);  // type
    putU16(body, 0);  // padding
    auto pos = vec3Bytes(x, y, z);
    body.insert(body.end(), pos.begin(), pos.end());
    putU16(body, boneIndex);
    putU16(body, 0);  // padding
    putU32(body, 0);  // shapes_base
    putU32(body, 0);  // shapes_count -- 0, no SHAP/SHP2 chunk needed
    appendChunkReversed(file, "BODY", body);
    return file;
}

TEST_CASE("husk export: --phys resolves a same-basename '.phys' file and attaches it as inert "
          "physics_bodies extras, end to end") {
    auto dir = defaultsDir("physdefault");
    writeFile(dir / "physdefault.m2", tinyValidM2());
    writeFile(dir / "physdefault00.skin", tinyMatchingSkin());
    writeFile(dir / "physdefault.skel", boneCorrectionSkel());
    writeFile(dir / "physdefault.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physdefault.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1 physics body") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys none never attaches physics bodies, even when a matching "
          "same-basename '.phys' sits in the default (model's own) directory") {
    auto dir = defaultsDir("physnone");
    writeFile(dir / "physnone.m2", tinyValidM2());
    writeFile(dir / "physnone00.skin", tinyMatchingSkin());
    writeFile(dir / "physnone.skel", boneCorrectionSkel());
    writeFile(dir / "physnone.phys", buildPhysFile(0, 0.01f, 0.02f, 0.03f));

    auto result = runHusk("export " + (dir / "physnone.m2").string() + " --phys none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("physics body") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --phys <path> resolves an explicitly-named .phys file, not requiring "
          "the same-basename convention") {
    auto m2Path = tempPath("phys-explicit.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("phys-explicit.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("phys-explicit.skel");
    writeFile(skelPath, boneCorrectionSkel());
    auto physPath = tempPath("physdata-under-a-different-name.phys");
    writeFile(physPath, buildPhysFile(0, 1, 2, 3));

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("phys-explicit.glb").string() + " --skel " + skelPath.string() +
                           " --phys " + physPath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("attached 1 physics body") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove(physPath);
}

TEST_CASE("husk export: a .phys body referencing a bone index out of range for the model's "
          "skeleton fails the export with a clear message, naming the offending file/index") {
    auto m2Path = tempPath("phys-oor.m2");
    writeFile(m2Path, tinyValidM2());
    auto skinPath = tempPath("phys-oor.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto skelPath = tempPath("phys-oor.skel");
    writeFile(skelPath, boneCorrectionSkel());  // 1 bone (index 0) only
    auto physPath = tempPath("phys-oor.phys");
    writeFile(physPath, buildPhysFile(5, 0, 0, 0));  // bone 5 doesn't exist

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("phys-oor.glb").string() + " --skel " + skelPath.string() +
                           " --phys " + physPath.string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("phys-oor.phys") != std::string::npos);
    CHECK(result.output.find("body 0") != std::string::npos);
    CHECK(result.output.find("bone 5") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(skelPath);
    fs::remove(physPath);
}

TEST_CASE("husk export: --textures defaults to the model's own directory -- a FileDataID-named "
          "file already sitting there is embedded without passing the flag") {
    auto dir = defaultsDir("textures");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});  // fake PNG bytes -- husk embeds raw, doesn't decode

    auto result = runHusk("export " + (dir / "textured.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures none never embeds an image, even when a matching "
          "<FileDataID>.png sits in the default (model's own) directory") {
    auto dir = defaultsDir("texturesnone");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "textured.m2").string() + " --textures none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("0 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --anim defaults to the model's own directory -- an external sequence's "
          ".anim file already sitting there resolves without passing the flag") {
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
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

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
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

// Regression test: real shipped Blizzard data (5 real
// files -- world bosses, base character rigs, one world doodad, all on
// `rotation`) has an exact-duplicate keyframe timestamp -- a genuinely-
// authored "hard cut" pose (two values meant to apply at the same instant),
// not corruption. This used to be rejected identically to real disorder (a
// timestamp genuinely *decreasing*, still covered by the test above); it's
// now repaired instead (the later duplicate nudged forward 1ms) so both
// authored values survive, rather than collapsing one away.
TEST_CASE("husk export: an exact-duplicate keyframe timestamp is repaired (nudged forward 1ms), "
          "not rejected like genuine disorder") {
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

    fillTrack(b, boneOff + 0x10, {0}, {vec3Bytes(0, 0, 0)});
    // Rotation keyframes 1 and 2 share timestamp 500 -- the real shape found
    // on yoggsaronbrain.m2/maldraxxusskeleton.m2/mechagnomemale.m2/etc.
    fillTrack(b, boneOff + 0x24, {0, 500, 500},
              {identityQuatBytes(), identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("duplicate-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("duplicate-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("duplicate-keyframe.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("isn't strictly greater than") == std::string::npos);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

// A genuine 3-way cascading duplicate run (T, T, T) must repair cleanly
// into strictly-increasing timestamps (T, T+1, T+2), not misfire the
// disorder check on the second duplicate -- see
// repairDuplicateTimestampsAndValidate's own doc comment for why comparing
// against the *original* (not already-nudged) previous timestamp matters.
TEST_CASE("husk export: a 3-way cascading duplicate keyframe timestamp run repairs cleanly, not "
          "just a single pair") {
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

    fillTrack(b, boneOff + 0x10, {0}, {vec3Bytes(0, 0, 0)});
    fillTrack(b, boneOff + 0x24, {0, 500, 500, 500},
              {identityQuatBytes(), identityQuatBytes(), identityQuatBytes(), identityQuatBytes()});
    fillTrack(b, boneOff + 0x38, {0}, {vec3Bytes(1, 1, 1)});

    auto m2Path = tempPath("cascading-duplicate-keyframe.m2");
    writeFile(m2Path, b);
    auto skinPath = tempPath("cascading-duplicate-keyframe.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("cascading-duplicate-keyframe.glb").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("isn't strictly greater than") == std::string::npos);
    CHECK(result.output.find("couldn't be repaired") == std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

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

// Regression tests: a genuinely geometry-less M2 (0
// vertices, an empty .skin) used to make buildMaterialsAndPrimitives
// manufacture one primitive with empty `indices`, which writeGlbMulti's
// hard "primitive indices must not be empty" check then rejected outright
// -- 3,807 real corpus files (particle/ribbon-only VFX models) failed this
// way. Fixed by skipping mesh output for a geometry-less LOD tier entirely
// (see cmd_export.cpp/gltf.cpp) rather than trying to represent "zero
// triangles" as a mesh at all.
TEST_CASE("husk export: a genuinely geometry-less model (0 vertices, empty .skin) exports "
          "successfully with no mesh, keeping its skeleton") {
    auto dir = defaultsDir("novtx");
    writeFile(dir / "vfx.m2", zeroVertexOneBoneM2());
    writeFile(dir / "vfx00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "vfx.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no renderable geometry") != std::string::npos);
    CHECK(result.output.find("bad_alloc") == std::string::npos);
    CHECK(fs::exists(dir / "vfx.glb"));
    CHECK(fs::file_size(dir / "vfx.glb") > 0);

    fs::remove_all(dir);
}

// A model with genuinely nothing to export at all -- 0 vertices, 0 bones --
// still fails loudly (writeGlbMulti's "meshes must not be empty without a
// skeleton to fall back to" case): there's no mesh *and* no skeleton for the
// zero-mesh path above to fall back to, so this isn't the real corpus shape
// (every real particle-only file found in the corpus had at least one bone)
// and should stay a hard error rather than silently emitting an empty glTF.
TEST_CASE("husk export: a model with 0 vertices and 0 bones (nothing at all to export) still "
          "fails cleanly") {
    auto dir = defaultsDir("nothingatall");
    writeFile(dir / "nothing.m2", minimalMd20());
    writeFile(dir / "nothing00.skin", emptySkin());

    auto result = runHusk("export " + (dir / "nothing.m2").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("bad_alloc") == std::string::npos);

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

// --skel/--textures/--anim/--skin-dir three(-plus)-state coverage
// (DESIGN.md's "CLI argument grammar for export") not already exercised above.

TEST_CASE("husk export: --skel none forces an unskinned mesh even when a same-basename .skel "
          "exists next to the model") {
    auto dir = defaultsDir("skelnone");
    writeFile(dir / "rigged.m2", tinyValidM2());  // inline bones empty
    writeFile(dir / "rigged00.skin", tinyMatchingSkin());
    // Same fixture as the "0-inline-bone model... resolves the skeleton
    // automatically" test above, minus the flag -- this one's whole point
    // is that --skel none must still ignore it.
    writeFile(dir / "rigged.skel", buildSkel({{-1, -1}}));

    auto result = runHusk("export " + (dir / "rigged.m2").string() + " --skel none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("found and using") == std::string::npos);
    CHECK(result.output.find("bones") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --anim auto (explicit) produces the identical clip count as the "
          "default (omitted) case -- 'auto' is genuinely the default, not merely documented as "
          "one") {
    auto m2Path = tempPath("animated-anim-auto.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("animated-anim-auto.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("animated-anim-auto.glb").string() + " --anim auto");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim inline still resolves a model's own inline + global-sequence "
          "clips (only external-directory resolution is what --anim inline turns off)") {
    auto m2Path = tempPath("anim-inline-still-inline.m2");
    writeFile(m2Path, tinyAnimatedM2());
    auto skinPath = tempPath("anim-inline-still-inline.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-inline-still-inline.glb").string() + " --anim inline");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 animation(s)") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: --anim inline skips external-directory resolution entirely -- an "
          "external sequence's otherwise-resolvable --anim <dir> file is ignored, even sitting "
          "right there") {
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

    auto m2Path = tempPath("anim-inline-ignores-external.m2");
    writeFile(m2Path, file);
    auto skinPath = tempPath("anim-inline-ignores-external.skin");
    writeFile(skinPath, tinyMatchingSkin());
    auto animDir = fs::temp_directory_path() / "husk-cli-test-anim-inline-dir";
    fs::create_directories(animDir);
    writeFile(animDir / "777.anim", tinyAnimFile());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-inline-ignores-external.glb").string() +
                           " --anim inline");
    CHECK(result.exitCode == 0);
    // Same model/skin/AFID/anim-dir fixture as the "--anim <dir> resolves
    // an external sequence" test above, which asserts "1 animation(s)" for
    // the exact same file layout with --anim <dir> instead -- the only
    // difference here is the flag value, proving --anim inline really does
    // suppress the external lookup rather than happening to find nothing.
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove_all(animDir);
}

TEST_CASE("husk export: --anim none produces zero animation clips, but JOINTS_0/WEIGHTS_0 (the "
          "bind-pose skin) are still present when the model has bones") {
    auto m2Path = tempPath("anim-none.m2");
    writeFile(m2Path, tinyAnimatedM2());  // has 1 bone + 1 real inline sequence
    auto skinPath = tempPath("anim-none.skin");
    writeFile(skinPath, tinyMatchingSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("anim-none.glb").string() + " --anim none");
    CHECK(result.exitCode == 0);
    // Bones (and thus JOINTS_0/WEIGHTS_0 -- buildSkinning runs whenever
    // bones aren't empty, unconditionally on --anim) still print; only the
    // animation-clip resolution that --anim none suppresses is gone.
    CHECK(result.output.find("1 bones") != std::string::npos);
    CHECK(result.output.find("animation(s)") == std::string::npos);
    CHECK(result.output.find("bind pose only, no animation") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
}

TEST_CASE("husk export: -i/--input and -o/--output work identically as named flags and as bare "
          "positionals") {
    auto dir = defaultsDir("namedflags");
    writeFile(dir / "flagged.m2", tinyValidM2());
    writeFile(dir / "flagged00.skin", tinyMatchingSkin());

    auto outPath = dir / "explicit-out.glb";
    auto result =
        runHusk("export -i " + (dir / "flagged.m2").string() + " -o " + outPath.string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    auto outPath2 = dir / "explicit-out2.glb";
    auto result2 = runHusk("export --input " + (dir / "flagged.m2").string() + " --output " +
                            outPath2.string());
    CHECK(result2.exitCode == 0);
    CHECK(fs::exists(outPath2));

    // -o given *before* the model on the command line: proves the bare
    // positional (the model path) still binds to --input regardless of
    // where a named flag sits in argv, not just when named flags trail it.
    auto outPath3 = dir / "explicit-out3.glb";
    auto result3 =
        runHusk("export -o " + outPath3.string() + " " + (dir / "flagged.m2").string());
    CHECK(result3.exitCode == 0);
    CHECK(fs::exists(outPath3));

    fs::remove_all(dir);
}

TEST_CASE("husk export: every flag accepted named, in arbitrary order") {
    auto dir = defaultsDir("allflags");
    writeFile(dir / "m.m2", tinyValidM2());
    writeFile(dir / "m.skin", tinyMatchingSkin());
    auto outPath = dir / "out.glb";

    // Every flag but --skin-dir/--lod (both only meaningful alongside
    // --skin auto -- combining them with an explicit --skin path is its
    // own rejected case, covered by its own dedicated tests above),
    // scrambled out of the order addExportOptions declares them in, proving
    // none of them depend on argv position.
    auto result = runHusk("export --anim none --textures none --skel none -o " + outPath.string() +
                           " --skin " + (dir / "m.skin").string() + " -i " +
                           (dir / "m.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(fs::exists(outPath));

    fs::remove_all(dir);
}

// `export`'s --help/-h no longer needs commands::isHelpFlag's hand-rolled
// pre-check (see commands.hpp's doc comment, and info/dump-chunks below,
// which still do) -- CLI11 recognizes -h/--help itself, anywhere in argv,
// and prints its own auto-generated help sourced from addExportOptions's
// real flag surface, not a hand-written usage block. These three tests
// check for real flag names in that output, not the old hand-written prose.
TEST_CASE("husk export --help prints CLI11's own generated help (real flag names) and exits 0, "
          "not a file-not-found error") {
    auto result = runHusk("export --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("--anim") != std::string::npos);
    CHECK(result.output.find("--lod") != std::string::npos);
    CHECK(result.output.find("couldn't open") == std::string::npos);
}

TEST_CASE("husk export -h (shorthand) prints the same CLI11-generated help and exits 0") {
    auto result = runHusk("export -h");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
    CHECK(result.output.find("--anim") != std::string::npos);
}

TEST_CASE("husk export <file.m2> --help (help after a positional) still prints CLI11's help, "
          "without ever trying to open the (nonexistent) model path") {
    auto result = runHusk("export nonexistent-model.m2 --help");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--skin") != std::string::npos);
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

// Remaining CLI argv edge cases (FINDINGS.md §4.3): each subcommand's argc
// guard. export's own guard is now CLI11's own machinery -- --input is
// ->required() (addExportOptions), and a flag given with no value is a
// real CLI11 parse-time error with CLI11's own named exit code (see
// CLI::ExitCodes in /nix/store/*-cli11-*/include/CLI/Error.hpp), not the
// old code's uniform "usage: husk export" text + exit 1. info/dump-chunks
// weren't touched by this migration, so their argc guards below are
// unchanged.

TEST_CASE("husk export with no arguments at all fails via CLI11's RequiredError (--input is "
          "required), not the old hand-written usage text") {
    auto result = runHusk("export");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--input") != std::string::npos);
    CHECK(result.output.find("required") != std::string::npos);
}

TEST_CASE("husk export --textures with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --textures");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--textures") != std::string::npos);
}

TEST_CASE("husk export --skin-dir with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --skin-dir");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skin-dir") != std::string::npos);
}

TEST_CASE("husk export --anim with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --anim");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--anim") != std::string::npos);
}

TEST_CASE("husk export --skel with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --skel");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--skel") != std::string::npos);
}

TEST_CASE("husk export --lod with no value fails via CLI11's own parse-time error") {
    auto result = runHusk("export some.m2 --lod");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--lod") != std::string::npos);
}

TEST_CASE("husk export with a 3rd bare positional fails via CLI11's ExtrasError -- only -i/-o "
          "have a positional fallback (see addExportOptions's 'input'/'output' names), so a 3rd "
          "bare word is always rejected, regardless of how many named flags exist (replaces the "
          "old 'more than 4 positionals' test -- there's no 4th positional slot left to overflow "
          "into anymore)") {
    auto result = runHusk("export some.m2 a.glb extra-positional");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("not expected") != std::string::npos);
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("corrupted .skin?") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch materialIndex out of range for materials fails cleanly") {
    auto dir = defaultsDir("badmaterial");
    writeFile(dir / "m.m2", tinyValidM2());       // 0 materials
    writeFile(dir / "m.skin", oneBatchSkin({}));  // materialIndex defaults to 0

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("materialIndex (0) is out of range for 0 materials") !=
          std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch colorIndex out of range for colors fails cleanly") {
    auto dir = defaultsDir("badcolor");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 1 material, 0 colors
    writeFile(dir / "m.skin", oneBatchSkin({.colorIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("colorIndex (0) is out of range for 0 colors") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureWeightComboIndex out of range for textureWeightCombos "
          "fails cleanly") {
    auto dir = defaultsDir("badweightcombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 1, 0, 0, 0));  // 1 textureWeightCombos entry
    writeFile(dir / "m.skin", oneBatchSkin({.textureWeightComboIndex = 5}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("texture weight (index 99 via textureWeightCombos[0]) is out of "
                              "range for 0 textureWeights entries") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: batch textureComboIndex out of range for textureCombos fails cleanly") {
    auto dir = defaultsDir("badtexturecombo");
    writeFile(dir / "m.m2", materialsFixtureM2(1, 0, 0, 0, 0, 0, 0));  // 0 textureCombos
    writeFile(dir / "m.skin", oneBatchSkin({.textureCount = 1, .textureComboIndex = 0}));

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
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

    auto result = runHusk("export " + (dir / "m.m2").string() + " --skin " + (dir / "m.skin").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("UV transform") == std::string::npos);

    fs::remove_all(dir);
}
