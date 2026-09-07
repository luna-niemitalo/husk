// CLI tier: README.md drift check for the generated flag-reference table.
// `husk --print-flag-docs=<subcommand>` (src/main.cpp) walks the same real
// CLI11 registrations `--help` reads, so a flag's description/default only
// has to be written once (in addExportOptions, cmd_export.cpp) instead of
// also being hand-copied into README.md -- see README.md's "Flags:" intro
// under `export` and DESIGN.md's doc-bloat-reduction notes. This test is
// the actual enforcement: without it, nothing stops README.md's checked-in
// copy from silently drifting the next time a flag changes.

#include <doctest/doctest.h>
#include <fstream>
#include <sstream>
#include <string>

#include "run_husk.hpp"

using husk::test::runHusk;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    REQUIRE(in.is_open());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Extracts the generated block for `subName`, markers included, from
// README.md's full text -- the exact substring `--print-flag-docs`'s own
// output should reproduce.
std::string extractGeneratedBlock(const std::string& readme, const std::string& subName) {
    std::string beginMarker = "<!-- BEGIN GENERATED FLAG TABLE: " + subName + " -->";
    std::string endMarker = "<!-- END GENERATED FLAG TABLE: " + subName + " -->";
    size_t begin = readme.find(beginMarker);
    REQUIRE(begin != std::string::npos);
    size_t end = readme.find(endMarker, begin);
    REQUIRE(end != std::string::npos);
    end += endMarker.size();
    return readme.substr(begin, end - begin);
}

}  // namespace

TEST_CASE("README.md's generated export flag table matches --print-flag-docs=export's live "
          "output") {
    auto result = runHusk("--print-flag-docs=export");
    REQUIRE(result.exitCode == 0);

    std::string readme = readFile(HUSK_README_PATH);
    std::string checkedIn = extractGeneratedBlock(readme, "export");

    // Trailing-newline differences between a subprocess's stdout capture and
    // a file read aren't a real drift -- compare trimmed.
    auto trim = [](std::string s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    };
    CHECK(trim(checkedIn) == trim(result.output));
}

TEST_CASE("husk --print-flag-docs=export never lists --config or --help") {
    auto result = runHusk("--print-flag-docs=export");
    REQUIRE(result.exitCode == 0);
    CHECK(result.output.find("`--config`") == std::string::npos);
    CHECK(result.output.find("`--help`") == std::string::npos);
}

TEST_CASE("husk --print-flag-docs rejects an unknown subcommand") {
    auto result = runHusk("--print-flag-docs=not-a-real-command");
    CHECK(result.exitCode == 1);
    CHECK(result.output.find("unknown subcommand") != std::string::npos);
}
