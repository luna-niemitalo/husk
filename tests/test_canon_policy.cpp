// Tests for husk::canon::enforcePartialFailurePolicy (src/canon_policy.hpp/
// .cpp) -- the Stage 3 seed formalizing which m2::Model consumers tolerate
// a parse failure versus fail fast on one.

#include <doctest/doctest.h>

#include "canon_policy.hpp"

using husk::canon::enforcePartialFailurePolicy;
using husk::canon::PartialFailurePolicy;

namespace {

husk::m2::Model modelWithFailure(const char* field, const char* what) {
    husk::m2::Model model;
    model.parseFailures.push_back({field, what});
    return model;
}

}  // namespace

TEST_CASE("enforcePartialFailurePolicy: Tolerant never throws, failed or not") {
    auto model = modelWithFailure("bones", "expected 24 bytes, blob is 4");
    CHECK_NOTHROW(enforcePartialFailurePolicy(model, "bones", PartialFailurePolicy::Tolerant));
    CHECK_NOTHROW(enforcePartialFailurePolicy(model, "vertices", PartialFailurePolicy::Tolerant));
}

TEST_CASE("enforcePartialFailurePolicy: Strict throws the matching failure's message verbatim") {
    auto model = modelWithFailure("bones", "expected 24 bytes, blob is 4");
    CHECK_THROWS_WITH_AS(enforcePartialFailurePolicy(model, "bones", PartialFailurePolicy::Strict),
                         "expected 24 bytes, blob is 4", std::runtime_error);
}

TEST_CASE("enforcePartialFailurePolicy: Strict is a no-op for a field that isn't in parseFailures") {
    auto model = modelWithFailure("bones", "expected 24 bytes, blob is 4");
    CHECK_NOTHROW(enforcePartialFailurePolicy(model, "vertices", PartialFailurePolicy::Strict));
}
