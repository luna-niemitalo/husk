// Provides doctest's main() plus the startup banner naming which optional
// real-data fixtures (tests/test_data_paths.hpp) and downstream tools
// (gltf_validator, Blender) were found on this run -- see
// TEST_DESIGN.md#Fixture-resolution-model for the doctest::skip/HUSK_TEST_*
// convention this banner reports on. All actual test cases live in the
// other test_*.cpp files.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <iostream>

#include "test_data_paths.hpp"

namespace {

void printFixture(const char* envVar, const std::string& resolved) {
    std::cerr << "husk-tests:   " << envVar << ": "
              << (resolved.empty() ? "not found -- tests needing it will skip" : resolved) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::cerr << "husk-tests: real-data fixtures (an env var always overrides; otherwise "
                 "falls back to test_data/ when present):\n";
    printFixture("HUSK_TEST_M2", husk::test::testM2());
    printFixture("HUSK_TEST_SKIN", husk::test::testSkin());
    printFixture("HUSK_TEST_MISMATCHED_SKIN", husk::test::testMismatchedSkin());
    printFixture("HUSK_TEST_TEXTURES_DIR", husk::test::testTexturesDir());
    printFixture("HUSK_TEST_SKIN_DIR", husk::test::testSkinDir());
    printFixture("HUSK_TEST_SKEL_M2", husk::test::testSkelM2());
    printFixture("HUSK_TEST_SKEL_SKIN", husk::test::testSkelSkin());
    printFixture("HUSK_TEST_SKEL", husk::test::testSkel());
    printFixture("HUSK_TEST_BONES_DIR", husk::test::testBonesDir());
    printFixture("HUSK_TEST_ANIM_DIR", husk::test::testAnimDir());
    printFixture("HUSK_TEST_WEAPON_RIBBON", husk::test::testWeaponRibbon());
    printFixture("HUSK_TEST_WEAPON_PARTICLE_A", husk::test::testWeaponParticleA());
    printFixture("HUSK_TEST_WEAPON_PARTICLE_B", husk::test::testWeaponParticleB());
    printFixture("HUSK_TEST_WEAPON_PARTICLE_STRESS", husk::test::testWeaponParticleStress());
    printFixture("HUSK_TEST_MULTITEX_M2", husk::test::testMultiTextureLayerM2());
    printFixture("HUSK_TEST_MULTITEX_SKIN", husk::test::testMultiTextureLayerSkin());
    printFixture("HUSK_TEST_COORDCOMBO_M2", husk::test::testTextureCoordComboM2());
    printFixture("HUSK_TEST_COORDCOMBO_SKIN", husk::test::testTextureCoordComboSkin());
    printFixture("HUSK_TEST_WEAPON_PHYS", husk::test::testWeaponPhys());
    printFixture("HUSK_TEST_WEAPON_PHYS_SKIN", husk::test::testWeaponPhysSkin());
    printFixture("HUSK_TEST_EXP2_M2", husk::test::testExp2VerificationM2());
    printFixture("HUSK_TEST_PFDC_M2", husk::test::testPfdcVerificationM2());
    printFixture("HUSK_TEST_BLP2_ANOMALY_M2", husk::test::testBlp2AnomalyM2());
    printFixture("HUSK_TEST_PCOL_M2", husk::test::testPcolVerificationM2());
    printFixture("HUSK_TEST_QUADRUPED_M2", husk::test::testQuadrupedM2());
    printFixture("HUSK_TEST_QUADRUPED_SKIN", husk::test::testQuadrupedSkin());
    printFixture("HUSK_TEST_TEXTURE_TRANSFORM_ROTATION_M2", husk::test::testTextureTransformRotationM2());
    printFixture("HUSK_TEST_TEXTURE_TRANSFORM_ROTATION_SKIN",
                 husk::test::testTextureTransformRotationSkin());
    printFixture("HUSK_TEST_TEXTURE_TRANSFORM_SCALE_M2", husk::test::testTextureTransformScaleM2());
    printFixture("HUSK_TEST_TEXTURE_TRANSFORM_SCALE_SKIN", husk::test::testTextureTransformScaleSkin());
#ifdef HUSK_GLTF_VALIDATOR
    std::cerr << "husk-tests:   HUSK_GLTF_VALIDATOR: " << HUSK_GLTF_VALIDATOR << "\n";
#else
    std::cerr << "husk-tests:   HUSK_GLTF_VALIDATOR: not found on PATH -- validator test will skip\n";
#endif
#if defined(HUSK_BLENDER) && defined(HUSK_BLENDER_IMPORT_SCRIPT)
    std::cerr << "husk-tests:   HUSK_BLENDER: " << HUSK_BLENDER << "\n";
#else
    std::cerr << "husk-tests:   HUSK_BLENDER: not found on PATH -- Blender import test will skip\n";
#endif

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    int result = context.run();
    if (context.shouldExit()) {
        return result;
    }
    return result;
}
