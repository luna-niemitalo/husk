// src/creature_options_input.hpp (synthetic display rows) and
// src/creature_display_db2.hpp (real 12.1.0 tables on the casc-tool mount,
// skipped without it).

#include <doctest/doctest.h>
#include <filesystem>
#include <sstream>

#include "../src/creature_display_db2.hpp"
#include "../src/creature_options_input.hpp"
#include "test_data_paths.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kMountDb2Dir = "/media/luna/wow_export/live/dbfilesclient";
constexpr uint32_t kModelFile = 500;

canon::Model modelWithGeosets(std::vector<uint32_t> geosetIds) {
    canon::Model model;
    for (uint32_t id : geosetIds) {
        canon::PrimitiveGeoset p;
        p.geoset = canon::Geoset::fromRawId(id);
        model.mesh.primitives.push_back(p);
    }
    model.textureSlots = {{"monster_2", 12}, {"monster_1", 11}};
    return model;
}

canon::TextureRef textureNamed(uint32_t fdid) {
    canon::TextureRef ref;
    ref.state = canon::TextureRef::State::Resolved;
    ref.resolved.id = canon::FileDataId{fdid};
    return ref;
}

creaturedisplay::Display display(uint32_t id, std::array<uint32_t, 4> textures) {
    creaturedisplay::Display d;
    d.id = id;
    d.textures = textures;
    return d;
}

}  // namespace

TEST_CASE("addCreatureOptions: every display texture is a slot option; each distinct combination one preset") {
    creaturedisplay::Data data;
    data.modelDataByFile[kModelFile] = {{1, 2.0f, false}};
    creaturedisplay::Display a = display(10, {100, 200, 0, 0});
    a.scale = 1.5f;
    creaturedisplay::Display b = display(11, {100, 200, 0, 0});
    creaturedisplay::Display c = display(12, {101, 200, 0, 400});
    c.alpha = 51;
    data.displaysByModelData[1] = {a, b, c};

    canon::Model model = modelWithGeosets({0, 101});
    std::vector<uint32_t> asked;
    creatureinput::OptionsReport report;
    creatureinput::addCreatureOptions(
        data, kModelFile, model, [&](uint32_t fdid) { asked.push_back(fdid); return textureNamed(fdid); }, report);

    CHECK(report.displays == 3);
    CHECK(report.humanoid == 0);
    // Entry 0 fills type 11 (slot 1), entry 1 type 12 (slot 0); each texture once.
    REQUIRE(model.textureSlots[1].options.size() == 2);
    CHECK(std::get<canon::FileDataId>(model.textureSlots[1].options[1].resolved.id).value == 101);
    REQUIRE(model.textureSlots[0].options.size() == 1);
    CHECK(asked == std::vector<uint32_t>{100, 200, 101, 400});

    REQUIRE(model.presets.size() == 2);
    const canon::Preset& first = model.presets[0];
    REQUIRE(first.textures.size() == 2);
    CHECK(first.textures[0].slot == 1);
    CHECK(first.textures[0].option == 0);
    CHECK(first.textures[1].slot == 0);
    REQUIRE(first.sources.size() == 2);
    CHECK(first.sources[0].scale == 1.5f);
    CHECK(first.sources[0].baseScale == 2.0f);
    CHECK_FALSE(first.shownGeosets.has_value());
    const canon::Preset& second = model.presets[1];
    CHECK(second.textures[0].option == 1);
    CHECK(second.textures[1].option == 0);
    CHECK(std::get<canon::Db2Row>(second.sources[0].ref.id).row == 12);
    // The fourth entry has no texture type: an inferred slot of its own.
    REQUIRE(model.textureSlots.size() == 3);
    const canon::TextureSlot& fourth = model.textureSlots[2];
    CHECK(fourth.name == "variation_4");
    CHECK_FALSE(fourth.sourceType.has_value());
    REQUIRE(fourth.inferredFrom.has_value());
    REQUIRE(fourth.options.size() == 1);
    REQUIRE(second.textures.size() == 3);
    CHECK(second.textures[2].slot == 2);
    CHECK(second.unresolved.empty());
    CHECK(second.opacity == doctest::Approx(0.2f));
}

TEST_CASE("addCreatureOptions: a texture for a type no layer reads gets that type's slot, not an inference") {
    creaturedisplay::Data data;
    data.modelDataByFile[kModelFile] = {{1, 1.0f, false}};
    data.displaysByModelData[1] = {display(10, {0, 0, 300, 0})};
    canon::Model model = modelWithGeosets({0});
    creatureinput::OptionsReport report;
    creatureinput::addCreatureOptions(data, kModelFile, model, textureNamed, report);
    REQUIRE(model.textureSlots.size() == 3);
    CHECK(model.textureSlots[2].name == "monster_3");
    CHECK(model.textureSlots[2].sourceType == 13u);
    CHECK_FALSE(model.textureSlots[2].inferredFrom.has_value());
    CHECK(model.presets[0].textures[0].slot == 2);
}

TEST_CASE("addCreatureOptions: a humanoid NPC's bake fills the lowest-type slot; choices and items ride along by id") {
    creaturedisplay::Data data;
    data.modelDataByFile[kModelFile] = {{1, 1.0f, false}};
    creaturedisplay::Display npc = display(10, {});
    npc.extra = 77;
    creaturedisplay::Display twin = display(11, {});
    twin.extra = 78;
    creaturedisplay::Display lost = display(12, {});
    lost.extra = 79;
    data.displaysByModelData[1] = {npc, twin, lost};
    data.extras[77] = {500, 600};  // the HD bake wins
    data.extras[78] = {500, 600};
    data.extras[79] = {501, 0};    // no texture for this bake
    data.bakeTextures = {{500, 1500}, {600, 1600}};
    data.customizationByExtra[77] = {{3, 30}, {4, 40}};
    data.customizationByExtra[78] = {{3, 30}, {4, 40}};
    data.itemsByExtra[77] = {{9000, 0}, {0, 3}, {9011, 11}};
    // ChrModels 5 and 6 share the file: 5's option is 3 (choice 30), 6's is
    // 4 (40 and 41).
    data.chrModelsByFile[kModelFile] = {5, 6};
    data.customization.options = {{3, 5, "Hair", 0, 0}, {4, 6, "Face", 1, 0}};
    data.customization.choices = {{30, 3, "Long", 0, std::nullopt}, {41, 4, "", 0, std::nullopt},
                                  {40, 4, "", 1, std::nullopt}};
    data.itemsByExtra[78] = {{9000, 0}, {0, 3}, {9011, 11}};

    canon::Model model = modelWithGeosets({0});
    model.textureSlots.push_back({"skin", 1, {}, std::nullopt});
    creatureinput::OptionsReport report;
    creatureinput::addCreatureOptions(data, kModelFile, model, textureNamed, report);
    CHECK(report.humanoid == 3);
    REQUIRE(model.textureSlots[2].options.size() == 1);
    CHECK(std::get<canon::FileDataId>(model.textureSlots[2].options[0].resolved.id).value == 1600);
    // Two NPCs dressed alike are one preset.
    REQUIRE(model.presets.size() == 2);
    const canon::Preset& p = model.presets[0];
    CHECK(p.sources.size() == 2);
    REQUIRE(p.textures.size() == 1);
    CHECK(p.textures[0].slot == 2);
    REQUIRE(model.customization.size() == 2);
    CHECK(model.customization[1].chrModelId == 6);
    // Option 1 of the flat list is ChrModel 6's first; choice 40 its second.
    REQUIRE(p.customization.size() == 2);
    CHECK(p.customization[0].option == 0);
    CHECK(p.customization[0].choice == 0);
    CHECK(p.customization[1].option == 1);
    CHECK(p.customization[1].choice == 1);
    // An empty item row is no item; slot 11 has no name.
    REQUIRE(p.items.size() == 2);
    CHECK(p.items[0].slot == std::optional<std::string>("head"));
    REQUIRE(model.items.size() == 2);
    CHECK(std::get<canon::Db2Row>(model.items[p.items[0].item].id).row == 9000);
    CHECK_FALSE(p.items[1].slot.has_value());
    CHECK(p.items[1].sourceSlot == 11);
    REQUIRE(model.presets[1].unresolved.size() == 1);
    CHECK(model.presets[1].unresolved[0].field == "BakeMaterialResourcesID");
}

TEST_CASE("addCreatureOptions: a model whose displays pick geosets shows 0, 900+ and the picked ones") {
    creaturedisplay::Data data;
    data.modelDataByFile[kModelFile] = {{1, 1.0f, true}};
    data.displaysByModelData[1] = {display(10, {}), display(11, {})};
    data.geosetsByDisplay[10] = {102, 201};
    canon::Model model = modelWithGeosets({0, 101, 102, 201, 202, 901});
    creatureinput::OptionsReport report;
    creatureinput::addCreatureOptions(data, kModelFile, model, textureNamed, report);
    REQUIRE(model.presets.size() == 2);
    CHECK(*model.presets[0].shownGeosets == std::vector<uint32_t>{0, 102, 201, 901});
    CHECK(*model.presets[1].shownGeosets == std::vector<uint32_t>{0, 901});
}

TEST_CASE("addCreatureOptions: particle colour rows are options with ARGB unpacked to RGBA; an unknown id stays unresolved") {
    creaturedisplay::Data data;
    data.modelDataByFile[kModelFile] = {{1, 1.0f, false}};
    creaturedisplay::Display known = display(10, {});
    known.particleColorId = 5;
    creaturedisplay::Display again = display(11, {});
    again.particleColorId = 5;
    again.alpha = 0;
    creaturedisplay::Display missing = display(12, {});
    missing.particleColorId = 6;
    data.displaysByModelData[1] = {known, again, missing};
    data.particleColors[5] = {{0xFFFF0000, 0, 0}, {0x8000FF00, 0, 0}, {0xFF0000FF, 0, 0}};
    canon::Model model = modelWithGeosets({0});
    creatureinput::OptionsReport report;
    creatureinput::addCreatureOptions(data, kModelFile, model, textureNamed, report);
    REQUIRE(model.particleColorOptions.size() == 1);
    CHECK(std::get<canon::Db2Row>(model.particleColorOptions[0].ref.id).row == 5);
    const std::vector<canon::ParticleColorRamp>& ramps = model.particleColorOptions[0].ramps;
    REQUIRE(ramps.size() == 3);
    CHECK(ramps[0].start == std::array<float, 4>{1, 0, 0, 1});
    CHECK(ramps[0].mid[3] == doctest::Approx(128.0f / 255.0f));
    CHECK(ramps[0].end == std::array<float, 4>{0, 0, 1, 1});
    REQUIRE(model.presets.size() == 3);
    CHECK(model.presets[0].particleColors == 0u);
    CHECK(model.presets[1].particleColors == 0u);
    CHECK_FALSE(model.presets[2].particleColors.has_value());
    REQUIRE(model.presets[2].unresolved.size() == 1);
    CHECK(model.presets[2].unresolved[0].field == "ParticleColorID");
}

TEST_CASE("creaturedisplay::load: real 12.1.0 tables -- the mana wyrm mount's displays" *
          doctest::skip(!fs::exists(fs::path(kMountDb2Dir) / "creaturedisplayinfo.db2") || test::testDbdDir().empty())) {
    std::ostringstream err;
    std::optional<creaturedisplay::Data> data = creaturedisplay::load(kMountDb2Dir, test::testDbdDir(), err);
    INFO(err.str());
    REQUIRE(data.has_value());
    // creature/manawyrm2mount/manawyrm2mount.m2
    const std::vector<creaturedisplay::ModelData>& rows = data->modelDataByFile.at(6423198);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].displaysPickGeosets);
    const std::vector<creaturedisplay::Display>& displays = data->displaysByModelData.at(rows[0].id);
    CHECK(displays.size() == 9);
    auto white = std::find_if(displays.begin(), displays.end(), [](const auto& d) { return d.id == 137144; });
    REQUIRE(white != displays.end());
    CHECK(white->particleColorId == 2426);
    CHECK(white->extra == 0);
    CHECK(white->textures[3] == 7266830);
    CHECK(data->geosetsByDisplay.at(137144) == std::vector<uint32_t>{102, 201});
    CHECK(data->particleColors.count(2426) == 1);
}
