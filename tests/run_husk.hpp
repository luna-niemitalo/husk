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

// `HUSK_CONFIG=/dev/null` unless `args` already sets it: forces "no config
// file" isolation from whatever real machine the test suite happens to run
// on, same discipline runCommand's own callers rely on for a clean
// environment otherwise. Without this, a developer machine's own real
// `~/.config/husk/config.toml` (or a set $HUSK_CONFIG) is auto-discovered
// by every `husk export`/`db2-export`/... invocation exactly like a real
// user's would be -- config-file support (husk_config.hpp) is deliberately
// global, not test-aware -- silently injecting real `--dbd-dir`/`--db2-dir`/
// `--listfile-root` values into tests that never asked for them and don't
// expect them. Found real, not theorized: 3 CLI-tier tests (a --listfile
// corpus-root test, a --db2-dir-required-message test, a db2-export
// generic-vs-real-column-name test) all failed on a machine with such a
// config file present, each for a different symptom, until traced to this
// one shared cause and fixed here once instead of patched three separate
// times. A test that specifically exercises config-file behavior itself
// (tests/test_cli_config.cpp) calls runCommand directly with its own
// explicit HUSK_CONFIG/XDG_CONFIG_HOME/--config overrides, bypassing this
// default entirely -- unaffected either way.
inline RunResult runHusk(const std::string& args) {
    return runCommand("HUSK_CONFIG=/dev/null " + std::string(HUSK_BINARY) + " " + args);
}

}  // namespace husk::test
