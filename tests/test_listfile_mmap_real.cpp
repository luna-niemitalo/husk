// Exhaustive correctness proof for listfile_mmap_index.hpp's two backends
// (MmapSortedListfileIndex/"option A", MmapHashListfileIndex/"option B")
// against the real, full community-listfile.csv this machine has --
// DESIGN.md's "Listfile index" section requires byte-identical resolution
// results, and a synthetic few-entry fixture can't exercise the same
// collision/offset-arithmetic edge cases 2.2M real rows do. Opt-in via
// $HUSK_TEST_REAL_LISTFILE (same "skip cleanly when the fixture isn't
// present on this machine" convention as every other real-data test in
// this suite, tests/test_data_paths.hpp) -- there is no synthetic
// fallback, since the whole point is scale this test needs a real file
// for.
//
// This is NOT a sampled check: every single row loadListfile() parses is
// looked up through both mmap backends and compared against the same
// std::unordered_map loadListfile() itself produced. Runs in-process (no
// husk subprocess, no --explain-textures ledger diffing) since the object
// under test is the lookup layer itself, not the texture-resolution
// pipeline built on top of it.

#include <doctest/doctest.h>

#include <cstdlib>
#include <random>

#include "../src/listfile.hpp"
#include "../src/listfile_mmap_index.hpp"

using namespace husk;

namespace {
std::string realListfilePath() {
    const char* env = std::getenv("HUSK_TEST_REAL_LISTFILE");
    return env ? std::string(env) : std::string();
}
}  // namespace

TEST_CASE("MmapSortedListfileIndex/MmapHashListfileIndex: every real listfile row round-trips "
          "byte-identically through both mmap backends") {
    std::string path = realListfilePath();
    if (path.empty()) {
        MESSAGE("HUSK_TEST_REAL_LISTFILE not set -- skipping exhaustive real-listfile check");
        return;
    }

    auto reference = loadListfile(path);
    REQUIRE(reference.size() > 1'000'000);  // sanity: this really is the full real listfile, not a stub

    std::string sortedCachePath = path + ".test_sorted.bin";
    std::string hashCachePath = path + ".test_hash.bin";
    MmapSortedListfileIndex::build(sortedCachePath, path, 0, 0, reference);
    MmapHashListfileIndex::build(hashCachePath, path, 0, 0, reference);

    std::unique_ptr<MmapSortedListfileIndex> sorted;
    std::unique_ptr<MmapHashListfileIndex> hashed;
    REQUIRE(MmapSortedListfileIndex::tryLoad(sortedCachePath, path, 0, 0, sorted));
    REQUIRE(MmapHashListfileIndex::tryLoad(hashCachePath, path, 0, 0, hashed));

    CHECK(sorted->size() == reference.size());
    CHECK(hashed->size() == reference.size());

    // Every real row -- not a sample. Both backends must agree with the
    // reference map on every single FileDataID.
    size_t checked = 0;
    for (const auto& [fdid, expectedPath] : reference) {
        auto sortedResult = sorted->lookup(fdid);
        auto hashResult = hashed->lookup(fdid);
        REQUIRE(sortedResult.has_value());
        REQUIRE(hashResult.has_value());
        CHECK(*sortedResult == expectedPath);
        CHECK(*hashResult == expectedPath);
        ++checked;
    }
    CHECK(checked == reference.size());

    // A seeded random sample of FileDataIDs guaranteed to have NO real row
    // (well outside the real ID space this listfile snapshot covers) must
    // miss cleanly on both backends -- not crash, not return a neighbor's
    // value from a probe-chain/binary-search off-by-one.
    std::mt19937 rng(20260830);
    std::uniform_int_distribution<uint32_t> dist(4'000'000'000u, 4'294'000'000u);
    for (int i = 0; i < 4000; ++i) {
        uint32_t fdid = dist(rng);
        if (reference.count(fdid)) continue;  // extremely unlikely, but skip a real collision if seeded into one
        CHECK_FALSE(sorted->lookup(fdid).has_value());
        CHECK_FALSE(hashed->lookup(fdid).has_value());
    }

    std::remove(sortedCachePath.c_str());
    std::remove(hashCachePath.c_str());
}
