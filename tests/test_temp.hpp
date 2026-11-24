#pragma once

#include <unistd.h>

#include <filesystem>
#include <random>
#include <string>

namespace husk::test {

// Every scratch path a test writes lives under this directory, never at a
// fixed name directly under the system temp dir: husk-tests runs from
// sibling worktrees share TMPDIR, and fixed names let one run delete or
// overwrite another's files mid-test. pid + random suffix, so a reused pid
// still gets a fresh directory. Created once per process; not removed at
// exit (tests clean their own paths, as before).
inline const std::filesystem::path& testRunDir() {
    static const std::filesystem::path dir = [] {
        std::random_device random;
        auto path = std::filesystem::temp_directory_path() /
                    ("husk-tests-" + std::to_string(getpid()) + "-" + std::to_string(random()));
        std::filesystem::create_directories(path);
        return path;
    }();
    return dir;
}

}  // namespace husk::test
