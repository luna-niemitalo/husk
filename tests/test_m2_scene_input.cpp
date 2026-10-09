// m2input::assembleScene (m2_scene_input.hpp) against real fixtures: every
// parsed attachment/light/emitter reaches canon::Scene, each track resolves
// to the same keyframes the m2 resolvers give directly, and positions are
// model-space points on their bone's pivot.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_model.hpp"
#include "m2.hpp"
#include "m2_animation.hpp"
#include "m2_canon_input.hpp"
#include "m2_scene_input.hpp"
#include "skel.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void checkSamePoint(const canon::Vec3& a, const m2::Vec3& b) {
    CHECK(a.x == doctest::Approx(b.x).epsilon(1e-4));
    CHECK(a.y == doctest::Approx(b.y).epsilon(1e-4));
    CHECK(a.z == doctest::Approx(b.z).epsilon(1e-4));
}

size_t sequencesWithKeys(const std::vector<uint8_t>& blob, uint32_t trackOffset, uint32_t sequenceCount) {
    size_t n = 0;
    for (uint32_t si = 0; si < sequenceCount; ++si) {
        if (!m2::resolveFloatTrackSequence(blob, trackOffset, si).empty()) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("particleTextureIndices: one index, or three 5-bit indices for a MultiTexture emitter") {
    m2::ParticleEmitter single;
    single.textureId = 37;
    CHECK(m2input::particleTextureIndices(single) == std::vector<uint16_t>{37});

    m2::ParticleEmitter multi;
    multi.flags = 0x10000000;
    multi.textureId = static_cast<uint16_t>(3 | (4 << 5) | (5 << 10));
    CHECK(m2input::particleTextureIndices(multi) == std::vector<uint16_t>{3, 4, 5});
}

TEST_CASE("assembleScene: fox attachments carry id, bone, wiki name and a position on the bone's pivot" *
          doctest::skip(test::testFoxM2().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testFoxM2()));
    REQUIRE(!model.attachments.empty());

    canon::Scene scene = m2input::assembleScene(model, static_cast<uint32_t>(model.sequences.size()), {}, {}, nullptr);

    REQUIRE(scene.attachments.size() == model.attachments.size());
    for (size_t i = 0; i < model.attachments.size(); ++i) {
        const m2::Attachment& source = model.attachments[i];
        const canon::Attachment& a = scene.attachments[i];
        CHECK(a.pointId == source.id);
        CHECK(std::get<canon::RecordIndex>(a.bone.id).value == static_cast<uint32_t>(source.bone));
        checkSamePoint(a.position, model.bones[static_cast<size_t>(source.bone)].pivot);
    }
    CHECK(scene.attachments[0].ref.name == m2::attachmentTypeName(model.attachments[0].id));
    CHECK(scene.events.size() == model.events.size());
}

TEST_CASE("assembleScene: weapon emitters keep every texture, curve and lifetime curve" *
          doctest::skip(test::testWeaponParticleA().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testWeaponParticleA()));
    REQUIRE(!model.particleEmitters.empty());
    REQUIRE(!model.ribbonEmitters.empty());
    auto sequenceCount = static_cast<uint32_t>(model.sequences.size());

    canon::Scene scene = m2input::assembleScene(model, sequenceCount, {}, {}, nullptr);

    REQUIRE(scene.particles.size() == model.particleEmitters.size());
    for (size_t i = 0; i < model.particleEmitters.size(); ++i) {
        const m2::ParticleEmitter& source = model.particleEmitters[i];
        const canon::ParticleEmitter& p = scene.particles[i];
        CHECK(p.textures.size() == m2input::particleTextureIndices(source).size());
        CHECK(p.emissionRate.size() == sequencesWithKeys(model.blob, source.emissionRateTrackOffset, sequenceCount));
        CHECK(p.color.keyframes.size() == m2::resolveFBlockVec3(model.blob, source.colorTrackBlockOffset).size());
        CHECK(p.scale.keyframes.size() == m2::resolveFBlockVec2(model.blob, source.scaleTrackBlockOffset).size());
        checkSamePoint(p.position, model.bones[source.boneId].pivot);
    }

    REQUIRE(scene.ribbons.size() == model.ribbonEmitters.size());
    const m2::Ribbon& ribbonSource = model.ribbonEmitters[0];
    const canon::RibbonEmitter& ribbon = scene.ribbons[0];
    CHECK(ribbon.textures.size() == ribbonSource.textureIndices.size());
    CHECK(ribbon.materials.size() == ribbonSource.materialIndices.size());
    CHECK(ribbon.heightAbove.size() == sequencesWithKeys(model.blob, ribbonSource.heightAboveTrackOffset, sequenceCount));
    // No resolutions were supplied, so every texture says why it is unresolved.
    CHECK(ribbon.textures[0].state == canon::TextureRef::State::KnownUnresolved);
    CHECK(!ribbon.textures[0].unresolvedReason.empty());
}

TEST_CASE("assembleScene: lights resolve every animated property per sequence" *
          doctest::skip(test::testLightM2().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testLightM2()));
    REQUIRE(!model.lights.empty());
    auto sequenceCount = static_cast<uint32_t>(model.sequences.size());

    canon::Scene scene = m2input::assembleScene(model, sequenceCount, {}, {}, nullptr);

    REQUIRE(scene.lights.size() == model.lights.size());
    for (size_t i = 0; i < model.lights.size(); ++i) {
        const m2::Light& source = model.lights[i];
        const canon::Light& light = scene.lights[i];
        CHECK(light.type == source.type);
        CHECK(light.bone.has_value() == (source.bone >= 0));
        CHECK(light.ambientIntensity.size() ==
              sequencesWithKeys(model.blob, source.ambientIntensityTrackOffset, sequenceCount));
        CHECK(light.attenuationEnd.size() ==
              sequencesWithKeys(model.blob, source.attenuationEndTrackOffset, sequenceCount));
    }
}

TEST_CASE("buildCanonModel: a .skel-sourced model takes its attachments from SKA1, on the .skel's bone pivots" *
          doctest::skip(test::testSkelM2().empty() || test::testSkelSkin().empty() || test::testSkel().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testSkelM2()));
    REQUIRE(model.attachments.empty());
    std::vector<uint8_t> skinFile = readFile(test::testSkelSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<uint8_t> skelBytes = readFile(test::testSkel());

    m2input::ExternalSkeletonSource skel;
    skel.bones = skel::parseBones(skelBytes);
    skel.blob = skel::boneTrackBlob(skelBytes);
    skel.sequences = skel::parseSequences(skelBytes);
    skel.attachments = skel::findAttachments(skelBytes);
    REQUIRE(skel.attachments.has_value());
    REQUIRE(!skel.attachments->attachments.empty());

    canon::Model result = m2input::buildCanonModel(model, skin::parseBatches(skinFile, header.batches),
                                                   skin::parseSubmeshes(skinFile, header.submeshes),
                                                   skin::resolveTriangleIndices(skinFile, header), {}, {}, &skel);

    REQUIRE(result.scene.attachments.size() == skel.attachments->attachments.size());
    for (size_t i = 0; i < result.scene.attachments.size(); ++i) {
        const m2::Attachment& source = skel.attachments->attachments[i];
        checkSamePoint(result.scene.attachments[i].position, skel.bones[static_cast<size_t>(source.bone)].pivot);
    }
}
