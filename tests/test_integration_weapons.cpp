// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- real
// weapon-model fixtures specifically (ribbon/particle emitters, .phys
// bodies, attachment/event/light nodes, animated tint/fade) -- split out
// of test_integration.cpp per FILE_SPLIT_TODO.md's Item 5 re-measurement
// (test_integration.cpp had crept back over the 1000-line hard limit).
// Same "deliberately not mocked, shape-only assertions" policy as the file
// it was split from -- see that file's own doc comment.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <tiny_gltf.h>

#include "m2.hpp"
#include "run_husk.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testWeaponParticleA;
using husk::test::testWeaponParticleB;
using husk::test::testWeaponParticleStress;
using husk::test::testWeaponPhys;
using husk::test::testWeaponPhysSkin;
using husk::test::testWeaponRibbon;

// Real ribbon/particle emitter data (weapon models -- see
// tests/test_data_paths.hpp's kWeaponRibbon/kWeaponParticleA/B/Stress doc
// comment for how these were chosen). Checks the exported .glb's skin
// extras ribbon_emitters/particle_emitters anchor arrays against the real
// header counts -- exact, not a tolerance.
void checkEmitterAnchorCounts(const std::string& m2Path, size_t expectedRibbons,
                               size_t expectedParticles) {
    auto header = husk::m2::loadFile(m2Path);
    CHECK(header.ribbonEmitters.count == expectedRibbons);
    CHECK(header.particleEmitters.count == expectedParticles);

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-emitters.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    const auto& extras = model.skins[0].extras;

    if (expectedRibbons > 0) {
        REQUIRE(extras.IsObject());
        const auto& ribbons = extras.Get("ribbon_emitters");
        REQUIRE(ribbons.IsArray());
        CHECK(static_cast<size_t>(ribbons.ArrayLen()) == expectedRibbons);
        const auto& r0 = ribbons.Get(0);
        CHECK(r0.Get("joint").GetNumberAsInt() >= 0);
        REQUIRE(r0.Get("position").IsObject());
    } else if (extras.IsObject()) {
        CHECK_FALSE(extras.Get("ribbon_emitters").IsArray());
    }

    if (expectedParticles > 0) {
        REQUIRE(extras.IsObject());
        const auto& particles = extras.Get("particle_emitters");
        REQUIRE(particles.IsArray());
        CHECK(static_cast<size_t>(particles.ArrayLen()) == expectedParticles);
    } else if (extras.IsObject()) {
        CHECK_FALSE(extras.Get("particle_emitters").IsArray());
    }

    std::filesystem::remove(outPath);
}

}  // namespace

TEST_CASE("husk export: a real ribbon-only weapon (Ashbringer) gets exactly 3 ribbon anchors, 0 "
          "particle anchors" *
          doctest::skip(testWeaponRibbon().empty())) {
    checkEmitterAnchorCounts(testWeaponRibbon(), /*expectedRibbons=*/3, /*expectedParticles=*/0);
}

TEST_CASE("husk export: a real combined ribbon+particle weapon gets exactly 1 ribbon anchor and "
          "2 particle anchors" *
          doctest::skip(testWeaponParticleA().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleA(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a second real combined ribbon+particle weapon gets exactly 1 ribbon "
          "anchor and 2 particle anchors" *
          doctest::skip(testWeaponParticleB().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleB(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a real 64-particle-emitter weapon (stress case) gets exactly 64 particle "
          "anchors, 0 ribbon anchors" *
          doctest::skip(testWeaponParticleStress().empty())) {
    checkEmitterAnchorCounts(testWeaponParticleStress(), /*expectedRibbons=*/0,
                              /*expectedParticles=*/64);
}

TEST_CASE("husk export: a real weapon's .phys sidecar (auto-detected, same basename) attaches "
          "exactly 10 physics_bodies extras, one per real body, boneIndex values {0..9} of 17 "
          "real bones" *
          doctest::skip(testWeaponPhys().empty() || testWeaponPhysSkin().empty())) {
    std::string m2Path = testWeaponPhys();
    std::string skinPath = testWeaponPhysSkin();

    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-export-phys.glb").string();
    std::filesystem::remove(outPath);
    auto result =
        runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\" --skin \"" + skinPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.skins.size() == 1);
    const auto& extras = model.skins[0].extras;
    REQUIRE(extras.IsObject());
    const auto& bodies = extras.Get("physics_bodies");
    REQUIRE(bodies.IsArray());
    REQUIRE(static_cast<size_t>(bodies.ArrayLen()) == 10);

    std::set<int> boneIndices;
    for (int i = 0; i < bodies.ArrayLen(); ++i) {
        const auto& b = bodies.Get(i);
        CHECK(b.Get("id").GetNumberAsInt() == i);
        int joint = b.Get("joint").GetNumberAsInt();
        CHECK(joint >= 0);
        CHECK(joint < 17);  // this fixture's own real bone count
        REQUIRE(b.Get("position").IsObject());
        boneIndices.insert(joint);
    }
    // Every body claims a distinct bone -- confirmed by hand against the
    // real .phys file (boneIndex values {0..9} of 17 real bones).
    CHECK(boneIndices.size() == 10);
    CHECK(*boneIndices.begin() == 0);
    CHECK(*boneIndices.rbegin() == 9);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk export: a real weapon's attachments/events/lights become exactly "
          "header.attachments.count/events.count/lights.count real child nodes, never "
          "counted as skeleton joints" *
          doctest::skip(testWeaponParticleStress().empty())) {
    auto m2Path = testWeaponParticleStress();
    auto header = husk::m2::loadFile(m2Path);
    // This fixture's own real counts (checked by hand via `husk info`) --
    // the case picked deliberately because it's the only committed fixture
    // with all three non-zero at once.
    CHECK(header.attachments.count == 5);
    CHECK(header.events.count == 2);
    CHECK(header.lights.count == 4);

    auto outPath =
        (std::filesystem::temp_directory_path() / "husk-test-export-anchor-nodes.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + outPath + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, outPath);
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);

    size_t attachmentNodes = 0, eventNodes = 0, lightNodes = 0;
    for (const auto& n : model.nodes) {
        if (n.name.rfind("attachment_", 0) == 0) {
            ++attachmentNodes;
        } else if (n.name.rfind("event_", 0) == 0) {
            ++eventNodes;
        } else if (n.name.rfind("light_", 0) == 0) {
            ++lightNodes;
        }
    }
    CHECK(attachmentNodes == header.attachments.count);
    CHECK(eventNodes == header.events.count);
    CHECK(lightNodes == header.lights.count);

    // M2Event::data round-trips as this fixture's real raw values -- cross-
    // checked against a direct re-parse of the same file (independent of
    // the export path) rather than assumed. `husk info` on this same
    // fixture shows both real events ($WTB/$WTT) carry `data=0` -- not a
    // placeholder, the field's genuine on-disk value here.
    std::ifstream m2Stream(m2Path, std::ios::binary);
    std::vector<uint8_t> fileBytes((std::istreambuf_iterator<char>(m2Stream)),
                                    std::istreambuf_iterator<char>());
    auto blob = husk::m2::extractBlob(fileBytes);
    auto rawEvents = husk::m2::parseEvents(blob, header.events);
    for (const auto& n : model.nodes) {
        if (n.name.rfind("event_", 0) != 0) continue;
        auto identifier = n.name.substr(std::strlen("event_"));
        auto it = std::find_if(rawEvents.begin(), rawEvents.end(),
                                [&](const auto& e) { return e.identifier == identifier; });
        REQUIRE(it != rawEvents.end());
        REQUIRE(n.extras.IsObject());
        CHECK(static_cast<uint32_t>(n.extras.Get("data").GetNumberAsInt()) == it->data);
    }

    // Never added to skin.joints -- this fixture's real bone count is 78.
    // skin.joints does legitimately grow past that by one geoset tag joint
    // per distinct geoset ID -- counted here from
    // each primitive's own "geoset_id" extras (gltf_mesh.cpp), a real
    // cross-check against gltf_skeleton.cpp's independent
    // Skeleton::geosetTags-driven joint count, not a tautology.
    std::set<int> geosetIds;
    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (prim.extras.IsObject() && prim.extras.Has("geoset_id")) {
                geosetIds.insert(prim.extras.Get("geoset_id").GetNumberAsInt());
            }
        }
    }
    REQUIRE(model.skins.size() == 1);
    CHECK(model.skins[0].joints.size() == 78 + geosetIds.size());

    std::filesystem::remove(outPath);
}

// a real weapon's animated tint/fade curve
// ('fade_animation' extras) decodes to plausible, finite, in-range values --
// not garbage or NaN. Ashbringer (kWeaponRibbon) is known (checked by hand
// running `husk export` and inspecting the resulting .glb before writing
// this test) to have real animated M2TextureWeight/M2Color batches.
TEST_CASE("husk export: a real weapon's 'fade_animation' extras decode to finite, in-range "
          "keyframe values" *
          doctest::skip(testWeaponRibbon().empty())) {
    auto outPath = (std::filesystem::temp_directory_path() / "husk-test-fade-animation.glb").string();
    std::filesystem::remove(outPath);
    auto result = runHusk("export \"" + testWeaponRibbon() + "\" -o \"" + outPath + "\"");
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
        for (int i = 0; i < curves.ArrayLen(); ++i) {
            const auto& c = curves.Get(i);
            REQUIRE(c.Get("keyframes").IsArray());
            double lastTime = -1.0;
            for (int k = 0; k < c.Get("keyframes").ArrayLen(); ++k) {
                const auto& kf = c.Get("keyframes").Get(k);
                double t = kf.Get("time").GetNumberAsDouble();
                double v = kf.Get("value").GetNumberAsDouble();
                CHECK(std::isfinite(t));
                CHECK(std::isfinite(v));
                CHECK(v >= 0.0);
                CHECK(v <= 1.0);
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    };

    bool foundFadeAnimation = false;
    for (const auto& mat : model.materials) {
        if (!mat.extras.IsObject()) {
            continue;
        }
        const auto& fade = mat.extras.Get("fade_animation");
        if (!fade.IsObject()) {
            continue;
        }
        foundFadeAnimation = true;
        if (fade.Get("alpha").IsArray()) {
            checkScalarCurves(fade.Get("alpha"));
        }
        if (fade.Get("weight").IsArray()) {
            checkScalarCurves(fade.Get("weight"));
        }
    }
    REQUIRE(foundFadeAnimation);

    std::filesystem::remove(outPath);
}

TEST_CASE("husk dump-chunks: a real weapon's particle_emitters JSON resolves plausible, "
          "finite color/alpha/scale curve values, not garbage or NaN" *
          doctest::skip(testWeaponParticleStress().empty())) {
    auto result = runHusk("dump-chunks \"" + testWeaponParticleStress() + "\"");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("\"particle_id\"") != std::string::npos);
    CHECK(result.output.find("\"color_track\"") != std::string::npos);
    CHECK(result.output.find("\"alpha_track\"") != std::string::npos);
    // A NaN keyframe would serialize as the JSON literal `null` (see
    // json::Writer::value(double)'s isfinite guard) -- if that ever shows
    // up inside a resolved curve, something upstream decoded garbage.
    CHECK(result.output.find("\"value\": null") == std::string::npos);
}
