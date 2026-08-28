// Tests for src/sources/listfile_catalog.hpp/.cpp -- the first slice of
// REFACTOR/RESOURCE_CATALOG.md's FileDataID<->path surface. Previously
// unexercised directly (only reachable via CLI-level --chr-model-id auto
// tests) since it lived as a file-private static in export_extras.cpp.

#include <doctest/doctest.h>

#include "../src/sources/listfile_catalog.hpp"

using husk::sources::fileDataIdForPath;

TEST_CASE("sources::fileDataIdForPath matches a real listfile row, case-insensitively") {
    std::unordered_map<uint32_t, std::string> listfile{
        {4395382, "character/dracthyr/dracthyrmale.m2"},
        {123, "world/some/other/path.m2"},
    };
    auto result = fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/Character/Dracthyr/DracthyrMale.m2", "/tmp/husk-catalog-test-root");
    REQUIRE(result.has_value());
    CHECK(*result == 4395382);
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when the listfile is empty") {
    std::unordered_map<uint32_t, std::string> listfile;
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/foo.m2", "/tmp/husk-catalog-test-root").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when listfileRoot is empty") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "foo.m2"}};
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/foo.m2", "").has_value());
}

TEST_CASE("sources::fileDataIdForPath returns nullopt when no row matches") {
    std::unordered_map<uint32_t, std::string> listfile{{1, "world/some/other/path.m2"}};
    CHECK_FALSE(fileDataIdForPath(listfile, "/tmp/husk-catalog-test-root/character/nope.m2", "/tmp/husk-catalog-test-root").has_value());
}
