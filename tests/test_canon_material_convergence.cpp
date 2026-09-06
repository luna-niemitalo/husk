// Structural convergence proof for canon::assembleMaterial
// (canon_material_builder.hpp) against export_materials.cpp's per-batch
// material construction, the real production logic -- REFACTOR/README.md
// stage 3's gate. That function is too entangled with texture-byte
// resolution (DB2/listfile/catalog dependencies) to call directly for just
// this slice, so this checks the canon result's structural properties
// against facts derived independently, straight from the same parsed M2/
// .skin tables, rather than against buildMaterialsAndPrimitives's own
// output -- same approach test_canon_mesh_convergence.cpp already
// establishes for geosets.

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>

#include "canon_material_builder.hpp"
#include "m2_model.hpp"
#include "skin.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

canon::M2MaterialInputs toCanonInputs(const m2::Model& model) {
    canon::M2MaterialInputs m2;
    m2.materials = model.materials;
    m2.textures = model.textures;
    m2.textureCombos = model.textureCombos;
    m2.textureCoordCombos = model.textureCoordCombos;
    m2.colors = model.colors;
    m2.textureWeights = model.textureWeights;
    m2.textureWeightCombos = model.textureWeightCombos;
    m2.textureTransforms = model.textureTransforms;
    m2.textureTransformCombos = model.textureTransformCombos;
    m2.blob = &model.blob;
    return m2;
}

// Independent re-derivation of blendModeToBlendOp
// (canon_material_builder.cpp, anonymous-namespace, not exported) -- a
// convergence test must check against a fact worked out separately, not by
// calling the very function under test.
canon::BlendOp expectedBlendOp(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return canon::BlendOp::Replace;
        case 1: return canon::BlendOp::Replace;
        case 2: return canon::BlendOp::Fade;
        case 3: return canon::BlendOp::Add;
        case 4: return canon::BlendOp::Add;
        case 5: return canon::BlendOp::Modulate;
        case 6: return canon::BlendOp::Modulate2x;
        default: return canon::BlendOp::Fade;
    }
}

}  // namespace

TEST_CASE("canon::assembleMaterial converges with export_materials.cpp's per-batch material "
          "construction on a real fixture" *
          doctest::skip(test::testM2().empty() || test::testSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testM2()));
    std::vector<uint8_t> skinFile = readFile(test::testSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    REQUIRE(!batches.empty());

    canon::M2MaterialInputs m2in = toCanonInputs(model);
    const uint32_t sequenceIndex = 0;

    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        INFO("batch index ", bi);

        canon::Material result = canon::assembleMaterial(b, bi, m2in, sequenceIndex);

        if (b.textureCount == 0) {
            CHECK(result.layers.empty());
            continue;
        }

        REQUIRE(b.materialIndex < model.materials.size());
        canon::BlendOp expectedOp = expectedBlendOp(model.materials[b.materialIndex].blendMode);

        // Independently re-derive layer 0's real texture index the same
        // way export_materials.cpp's batch loop does.
        REQUIRE(b.textureComboIndex < m2in.textureCombos.size());
        uint16_t textureIndex0 = m2in.textureCombos[b.textureComboIndex];
        REQUIRE(textureIndex0 < m2in.textures.size());

        REQUIRE(!result.layers.empty());
        const auto& layer0 = result.layers.front();

        REQUIRE(std::holds_alternative<canon::RecordIndex>(layer0.identity.id));
        CHECK(std::get<canon::RecordIndex>(layer0.identity.id).value == textureIndex0);
        CHECK(layer0.blendIntoPrevious == expectedOp);
        CHECK(layer0.texture.state == canon::TextureRef::State::KnownUnresolved);
        CHECK_FALSE(layer0.texture.unresolvedReason.empty());

        // UV: real fixture's textureCoordCombos is expected empty (pre-
        // Cataclysm-only feature, per M2MaterialInputs::textureCoordCombos'
        // own doc comment) -- every layer should be plain UV set 0.
        if (m2in.textureCoordCombos.empty()) {
            REQUIRE(std::holds_alternative<canon::UvSetIndex>(layer0.uv));
            CHECK(std::get<canon::UvSetIndex>(layer0.uv).index == 0);
        }

        // Role: Diffuse when the texture's own type has no wiki name (type
        // 0, an ordinary embedded/FileDataID texture), else that type's
        // real name string.
        uint32_t textureType0 = m2in.textures[textureIndex0].type;
        if (const char* typeName = m2::textureTypeName(textureType0)) {
            REQUIRE(std::holds_alternative<std::string>(layer0.role));
            CHECK(std::get<std::string>(layer0.role) == typeName);
            CHECK_FALSE(result.diffuseLayer.has_value());
        } else {
            REQUIRE(std::holds_alternative<canon::KnownRole>(layer0.role));
            CHECK(std::get<canon::KnownRole>(layer0.role) == canon::KnownRole::Diffuse);
            REQUIRE(result.diffuseLayer.has_value());
            CHECK(std::get<canon::RecordIndex>(result.diffuseLayer->id).value == textureIndex0);
        }

        // Every layer in this batch shares the same blend op (one
        // M2Material per batch, not one per texture unit).
        for (const auto& layer : result.layers) {
            CHECK(layer.blendIntoPrevious == expectedOp);
            CHECK(layer.texture.state == canon::TextureRef::State::KnownUnresolved);
        }

        // Independently reconstruct how many additional layers should have
        // resolved, mirroring export_materials.cpp's own break-vs-continue
        // rule.
        size_t expectedLayerCount = 1;
        for (uint16_t layerOffset = 1; layerOffset < b.textureCount; ++layerOffset) {
            size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layerOffset;
            if (comboIdx >= m2in.textureCombos.size()) break;
            uint16_t layerTextureIndex = m2in.textureCombos[comboIdx];
            if (layerTextureIndex >= m2in.textures.size()) continue;
            ++expectedLayerCount;
        }
        CHECK(result.layers.size() == expectedLayerCount);
    }
}

TEST_CASE("canon::assembleMaterial: a real multi-texture-layer fixture's additional layers "
          "resolve at the expected offsets" *
          doctest::skip(test::testMultiTextureLayerM2().empty() ||
                        test::testMultiTextureLayerSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testMultiTextureLayerM2()));
    std::vector<uint8_t> skinFile = readFile(test::testMultiTextureLayerSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    REQUIRE(!batches.empty());

    canon::M2MaterialInputs m2in = toCanonInputs(model);

    bool foundMultiTexture = false;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.textureCount <= 1) continue;
        foundMultiTexture = true;

        canon::Material result = canon::assembleMaterial(b, bi, m2in, 0);
        REQUIRE(result.layers.size() >= 2);

        for (uint16_t layerOffset = 1; layerOffset < result.layers.size(); ++layerOffset) {
            size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layerOffset;
            REQUIRE(comboIdx < m2in.textureCombos.size());
            uint16_t textureIndex = m2in.textureCombos[comboIdx];
            REQUIRE(textureIndex < m2in.textures.size());
            REQUIRE(std::holds_alternative<canon::RecordIndex>(result.layers[layerOffset].identity.id));
            CHECK(std::get<canon::RecordIndex>(result.layers[layerOffset].identity.id).value == textureIndex);
        }
    }
    INFO("real fixture has a batch with textureCount > 1: ", foundMultiTexture);
    CHECK(foundMultiTexture);
}

TEST_CASE("canon::assembleMaterial: real texture-transform curves reflect each track's own "
          "Animated flag (only the animated, non-empty-for-this-sequence case resolves a curve)" *
          doctest::skip(test::testTextureTransformRotationM2().empty() ||
                        test::testTextureTransformRotationSkin().empty())) {
    m2::Model model = m2::loadModel(readFile(test::testTextureTransformRotationM2()));
    std::vector<uint8_t> skinFile = readFile(test::testTextureTransformRotationSkin());
    skin::Header header = skin::parseHeader(skinFile);
    std::vector<skin::Batch> batches = skin::parseBatches(skinFile, header.batches);
    REQUIRE(!batches.empty());

    canon::M2MaterialInputs m2in = toCanonInputs(model);
    const uint32_t sequenceIndex = 0;

    bool foundTransformBatch = false;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.textureTransformComboIndex == 0xFFFF ||
            b.textureTransformComboIndex >= m2in.textureTransformCombos.size()) {
            continue;
        }
        uint16_t transformIndex = m2in.textureTransformCombos[b.textureTransformComboIndex];
        if (transformIndex >= m2in.textureTransforms.size()) continue;
        foundTransformBatch = true;
        INFO("batch index ", bi, ", transform index ", transformIndex);

        const auto& xf = m2in.textureTransforms[transformIndex];
        canon::Material result = canon::assembleMaterial(b, bi, m2in, sequenceIndex);
        REQUIRE(!result.layers.empty());
        const auto& layer0 = result.layers.front();

        // Independently derived expectation, straight from the same raw
        // M2Track resolvers assembleMaterial itself calls (not from
        // assembleMaterial's own output) -- real brewfestmount data has a
        // mix of constant (rotationAnimated == false throughout the whole
        // file, per this fixture's own tracks) and per-sequence-animated
        // (scaling, for several transform indices) tracks, so this checks
        // each of the three components on its own honest terms rather than
        // assuming one fixed shape for the whole file.
        bool expectTranslation =
            xf.translationAnimated &&
            !m2::resolveVec3TrackSequence(model.blob, xf.translationTrackOffset, sequenceIndex).empty();
        bool expectRotation =
            xf.rotationAnimated &&
            !m2::resolveRawQuatTrackSequence(model.blob, xf.rotationTrackOffset, sequenceIndex).empty();
        bool expectScaling =
            xf.scalingAnimated &&
            !m2::resolveVec3TrackSequence(model.blob, xf.scalingTrackOffset, sequenceIndex).empty();

        if (!expectTranslation && !expectRotation && !expectScaling) {
            // A real, intentional gap: canon::MaterialLayer has no field
            // for a *constant* UV transform value, only the animated
            // shape -- see canon_material_builder.hpp's own doc comment on
            // the analogous tint/alphaFade gap. A constant-only transform
            // (this file's own real rotationAnimated == false case)
            // correctly surfaces no uvAnimation at all, not a
            // misrepresented "no transform".
            CHECK_FALSE(layer0.uvAnimation.has_value());
        } else {
            REQUIRE(layer0.uvAnimation.has_value());
            CHECK(layer0.uvAnimation->translation.has_value() == expectTranslation);
            CHECK(layer0.uvAnimation->rotation.has_value() == expectRotation);
            CHECK(layer0.uvAnimation->scaling.has_value() == expectScaling);
        }
    }
    INFO("real fixture has a batch referencing a real M2TextureTransform: ", foundTransformBatch);
    CHECK(foundTransformBatch);
}

TEST_CASE("canon::assembleMaterial throws on an out-of-range materialIndex, same as "
          "export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);

    skin::Batch batch;
    batch.materialIndex = 5;  // out of range for 1 material
    batch.textureCount = 0;

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial throws on an out-of-range textureComboIndex, same as "
          "export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 5;  // out of range for 1 textureCombos entry

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial throws when a resolved texture index is out of range for the "
          "primary layer, same as export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {7};  // points at texture index 7
    m2in.textures.resize(1);   // only 1 real texture

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial throws on an out-of-range textureCoordComboIndex when the "
          "table is non-empty, same as export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};
    m2in.textures.resize(1);
    m2in.textureCoordCombos.resize(1);  // non-empty: pre-Cataclysm feature in play

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;
    batch.textureCoordComboIndex = 5;  // out of range for 1 entry

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial throws on an out-of-range colorIndex, same as "
          "export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};
    m2in.textures.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;
    batch.colorIndex = 3;  // out of range: m2in.colors is empty

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial throws on an out-of-range textureWeightComboIndex when the "
          "table is non-empty, same as export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};
    m2in.textures.resize(1);
    m2in.textureWeightCombos.resize(1);  // non-empty: this field is required once present

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;
    batch.textureWeightComboIndex = 9;  // out of range for 1 entry

    CHECK_THROWS_AS(canon::assembleMaterial(batch, 0, m2in, 0), std::runtime_error);
}

TEST_CASE("canon::assembleMaterial: an out-of-range additional-layer combo index stops emitting "
          "further layers (break), same as export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};  // only 1 entry -- layer 1's combo index (0+1=1) is out of range
    m2in.textures.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 3;  // claims 3 texture units
    batch.textureComboIndex = 0;

    canon::Material result = canon::assembleMaterial(batch, 0, m2in, 0);
    REQUIRE(result.layers.size() == 1);  // only the primary layer resolved
}

TEST_CASE("canon::assembleMaterial: an out-of-range additional-layer texture index skips just "
          "that layer (continue), same as export_materials.cpp's batch loop") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    // combo[0] -> texture 0 (valid), combo[1] -> texture 9 (invalid), combo[2] -> texture 0 (valid)
    m2in.textureCombos = {0, 9, 0};
    m2in.textures.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 3;
    batch.textureComboIndex = 0;

    canon::Material result = canon::assembleMaterial(batch, 0, m2in, 0);
    // Layer offset 1 (texture 9) is skipped; layer offset 2 (texture 0)
    // still resolves -- 2 layers total, not 3, and not stopped at 1.
    REQUIRE(result.layers.size() == 2);
}

TEST_CASE("canon::assembleMaterial: textureCount == 0 produces zero layers, still validates "
          "materialIndex") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 0;

    canon::Material result = canon::assembleMaterial(batch, 0, m2in, 0);
    CHECK(result.layers.empty());
    CHECK_FALSE(result.diffuseLayer.has_value());
}

TEST_CASE("canon::assembleMaterial: environment-mapped UV maps to KnownRole::Env, not a guessed "
          "diffuse role") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};
    m2in.textures.resize(1);
    m2in.textureCoordCombos = {0xFFFF};  // -1 sentinel: environment mapping

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;
    batch.textureCoordComboIndex = 0;

    canon::Material result = canon::assembleMaterial(batch, 0, m2in, 0);
    REQUIRE(result.layers.size() == 1);
    CHECK(std::holds_alternative<canon::EnvironmentMapped>(result.layers[0].uv));
    REQUIRE(std::holds_alternative<canon::KnownRole>(result.layers[0].role));
    CHECK(std::get<canon::KnownRole>(result.layers[0].role) == canon::KnownRole::Env);
    CHECK_FALSE(result.diffuseLayer.has_value());
}

TEST_CASE("canon::assembleMaterial: every layer's texture is KnownUnresolved -- byte/FileDataID "
          "resolution is out of scope for canon assembly") {
    canon::M2MaterialInputs m2in;
    m2in.materials.resize(1);
    m2in.textureCombos = {0};
    m2in.textures.resize(1);

    skin::Batch batch;
    batch.materialIndex = 0;
    batch.textureCount = 1;
    batch.textureComboIndex = 0;

    canon::Material result = canon::assembleMaterial(batch, 0, m2in, 0);
    REQUIRE(result.layers.size() == 1);
    CHECK(result.layers[0].texture.state == canon::TextureRef::State::KnownUnresolved);
    CHECK(result.layers[0].texture.unresolvedReason == "texture resolution out of scope for canon assembly");
    CHECK(result.layers[0].texture.candidates.empty());
}
