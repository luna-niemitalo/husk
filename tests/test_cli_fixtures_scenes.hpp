// Composite, scenario-specific byte-builder fixtures for tests/
// test_cli*.cpp -- unlike test_cli_fixtures.hpp's cross-cutting primitives
// (used by 3+ split files), each fixture here builds one real, complete M2/
// .skin/.skel/.phys/.bone/.anim shape for one particular family of tests
// (materials/animated-tint, inline/external-sequence animation, hardcoded-
// vs-FileDataID texture resolution, bone/phys correction data), each used
// by only 1-2 of the split .cpp files -- see FILE_SPLIT_TODO.md's post-
// completion audit for why this file exists (test_cli_fixtures.hpp was
// still over the 1000-line hard limit after Item 5's own split). Depends on
// test_cli_fixtures.hpp's primitives (tinyValidM2/putU16/putArrayAt/
// fillTrack/vec3Bytes/appendChunkTo/buildSkel/etc.) -- always include that
// header first. Anonymous namespace, same rationale as that file's own doc
// comment. `runHusk` itself is NOT re-exported here, same as
// test_cli_fixtures.hpp.
#pragma once

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

#include "test_cli_fixtures.hpp"

namespace {

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

// Two *both-hardcoded* texture slots (no TXID chunk at all, fdid stays 0
// for both), of independently chosen M2Texture::type values, sharing one
// material -- regression fixture for export_materials.cpp's per-slot
// fuzzy-candidate category filter: two slots of genuinely different types
// (e.g. skin vs. char_jewelry) must each only ever see the candidates
// compatible with *their own* type, never the other slot's, even though
// both draw from one shared same-basename pool.
std::vector<uint8_t> twoHardcodedTexturedModel(uint32_t type0, uint32_t type1) {
    auto md20 = tinyValidM2();
    uint32_t texOff = static_cast<uint32_t>(md20.size());
    md20.resize(texOff + 32, 0);  // 2x M2Texture (type/flags/filename M2Array)
    std::memcpy(md20.data() + texOff, &type0, 4);         // textures[0].type
    std::memcpy(md20.data() + texOff + 16, &type1, 4);    // textures[1].type
    uint32_t two = 2;
    std::memcpy(md20.data() + 0x050, &two, 4);     // textures.count
    std::memcpy(md20.data() + 0x054, &texOff, 4);  // textures.offset

    uint32_t matOff = static_cast<uint32_t>(md20.size());
    md20.resize(matOff + 4, 0);  // 1 M2Material, shared by both batches
    uint32_t one = 1;
    std::memcpy(md20.data() + 0x070, &one, 4);
    std::memcpy(md20.data() + 0x074, &matOff, 4);

    uint32_t comboOff = static_cast<uint32_t>(md20.size());
    putU16(md20, 0);  // textureCombos[0] -> texture index 0
    putU16(md20, 1);  // textureCombos[1] -> texture index 1
    std::memcpy(md20.data() + 0x080, &two, 4);       // textureCombos.count
    std::memcpy(md20.data() + 0x084, &comboOff, 4);  // textureCombos.offset

    std::vector<uint8_t> file;
    putTag(file, "MD21");
    putU32(file, static_cast<uint32_t>(md20.size()));
    file.insert(file.end(), md20.begin(), md20.end());
    return file;  // no TXID chunk -- both slots stay fdid == 0
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
