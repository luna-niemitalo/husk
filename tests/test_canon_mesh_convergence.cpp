// Structural convergence proof for canon::assemblePrimitiveGeosets
// (canon_mesh_builder.hpp) against export_materials.cpp's batch loop, the
// real production logic -- REFACTOR/README.md stage 3's gate. That
// function is too entangled with material/texture resolution (DB2/listfile/
// catalog dependencies) to call directly for just this slice, so this
// checks the canon result's structural properties against facts derived
// independently, straight from the same parsed .skin tables, rather than
// against buildMaterialsAndPrimitives's own output.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>

#include "canon_mesh_builder.hpp"
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

}  // namespace

TEST_CASE("canon::assemblePrimitiveGeosets converges with export_materials.cpp's batch loop on a "
          "real fixture" *
          doctest::skip(test::testSkin().empty())) {
    std::vector<uint8_t> file = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(file);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(file, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(file, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(file, header);

    REQUIRE(!submeshes.empty());
    REQUIRE(!batches.empty());

    std::vector<canon::PrimitiveGeoset> result =
        canon::assemblePrimitiveGeosets(batches, submeshes, triangleIndices.size());

    // Independently derived from the same parsed submeshes/batches, not
    // from canon::assemblePrimitiveGeosets's own output: the real distinct
    // skinSectionId set among batches whose submesh has real geometry.
    std::set<uint16_t> expectedIds;
    size_t expectedSkippedZeroIndexCount = 0;
    for (const auto& b : batches) {
        const auto& sm = submeshes[b.skinSectionIndex];
        if (sm.indexCount == 0) {
            ++expectedSkippedZeroIndexCount;
            continue;
        }
        expectedIds.insert(sm.skinSectionId);
    }

    CHECK(result.size() == batches.size() - expectedSkippedZeroIndexCount);

    // One CHECK per kind of fact verified, not one per primitive -- a real
    // fixture's `.skin` runs to dozens/hundreds of primitives, and
    // asserting per-primitive-per-field multiplies this test's assertion
    // count by that corpus size for no added coverage (every primitive is
    // still examined; only the assertion *count* changes). Same
    // aggregate-then-diagnose discipline test_canon_skeleton_convergence.cpp's
    // per-joint loop already establishes. A structural precondition that
    // would crash on `std::get` if false is guarded with `continue` instead
    // of `REQUIRE`.
    std::set<uint16_t> actualIds;
    std::optional<size_t> idKindMismatch, groupVariantMismatch, indexRangeMismatch;
    for (size_t i = 0; i < result.size(); ++i) {
        const auto& pg = result[i];
        if (!std::holds_alternative<canon::RecordIndex>(pg.geoset.ref.id)) {
            if (!idKindMismatch) idKindMismatch = i;
            continue;
        }
        uint32_t rawId = std::get<canon::RecordIndex>(pg.geoset.ref.id).value;
        actualIds.insert(static_cast<uint16_t>(rawId));

        // group*100+variant must reconstruct the raw id exactly -- the
        // same decomposition gltf_mesh.cpp's geoset_group/geoset_variant
        // extras use, already proven correct in canon_geoset's own tests;
        // this just confirms the real fixture's real ids decompose
        // sensibly (no negative/nonsensical values -- unsigned types make
        // "negative" impossible by construction, so this is really just a
        // roundtrip check).
        if (pg.geoset.group * 100 + pg.geoset.variant != rawId && !groupVariantMismatch) {
            groupVariantMismatch = i;
        }

        if (pg.indexStart + pg.indexCount > triangleIndices.size() && !indexRangeMismatch) {
            indexRangeMismatch = i;
        }
    }
    INFO("first primitive whose geoset.ref.id isn't a RecordIndex (if any): ", idKindMismatch.value_or(-1));
    CHECK(!idKindMismatch.has_value());
    INFO("first primitive whose group*100+variant doesn't reconstruct the raw id (if any): ",
         groupVariantMismatch.value_or(-1));
    CHECK(!groupVariantMismatch.has_value());
    INFO("first primitive whose index range runs past the triangle-index buffer (if any): ",
         indexRangeMismatch.value_or(-1));
    CHECK(!indexRangeMismatch.has_value());
    CHECK(actualIds == expectedIds);

    // Spot-check every real batch's index range directly against its
    // resolved submesh -- aggregated the same way, not one CHECK pair per
    // batch.
    std::optional<size_t> resultIndexOOR, indexStartMismatch, indexCountMismatch;
    size_t resultIdx = 0;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& sm = submeshes[batches[bi].skinSectionIndex];
        if (sm.indexCount == 0) continue;
        if (resultIdx >= result.size()) {
            if (!resultIndexOOR) resultIndexOOR = bi;
            break;
        }
        if (result[resultIdx].indexStart != sm.indexStart && !indexStartMismatch) indexStartMismatch = bi;
        if (result[resultIdx].indexCount != sm.indexCount && !indexCountMismatch) indexCountMismatch = bi;
        ++resultIdx;
    }
    INFO("first batch index where `result` ran out of entries (if any): ", resultIndexOOR.value_or(-1));
    CHECK(!resultIndexOOR.has_value());
    INFO("first batch index with a mismatched indexStart (if any): ", indexStartMismatch.value_or(-1));
    CHECK(!indexStartMismatch.has_value());
    INFO("first batch index with a mismatched indexCount (if any): ", indexCountMismatch.value_or(-1));
    CHECK(!indexCountMismatch.has_value());
}

TEST_CASE("canon::assemblePrimitiveGeosets: the real fixture's zero-indexCount submeshes, if any, "
          "are skipped" *
          doctest::skip(test::testSkin().empty())) {
    std::vector<uint8_t> file = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(file);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(file, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(file, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(file, header);

    bool hasZeroIndexCountBatch = false;
    for (const auto& b : batches) {
        if (submeshes[b.skinSectionIndex].indexCount == 0) {
            hasZeroIndexCountBatch = true;
            break;
        }
    }
    INFO("real fixture has a zero-indexCount submesh referenced by a batch: ",
         hasZeroIndexCountBatch);

    std::vector<canon::PrimitiveGeoset> result =
        canon::assemblePrimitiveGeosets(batches, submeshes, triangleIndices.size());
    std::optional<size_t> zeroCountMismatch;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i].indexCount == 0 && !zeroCountMismatch) zeroCountMismatch = i;
    }
    INFO("first result primitive with a zero indexCount (if any): ", zeroCountMismatch.value_or(-1));
    CHECK(!zeroCountMismatch.has_value());
}

TEST_CASE("canon::assemblePrimitiveGeosets throws on an out-of-range skinSectionIndex, same as "
          "export_materials.cpp's batch loop") {
    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexCount = 3;

    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 5;  // out of range for 1 submesh

    CHECK_THROWS_AS(canon::assemblePrimitiveGeosets(batches, submeshes, 100), std::runtime_error);
}

TEST_CASE("canon::assemblePrimitiveGeosets throws when a submesh's index range runs past the "
          "triangle-index buffer, same as export_materials.cpp's batch loop") {
    std::vector<skin::Submesh> submeshes(1);
    submeshes[0].indexStart = 90;
    submeshes[0].indexCount = 20;  // 90 + 20 = 110, past a 100-entry buffer

    std::vector<skin::Batch> batches(1);
    batches[0].skinSectionIndex = 0;

    CHECK_THROWS_AS(canon::assemblePrimitiveGeosets(batches, submeshes, 100), std::runtime_error);
}

TEST_CASE("canon::assemblePrimitiveGeosets skips a zero-indexCount submesh, producing no primitive "
          "for it") {
    std::vector<skin::Submesh> submeshes(2);
    submeshes[0].skinSectionId = 0;
    submeshes[0].indexStart = 0;
    submeshes[0].indexCount = 0;  // empty geoset
    submeshes[1].skinSectionId = 100;
    submeshes[1].indexStart = 0;
    submeshes[1].indexCount = 6;

    std::vector<skin::Batch> batches(2);
    batches[0].skinSectionIndex = 0;
    batches[1].skinSectionIndex = 1;

    auto result = canon::assemblePrimitiveGeosets(batches, submeshes, 6);
    REQUIRE(result.size() == 1);
    CHECK(std::get<canon::RecordIndex>(result[0].geoset.ref.id).value == 100);
    CHECK(result[0].geoset.group == 1);
    CHECK(result[0].geoset.variant == 0);
}

// ---------------------------------------------------------------------
// canon::assembleMesh
// ---------------------------------------------------------------------

TEST_CASE("canon::assembleMesh converges with the real M2 vertex data + buildSkinning-equivalent "
          "skinning, on a real fixture" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Submesh> submeshes = skin::parseSubmeshes(skinFile, header.submeshes);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    std::vector<uint32_t> triangleIndices = skin::resolveTriangleIndices(skinFile, header);

    REQUIRE(!model.vertices.empty());
    REQUIRE(!model.bones.empty());

    canon::Mesh mesh =
        canon::assembleMesh(model.vertices, model.bones.size(), batches, submeshes, triangleIndices);

    REQUIRE(mesh.positions.size() == model.vertices.size());
    REQUIRE(mesh.normals.size() == model.vertices.size());
    REQUIRE(mesh.uv0.size() == model.vertices.size());

    // Spot-check a handful of vertices -- raw M2 fields, no transform.
    std::vector<size_t> spotChecks = {0, model.vertices.size() / 3, model.vertices.size() / 2,
                                       model.vertices.size() - 1};
    for (size_t vi : spotChecks) {
        INFO("vertex index ", vi);
        const auto& v = model.vertices[vi];
        CHECK(mesh.positions[vi].x == v.pos.x);
        CHECK(mesh.positions[vi].y == v.pos.y);
        CHECK(mesh.positions[vi].z == v.pos.z);
        CHECK(mesh.normals[vi].x == v.normal.x);
        CHECK(mesh.normals[vi].y == v.normal.y);
        CHECK(mesh.normals[vi].z == v.normal.z);
        CHECK(mesh.uv0[vi].x == v.texCoords[0].x);
        CHECK(mesh.uv0[vi].y == v.texCoords[0].y);
    }

    // Independently re-derive expected skinning -- same bounds-check/
    // normalization as buildSkinning (export_skeleton.cpp), computed here
    // rather than by calling canon::assembleMesh's own output back at itself.
    bool expectSkinned = false;
    for (const auto& v : model.vertices) {
        for (int j = 0; j < 4; ++j) {
            if (v.boneWeights[j] != 0) expectSkinned = true;
        }
    }
    if (expectSkinned) {
        REQUIRE(mesh.skinning.size() == model.vertices.size());
        for (size_t vi : spotChecks) {
            INFO("vertex index ", vi);
            const auto& v = model.vertices[vi];
            for (int j = 0; j < 4; ++j) {
                REQUIRE(static_cast<size_t>(v.boneIndices[j]) < model.bones.size());
                CHECK(mesh.skinning[vi].joints[static_cast<size_t>(j)] == v.boneIndices[j]);
                CHECK(mesh.skinning[vi].weights[static_cast<size_t>(j)] ==
                      doctest::Approx(static_cast<float>(v.boneWeights[j]) / 255.0f));
            }
        }
    } else {
        CHECK(mesh.skinning.empty());
    }

    // primitives/indices: already-proven output of assemblePrimitiveGeosets,
    // reused as a known-good fact rather than re-derived from scratch.
    std::vector<canon::PrimitiveGeoset> expectedPrimitives =
        canon::assemblePrimitiveGeosets(batches, submeshes, triangleIndices.size());
    REQUIRE(mesh.primitives.size() == expectedPrimitives.size());
    // Aggregated the same way as assemblePrimitiveGeosets's own convergence
    // test above -- a real fixture's primitive count scales with the
    // corpus, not with the number of distinct facts being verified.
    std::optional<size_t> indexStartMismatch, indexCountMismatch, geosetIdMismatch;
    for (size_t i = 0; i < expectedPrimitives.size(); ++i) {
        if (mesh.primitives[i].indexStart != expectedPrimitives[i].indexStart && !indexStartMismatch) {
            indexStartMismatch = i;
        }
        if (mesh.primitives[i].indexCount != expectedPrimitives[i].indexCount && !indexCountMismatch) {
            indexCountMismatch = i;
        }
        if (std::get<canon::RecordIndex>(mesh.primitives[i].geoset.ref.id).value !=
                std::get<canon::RecordIndex>(expectedPrimitives[i].geoset.ref.id).value &&
            !geosetIdMismatch) {
            geosetIdMismatch = i;
        }
    }
    INFO("first primitive with a mismatched indexStart (if any): ", indexStartMismatch.value_or(-1));
    CHECK(!indexStartMismatch.has_value());
    INFO("first primitive with a mismatched indexCount (if any): ", indexCountMismatch.value_or(-1));
    CHECK(!indexCountMismatch.has_value());
    INFO("first primitive with a mismatched geoset id (if any): ", geosetIdMismatch.value_or(-1));
    CHECK(!geosetIdMismatch.has_value());
    CHECK(mesh.indices == triangleIndices);
}

TEST_CASE("canon::assembleMesh throws on an out-of-range bone index, same as buildSkinning") {
    std::vector<m2::Vertex> vertices(1);
    vertices[0].boneWeights[0] = 255;  // genuinely skinned -- the check must actually run
    vertices[0].boneIndices[0] = 5;    // out of range for 2 bones

    std::vector<skin::Submesh> submeshes;
    std::vector<skin::Batch> batches;

    CHECK_THROWS_AS(canon::assembleMesh(vertices, /*boneCount=*/2, batches, submeshes, {}),
                    std::runtime_error);
}

TEST_CASE("canon::assembleMesh leaves skinning empty for an all-zero-weight (unskinned) model, "
          "rather than filling it with meaningless zero entries") {
    std::vector<m2::Vertex> vertices(3);  // every boneWeights/boneIndices default to 0

    std::vector<skin::Submesh> submeshes;
    std::vector<skin::Batch> batches;

    canon::Mesh mesh = canon::assembleMesh(vertices, /*boneCount=*/0, batches, submeshes, {});
    CHECK(mesh.skinning.empty());
}

TEST_CASE("canon::assembleMesh leaves uv1 absent when every vertex's second UV coordinate is the "
          "origin") {
    std::vector<m2::Vertex> vertices(2);  // texCoords[1] defaults to {0, 0}

    std::vector<skin::Submesh> submeshes;
    std::vector<skin::Batch> batches;

    canon::Mesh mesh = canon::assembleMesh(vertices, /*boneCount=*/0, batches, submeshes, {});
    CHECK_FALSE(mesh.uv1.has_value());
}

TEST_CASE("canon::assembleMesh populates uv1 when at least one vertex's second UV coordinate is "
          "genuinely non-origin") {
    std::vector<m2::Vertex> vertices(2);
    vertices[1].texCoords[1] = {0.25f, 0.75f};

    std::vector<skin::Submesh> submeshes;
    std::vector<skin::Batch> batches;

    canon::Mesh mesh = canon::assembleMesh(vertices, /*boneCount=*/0, batches, submeshes, {});
    REQUIRE(mesh.uv1.has_value());
    REQUIRE(mesh.uv1->size() == 2);
    CHECK((*mesh.uv1)[0].x == 0.0f);
    CHECK((*mesh.uv1)[0].y == 0.0f);
    CHECK((*mesh.uv1)[1].x == 0.25f);
    CHECK((*mesh.uv1)[1].y == 0.75f);
}
