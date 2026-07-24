#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <string>

// Shared by tests/test_integration.cpp and tests/test_cli.cpp: spawns the
// actual compiled `husk` binary (HUSK_BINARY, a compile definition set by
// CMakeLists.txt) as a subprocess and captures combined stdout+stderr and
// the exit code. Both files want to exercise the real CLI boundary --
// argv parsing, cmd_info.cpp/cmd_export.cpp's own exception handling --
// not the underlying parser functions directly.
namespace husk::test {

struct RunResult {
    std::string output;
    int exitCode;
};

inline std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

inline RunResult runHusk(const std::string& args) {
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

}  // namespace husk::test
