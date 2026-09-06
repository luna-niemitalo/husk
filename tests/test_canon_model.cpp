// Structural convergence proof for canon::assembleModel (canon_model.hpp) --
// the first whole-model composition root, wiring together every already
// independently-convergence-proven canon:: piece (assembleSkeleton,
// assembleMesh, assembleMaterial, assembleBoneAnimation) for one real M2 +
// .skin fixture. This file checks assembleModel's own wiring (sizes, index
// correspondence, filter conditions), not those pieces' internal logic --
// each already has its own dedicated convergence test.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_model.hpp"
#include "m2.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

constexpr uint32_t kSequenceStoredInlineFlag = 0x20;  // mirrors canon_model.cpp's own private constant

}  // namespace

TEST_CASE("canon::assembleModel wires together skeleton/mesh/materials/animations on a real "
          "fixture" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    REQUIRE(!model.bones.empty());
    REQUIRE(!batches.empty());

    canon::Model result = canon::assembleModel(model, batches, submeshes, triangleIndices);

    CHECK(result.skeleton.joints.size() == model.bones.size());

    // materials.size() == mesh.primitives.size(): the stated 1:1
    // correspondence (canon_model.hpp's own Model::materials doc comment).
    CHECK(result.materials.size() == result.mesh.primitives.size());

    // Independently counted real inline-sequence count -- same bit test
    // assembleModel itself uses (canon_model.cpp's isRealInlineSequence),
    // counted here from the raw parsed sequence array rather than by
    // reading result.animations.size() back at itself.
    size_t expectedInlineCount = 0;
    for (const auto& seq : model.sequences) {
        if ((seq.flags & kSequenceStoredInlineFlag) != 0) ++expectedInlineCount;
    }
    CHECK(result.animations.size() == expectedInlineCount);

    if (!result.animations.empty()) {
        const auto& clip = result.animations.front();
        CHECK(clip.boneCurves.size() == result.skeleton.joints.size());

        // Spot-check bone 0's curves for this clip against
        // assembleBoneAnimation's own already-proven output directly --
        // reusing a proven fact, not deriving from the function under test.
        auto expected = canon::assembleBoneAnimation(model.blob, model.bones[0], 0, clip.sequence.index);
        REQUIRE(clip.boneCurves[0].has_value() == expected.has_value());
        if (expected.has_value()) {
            CHECK(clip.boneCurves[0]->translation.keyframes.size() == expected->translation.keyframes.size());
            CHECK(clip.boneCurves[0]->rotation.keyframes.size() == expected->rotation.keyframes.size());
            CHECK(clip.boneCurves[0]->scale.keyframes.size() == expected->scale.keyframes.size());
        }
    }
}

TEST_CASE("canon::assembleModel throws on a corrupted bone index, mirroring assembleMesh") {
    m2::Model model;
    model.bones.resize(2);
    model.vertices.resize(1);
    model.vertices[0].boneWeights[0] = 255;
    model.vertices[0].boneIndices[0] = 9;  // out of range for 2 bones

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    CHECK_THROWS_AS(canon::assembleModel(model, batches, submeshes, {}), std::runtime_error);
}

TEST_CASE("canon::assembleModel excludes a pure-alias (non-inline) sequence from animations") {
    m2::Model model;
    model.bones.resize(1);  // default offsets (0) all point at the zeroed track below -- see
                             // test_canon_animation_convergence.cpp's own "zeroed, not empty" note
    model.blob.assign(64, 0);
    model.vertices.clear();

    m2::Sequence inlineSeq;
    inlineSeq.flags = kSequenceStoredInlineFlag;
    m2::Sequence aliasSeq;
    aliasSeq.flags = 0x40;  // kSequenceAliasFlag, no inline bit -- pure alias
    model.sequences = {inlineSeq, aliasSeq};

    std::vector<skin::Batch> batches;
    std::vector<skin::Submesh> submeshes;

    canon::Model result = canon::assembleModel(model, batches, submeshes, {});
    REQUIRE(result.animations.size() == 1);
    CHECK(result.animations[0].sequence.index == 0);
}
