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
#include <set>
#include <stdexcept>

#include "canon_mesh_builder.hpp"
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

    std::set<uint16_t> actualIds;
    for (const auto& pg : result) {
        REQUIRE(std::holds_alternative<canon::RecordIndex>(pg.geoset.ref.id));
        uint32_t rawId = std::get<canon::RecordIndex>(pg.geoset.ref.id).value;
        actualIds.insert(static_cast<uint16_t>(rawId));

        // group*100+variant must reconstruct the raw id exactly -- the
        // same decomposition gltf_mesh.cpp's geoset_group/geoset_variant
        // extras use, already proven correct in canon_geoset's own tests;
        // this just confirms the real fixture's real ids decompose
        // sensibly (no negative/nonsensical values -- unsigned types make
        // "negative" impossible by construction, so this is really just a
        // roundtrip check).
        CHECK(pg.geoset.group * 100 + pg.geoset.variant == rawId);

        CHECK(pg.indexStart + pg.indexCount <= triangleIndices.size());
    }
    CHECK(actualIds == expectedIds);

    // Spot-check a few real batches' index ranges directly against their
    // resolved submesh.
    size_t resultIdx = 0;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& sm = submeshes[batches[bi].skinSectionIndex];
        if (sm.indexCount == 0) continue;
        INFO("batch index ", bi);
        REQUIRE(resultIdx < result.size());
        CHECK(result[resultIdx].indexStart == sm.indexStart);
        CHECK(result[resultIdx].indexCount == sm.indexCount);
        ++resultIdx;
    }
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
    for (const auto& pg : result) {
        CHECK(pg.indexCount > 0);
    }
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
