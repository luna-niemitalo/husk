// wmoinput::buildCanonWmo (wmo_canon_input.hpp): a WMO as an ordinary
// canon::Model -- groups as mesh parts, MOMT as materials, MODS as owned
// placement sets (REFACTOR/PLACEMENT_SETS.md).

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "test_data_paths.hpp"
#include "wmo.hpp"
#include "wmo_canon_input.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Two groups: group 0 has two triangles and two UV sets, group 1 one
// triangle and one UV set. Two materials, two doodad sets over three doodads.
wmo::RootFile syntheticRoot() {
    wmo::RootFile root;
    root.header.groupCount = 2;
    root.groups.resize(2);
    root.groupNames = std::string("hall\0", 5);
    root.groups[0].nameOffset = 0;
    root.groups[0].flags = 0x8;
    wmo::Material diffuse;
    diffuse.shader = 0;
    diffuse.blendMode = 1;
    diffuse.flags = 0x5;  // unlit, two-sided
    diffuse.textures = {101, 0, 0};
    wmo::Material twoLayer;
    twoLayer.shader = 6;
    twoLayer.blendMode = 7;  // GxBlend_InvSrcAlphaAdd: no canon name
    twoLayer.textures = {102, 103, 0};
    root.materials = {diffuse, twoLayer};
    root.uvScrollSpeeds = {{{{0, 0}, {0, 0}}}, {{{0.5f, 0}, {0, 0.25f}}}};
    root.doodadFileDataIds = {7001, 0};
    for (uint32_t i = 0; i < 3; ++i) {
        wmo::Doodad d;
        d.nameIndex = i == 2 ? 1 : 0;
        d.position = {float(i), 0, 0};
        d.color = 0x80102030u;  // BGRA bytes 30 20 10 80 -> RGBA 10 20 30 80
        root.doodads.push_back(d);
    }
    root.doodadColorMultipliers = {1.0f, 2.0f, 3.0f};
    root.doodadSets = {{"Set_$DefaultGlobal", 0, 1}, {"Set_B", 1, 2}};
    return root;
}

wmo::GroupFile group(uint32_t vertexCount, uint32_t uvSets, uint32_t material, uint32_t triangles) {
    wmo::GroupFile g;
    for (uint32_t v = 0; v < vertexCount; ++v) {
        g.positions.push_back({float(v), 0, 0});
        g.normals.push_back({0, 0, 1});
    }
    for (uint32_t s = 0; s < uvSets; ++s) g.uvSets.emplace_back(vertexCount, wmo::Vec2{float(s + 1), 0});
    g.colorSets.emplace_back(vertexCount, 0xFF000000u);
    for (uint32_t t = 0; t < triangles * 3; ++t) g.indices.push_back(t % vertexCount);
    wmo::Batch b;
    b.indexCount = triangles * 3;
    b.material = material;
    g.batches.push_back(b);
    return g;
}

}  // namespace

TEST_CASE("buildCanonWmo: groups become parts sharing one vertex buffer, batches become primitives") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {group(4, 2, 1, 2), group(3, 1, 0, 1)};

    canon::Model model = wmoinput::buildCanonWmo(in);

    CHECK(model.skeleton.joints.empty());
    REQUIRE(model.mesh.parts.size() == 2);
    CHECK(model.mesh.parts[0].ref.name == "hall");
    CHECK(model.mesh.parts[0].ref.source == canon::NameSource::WmoEmbedded);
    CHECK(model.mesh.parts[0].flags == 0x8);
    CHECK(model.mesh.parts[1].ref.source == canon::NameSource::None);
    CHECK(model.mesh.positions.size() == 7);
    REQUIRE(model.mesh.uv1.has_value());
    CHECK((*model.mesh.uv1)[0].x == 2.0f);
    CHECK((*model.mesh.uv1)[4].x == 0.0f);  // group 1 has no second set: zero-filled
    REQUIRE(model.mesh.primitives.size() == 2);
    CHECK(model.mesh.primitives[1].indexStart == 6);
    CHECK(model.mesh.primitives[1].part == 1u);
    CHECK(model.mesh.indices[6] == 4);  // group 1's vertex 0, offset past group 0's 4 vertices
    CHECK(canon::resolveMaterialIndex(model, 0) == 1u);
}

TEST_CASE("buildCanonWmo: materials carry blend, shared flags, shader name, textures and UV scroll") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    in.textures.emplace(101, resolved);

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.materials.size() == 2);
    const canon::Material& diffuse = model.materials[0];
    CHECK(diffuse.framebufferBlend == canon::FramebufferBlend::AlphaKey);
    CHECK(diffuse.unlit);
    CHECK(diffuse.twoSided);
    CHECK_FALSE(diffuse.unfogged);
    REQUIRE(diffuse.shader.has_value());
    CHECK(diffuse.shader->name == "Diffuse");
    REQUIRE(diffuse.layers.size() == 1);
    CHECK(diffuse.layers[0].texture.state == canon::TextureRef::State::Resolved);

    const canon::Material& twoLayer = model.materials[1];
    CHECK_FALSE(twoLayer.framebufferBlend.has_value());
    CHECK(twoLayer.shader->name == "TwoLayerDiffuse");
    REQUIRE(twoLayer.layers.size() == 2);
    CHECK(twoLayer.layers[1].texture.unresolvedReason == "texture FileDataID 103 was not resolved");
    REQUIRE(twoLayer.layers[0].uvScroll.has_value());
    CHECK(twoLayer.layers[0].uvScroll->x == 0.5f);
    CHECK(twoLayer.layers[1].uvScroll->y == 0.25f);
    CHECK(model.mesh.primitives.empty());  // both group files missing
}

TEST_CASE("buildCanonWmo: doodad sets become owned placement sets, set 0 always on") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt, std::nullopt};
    in.assetNames.emplace(7001, "barrel01");

    canon::Model model = wmoinput::buildCanonWmo(in);

    REQUIRE(model.placementSets.size() == 2);
    CHECK(model.placementSets[0].alwaysOn);
    CHECK_FALSE(model.placementSets[1].alwaysOn);
    CHECK(model.placementSets[1].set.ref.name == "Set_B");
    const auto& instances = model.placementSets[1].set.instances;
    REQUIRE(instances.size() == 2);
    CHECK(instances[0].id == 1);
    CHECK(std::get<canon::FileDataId>(instances[0].asset.id).value == 7001);
    CHECK(instances[0].asset.name == "barrel01");
    CHECK(instances[0].translation.x == 1.0f);
    REQUIRE(instances[0].tint.has_value());
    CHECK(instances[0].tint->rgba == std::array<uint8_t, 4>{0x10, 0x20, 0x30, 0x80});
    CHECK(instances[0].tint->multiplier == 2.0f);
    CHECK(std::holds_alternative<canon::None>(instances[1].asset.id));  // MODI slot 1 is empty
}

TEST_CASE("buildCanonWmo: inconsistent inputs throw with expected and actual") {
    wmo::RootFile root = syntheticRoot();
    wmoinput::WmoInputs in;
    in.root = &root;
    in.groups = {std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO groups: expected one entry per MOGI group (2), got 1",
                         std::runtime_error);

    in.groups = {group(3, 1, 5, 1), std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "WMO group 0 batch 0: expected a material < 2, got 5",
                         std::runtime_error);

    root.doodads[0].nameIndex = 9;
    in.groups = {std::nullopt, std::nullopt};
    CHECK_THROWS_WITH_AS(wmoinput::buildCanonWmo(in), "MODD doodad 0: expected a MODI index < 2, got 9",
                         std::runtime_error);
}

TEST_CASE("buildCanonWmo: real guardtower -- every batch in range, every set and doodad carried" *
          doctest::skip(test::testWmoGuardTower().empty())) {
    std::filesystem::path rootPath = test::testWmoGuardTower();
    wmo::RootFile root = wmo::parseRoot(readFile(rootPath.string()));
    wmoinput::WmoInputs in;
    in.root = &root;
    size_t batches = 0;
    for (int g = 0; g < 2; ++g) {
        auto path = rootPath.parent_path() / ("guardtower_00" + std::to_string(g) + ".wmo");
        in.groups.emplace_back(wmo::parseGroup(readFile(path.string())));
        batches += in.groups.back()->batches.size();
    }

    canon::Model model = wmoinput::buildCanonWmo(in);

    CHECK(model.mesh.primitives.size() == batches);
    CHECK(model.materials.size() == 20);
    REQUIRE(model.placementSets.size() == 3);
    size_t instances = 0;
    for (const auto& owned : model.placementSets) instances += owned.set.instances.size();
    CHECK(instances == 4 + 58 + 108);
    CHECK(model.mesh.colorSets.size() == 1);
}
