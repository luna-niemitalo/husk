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

// brewfestmount.m2's transform index 0 carries real per-sequence
// translation/scaling tracks whose values all happen to be identity -- a
// structured-but-trivial track (m2_animation.cpp's trackHasAnimatedData
// distinguishes it from "no track at all"), which a cruder scan
// (tools/find_texture_transform_files.py) reported as constant. Its
// values are the animation of a sequence, so the bundle must carry them as
// a sequence's curve rather than dropping or folding them.
TEST_CASE("husk export: brewfestmount.m2's structurally per-sequence texture transform, every "
          "value identity, is carried as a sequence's uv_map transform curve" *
          doctest::skip(testTextureTransformRotationM2().empty() ||
                         testTextureTransformRotationSkin().empty())) {
    auto bundle = fs::temp_directory_path() / "husk-test-texture-transform-rot.bundle";
    fs::remove_all(bundle);

    auto result = runHusk("export \"" + testTextureTransformRotationM2() + "\" -o \"" + bundle.string() +
                           "\" --skin \"" + testTextureTransformRotationSkin() + "\" --textures none");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    Json manifest = parseManifest(bundle);
    bool foundIdentityScaling = false;
    for (const Json& uvMap : manifest["resources"]["uv_maps"]) {
        if (!uvMap.contains("transform")) continue;
        for (const auto& [property, curve] : uvMap["transform"].items()) {
            INFO(property);
            CHECK(curve["sequence_kind"] == "sequence");
            CHECK(curve["sequence_index"] == 0);
        }
        if (!uvMap["transform"].contains("scaling")) continue;
        const Json& keyframes = uvMap["transform"]["scaling"]["keyframes"];
        if (keyframes.size() != 1) continue;
        const Json& value = keyframes[0][1];
        if (keyframes[0][0] == 0 && value[0] == 1.0 && value[1] == 1.0 && value[2] == 0.0) {
            foundIdentityScaling = true;
        }
    }
    CHECK(foundIdentityScaling);

    fs::remove_all(bundle);
}
