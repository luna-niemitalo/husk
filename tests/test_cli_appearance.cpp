// CLI tier: `husk appearance-string` -- exercises the real compiled binary
// (see run_husk.hpp). Written alongside the command's migration from
// hand-rolled argv parsing to CLI11 (TODO/CLEANUP_TODO.md #3) -- this
// command previously had zero CLI-tier coverage.

#include <doctest/doctest.h>

#include "run_husk.hpp"

using husk::test::runHusk;

TEST_CASE("husk appearance-string --validate: a well-formed string round-trips") {
    auto result = runHusk("appearance-string --validate \"husk-appearance/1 race=10 sex=1\"");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("valid") != std::string::npos);
    CHECK(result.output.find("race=10 sex=1") != std::string::npos);
}

TEST_CASE("husk appearance-string --validate: a malformed string fails cleanly") {
    auto result = runHusk("appearance-string --validate \"not-a-real-appearance-string\"");
    CHECK(result.exitCode != 0);
}

TEST_CASE("husk appearance-string: --validate is required -- CLI11's RequiredError, not a crash") {
    auto result = runHusk("appearance-string");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("required") != std::string::npos);
}
