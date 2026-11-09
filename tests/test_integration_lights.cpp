// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- a real
// M2Light fixture specifically (animated ambient/diffuse color+intensity/
// attenuation/visibility curves, the bundle's resources.scene.
// animated_lights) -- its own file rather than test_integration_weapons.cpp
// since every fixture there is weapon-scoped and this one isn't (M2Light
// data is essentially absent from creature/item models in practice; a real
// login-screen "glue" model was needed instead). Same "deliberately not
// mocked, shape-only/range assertions" policy as
// test_integration_weapons.cpp.

#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>

#include "m2.hpp"
#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testLightM2;

// A real login-screen glue model (ui_mainmenu_pandaria.m2, found via a
// corpus scan of interface/glues/models/ui_mainmenu_*) with 2 real M2Light
// records carrying keyframe data -- checked by hand before writing this
// test. The light curves must decode to plausible, finite values, not
// garbage or NaN -- same discipline as the weapon alpha-fade real-data test.
TEST_CASE("husk export: a real glue model's animated lights get a plausible kind/type and curves, "
          "not garbage or NaN" *
          doctest::skip(testLightM2().empty())) {
    fs::path dir = fs::temp_directory_path() / "husk-test-light-animation.bundle";
    fs::remove_all(dir);
    auto result = runHusk("export \"" + testLightM2() + "\" -o \"" + dir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);

    auto checkScalarCurves = [](const nlohmann::json& curves) {
        REQUIRE(curves.is_array());
        for (const auto& curve : curves) {
            double lastTime = -1.0;
            for (const auto& kf : curve["keyframes"]) {
                auto t = kf[0].get<double>();
                auto v = kf[1].get<double>();
                CHECK(std::isfinite(t));
                CHECK(std::isfinite(v));
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };
    auto checkColorCurves = [](const nlohmann::json& curves) {
        REQUIRE(curves.is_array());
        for (const auto& curve : curves) {
            double lastTime = -1.0;
            for (const auto& kf : curve["keyframes"]) {
                auto t = kf[0].get<double>();
                const auto& v = kf[1];
                REQUIRE(v.size() == 3);
                CHECK(std::isfinite(t));
                for (const auto& component : v) {
                    auto c = component.get<double>();
                    CHECK(std::isfinite(c));
                    CHECK(c >= 0.0);
                    CHECK(c <= 1.0);
                }
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };

    const auto& lights = manifest["resources"]["scene"]["animated_lights"];
    CHECK(lights.size() == husk::m2::loadFile(testLightM2()).lights.count);
    CHECK(lights.size() == 2);
    size_t lightsWithKeyframes = 0;
    for (const auto& light : lights) {
        // type is 0 (directional) or 1 (point) per wowdev.wiki M2#Lights,
        // and the kind names it.
        auto type = light["source"]["type"].get<int>();
        CHECK((type == 0 || type == 1));
        CHECK(light["kind"] == (type == 0 ? "directional" : "point"));

        checkColorCurves(light["ambient_color"]);
        checkColorCurves(light["diffuse_color"]);
        checkScalarCurves(light["ambient_intensity"]);
        checkScalarCurves(light["diffuse_intensity"]);
        checkScalarCurves(light["attenuation_start"]);
        checkScalarCurves(light["attenuation_end"]);
        checkScalarCurves(light["visibility"]);

        bool hasKeyframes = false;
        for (const char* field : {"ambient_color", "diffuse_color", "ambient_intensity", "diffuse_intensity",
                                  "attenuation_start", "attenuation_end", "visibility"}) {
            for (const auto& curve : light[field]) hasKeyframes = hasKeyframes || !curve["keyframes"].empty();
        }
        if (hasKeyframes) ++lightsWithKeyframes;
    }
    CHECK(lightsWithKeyframes > 0);

    fs::remove_all(dir);
}

}  // namespace
