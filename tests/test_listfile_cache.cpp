// Tests for src/listfile_cache.hpp/.cpp -- the persistent, on-disk cache
// layered in front of listfile.hpp's loadListfile. See that header and
// DESIGN.md's "Listfile cache" section for the full staleness/
// invalidation/concurrency design this exercises.
//
// Every test below points $HUSK_CACHE_DIR at its own throwaway temp
// directory (never the real $HOME/.cache/husk) so this suite never reads
// or writes a real machine's actual cache.

#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "../src/listfile.hpp"
#include "../src/listfile_cache.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

// RAII $HUSK_CACHE_DIR override, isolating each test's cache into its own
// fresh temp directory and restoring whatever was there before on scope
// exit -- doctest runs every TEST_CASE in the same process, so a leaked
// env var would leak into whichever test runs next.
struct ScopedCacheDir {
    fs::path dir;
    std::string previous;
    bool hadPrevious;

    explicit ScopedCacheDir(const std::string& name) {
        dir = fs::temp_directory_path() /
              ("husk_test_listfile_cache_" + name + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::remove_all(dir);
        const char* old = std::getenv("HUSK_CACHE_DIR");
        hadPrevious = old != nullptr;
        if (hadPrevious) previous = old;
        setenv("HUSK_CACHE_DIR", dir.c_str(), 1);
    }

    ~ScopedCacheDir() {
        if (hadPrevious) {
            setenv("HUSK_CACHE_DIR", previous.c_str(), 1);
        } else {
            unsetenv("HUSK_CACHE_DIR");
        }
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

fs::path writeListfile(const fs::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << content;
    f.close();
    return path;
}

constexpr const char* kContentA =
    "200859;world/goober/bubble.blp\n"
    "1018799;character/human/male/deathknighteyeglow.blp\n";

// Same byte length as kContentA, line by line -- lets a test overwrite the
// source's *content* while deliberately restoring its original size and
// mtime afterward, to prove a warm hit reads the persisted cache rather
// than silently re-stat-ing and re-parsing the (in this test, now
// different) bytes on disk.
constexpr const char* kContentASameLength =
    "200859;world/goober/BUBBLE.blp\n"
    "1018799;character/human/male/deathknighteyeglow.blp\n";

static_assert(sizeof(kContentA) == sizeof(kContentASameLength), "fixtures must stay byte-identical in length");

}  // namespace

TEST_CASE("loadListfileCached: cold cache produces the same result loadListfile itself would") {
    ScopedCacheDir cacheDir("cold");
    auto path = writeListfile(fs::temp_directory_path() / "husk_test_lfc_cold.csv", kContentA);

    auto direct = loadListfile(path.string());
    auto cached = loadListfileCached(path.string());

    REQUIRE(cached.size() == direct.size());
    CHECK(cached.at(200859) == direct.at(200859));
    CHECK(cached.at(1018799) == direct.at(1018799));
    CHECK(cached.at(200859) == "world/goober/bubble.blp");

    // A cold call is expected to leave a real cache artifact behind for
    // next time -- both the packed binary and the freshness tag.
    CHECK(fs::exists(listfileCacheDir() / "listfile.bin"));
    CHECK(fs::exists(listfileCacheDir() / "listfile.tag"));

    std::remove(path.c_str());
}

TEST_CASE("loadListfileCached: a warm hit reads the persisted cache, not the source file's current bytes") {
    ScopedCacheDir cacheDir("warm");
    auto path = writeListfile(fs::temp_directory_path() / "husk_test_lfc_warm.csv", kContentA);

    auto first = loadListfileCached(path.string());
    REQUIRE(first.at(200859) == "world/goober/bubble.blp");

    // Preserve the file's exact size+mtime (so the cache's source-identity
    // check still matches) while swapping its *content* -- if the warm
    // path re-parsed the file instead of trusting the cache, this would
    // be visible in the result below.
    auto sizeBefore = fs::file_size(path);
    auto mtimeBefore = fs::last_write_time(path);
    writeListfile(path, kContentASameLength);
    fs::last_write_time(path, mtimeBefore);
    REQUIRE(fs::file_size(path) == sizeBefore);  // sanity: the fixture really is the same length

    auto second = loadListfileCached(path.string());
    CHECK(second.at(200859) == "world/goober/bubble.blp");  // still the ORIGINAL value -- proves cache reuse
    CHECK(second.at(200859) != "world/goober/BUBBLE.blp");

    std::remove(path.c_str());
}

TEST_CASE("loadListfileCached: an expired tag file forces a rebuild even though the source is unchanged") {
    ScopedCacheDir cacheDir("tag-expiry");
    auto path = writeListfile(fs::temp_directory_path() / "husk_test_lfc_expiry.csv", kContentA);

    auto first = loadListfileCached(path.string());
    REQUIRE(first.at(200859) == "world/goober/bubble.blp");

    auto cacheBin = listfileCacheDir() / "listfile.bin";
    auto tagPath = listfileCacheDir() / "listfile.tag";
    REQUIRE(fs::exists(cacheBin));
    auto binMtimeBefore = fs::last_write_time(cacheBin);

    // Backdate the tag past the 10-minute freshness window (DESIGN.md) --
    // the source file itself is left completely untouched.
    fs::last_write_time(tagPath, fs::file_time_type::clock::now() - std::chrono::minutes(11));

    auto second = loadListfileCached(path.string());
    CHECK(second.at(200859) == "world/goober/bubble.blp");  // result is still correct either way

    // The real signal: an expired tag must force a genuine rebuild, not a
    // silent reuse of the still-technically-valid cache blob -- observable
    // as listfile.bin being rewritten (a fresh mtime), even though its
    // *content* would come out identical either way.
    auto binMtimeAfter = fs::last_write_time(cacheBin);
    CHECK(binMtimeAfter > binMtimeBefore);

    std::remove(path.c_str());
}

TEST_CASE("loadListfileCached: a changed source file forces a rebuild even though the tag is fresh") {
    ScopedCacheDir cacheDir("source-mismatch");
    auto path = writeListfile(fs::temp_directory_path() / "husk_test_lfc_mismatch.csv", kContentA);

    auto first = loadListfileCached(path.string());
    REQUIRE(first.at(200859) == "world/goober/bubble.blp");

    // A genuine edit -- different content, and (unlike the warm-hit test
    // above) a real, unrestored mtime change too. The tag file is left
    // exactly as fresh as it was a moment ago.
    writeListfile(path, "200859;world/goober/RENAMED.blp\n"
                         "1018799;character/human/male/deathknighteyeglow.blp\n");

    auto second = loadListfileCached(path.string());
    CHECK(second.at(200859) == "world/goober/RENAMED.blp");  // NOT the stale cached value

    std::remove(path.c_str());
}

TEST_CASE("loadListfileCached: a different --listfile path is also a source mismatch, not a false hit") {
    ScopedCacheDir cacheDir("different-path");
    auto pathA = writeListfile(fs::temp_directory_path() / "husk_test_lfc_pathA.csv", kContentA);
    auto pathB =
        writeListfile(fs::temp_directory_path() / "husk_test_lfc_pathB.csv",
                      "555;some/other/file.blp\n");

    auto fromA = loadListfileCached(pathA.string());
    REQUIRE(fromA.at(200859) == "world/goober/bubble.blp");

    // Same tag freshness window, genuinely different file -- must not
    // silently hand back file A's cached contents for file B's path.
    auto fromB = loadListfileCached(pathB.string());
    REQUIRE(fromB.size() == 1);
    CHECK(fromB.at(555) == "some/other/file.blp");
    CHECK(fromB.find(200859) == fromB.end());

    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
}

TEST_CASE("loadListfileCached: a bad --listfile path throws, same contract as loadListfile") {
    ScopedCacheDir cacheDir("bad-path");
    CHECK_THROWS_AS(loadListfileCached("/nonexistent/path/does_not_exist.csv"), std::runtime_error);
}

TEST_CASE("listfileCacheDir: $HUSK_CACHE_DIR overrides the XDG default") {
    ScopedCacheDir cacheDir("dir-override");
    CHECK(listfileCacheDir() == cacheDir.dir);
}
