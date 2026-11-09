// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- real
// M2TextureTransform fixtures, read back from the exported bundle's
// resources.uv_maps (REFACTOR/BUNDLE_FORMAT.md; a layer's uv_map carries
// its coordinate animation as `transform` curves). Same "deliberately not
// mocked, shape-only assertions where possible" policy as
// test_integration.cpp.

#include <doctest/doctest.h>
#include <filesystem>
#include <string>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testTextureTransformRotationM2;
using husk::test::testTextureTransformRotationSkin;
using Json = nlohmann::json;

}  // namespace

// brewfestmount.m2: global-sequence-driven UV scrolling (many keys), a
// constant 180-degree rotation, and a structured-but-trivial scaling track
// (m2_animation.cpp's trackHasAnimatedData tells it from "no track").
TEST_CASE("husk export: brewfestmount.m2's texture transforms -- global-loop scrolling, "
          "a constant rotation, a trivial scaling -- are each carried as uv_map curves" *
          doctest::skip(testTextureTransformRotationM2().empty() ||
                         testTextureTransformRotationSkin().empty())) {
    auto bundle = fs::temp_directory_path() / "husk-test-texture-transform-rot.bundle";
    fs::remove_all(bundle);

    auto result = runHusk("export \"" + testTextureTransformRotationM2() + "\" -o \"" + bundle.string() +
                           "\" --skin \"" + testTextureTransformRotationSkin() + "\" --textures none");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    bool foundScaling = false;
    bool foundConstantRotation = false;
    bool foundGlobalTranslation = false;
    for (const Json& uvMap : manifest["resources"]["uv_maps"]) {
        if (!uvMap.contains("transform")) continue;
        const Json& transform = uvMap["transform"];
        if (transform.contains("scaling")) {
            const Json& keyframes = transform["scaling"]["keyframes"];
            const Json& value = keyframes[0][1];
            foundScaling |= keyframes.size() == 1 && value[0] == 1.0 && value[1] == 1.0 && value[2] == 0.0;
        }
        // The constant 180-degree Z rotation: one step key.
        if (transform.contains("rotation")) {
            const Json& rotation = transform["rotation"];
            const Json& value = rotation["keyframes"][0][1];
            foundConstantRotation |= rotation["keyframes"].size() == 1 && rotation["interpolation"] == "step" &&
                                     value == Json::array({0.0, 0.0, -1.0, 0.0});
        }
        // Scrolling driven by a global sequence: its loop, many keys.
        if (transform.contains("translation")) {
            const Json& translation = transform["translation"];
            foundGlobalTranslation |= translation["sequence_kind"] == "global_sequence" &&
                                      translation["keyframes"].size() > 2;
        }
    }
    CHECK(foundScaling);
    CHECK(foundConstantRotation);
    CHECK(foundGlobalTranslation);

    fs::remove_all(bundle);
}
