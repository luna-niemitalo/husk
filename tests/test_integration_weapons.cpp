// Integration tier (see TEST_DESIGN.md#Four-tier-architecture) -- real
// weapon-model fixtures specifically (ribbon/particle emitters, .phys
// bodies, attachment/event/light records, animated fade), split out of
// test_integration.cpp to keep it under FILE_SIZE.md's 1000-line ceiling.
// Same "deliberately not mocked, shape-only assertions" policy as the file
// it was split from -- see that file's own doc comment.

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "m2.hpp"
#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_data_paths.hpp"

namespace {

using husk::test::runHusk;
using husk::test::testWeaponParticleA;
using husk::test::testWeaponParticleB;
using husk::test::testWeaponParticleStress;
using husk::test::testWeaponPhys;
using husk::test::testWeaponPhysSkin;
using husk::test::testWeaponRibbon;

fs::path freshBundleDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / (name + ".bundle");
    fs::remove_all(dir);
    return dir;
}

void writeOnePixelPng(const fs::path& path) {
    std::ofstream pf(path, std::ios::binary);
    static const unsigned char kPng1x1[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8,
        0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0x18, 0xdd, 0x8d, 0xb0, 0x00, 0x00, 0x00,
        0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    pf.write(reinterpret_cast<const char*>(kPng1x1), sizeof(kPng1x1));
}

// The emitter instances of the model's inline effects set(s) whose source
// record is `table` (M2Ribbon or M2Particle) -- every emitter is placed as
// an effect instance (REFACTOR/EFFECTS.md), not a scene record.
std::vector<nlohmann::json> emitterInstances(const nlohmann::json& manifest, const std::string& table) {
    std::vector<nlohmann::json> found;
    for (const auto& reference : manifest["references"]) {
        if (reference["kind"] != "placement_set" || !reference.contains("document")) continue;
        for (const auto& instance : reference["document"]["instances"]) {
            if (instance["source"]["record"]["table"] == table) found.push_back(instance);
        }
    }
    return found;
}

// Real ribbon/particle emitter data (weapon models -- see
// tests/test_data_paths.hpp's kWeaponRibbon/kWeaponParticleA/B/Stress doc
// comment for how these were chosen). Checks the bundle's emitter
// placements against the real header counts -- exact, not a tolerance.
void checkEmitterPlacementCounts(const std::string& m2Path, size_t expectedRibbons, size_t expectedParticles) {
    auto header = husk::m2::loadFile(m2Path);
    CHECK(header.ribbonEmitters.count == expectedRibbons);
    CHECK(header.particleEmitters.count == expectedParticles);

    fs::path dir = freshBundleDir("husk-test-export-emitters");
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);
    auto jointCount = manifest["resources"]["rig"]["skeleton"]["joint_count"].get<int64_t>();

    auto ribbons = emitterInstances(manifest, "M2Ribbon");
    CHECK(ribbons.size() == expectedRibbons);
    auto particles = emitterInstances(manifest, "M2Particle");
    CHECK(particles.size() == expectedParticles);
    for (const auto* set : {&ribbons, &particles}) {
        for (const auto& instance : *set) {
            CAPTURE(instance["source"]);
            REQUIRE(instance["anchor"]["id"]["kind"] == "record_index");
            CHECK(instance["anchor"]["id"]["value"].get<int64_t>() >= 0);
            CHECK(instance["anchor"]["id"]["value"].get<int64_t>() < jointCount);
            CHECK(instance["translation"].size() == 3);
        }
    }

    fs::remove_all(dir);
}

}  // namespace

TEST_CASE("husk export: a real ribbon-only weapon (Ashbringer) gets exactly 3 ribbon placements, 0 "
          "particle placements" *
          doctest::skip(testWeaponRibbon().empty())) {
    checkEmitterPlacementCounts(testWeaponRibbon(), /*expectedRibbons=*/3, /*expectedParticles=*/0);
}

TEST_CASE("husk export: a real combined ribbon+particle weapon gets exactly 1 ribbon placement and "
          "2 particle placements" *
          doctest::skip(testWeaponParticleA().empty())) {
    checkEmitterPlacementCounts(testWeaponParticleA(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a second real combined ribbon+particle weapon gets exactly 1 ribbon "
          "placement and 2 particle placements" *
          doctest::skip(testWeaponParticleB().empty())) {
    checkEmitterPlacementCounts(testWeaponParticleB(), /*expectedRibbons=*/1, /*expectedParticles=*/2);
}

TEST_CASE("husk export: a real 64-particle-emitter weapon (stress case) gets exactly 64 particle "
          "placements, 0 ribbon placements" *
          doctest::skip(testWeaponParticleStress().empty())) {
    checkEmitterPlacementCounts(testWeaponParticleStress(), /*expectedRibbons=*/0, /*expectedParticles=*/64);
}

TEST_CASE("husk export: a real weapon's .phys sidecar (auto-detected, same basename) becomes exactly "
          "10 physics bodies, one per real body, joints {0..9} of 17 real bones" *
          doctest::skip(testWeaponPhys().empty() || testWeaponPhysSkin().empty())) {
    fs::path dir = freshBundleDir("husk-test-export-phys");
    auto result = runHusk("export \"" + testWeaponPhys() + "\" -o \"" + dir.string() + "\" --skin \"" +
                          testWeaponPhysSkin() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    CHECK(manifest["resources"]["rig"]["skeleton"]["joint_count"] == 17);  // this fixture's own real bone count
    REQUIRE(manifest["resources"].contains("physics"));
    const auto& bodies = manifest["resources"]["physics"]["bodies"];
    REQUIRE(bodies.size() == 10);

    std::set<int64_t> joints;
    for (const auto& b : bodies) {
        auto joint = b["joint"].get<int64_t>();
        CHECK(joint >= 0);
        CHECK(joint < 17);
        CHECK(b["position"].size() == 3);
        joints.insert(joint);
    }
    // Every body claims a distinct bone -- confirmed by hand against the
    // real .phys file (boneIndex values {0..9} of 17 real bones).
    CHECK(joints.size() == 10);
    CHECK(*joints.begin() == 0);
    CHECK(*joints.rbegin() == 9);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a real weapon's attachments/events/lights become exactly "
          "header.attachments.count/events.count/lights.count scene records, never counted as "
          "skeleton joints" *
          doctest::skip(testWeaponParticleStress().empty())) {
    auto m2Path = testWeaponParticleStress();
    auto header = husk::m2::loadFile(m2Path);
    // This fixture's own real counts (checked by hand via `husk info`) --
    // the only committed fixture with all three non-zero at once.
    CHECK(header.attachments.count == 5);
    CHECK(header.events.count == 2);
    CHECK(header.lights.count == 4);

    fs::path dir = freshBundleDir("husk-test-export-scene");
    auto result = runHusk("export \"" + m2Path + "\" -o \"" + dir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);

    nlohmann::json manifest = parseManifest(dir);
    const auto& scene = manifest["resources"]["scene"];
    CHECK(scene["attachments"].size() == header.attachments.count);
    CHECK(scene["events"].size() == header.events.count);
    CHECK(scene["animated_lights"].size() == header.lights.count);

    // M2Event::data round-trips as this fixture's real raw values -- cross-
    // checked against a direct re-parse of the same file (independent of
    // the export path). Both real events ($WTB/$WTT) carry data=0 -- the
    // field's genuine on-disk value here, not a placeholder.
    auto blob = husk::m2::extractBlob(readFile(m2Path));
    auto rawEvents = husk::m2::parseEvents(blob, header.events);
    for (const auto& event : scene["events"]) {
        auto identifier = event["identifier"].get<std::string>();
        auto it = std::find_if(rawEvents.begin(), rawEvents.end(),
                               [&](const auto& e) { return e.identifier == identifier; });
        REQUIRE(it != rawEvents.end());
        CHECK(event["data"].get<uint32_t>() == it->data);
    }

    // Scene records are never joints: the skeleton is exactly this
    // fixture's 78 real bones.
    CHECK(header.bones.count == 78);
    CHECK(manifest["resources"]["rig"]["skeleton"]["joint_count"] == header.bones.count);

    fs::remove_all(dir);
}

// A real weapon's animated transparency fade decodes to plausible, finite,
// in-range values -- not garbage or NaN. Ashbringer (kWeaponRibbon) has
// real animated M2TextureWeight/M2Color batches, checked by hand.
TEST_CASE("husk export: a real weapon's layer alpha fade curves decode to finite, in-range, time-ordered "
          "keyframes" *
          doctest::skip(testWeaponRibbon().empty())) {
    fs::path dir = freshBundleDir("husk-test-fade-animation");
    auto result = runHusk("export \"" + testWeaponRibbon() + "\" -o \"" + dir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);

    size_t animatedFades = 0;
    for (const auto& material : manifest["resources"]["materials"]) {
        for (const auto& layer : material["layers"]) {
            if (!layer.contains("alpha_fade")) continue;
            const auto& keyframes = layer["alpha_fade"]["keyframes"];
            if (keyframes.size() > 1) ++animatedFades;
            double lastTime = -1.0;
            for (const auto& kf : keyframes) {
                auto t = kf[0].get<double>();
                auto v = kf[1].get<double>();
                CHECK(std::isfinite(t));
                CHECK(std::isfinite(v));
                CHECK(v >= 0.0);
                CHECK(v <= 1.0);
                CHECK(t >= lastTime);
                lastTime = t;
            }
        }
    }
    CHECK(animatedFades > 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export --object-skin-texture-id: fills a real fdid==0 type=2 (object_skin) "
          "slot with the supplied FileDataID's texture" *
          doctest::skip(testWeaponRibbon().empty())) {
    auto texDir = fs::temp_directory_path() / "husk-test-object-skin-tex";
    fs::remove_all(texDir);
    fs::create_directories(texDir);
    writeOnePixelPng(texDir / "999999999.png");

    fs::path dir = freshBundleDir("husk-test-object-skin-tex");
    auto result = runHusk("export \"" + testWeaponRibbon() + "\" -o \"" + dir.string() + "\" --textures \"" +
                          texDir.string() + "\" --object-skin-texture-id 999999999");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);
    const auto& slots = manifest["definition"]["texture_slots"];

    bool foundObjectSkinTexture = false;
    for (const auto& material : manifest["resources"]["materials"]) {
        for (const auto& layer : material["layers"]) {
            if (!layer.contains("slot") || slots[layer["slot"].get<size_t>()]["source"]["texture_type"] != 2) continue;
            const auto& texture = textureAt(manifest, layer["texture"]);
            if (texture["state"] != "resolved") continue;
            CHECK(texture["identity"]["id"]["value"] == 999999999);
            CHECK(fs::exists(dir / texture["payload"]["uri"].get<std::string>()));
            foundObjectSkinTexture = true;
        }
    }
    CHECK(foundObjectSkinTexture);

    fs::remove_all(dir);
    fs::remove_all(texDir);
}

TEST_CASE("husk export: a race/gender-suffixed model with no exact-basename texture falls back "
          "to its family basename (suffix stripped), same directory" *
          doctest::skip(testWeaponRibbon().empty())) {
    auto workDir = fs::temp_directory_path() / "husk-test-race-suffix-fallback";
    fs::remove_all(workDir);
    fs::create_directories(workDir);

    // Real weapon fixture data, copied under a race/gender-suffixed name
    // ("_orm") -- the model's own bytes don't need to match this filename,
    // only the basename-fuzzy-matching path being exercised cares about it.
    auto m2Src = fs::path(testWeaponRibbon());
    std::string familyBasename = "husk_test_family_item";
    auto m2Dst = workDir / (familyBasename + "_orm.m2");
    fs::copy_file(m2Src, m2Dst);
    for (const auto& entry : fs::directory_iterator(m2Src.parent_path())) {
        auto skinName = entry.path().filename().string();
        if (skinName.rfind(m2Src.stem().string(), 0) == 0 && entry.path().extension() == ".skin") {
            std::string suffix = skinName.substr(m2Src.stem().string().size());
            fs::copy_file(entry.path(), workDir / (familyBasename + "_orm" + suffix));
        }
    }
    writeOnePixelPng(workDir / (familyBasename + ".png"));

    fs::path dir = workDir / "out.bundle";
    auto result = runHusk("export \"" + m2Dst.string() + "\" -o \"" + dir.string() + "\"");
    INFO("output:\n", result.output);
    REQUIRE(result.exitCode == 0);
    nlohmann::json manifest = parseManifest(dir);

    bool foundFamilyTexture = false;
    for (const auto& texture : manifest["resources"]["textures"]) {
        if (texture["state"] != "resolved" || !texture.contains("payload")) continue;
        if (texture["identity"]["name"] != familyBasename) continue;
        foundFamilyTexture = true;
        CHECK(fs::exists(dir / texture["payload"]["uri"].get<std::string>()));
    }
    CHECK(foundFamilyTexture);

    fs::remove_all(workDir);
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
