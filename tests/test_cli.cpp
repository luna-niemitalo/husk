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

TEST_CASE("husk export: 'auto' .skin path without --skin-dir fails cleanly") {
    auto m2Path = tempPath("auto-no-skindir.m2");
    writeFile(m2Path, chunkedM2WithSfid(tinyValidM2(), {12345}));

    auto result = runHusk("export " + m2Path.string() + " auto " +
                           tempPath("auto-no-skindir.glb").string());
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("--skin-dir") != std::string::npos);

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
