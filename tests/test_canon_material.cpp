// Tests for husk::canon::Material and friends (src/canon_material.hpp).

#include <doctest/doctest.h>

#include "canon_material.hpp"

using namespace husk::canon;

TEST_CASE("LayerRole: KnownRole and open-string alternatives") {
    LayerRole diffuse = KnownRole::Diffuse;
    LayerRole custom = std::string("weird_combiner_slot");

    REQUIRE(std::holds_alternative<KnownRole>(diffuse));
    CHECK(std::get<KnownRole>(diffuse) == KnownRole::Diffuse);

    REQUIRE(std::holds_alternative<std::string>(custom));
    CHECK(std::get<std::string>(custom) == "weird_combiner_slot");
}

TEST_CASE("UvRef: UV set index vs environment-mapped, no compatibility scoring") {
    UvRef uv0 = UvSetIndex{0};
    UvRef uv1 = UvSetIndex{1};
    UvRef env = EnvironmentMapped{};

    REQUIRE(std::holds_alternative<UvSetIndex>(uv0));
    CHECK(std::get<UvSetIndex>(uv0).index == 0);
    REQUIRE(std::holds_alternative<UvSetIndex>(uv1));
    CHECK(std::get<UvSetIndex>(uv1).index == 1);
    CHECK(std::holds_alternative<EnvironmentMapped>(env));
}

TEST_CASE("TextureRef: Resolved carries a real Ref, other fields inert") {
    TextureRef ref;
    ref.state = TextureRef::State::Resolved;
    ref.resolved.id = FileDataId{123456};
    ref.resolved.name = "bloodelffemale_hd_skin_color";
    ref.resolved.source = NameSource::Listfile;

    CHECK(ref.state == TextureRef::State::Resolved);
    REQUIRE(std::holds_alternative<FileDataId>(ref.resolved.id));
    CHECK(std::get<FileDataId>(ref.resolved.id).value == 123456);
    CHECK(ref.unresolvedReason.empty());
    CHECK(ref.candidates.empty());
}

TEST_CASE("TextureRef: KnownUnresolved carries a reason") {
    TextureRef ref;
    ref.state = TextureRef::State::KnownUnresolved;
    ref.unresolvedReason = "customization-driven slot, no --db2-dir given";

    CHECK(ref.state == TextureRef::State::KnownUnresolved);
    CHECK_FALSE(ref.unresolvedReason.empty());
}

TEST_CASE("TextureRef: Ambiguous keeps the full candidate set, not a collapsed guess") {
    TextureRef ref;
    ref.state = TextureRef::State::Ambiguous;

    TextureRef::Candidate a;
    a.identity.id = FileDataId{111};
    a.identity.name = "bloodelffemale_hd_skin_color_00_00";
    a.category = "skin_color";
    a.width = 512;
    a.height = 512;
    ref.candidates.push_back(a);

    TextureRef::Candidate b;
    b.identity.id = FileDataId{222};
    b.category = "face";
    b.width = 256;
    b.height = 256;
    ref.candidates.push_back(b);

    REQUIRE(ref.candidates.size() == 2);
    CHECK(ref.candidates[0].category == "skin_color");
    CHECK(ref.candidates[0].width == 512);
    CHECK(ref.candidates[1].category == "face");
}

TEST_CASE("MaterialLayer: identity is a stable Ref, not a raw index") {
    MaterialLayer layer;
    layer.identity.id = RecordIndex{2};
    layer.identity.name = "diffuse";
    layer.role = KnownRole::Diffuse;
    layer.uv = UvSetIndex{0};

    REQUIRE(std::holds_alternative<RecordIndex>(layer.identity.id));
    CHECK(std::get<RecordIndex>(layer.identity.id).value == 2);
    REQUIRE(std::holds_alternative<KnownRole>(layer.role));
    CHECK(std::get<KnownRole>(layer.role) == KnownRole::Diffuse);
    CHECK(layer.blendIntoPrevious == BlendOp::Modulate);  // GxTexBlend_Opaque's own default
    CHECK_FALSE(layer.tint.has_value());
    CHECK_FALSE(layer.uvAnimation.has_value());
}

TEST_CASE("MaterialLayer: per-layer tint/alphaFade/uvAnimation reuse canon::Curve") {
    MaterialLayer layer;
    layer.tint = VecCurve{};
    layer.tint->keyframes.emplace_back(0.0f, husk::m2::Vec3{1.0f, 1.0f, 1.0f});
    layer.alphaFade = ScalarCurve{};
    layer.alphaFade->keyframes.emplace_back(0.0f, 1.0f);

    MaterialLayer::TextureTransformCurves uvAnim;
    uvAnim.scaling = VecCurve{};
    uvAnim.scaling->keyframes.emplace_back(0.0f, husk::m2::Vec3{2.0f, 2.0f, 2.0f});
    layer.uvAnimation = uvAnim;

    REQUIRE(layer.tint.has_value());
    REQUIRE(layer.tint->keyframes.size() == 1);
    CHECK(layer.tint->keyframes[0].second.x == doctest::Approx(1.0f));

    REQUIRE(layer.uvAnimation.has_value());
    REQUIRE(layer.uvAnimation->scaling.has_value());
    CHECK(layer.uvAnimation->scaling->keyframes[0].second.x == doctest::Approx(2.0f));
    CHECK_FALSE(layer.uvAnimation->translation.has_value());
    CHECK_FALSE(layer.uvAnimation->rotation.has_value());
}

TEST_CASE("Material: ordered layer stack plus optional named-slot references by identity") {
    Material mat;

    MaterialLayer diffuse;
    diffuse.identity.id = RecordIndex{0};
    diffuse.role = KnownRole::Diffuse;
    mat.layers.push_back(diffuse);

    MaterialLayer detail;
    detail.identity.id = RecordIndex{1};
    detail.role = KnownRole::Detail;
    mat.layers.push_back(detail);

    mat.diffuseLayer = mat.layers[0].identity;

    REQUIRE(mat.layers.size() == 2);
    REQUIRE(mat.diffuseLayer.has_value());
    REQUIRE(std::holds_alternative<RecordIndex>(mat.diffuseLayer->id));
    CHECK(std::get<RecordIndex>(mat.diffuseLayer->id).value == 0);
    CHECK_FALSE(mat.specularLayer.has_value());
    CHECK_FALSE(mat.emissionLayer.has_value());
    CHECK_FALSE(mat.alphaLayer.has_value());
}

TEST_CASE("Material: a layer with no honest PBR role stays role Unknown, not forced into a named slot") {
    Material mat;
    MaterialLayer combinerOnly;
    combinerOnly.identity.id = RecordIndex{0};
    combinerOnly.role = KnownRole::Unknown;
    mat.layers.push_back(combinerOnly);

    REQUIRE(std::holds_alternative<KnownRole>(mat.layers[0].role));
    CHECK(std::get<KnownRole>(mat.layers[0].role) == KnownRole::Unknown);
    CHECK_FALSE(mat.diffuseLayer.has_value());
}

TEST_CASE("BaseFunction: four base-function alternatives, Multiplicative carries no tintSource yet") {
    BaseFunction clip = AlphaClip{};
    BaseFunction blend = AlphaBlend{};
    BaseFunction add = Additive{};
    BaseFunction mul = Multiplicative{};

    CHECK(std::holds_alternative<AlphaClip>(clip));
    CHECK(std::holds_alternative<AlphaBlend>(blend));
    CHECK(std::holds_alternative<Additive>(add));
    CHECK(std::holds_alternative<Multiplicative>(mul));
}

TEST_CASE("Function: empty stages means not transcribed, GenuinelyUnknown territory") {
    Function fn;
    CHECK(fn.stages.empty());
}

TEST_CASE("Function: a flat combiner chain -- layer 1 modulated onto the previous stage's result") {
    Function fn;
    Function::Stage stage;
    stage.op = BlendOp::Modulate;
    stage.arg0.kind = Function::Operand::Kind::PreviousStage;
    stage.arg1.kind = Function::Operand::Kind::Layer;
    stage.arg1.layerIndex = 1;
    fn.stages.push_back(stage);

    REQUIRE(fn.stages.size() == 1);
    CHECK(fn.stages[0].op == BlendOp::Modulate);
    CHECK(fn.stages[0].arg0.kind == Function::Operand::Kind::PreviousStage);
    CHECK(fn.stages[0].arg1.kind == Function::Operand::Kind::Layer);
    CHECK(fn.stages[0].arg1.layerIndex == 1);
}

TEST_CASE("ShadingFunction: Verified confidence with math/description views of the same function") {
    ShadingFunction sf;
    sf.identity.name = "Combiners_Mod_Add";
    sf.identity.source = NameSource::Synthesized;
    sf.base = Additive{};
    sf.confidence = Confidence::Verified;
    sf.math = "dst = src0 * src1 + src2";
    sf.description = "Modulates the first two layers, then adds the third additively.";

    CHECK(sf.identity.name == "Combiners_Mod_Add");
    REQUIRE(std::holds_alternative<Additive>(sf.base));
    CHECK(sf.confidence == Confidence::Verified);
    REQUIRE(sf.math.has_value());
    REQUIRE(sf.description.has_value());
}

TEST_CASE("ShadingFunction: default confidence is GenuinelyUnknown, not a false Verified") {
    ShadingFunction sf;
    CHECK(sf.confidence == Confidence::GenuinelyUnknown);
    CHECK_FALSE(sf.math.has_value());
    CHECK_FALSE(sf.description.has_value());
}

TEST_CASE("ShadingFunction: default base is an explicit AlphaClip placeholder, not an accidental one") {
    ShadingFunction sf;
    REQUIRE(std::holds_alternative<AlphaClip>(sf.base));
}
