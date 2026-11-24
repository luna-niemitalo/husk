#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace husk::test {

// Every scratch path a test writes lives under this directory, never at a
// fixed name directly under the system temp dir: husk-tests runs from
// sibling worktrees share TMPDIR, and fixed names let one run delete or
// overwrite another's files mid-test. pid + random suffix, so a reused pid
// still gets a fresh directory. Created once per process and removed at
// exit (a full run leaves ~55 MB otherwise); HUSK_TEST_KEEP_TMP=1 keeps it
// for inspecting a failure.
struct TestRunDir {
    std::filesystem::path path;
    TestRunDir() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() /
               ("husk-tests-" + std::to_string(getpid()) + "-" + std::to_string(random()));
        std::filesystem::create_directories(path);
    }
    ~TestRunDir() {
        if (std::getenv("HUSK_TEST_KEEP_TMP")) return;
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

inline const std::filesystem::path& testRunDir() {
    static const TestRunDir dir;
    return dir.path;
}

}  // namespace husk::test
