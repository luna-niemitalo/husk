// Shared byte-builder primitives for tests/test_cli*.cpp -- factored out
// here because nearly every one of these (tempPath/defaultsDir/writeFile/
// putU32/putU16/putF32/putTag/minimalMd20/tinyValidM2/buildSkel/fillTrack/
// etc.) is used by 3+ of the split files (see FILE_SPLIT_TODO.md Item 5).
// Anonymous namespace: each including TU gets its own private copy, same as
// when these lived inline in the pre-split tests/test_cli.cpp. `runHusk`
// itself is NOT re-exported here -- each .cpp still does its own
// `#include "run_husk.hpp"` / `using husk::test::runHusk;`, since these
// builders never call it.
//
// Split further (FILE_SPLIT_TODO.md's post-completion audit -- this file
// was still over the 1000-line hard limit after Item 5's own split) into
// this file (true cross-cutting primitives: raw byte builders, and the
// minimal M2/.skin fixtures nearly every test_cli*.cpp file builds on) and
// tests/test_cli_fixtures_scenes.hpp (composite, scenario-specific
// fixtures -- one real M2+.skin shape for one particular family of tests --
// each used by only 1-2 of the split .cpp files). The scenes file includes
// this one and depends on several of these primitives (tinyValidM2/putU16/
// putArrayAt/fillTrack/vec3Bytes/appendChunkTo/buildSkel); nothing here
// depends on anything in the scenes file, so this file can be included
// alone by any .cpp that only needs the primitives.
#pragma once

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
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

void appendChunkTo(std::vector<uint8_t>& file, const char* tag, const std::vector<uint8_t>& payload) {
    putTag(file, tag);
    putU32(file, static_cast<uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}


}  // namespace
