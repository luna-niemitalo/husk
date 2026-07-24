// Runs the actual compiled `husk` binary against a real, game-extracted M2
// file. Deliberately not mocked, and deliberately not asserting on any
// specific model's field values (those belong in test_m2.cpp's synthetic
// fixtures, which encode the spec) -- this tier only answers "does it
// survive contact with a real file from the live game," the same
// smoke-test role test_integration.cpp plays in casc-tool.
//
// Point HUSK_TEST_M2 at any real .m2 extracted via casc-tool to run this
// for real:
//   HUSK_TEST_M2=/path/to/some.m2 ./build/husk-tests
// Without it, every case here is skipped (counted as passed), not failed.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <memory>
#include <string>

namespace {

std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

struct RunResult {
    std::string output;
    int exitCode;
};

RunResult runHusk(const std::string& args) {
    std::string cmd = std::string(HUSK_BINARY) + " " + args + " 2>&1";
    std::array<char, 4096> buf;
    std::string output;

    FILE* pipe = popen(cmd.c_str(), "r");
    REQUIRE(pipe != nullptr);
    while (fgets(buf.data(), buf.size(), pipe)) {
        output += buf.data();
    }
    int status = pclose(pipe);
    return {output, WEXITSTATUS(status)};
}

}  // namespace

TEST_CASE("husk info: real game-extracted M2 parses without error") {
    std::string path = envOrEmpty("HUSK_TEST_M2");
    if (path.empty()) {
        MESSAGE("SKIPPED (no real M2 file available -- set HUSK_TEST_M2)");
        return;
    }

    auto result = runHusk("info \"" + path + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("format:") != std::string::npos);
    CHECK(result.output.find("version:") != std::string::npos);
    // A model file always has *some* geometry; a zero vertex count would
    // mean the parser landed on the wrong offsets even though it didn't crash.
    CHECK(result.output.find("vertices: 0 ") == std::string::npos);
}

TEST_CASE("husk info: nonexistent path fails cleanly, not a crash") {
    auto result = runHusk("info /nonexistent/path/does-not-exist.m2");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("couldn't open") != std::string::npos);
}
