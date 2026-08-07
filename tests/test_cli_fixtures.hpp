// Shared byte-builder helpers for tests/test_cli*.cpp -- factored out here
// because nearly every one of these (tempPath/defaultsDir/writeFile/putU32/
// putU16/putF32/putTag/minimalMd20/tinyValidM2/etc.) is used by 3+ of the
// split files (see FILE_SPLIT_TODO.md Item 5). Anonymous namespace: each
// including TU gets its own private copy, same as when these lived inline in
// the pre-split tests/test_cli.cpp. `runHusk` itself is NOT re-exported here
// -- each .cpp still does its own `#include "run_husk.hpp"` /
// `using husk::test::runHusk;`, since these builders never call it.
#pragma once

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace {

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

void putF32(std::vector<uint8_t>& b, float v) {
    uint8_t bytes[4];
    std::memcpy(bytes, &v, 4);
    b.insert(b.end(), bytes, bytes + 4);
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
// REQUIRE guards against silently building the wrong-sized fixture.
// TODO: Remove: the exact mistake this generator's ad-hoc predecessor made
// during manual testing, see FAILURES.md's history.
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

// tinyValidM2() plus one real collision triangle (3 positions/3 indices/1
// face normal), for --collision's own tests -- tinyValidM2() alone leaves
// collisionPositions/collisionIndices at count 0, so the collision-mesh
// block in cmd_export.cpp never fires against it.
// TODO: Remove: BLENDER_EXPORT_TODO.md §3.
std::vector<uint8_t> tinyValidM2WithCollision() {
    auto b = tinyValidM2();

    uint32_t positionsOffset = static_cast<uint32_t>(b.size());
    putF32(b, 0.0f); putF32(b, 0.0f); putF32(b, 0.0f);
    putF32(b, 1.0f); putF32(b, 0.0f); putF32(b, 0.0f);
    putF32(b, 0.0f); putF32(b, 1.0f); putF32(b, 0.0f);

    uint32_t indicesOffset = static_cast<uint32_t>(b.size());
    putU16(b, 0); putU16(b, 1); putU16(b, 2);

    uint32_t faceNormalsOffset = static_cast<uint32_t>(b.size());
    putF32(b, 0.0f); putF32(b, 0.0f); putF32(b, 1.0f);

    uint32_t indicesCount = 3, positionsCount = 3, faceNormalsCount = 1;
    std::memcpy(b.data() + 0x0D8, &indicesCount, 4);
    std::memcpy(b.data() + 0x0DC, &indicesOffset, 4);
    std::memcpy(b.data() + 0x0E0, &positionsCount, 4);
    std::memcpy(b.data() + 0x0E4, &positionsOffset, 4);
    std::memcpy(b.data() + 0x0E8, &faceNormalsCount, 4);
    std::memcpy(b.data() + 0x0EC, &faceNormalsOffset, 4);
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
// skinSectionId/geoset regression test below -- needs two real vertices
// (one per submesh) and at least one material for two batches to resolve
// against.
// TODO: Remove: FAILURES2.md #1.
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
// exercises cmd_export.cpp's distinct-geoset-count note without needing any
// texture/color/weight combo tables.
// TODO: Remove: FAILURES2.md #1.
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
// out-of-range tests below: one submesh (skinSectionId = 0, indexStart = 0,
// indexCount = submeshIndexCount) and one batch built from `fields`. Every
// earlier check in buildMaterialsAndPrimitives's own chain
// (skinSectionIndex -> submesh index range -> materialIndex -> colorIndex
// -> textureWeightComboIndex[+resolved weightIndex] ->
// textureComboIndex[+resolved textureIndex] -> textureCoordComboIndex)
// must pass for a later one to even be reached, so each test below keeps
// every field before the one under test valid and deliberately breaks
// only that one.
// TODO: Remove: FINDINGS.md §4.2.
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
// m2::trackHasAnimatedData's outer.count > 1 case). Content of the two
// claimed sub-arrays is irrelevant; trackHasAnimatedData only inspects
// outer.count once it's already > 1. Requires colorCount >= 1.
// TODO: Remove: FINDINGS.md §3.2.
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
// textureTransformCombos table pointing at it -- the export-side
// regression test fixture below. Pair with
// oneBatchSkin({.textureTransformComboIndex = 0}).
// TODO: Remove: FINDINGS.md §3.1.
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
// 0x40-byte record shape tests/test_m2.cpp's putSequence uses. See
// TEST_DESIGN.md#Independent-transcription-convention for why this is
// transcribed fresh here rather than shared.
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

// Same shape as oneTexturedModel() but the single M2Texture has a real
// `type` (a hardcoded/runtime-resolved slot, wowdev.wiki M2#Textures) and
// no TXID chunk at all -- fdid stays 0, exercising the textureTypeName
// material-naming path and the fuzzy-filename-match fallback (neither of
// which oneTexturedModel()'s type=0/fdid-resolvable shape can reach).
std::vector<uint8_t> oneTexturedModelWithType(uint32_t type) {
    auto md20 = tinyValidM2();
    uint32_t one = 1;
    uint32_t texOff = static_cast<uint32_t>(md20.size());
    md20.resize(texOff + 16, 0);
    std::memcpy(md20.data() + texOff, &type, 4);  // M2Texture::type
    std::memcpy(md20.data() + 0x050, &one, 4);     // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset
    uint32_t matOff = static_cast<uint32_t>(md20.size());
    md20.resize(matOff + 4, 0);
    std::memcpy(md20.data() + 0x070, &one, 4);     // materials.count
    std::memcpy(md20.data() + 0x074, &matOff, 4);  // materials.offset
    uint32_t comboOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0);
    std::memcpy(md20.data() + 0x080, &one, 4);      // textureCombos.count
    std::memcpy(md20.data() + 0x084, &comboOff, 4);  // textureCombos.offset

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
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

// Two texture slots in one model, sharing one material: texture[0] is
// hardcoded (type=6, no TXID entry, fdid stays 0 -- exercises the fuzzy-
// match-only path); texture[1] is real/FileDataID-resolvable (type=0, a
// TXID entry). Regression fixture for the mixed-priority-order fix below:
// before it, letting *every* slot draw from the shared fuzzy pool (not
// just fdid==0 ones) let texture[0]'s hardcoded slot steal a real-named
// file that a later-processed, fdid-resolvable slot should have kept its
// own deterministic "<fdid>.png" match for instead.
std::vector<uint8_t> twoTexturedModel(uint32_t fileDataId) {
    auto md20 = tinyValidM2();
    uint32_t texOff = static_cast<uint32_t>(md20.size());
    md20.resize(texOff + 32, 0);  // 2x M2Texture (type/flags/filename M2Array)
    uint32_t hardcodedType = 6;   // TEX_COMPONENT_CHAR_HAIR
    std::memcpy(md20.data() + texOff, &hardcodedType, 4);  // textures[0].type
    // textures[1].type left at 0 (real/embedded-or-TXID-resolvable)
    uint32_t two = 2;
    std::memcpy(md20.data() + 0x050, &two, 4);     // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset

    uint32_t matOff = static_cast<uint32_t>(md20.size());
    md20.resize(matOff + 4, 0);  // 1 M2Material, shared by both batches
    uint32_t one = 1;
    std::memcpy(md20.data() + 0x070, &one, 4);
    std::memcpy(md20.data() + 0x074, &matOff, 4);

    uint32_t comboOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0);  // textureCombos[0] -> texture index 0 (hardcoded)
    putU16(md20, 1);  // textureCombos[1] -> texture index 1 (fdid-resolvable)
    std::memcpy(md20.data() + 0x080, &two, 4);       // textureCombos.count
    std::memcpy(md20.data() + 0x084, &comboOff, 4);  // textureCombos.offset

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    putTag(file, "TXID");
    putU32(file, 8);
    putU32(file, 0);           // textures[0]: not file-based
    putU32(file, fileDataId);  // textures[1]'s FileDataID
    return file;
}

// A .skin with two submesh/batch pairs over twoTexturedModel()'s two
// texture slots -- batch 0 -> textureComboIndex 0 (hardcoded), batch 1 ->
// textureComboIndex 1 (fdid-resolvable). Same degenerate single-vertex
// triangle shared by both submeshes as oneTexturedModelSkin() uses.
std::vector<uint8_t> twoBatchSkin() {
    std::vector<uint8_t> skin;
    putTag(skin, "SKIN");
    skin.resize(44, 0);
    auto patchArray = [&](size_t off, uint32_t count, uint32_t offset) {
        std::memcpy(skin.data() + off, &count, 4);
        std::memcpy(skin.data() + off + 4, &offset, 4);
    };

    uint32_t submeshOff = static_cast<uint32_t>(skin.size());
    skin.resize(skin.size() + 2 * 0x30, 0);
    for (int i = 0; i < 2; ++i) {
        uint16_t indexStart = 0, indexCount = 3;
        std::memcpy(skin.data() + submeshOff + i * 0x30 + 0x08, &indexStart, 2);
        std::memcpy(skin.data() + submeshOff + i * 0x30 + 0x0A, &indexCount, 2);
    }
    patchArray(0x1C, 2, submeshOff);

    uint32_t batchOff = static_cast<uint32_t>(skin.size());
    skin.resize(skin.size() + 2 * 0x18, 0);
    uint16_t zero16 = 0, one16 = 1, noColor = 0xFFFF;
    for (int i = 0; i < 2; ++i) {
        uint16_t submeshIndex = static_cast<uint16_t>(i);
        uint16_t comboIndex = static_cast<uint16_t>(i);
        std::memcpy(skin.data() + batchOff + i * 0x18 + 0x04, &submeshIndex, 2);
        std::memcpy(skin.data() + batchOff + i * 0x18 + 0x08, &noColor, 2);   // colorIndex: none
        std::memcpy(skin.data() + batchOff + i * 0x18 + 0x0A, &zero16, 2);   // materialIndex: 0 (shared)
        std::memcpy(skin.data() + batchOff + i * 0x18 + 0x0E, &one16, 2);    // textureCount: 1
        std::memcpy(skin.data() + batchOff + i * 0x18 + 0x10, &comboIndex, 2);  // textureComboIndex
    }
    patchArray(0x24, 2, batchOff);

    uint32_t vertOff = static_cast<uint32_t>(skin.size());
    putU16(skin, 0);
    patchArray(0x04, 1, vertOff);

    uint32_t idxOff = static_cast<uint32_t>(skin.size());
    putU16(skin, 0);
    putU16(skin, 0);
    putU16(skin, 0);
    patchArray(0x0C, 3, idxOff);

    patchArray(0x14, 0, 0);
    return skin;
}

// Shared by every --bones-dir test below: a 0-inline-bone model + a .skel
// with one root bone (index 0) and a BFID chunk declaring FileDataID
// 424242 -- the minimum shape needed to prove --bones-dir's resolution
// (model/.skel's BFID array -> '<dir>/<FileDataID>.bone'). Also shared with
// tests/test_cli_phys.cpp's --phys cases, which need a real .skel next to
// the model for their own bone-index cross-checks.
std::vector<uint8_t> boneCorrectionSkel() {
    auto skel = buildSkel({{-1, -1}});
    std::vector<uint8_t> bfid;
    putU32(bfid, 424242);
    appendChunkTo(skel, "BFID", bfid);
    return skel;
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

// A tinyValidM2() (1 vertex) with one material, one texture (type=0, empty
// filename), and a 1-entry textureCombos table pointing at it -- the
// minimum an M2 needs for a batch's textureComboIndex to resolve at all.
// Used by the multi-texture-batch regression test in tests/test_cli.cpp.
// TODO: Remove: FAILURES2.md #6.
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
// env-mapped "shine" pass), which husk only ever resolves the first of.
// Pairs with oneTextureOneMaterialM2().
// TODO: Remove: FAILURES2.md #6.
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

}  // namespace
