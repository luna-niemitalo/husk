// Tests for src/sources/resolved.hpp -- REFACTOR/RESOURCE_CATALOG.md's
// Resolved<T> scaffolding, landed ahead of the real sources::Catalog object
// per REFACTOR_LOG.md's "prep infrastructure first" pattern. Nothing in the
// codebase constructs one of these yet, so this is the only coverage this
// type has -- purely mechanical (construction/found()/reason plumbing), no
// resolution policy to verify.

#include <doctest/doctest.h>

#include "../src/sources/resolved.hpp"

using husk::sources::Resolved;
using husk::sources::ResolutionTier;
using husk::sources::tierName;

TEST_CASE("sources::Resolved::hit carries the value, tier, and reason") {
    auto r = Resolved<int>::hit(42, ResolutionTier::Literal, "found at /tmp/foo.png");
    CHECK(r.found());
    CHECK(static_cast<bool>(r));
    REQUIRE(r.value.has_value());
    CHECK(*r.value == 42);
    CHECK(r.tier == ResolutionTier::Literal);
    CHECK(r.reason == "found at /tmp/foo.png");
}

TEST_CASE("sources::Resolved::miss carries no value but still names the attempted tier and reason") {
    auto r = Resolved<int>::miss(ResolutionTier::Listfile, "no listfile row for fdid 999");
    CHECK_FALSE(r.found());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK_FALSE(r.value.has_value());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.reason == "no listfile row for fdid 999");
}

TEST_CASE("sources::Resolved default-constructs as a miss") {
    Resolved<std::string> r;
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::Miss);
}

TEST_CASE("sources::tierName covers every ResolutionTier with a distinct, non-empty name") {
    const ResolutionTier all[] = {
        ResolutionTier::Literal,      ResolutionTier::Listfile,
        ResolutionTier::ParentDirectorySameBasename, ResolutionTier::FuzzySameBasenamePool,
        ResolutionTier::KnowledgeBase, ResolutionTier::Miss,
    };
    std::string_view seen[std::size(all)];
    for (size_t i = 0; i < std::size(all); ++i) {
        seen[i] = tierName(all[i]);
        CHECK_FALSE(seen[i].empty());
        for (size_t j = 0; j < i; ++j) CHECK(seen[j] != seen[i]);
    }
}
