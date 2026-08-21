// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- a real
// M2Light fixture specifically (animated ambient/diffuse color+intensity/
// attenuation/visibility curves, gltf_skeleton.cpp's "light_animation" node
// extras) -- split into its own file rather than added to
// test_integration_weapons.cpp since every fixture there is weapon-scoped
// and this one isn't (M2Light data is essentially absent from creature/item
// models in practice -- every weapon/creature fixture already committed has
// lights.count == 0; a real login-screen "glue" model was needed instead).
// Same "deliberately not mocked, shape-only/range assertions" policy as
// test_integration_weapons.cpp/test_integration_texture_transform.cpp.

#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>
#include <tiny_gltf.h>

#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testLightM2;

// A real login-screen glue model (ui_mainmenu_pandaria.m2, found via a
// corpus scan of interface/glues/models/ui_mainmenu_*) with 2 real M2Light
// records, both carrying genuine per-sequence keyframe data on at least one
// animated field -- verified by hand (`husk export` + inspecting the
// resulting .glb) before writing this test. Checks the "type"/
// "light_animation" node extras (gltf_skeleton.cpp) decode to plausible,
// finite values, not garbage or NaN -- same discipline as the weapon
// fade_animation real-data test.
TEST_CASE("husk export: a real glue model's light nodes get plausible 'type'/'light_animation' "
          "extras, not garbage or NaN" *
          doctest::skip(testLightM2().empty())) {
    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-light-animation.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + testLightM2() + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    auto checkScalarCurves = [](const tinygltf::Value& curves) {
        REQUIRE(curves.IsArray());
        for (size_t i = 0; i < curves.ArrayLen(); ++i) {
            const auto& kfs = curves.Get(i).Get("keyframes");
            REQUIRE(kfs.IsArray());
            double lastTime = -1.0;
            for (size_t k = 0; k < kfs.ArrayLen(); ++k) {
                double t = kfs.Get(k).Get("time").GetNumberAsDouble();
                double v = kfs.Get(k).Get("value").GetNumberAsDouble();
                CHECK(std::isfinite(t));
                CHECK(std::isfinite(v));
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };
    auto checkColorCurves = [](const tinygltf::Value& curves) {
        REQUIRE(curves.IsArray());
        for (size_t i = 0; i < curves.ArrayLen(); ++i) {
            const auto& kfs = curves.Get(i).Get("keyframes");
            REQUIRE(kfs.IsArray());
            double lastTime = -1.0;
            for (size_t k = 0; k < kfs.ArrayLen(); ++k) {
                double t = kfs.Get(k).Get("time").GetNumberAsDouble();
                const auto& v = kfs.Get(k).Get("value");
                REQUIRE(v.IsArray());
                REQUIRE(v.ArrayLen() == 3);
                CHECK(std::isfinite(t));
                for (int c = 0; c < 3; ++c) {
                    double comp = v.Get(c).GetNumberAsDouble();
                    CHECK(std::isfinite(comp));
                    CHECK(comp >= 0.0);
                    CHECK(comp <= 1.0);
                }
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };

    int lightNodesFound = 0;
    int nodesWithAnimation = 0;
    for (const auto& node : model.nodes) {
        if (node.name.rfind("light_", 0) != 0) continue;
        ++lightNodesFound;
        REQUIRE(node.extras.IsObject());
        // type is always present, 0 (directional) or 1 (point) per
        // wowdev.wiki M2#Lights.
        int type = node.extras.Get("type").GetNumberAsInt();
        CHECK((type == 0 || type == 1));

        const auto& anim = node.extras.Get("light_animation");
        if (!anim.IsObject()) continue;
        ++nodesWithAnimation;
        if (anim.Get("ambient_color").IsArray()) checkColorCurves(anim.Get("ambient_color"));
        if (anim.Get("diffuse_color").IsArray()) checkColorCurves(anim.Get("diffuse_color"));
        if (anim.Get("ambient_intensity").IsArray()) checkScalarCurves(anim.Get("ambient_intensity"));
        if (anim.Get("diffuse_intensity").IsArray()) checkScalarCurves(anim.Get("diffuse_intensity"));
        if (anim.Get("attenuation_start").IsArray()) checkScalarCurves(anim.Get("attenuation_start"));
        if (anim.Get("attenuation_end").IsArray()) checkScalarCurves(anim.Get("attenuation_end"));
        if (anim.Get("visibility").IsArray()) checkScalarCurves(anim.Get("visibility"));
    }
    CHECK(lightNodesFound == 2);
    CHECK(nodesWithAnimation > 0);

    std::filesystem::remove(outPath);
}

}  // namespace
