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
// §4/TODO_correctness.md #3: that mapping is exactly the client-side
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
// The real bloodelffemale_hd_*.anim files (AFSB-shaped, see WIKI_FINDINGS.md
// §2) sit right next to the .m2/.skin/.skel above -- same directory, not a
// separate one, since that's exactly the layout a real casc-tool extraction
// produces.
constexpr const char* kAnimDir = "character/bloodelf/female";
// Real weapon models with ribbon/particle emitters (see WIKI_FINDINGS.md's
// particle/ribbon section) -- sit under this repo's own gitignored
// test_data/item/objectcomponents/weapon/ (Luna's own personally-owned
// extraction of the game's full weapon set, same convention as the
// character fixtures above, never committed). Chosen by a full 4112-file
// scan: kWeaponRibbon is ribbon-only (3 ribbons, 0 particles) for a clean
// first cross-check; kWeaponParticleA/B each have both (1-2 ribbons, 2
// particles) for a combined check; kWeaponParticleStress has 64 particle
// emitters and 0 ribbons, the largest real file available, for a
// multi-emitter stress check. All four are Cataclysm+ (version 272/274),
// at or above kMinVerifiedParticleVersion.
constexpr const char* kWeaponRibbon = "item/objectcomponents/weapon/sword_2h_ashbringer_a_01.m2";
constexpr const char* kWeaponParticleA =
    "item/objectcomponents/weapon/sword_1h_artifactskywall_d_06.m2";
constexpr const char* kWeaponParticleB = "item/objectcomponents/weapon/offhand_1h_revendreth_d_01.m2";
constexpr const char* kWeaponParticleStress = "item/objectcomponents/weapon/mace_2h_bolvar_d_01.m2";
// TODO_correctness.md #3 (multi-texture-layer arithmetic, textureComboIndex
// +layer / textureCoordComboIndex+layer): found by a full ~287k-file .skin
// scan of Luna's own real WoW extraction for batches with textureCount > 1
// (226,294 hits -- common) and a full ~130k-file .m2 scan for a nonzero
// textureCoordCombos array (only 3 hits -- rare, matching the "still
// present but unused in Cataclysm" wiki text). kMultiTextureLayer is a
// small guild-pennant doodad with a clean 6-layer batch, chosen for the
// primary textureComboIndex+layer cross-check (WIKI_FINDINGS.md's new §7
// has the receipts). kTextureCoordCombo is one of the 3 real files with a
// nonzero textureCoordCombos table (values [33, 34] -- not the documented
// -1/0/1 range, real but apparently vestigial data), chosen to prove the
// textureCoordComboIndex+layer path is exercised (not just skipped via the
// empty-table fast path) and still resolves safely.
constexpr const char* kMultiTextureLayerM2 =
    "world/replaceabletextureprops/guild/pennant_guild_alliance_a_01.m2";
constexpr const char* kMultiTextureLayerSkin =
    "world/replaceabletextureprops/guild/pennant_guild_alliance_a_0100.skin";
constexpr const char* kTextureCoordComboM2 =
    "world/expansion05/doodads/ironhorde/6ih_ironhorde_siegeweapon03.m2";
constexpr const char* kTextureCoordComboSkin =
    "world/expansion05/doodads/ironhorde/6ih_ironhorde_siegeweapon0300.skin";
// A real weapon .m2 with a matching .skin -- extracted into test_data/ this
// session, next to its already-committed .phys (a real, paired fixture:
// none of the other 6 committed .phys weapon fixtures had a real .skin
// sibling committed alongside them, only .m2+.phys). 10 real physics
// bodies, boneIndex values {0..9} of 17 real bones (confirmed by hand --
// see WIKI_FINDINGS.md §9).
constexpr const char* kWeaponPhys = "item/objectcomponents/weapon/mace_1h_warfrontsforsaken_d_01.m2";
constexpr const char* kWeaponPhysSkin =
    "item/objectcomponents/weapon/mace_1h_warfrontsforsaken_d_0100.skin";
// Real files pulled directly from a live CASC install (see WIKI_FINDINGS.md
// §13) to verify EXP2/PFDC against real bytes and lock in the BLP2/listfile
// -mismatch anomaly's correct-throw behavior. Sit under test_data/
// verification/, same gitignored convention as every other fixture here.
constexpr const char* kExp2VerificationM2 = "verification/exp2_126382.m2";
constexpr const char* kPfdcVerificationM2 = "verification/pfdc_1003471.m2";
constexpr const char* kBlp2AnomalyM2 = "verification/blp2_7507381.m2";
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
inline std::string testAnimDir() { return resolve("HUSK_TEST_ANIM_DIR", fixtures::kAnimDir); }
inline std::string testSkel() { return resolve("HUSK_TEST_SKEL", fixtures::kSkel); }
inline std::string testWeaponRibbon() {
    return resolve("HUSK_TEST_WEAPON_RIBBON", fixtures::kWeaponRibbon);
}
inline std::string testWeaponParticleA() {
    return resolve("HUSK_TEST_WEAPON_PARTICLE_A", fixtures::kWeaponParticleA);
}
inline std::string testWeaponParticleB() {
    return resolve("HUSK_TEST_WEAPON_PARTICLE_B", fixtures::kWeaponParticleB);
}
inline std::string testWeaponParticleStress() {
    return resolve("HUSK_TEST_WEAPON_PARTICLE_STRESS", fixtures::kWeaponParticleStress);
}
inline std::string testBonesDir() { return autoBonesDir(testSkel()); }
inline std::string testMultiTextureLayerM2() {
    return resolve("HUSK_TEST_MULTITEX_M2", fixtures::kMultiTextureLayerM2);
}
inline std::string testMultiTextureLayerSkin() {
    return resolve("HUSK_TEST_MULTITEX_SKIN", fixtures::kMultiTextureLayerSkin);
}
inline std::string testWeaponPhys() { return resolve("HUSK_TEST_WEAPON_PHYS", fixtures::kWeaponPhys); }
inline std::string testWeaponPhysSkin() {
    return resolve("HUSK_TEST_WEAPON_PHYS_SKIN", fixtures::kWeaponPhysSkin);
}
inline std::string testTextureCoordComboM2() {
    return resolve("HUSK_TEST_COORDCOMBO_M2", fixtures::kTextureCoordComboM2);
}
inline std::string testTextureCoordComboSkin() {
    return resolve("HUSK_TEST_COORDCOMBO_SKIN", fixtures::kTextureCoordComboSkin);
}
inline std::string testExp2VerificationM2() {
    return resolve("HUSK_TEST_EXP2_M2", fixtures::kExp2VerificationM2);
}
inline std::string testPfdcVerificationM2() {
    return resolve("HUSK_TEST_PFDC_M2", fixtures::kPfdcVerificationM2);
}
inline std::string testBlp2AnomalyM2() {
    return resolve("HUSK_TEST_BLP2_ANOMALY_M2", fixtures::kBlp2AnomalyM2);
}

}  // namespace husk::test
