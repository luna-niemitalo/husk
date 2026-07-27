#pragma once

// Central resolution for husk-tests' optional real-data fixtures, shared by
// test_main.cpp (the startup banner), test_integration.cpp, and
// test_conformance.cpp. An explicit HUSK_TEST_* env var always wins (the
// override case, e.g. pointing at a different real model); when unset,
// resolve() falls back to this repo's own (gitignored) test_data/
// directory -- populated once from a real, personally-owned WoW
// extraction, never committed, never CASC -- so the real-data test tier
// doesn't need eight environment variables hand-set every session just to
// exercise coverage the data for is already sitting in the checkout. Same
// auto/override/skip shape as husk export's own --skin-dir/--textures
// resolution (see cmd_export.cpp), applied here to test fixtures instead
// of CLI args.
//
// The same functions below back both the doctest::skip() decorators that
// gate each real-data TEST_CASE and test_main.cpp's startup banner -- one
// resolution, two consumers -- so "why did N tests just skip" is
// answered by the banner honestly, not by a second, potentially-drifted
// copy of the same logic.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "m2.hpp"
#include "skel.hpp"

namespace husk::test {

#ifdef HUSK_TEST_DATA_DIR
constexpr const char* kTestDataDir = HUSK_TEST_DATA_DIR;
#else
constexpr const char* kTestDataDir = nullptr;
#endif

// envVar always wins when set; otherwise defaultRelativePath is checked
// under kTestDataDir and used only if it's actually there on disk --
// never guessed, never silently wrong for a checkout that doesn't have
// this specific model extracted.
inline std::string resolve(const char* envVar, const std::string& defaultRelativePath) {
    if (const char* env = std::getenv(envVar)) {
        return std::string(env);
    }
    if (kTestDataDir == nullptr) {
        return std::string();
    }
    std::error_code ec;
    std::filesystem::path candidate = std::filesystem::path(kTestDataDir) / defaultRelativePath;
    if (std::filesystem::exists(candidate, ec)) {
        return candidate.string();
    }
    return std::string();
}

// Builds a --skin-dir fixture from an already-resolved (m2Path, skinPath)
// pair: reads the real SFID entry 0 out of the M2's own header (the exact
// field cmd_export.cpp's 'auto' resolution reads) and copies skinPath next
// to it under that FileDataID -- the same '<FileDataID>.skin' naming
// convention a real casc-tool extraction produces, so this exercises the
// genuine 'auto' + --skin-dir path rather than a shortcut around it.
// HUSK_TEST_SKIN_DIR, if set explicitly, still overrides this entirely.
// Returns "" if either input path is empty, the M2 has no SFID chunk, or
// the copy fails -- any of which just means this fixture isn't available
// here, not an error worth surfacing louder than a skip.
inline std::string autoSkinDir(const std::string& m2Path, const std::string& skinPath) {
    if (const char* env = std::getenv("HUSK_TEST_SKIN_DIR")) {
        return std::string(env);
    }
    if (m2Path.empty() || skinPath.empty()) {
        return std::string();
    }

    std::ifstream f(m2Path, std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    husk::m2::Header header;
    try {
        header = husk::m2::parseHeader(bytes);
    } catch (...) {
        return std::string();
    }
    if (!header.skinFileDataIds || header.skinFileDataIds->empty()) {
        return std::string();
    }

    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "husk-test-autoskin";
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path dest = dir / (std::to_string((*header.skinFileDataIds)[0]) + ".skin");
    std::filesystem::copy_file(skinPath, dest, std::filesystem::copy_options::overwrite_existing, ec);
    return ec ? std::string() : dir.string();
}

// Builds a --bones-dir fixture from an already-resolved .skel path: reads
// the real BFID entries out of the .skel's own BFID chunk (the exact field
// cmd_export.cpp's --bones-dir resolution reads for a .skel-sourced
// skeleton) and copies up to 3 real, already-in-this-repo '<basename>_NN
// .bone' fixture files (see test_data/character/bloodelf/female/) next to
// it under their corresponding BFID[NN] FileDataID -- the same
// '<FileDataID>.bone' naming convention a real casc-tool extraction would
// produce. HUSK_TEST_BONES_DIR, if set explicitly, still overrides this
// entirely. The NN -> BFID[NN] positional assignment is arbitrary for test
// purposes, not a claimed real slot mapping (see WIKI_FINDINGS.md
// §4/TODO_correctness.md #6: that mapping is exactly the client-side
// customization-choice data husk doesn't have access to) -- this fixture
// only needs to prove the resolution/parse/extras pipeline works end to
// end against real '.bone' bytes, not that any particular slot is
// semantically correct. Returns "" if skelPath is empty, has no BFID chunk,
// or none of the expected same-basename '_NN.bone' files exist next to it.
inline std::string autoBonesDir(const std::string& skelPath) {
    if (const char* env = std::getenv("HUSK_TEST_BONES_DIR")) {
        return std::string(env);
    }
    if (skelPath.empty()) {
        return std::string();
    }

    std::ifstream f(skelPath, std::ios::binary);
    if (!f) {
        return std::string();
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    std::optional<std::vector<uint32_t>> boneIds;
    try {
        boneIds = husk::skel::findBoneFileDataIds(bytes);
    } catch (...) {
        return std::string();
    }
    if (!boneIds || boneIds->empty()) {
        return std::string();
    }

    std::filesystem::path skelPathObj(skelPath);
    std::filesystem::path skelDir = skelPathObj.parent_path();
    std::string base = skelPathObj.stem().string();

    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "husk-test-autobones";
    std::filesystem::create_directories(dir, ec);

    size_t copied = 0;
    for (size_t i = 0; i < boneIds->size() && i < 3; ++i) {
        char nn[4];
        std::snprintf(nn, sizeof(nn), "%02zu", i);
        std::filesystem::path src = skelDir / (base + "_" + nn + ".bone");
        if (!std::filesystem::exists(src, ec)) {
            continue;
        }
        std::filesystem::path dest = dir / (std::to_string((*boneIds)[i]) + ".bone");
        std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            ++copied;
        }
    }
    return copied > 0 ? dir.string() : std::string();
}

// Default relative paths under test_data/, named once so every caller
// (banner + tests) shares the same literal instead of retyping it.
namespace fixtures {
constexpr const char* kM2 = "bloodelffemale.m2";
constexpr const char* kSkin = "bloodelffemale00.skin";
constexpr const char* kMismatchedSkin = "bloodelffemale_hd00.skin";
constexpr const char* kTexturesDir = "textures";
constexpr const char* kSkelM2 = "character/bloodelf/female/bloodelffemale_hd.m2";
constexpr const char* kSkelSkin = "character/bloodelf/female/bloodelffemale_hd00.skin";
constexpr const char* kSkel = "character/bloodelf/female/bloodelffemale_hd.skel";
}  // namespace fixtures

inline std::string testM2() { return resolve("HUSK_TEST_M2", fixtures::kM2); }
inline std::string testSkin() { return resolve("HUSK_TEST_SKIN", fixtures::kSkin); }
inline std::string testMismatchedSkin() {
    return resolve("HUSK_TEST_MISMATCHED_SKIN", fixtures::kMismatchedSkin);
}
inline std::string testTexturesDir() {
    return resolve("HUSK_TEST_TEXTURES_DIR", fixtures::kTexturesDir);
}
inline std::string testSkinDir() { return autoSkinDir(testM2(), testSkin()); }
inline std::string testSkelM2() { return resolve("HUSK_TEST_SKEL_M2", fixtures::kSkelM2); }
inline std::string testSkelSkin() { return resolve("HUSK_TEST_SKEL_SKIN", fixtures::kSkelSkin); }
inline std::string testSkel() { return resolve("HUSK_TEST_SKEL", fixtures::kSkel); }
inline std::string testBonesDir() { return autoBonesDir(testSkel()); }

}  // namespace husk::test
