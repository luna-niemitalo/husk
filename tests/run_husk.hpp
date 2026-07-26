#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <doctest/doctest.h>
#include <string>

// Shared by tests/test_integration.cpp, tests/test_cli.cpp, and
// tests/test_conformance.cpp: spawns a subprocess and captures combined
// stdout+stderr and the exit code. Everyone wants to exercise a real CLI
// boundary -- husk's own argv parsing/exception handling, or a real
// downstream tool's (gltf_validator, blender) actual behavior -- not a
// mocked stand-in.
namespace husk::test {

struct RunResult {
    std::string output;
    int exitCode;
};

inline std::string envOrEmpty(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// `command` is a fully-formed shell command line (executable + args,
// already quoted as needed by the caller) -- this just adds output
// capture, same as a hand-rolled `popen` call would, without repeating
// the boilerplate at each call site.
inline RunResult runCommand(const std::string& command) {
    std::string cmd = command + " 2>&1";
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

inline RunResult runHusk(const std::string& args) {
    return runCommand(std::string(HUSK_BINARY) + " " + args);
}

}  // namespace husk::test
