// Tests for husk::m2::Model / loadModel / loadModelFile (src/m2_model.hpp/
// .cpp) -- the whole-file parsed aggregate, REFACTOR/AUDIT.md §2.1. Pure
// addition: no existing command consumes this type yet (see
// REFACTOR_LOG.md's entry for this task), so these tests cover the type
// itself, not any cmd_*.cpp behavior.
//
// Reuses test_m2_fixtures.hpp's buildMd20Blob()/checkSentinelHeader() --
// that fixture already builds a fully-header-populated MD20 blob whose
// array descriptors all point at offsets (1000+) past the buffer's own
// ~312-byte length, which every other test_m2_*.cpp file relies on being
// *unreachable* (they only ever check the header fields themselves via
// checkSentinelHeader, never dereference the arrays). That happens to be
// exactly the fixture this file needs for the failure-isolation test
// below: every array loadModel dereferences is genuinely malformed at
// once, for free, with no separate "malformed" fixture to author and keep
// in sync.

#include "test_data_paths.hpp"
#include "test_m2_fixtures.hpp"

// A minimal but fully *valid* MD20 blob: every M2Array field left at its
// zero-initialized {count=0, offset=0} (default-constructed husk::m2::Array
// wire value), matching a genuinely empty M2 -- decorative props/quest
// objects with no bones/textures/animation are a real, common shape, not a
// contrived edge case. globalFlags is 0, so
// GlobalFlag::kUseTextureCombinerCombos is unset and header.
// textureCombinerCombos also stays {0,0} (parseHeader only wire-populates
// it when that bit is set).
std::vector<uint8_t> buildEmptyMd20Blob() {
    std::vector<uint8_t> buf(kFixedHeaderSize, 0);
    std::memcpy(buf.data(), "MD20", 4);
    putU32(buf, 0x004, 274);  // version -- >= kMinVerifiedParticleVersion (272)
    return buf;
}

TEST_CASE("loadModel: a genuinely empty (but valid) M2 parses cleanly, every field empty, zero "
          "failures") {
    auto model = husk::m2::loadModel(buildEmptyMd20Blob());

    CHECK(model.header.version == 274);
    CHECK(model.header.name.empty());
    CHECK(model.blob.size() == kFixedHeaderSize);

    CHECK(model.globalLoops.empty());
    CHECK(model.sequences.empty());
    CHECK(model.sequenceLookup.empty());
    CHECK(model.bones.empty());
    CHECK(model.boneLookup.empty());
    CHECK(model.boneCombos.empty());
    CHECK(model.vertices.empty());
    CHECK(model.colors.empty());
    CHECK(model.textures.empty());
    CHECK(model.textureWeights.empty());
    CHECK(model.textureTransforms.empty());
    CHECK(model.textureLookup.empty());
    CHECK(model.materials.empty());
    CHECK(model.textureCombos.empty());
    CHECK(model.textureCoordCombos.empty());
    CHECK(model.textureWeightCombos.empty());
    CHECK(model.textureTransformCombos.empty());
    CHECK(model.collisionMesh.positions.empty());
    CHECK(model.collisionMesh.indices.empty());
    CHECK(model.collisionMesh.faceNormals.empty());
    CHECK(model.attachments.empty());
    CHECK(model.attachmentLookup.empty());
    CHECK(model.events.empty());
    CHECK(model.lights.empty());
    CHECK(model.cameraLookup.empty());
    CHECK(model.ribbonEmitters.empty());
    CHECK(model.particleEmitters.empty());
    CHECK(model.textureCombinerCombos.empty());

    // The actual point of an empty-but-*valid* file: no field being empty
    // is a parse failure -- every array's own count was genuinely 0, never
    // attempted-and-caught. See the malformed-fixture test below for the
    // contrasting case.
    CHECK(model.parseFailures.empty());
}

TEST_CASE("loadModel: below kMinVerifiedParticleVersion, particle_emitters is skipped rather than "
          "attempted -- not a parse failure") {
    auto buf = buildEmptyMd20Blob();
    putU32(buf, 0x004, 100);       // well below kMinVerifiedParticleVersion (272)
    putArray(buf, 0x128, 1, 99999);  // particleEmitters -- would throw if ever dereferenced

    auto model = husk::m2::loadModel(buf);

    CHECK(model.particleEmitters.empty());
    for (const auto& f : model.parseFailures) {
        CHECK(f.field != "particle_emitters");
    }
}

TEST_CASE("loadModel: every dereferenced array in AN OTHERWISE-MALFORMED file fails "
          "independently -- one bad section does not blank out the rest of the model") {
    // buildMd20Blob() (test_m2_fixtures.hpp): a fully header-populated MD20
    // blob whose arrays all claim real, nonzero counts at offsets (1000+)
    // that don't exist in its own ~312-byte buffer -- every array below is
    // therefore genuinely malformed (out of bounds), while the header
    // itself parses perfectly cleanly. This is exactly the shape a real
    // corpus file with one corrupted section but an otherwise-intact
    // header would take.
    auto buf = buildMd20Blob();
    auto model = husk::m2::loadModel(buf);

    // The header/blob half of the contract: still fully populated,
    // completely unaffected by every array failure below it.
    checkSentinelHeader(model.header);
    CHECK(model.blob.size() == buf.size());

    // Every array field this model dereferences is left empty --
    // default-constructed, not partially written.
    CHECK(model.globalLoops.empty());
    CHECK(model.sequences.empty());
    CHECK(model.bones.empty());
    CHECK(model.vertices.empty());
    CHECK(model.materials.empty());
    CHECK(model.collisionMesh.positions.empty());
    CHECK(model.particleEmitters.empty());

    // texture_combiner_combos is the one field buildMd20Blob() doesn't
    // populate at all (globalFlags 0x1234 doesn't set
    // GlobalFlag::kUseTextureCombinerCombos, bit 0x8) -- header.
    // textureCombinerCombos stays a genuine {0,0}, so this one field
    // parses (trivially) cleanly rather than failing, proving the failure
    // list isn't just "everything is unconditionally marked failed".
    CHECK(model.textureCombinerCombos.empty());

    // 25 of the 26 fields loadModel attempts fail (every one this test
    // file's own header comment enumerates except texture_combiner_combos,
    // which never gets attempted with nonzero bounds at all -- see above).
    // An exact count, not just "non-empty", so a future field added to
    // Model without updating this fixture's own offsets shows up here as a
    // changed number, not a silently-still-passing test.
    CHECK(model.parseFailures.size() == 25);

    auto hasFailure = [&](const std::string& field) {
        for (const auto& f : model.parseFailures) {
            if (f.field == field) return true;
        }
        return false;
    };
    CHECK(hasFailure("global_loops"));
    CHECK(hasFailure("sequences"));
    CHECK(hasFailure("sequence_lookup"));
    CHECK(hasFailure("bones"));
    CHECK(hasFailure("bone_lookup"));
    CHECK(hasFailure("bone_combos"));
    CHECK(hasFailure("vertices"));
    CHECK(hasFailure("colors"));
    CHECK(hasFailure("textures"));
    CHECK(hasFailure("texture_weights"));
    CHECK(hasFailure("texture_transforms"));
    CHECK(hasFailure("texture_lookup"));
    CHECK(hasFailure("materials"));
    CHECK(hasFailure("texture_combos"));
    CHECK(hasFailure("texture_coord_combos"));
    CHECK(hasFailure("texture_weight_combos"));
    CHECK(hasFailure("texture_transform_combos"));
    CHECK(hasFailure("collision_mesh"));
    CHECK(hasFailure("attachments"));
    CHECK(hasFailure("attachment_lookup"));
    CHECK(hasFailure("events"));
    CHECK(hasFailure("lights"));
    CHECK(hasFailure("camera_lookup"));
    CHECK(hasFailure("ribbon_emitters"));
    CHECK(hasFailure("particle_emitters"));
    CHECK(!hasFailure("texture_combiner_combos"));

    // Every failure carries the real ParseError message, not an empty
    // placeholder -- CLAUDE.md's "on failure, always print expected and
    // actual values" reaching the caller via Model rather than stderr.
    for (const auto& f : model.parseFailures) {
        CHECK(!f.what.empty());
    }
}

TEST_CASE("loadModel: a header/blob-level failure (bad magic) still throws -- there is no Model "
          "without a header") {
    std::vector<uint8_t> bad = {'B', 'A', 'D', '!', 0, 0, 0, 0};
    CHECK_THROWS_AS(husk::m2::loadModel(bad), husk::m2::ParseError);
}

TEST_CASE("loadModelFile: nonexistent path throws ParseError") {
    CHECK_THROWS_AS(husk::m2::loadModelFile("/nonexistent/path/to/nothing.m2"), husk::m2::ParseError);
}

TEST_CASE("loadModel: real fixture (bloodelffemale.m2) -- every array's size matches its own "
          "header count, zero parse failures" *
          doctest::skip(husk::test::testM2().empty())) {
    auto model = husk::m2::loadModelFile(husk::test::testM2());

    CHECK(model.parseFailures.empty());

    // Cross-checked against test_cli_info_json.cpp's own real-fixture
    // assertions on the same file (vertices count 8061, particle_emitters
    // count 0) rather than re-deriving new magic numbers here.
    CHECK(model.vertices.size() == 8061);
    CHECK(model.particleEmitters.empty());

    // Internal consistency for every other dereferenced array: this
    // doesn't need independently-known ground truth, since a mismatch
    // between `header.X.count` and the actually-parsed `model.X.size()`
    // would itself be a real bug (or a real parse failure, which the
    // empty-parseFailures check above already rules out).
    CHECK(model.sequences.size() == model.header.sequences.count);
    CHECK(model.bones.size() == model.header.bones.count);
    CHECK(model.textures.size() == model.header.textures.count);
    CHECK(model.materials.size() == model.header.materials.count);
    CHECK(model.attachments.size() == model.header.attachments.count);
    CHECK(model.events.size() == model.header.events.count);
    CHECK(model.lights.size() == model.header.lights.count);
    CHECK(model.ribbonEmitters.size() == model.header.ribbonEmitters.count);
}
