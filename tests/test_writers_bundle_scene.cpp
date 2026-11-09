// husk::writers::writeBundle/writeWorldBundle's scene and effect output:
// attachments, events, lights, and the fog, ambient and particulate
// instances owned sets and world documents place. Synthetic models; shared
// helpers in test_bundle_fixtures.hpp.

#include <doctest/doctest.h>

#include "test_bundle_fixtures.hpp"

TEST_CASE("writeBundle: scene -- attachments, events, lights and emitters with inline and lifetime curves") {
    canon::Model model = buildSyntheticModel();
    canon::Attachment attachment;
    attachment.pointId = 11;
    attachment.bone = canon::boneRef(1);
    attachment.position = {1.0f, 2.0f, 3.0f};
    canon::ScalarCurve animate;
    animate.sequence = canon::SequenceRef::globalSequence(2);
    animate.interpolation = canon::Interpolation::Step;
    animate.keyframes = {{0.0f, 1.0f}};
    attachment.animateAttached.push_back(animate);
    model.scene.attachments.push_back(attachment);
    model.scene.events.push_back({"$DTH", 7, canon::boneRef(0), {}});
    model.scene.lights.push_back(canon::Light{});
    canon::PlacedInstance emitter;
    emitter.asset = canon::Ref{canon::None{}, "classic_m2_particle_emitter", canon::NameSource::Synthesized};
    emitter.anchor = canon::boneRef(0);
    emitter.translation = {0, 0, 1};
    canon::EffectPlacement effect;
    effect.inputs = {{"texture", {canon::TextureRef{}}}, {"tiles_x", {int64_t{2}}}, {"tiles_y", {int64_t{2}}},
                     {"frames", {int64_t{4}}}};
    canon::ParticleSystem system;
    system.headCell.keyframes = {{0, 4}, {32767, 9}};
    canon::StyleBlock style;
    style.particleSystem = system;
    effect.style = {{"particles", {style}}};
    effect.origin = {"M2Particle", 0};
    emitter.effect = effect;
    canon::Model::OwnedSet effects;
    effects.set.ref = canon::Ref{canon::RecordIndex{0}, "effects", canon::NameSource::Synthesized};
    effects.set.instances = {emitter};
    effects.alwaysOn = true;
    effects.inlineDocument = true;
    model.placementSets = {effects};

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-scene";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& scene = manifest["resources"]["scene"];

    REQUIRE(scene["attachments"].size() == 1);
    const auto& a = scene["attachments"][0];
    CHECK(a["point_id"] == 11);
    CHECK(a["bone"]["id"]["value"] == 1);
    CHECK(a["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    REQUIRE(a["animate_attached"].size() == 1);
    CHECK(a["animate_attached"][0]["sequence_kind"] == "global_sequence");
    CHECK(a["animate_attached"][0]["sequence_index"] == 2);
    CHECK(a["animate_attached"][0]["interpolation"] == "step");

    CHECK(scene["events"][0]["identifier"] == "$DTH");
    CHECK_FALSE(scene["animated_lights"][0].contains("bone"));
    CHECK(scene["animated_lights"][0]["kind"] == "directional");
    CHECK(scene["animated_lights"][0]["source"]["type"] == 0);
    CHECK_FALSE(scene.contains("particle_emitters"));

    // The emitter is an instance of the model's inline effects set.
    const auto& reference = manifest["references"][0];
    CHECK(reference["kind"] == "placement_set");
    CHECK_FALSE(reference["asset"].contains("uri"));
    CHECK_FALSE(fs::exists(dir / "sets"));
    const auto& set = reference["document"];
    CHECK(set["references"][0]["asset"]["uri"] == "effects/classic_m2_particle_emitter.json");
    const auto& p = set["instances"][0];
    CHECK(p["anchor"]["id"]["value"] == 0);
    CHECK(textureAt(set, p["inputs"]["texture"])["state"] == "known_unresolved");
    const auto& written = p["style"]["particles"]["particle_system"];
    CHECK(written["head_cell"] == nlohmann::json::array({{0, 4}, {32767, 9}}));
    CHECK(written["emission_rate"].empty());
}

TEST_CASE("writeBundle: local lights write their optional blocks only when present") {
    canon::Model model = buildSyntheticModel();
    canon::LocalLight spot;
    spot.ref = canon::Ref{canon::RecordIndex{0}, "mnld_0", canon::NameSource::Synthesized};
    spot.kind = canon::LocalLight::Kind::Spot;
    spot.position = {1, 2, 3};
    spot.eulerRotation = canon::Vec3{-3, 0, 0};
    spot.color = {1, 0.5f, 0};
    spot.intensity = 2;
    spot.attenuationEnd = 9;
    spot.cone = canon::LocalLight::Cone{5, 0.5f, 1.5f};
    spot.flicker = canon::LocalLight::Flicker{2, 0.25f, 3};
    spot.castsShadows = true;
    spot.cookie.emplace();
    spot.cookie->state = canon::TextureRef::State::KnownUnresolved;
    spot.cookie->resolved = canon::Ref{canon::FileDataId{9001}, "cookie_window", canon::NameSource::Listfile};
    spot.placementSet = canon::Ref{canon::RecordIndex{1}, "Set_B", canon::NameSource::WmoEmbedded};
    spot.sourceFlags = 0x3;
    canon::LocalLight omni;
    omni.ref = canon::Ref{canon::RecordIndex{0}, "molt_0", canon::NameSource::Synthesized};
    spot.origin = {"MNLD", 4};
    model.scene.localLights = {spot, omni};
    model.placementSets.resize(2);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-local-lights";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& lights = manifest["resources"]["scene"]["static_lights"];

    REQUIRE(lights.size() == 2);
    const auto& s = lights[0];
    CHECK(s["ref"]["name"] == "mnld_0");
    CHECK(s["kind"] == "spot");
    CHECK(s["euler_rotation"] == nlohmann::json::array({-3.0, 0.0, 0.0}));
    CHECK_FALSE(s.contains("rotation"));
    CHECK(s["color"] == nlohmann::json::array({1.0, 0.5, 0.0}));
    CHECK(s["cone"]["outer_angle"] == 1.5);
    CHECK(s["flicker"]["mode"] == 2);
    CHECK(s["casts_shadows"] == true);
    CHECK(manifest["resources"]["textures"][s["cookie"].get<size_t>()]["identity"]["id"]["value"] == 9001);
    CHECK(s["placement_set"] == 1);
    CHECK(s["source"]["flags"] == "0x3");
    CHECK(s["source"]["record"]["table"] == "MNLD");
    CHECK(s["source"]["record"]["index"] == 4);
    const auto& o = lights[1];
    CHECK(o["kind"] == "point");
    for (const char* key : {"rotation", "euler_rotation", "gradient", "cone", "flicker", "cookie", "placement_set"}) {
        CAPTURE(key);
        CHECK_FALSE(o.contains(key));
    }
}

TEST_CASE("writeBundle: a particulate instance writes its emitter images into the set, with one style block per emitter") {
    canon::Model model = buildSyntheticModel();
    wmo::ParticulateVolume v;
    v.position = {1, 2, 3};
    v.extent = {7, 8, 9};
    v.particleCount = 500;
    wmo::ParticulateEmitter dust;
    dust.textureFileDataId = 101;
    dust.blendMode = 3;
    dust.color = {1, 0.5f, 0, 1};
    dust.tilesX = 4;
    dust.tilesY = 4;
    dust.frames = 16;
    wmo::ParticulateEmitter odd = dust;
    odd.textureFileDataId = 555;
    odd.blendMode = 7;
    v.emitters = {dust, odd};
    canon::TextureRef resolved;
    resolved.state = canon::TextureRef::State::Resolved;
    resolved.resolved = canon::Ref{canon::FileDataId{101}, "dust1", canon::NameSource::Listfile};
    resolved.payload = canon::TextureRef::Payload{{1, 2, 3}, canon::TextureEncoding::Png};
    canon::PlacedInstance instance = wmoinput::particulateInstance(v, {{101, resolved}}, {"MPVD", 0});
    instance.id = 9;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {instance};
    model.placementSets = {set};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-particulate";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);  // validates the set against the effect too
    CHECK_FALSE(manifest["resources"]["scene"].contains("ambient_particle_volumes"));
    CHECK_FALSE(fs::exists(dir / "particulate.bin"));

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    const auto& written = setDoc["instances"][0];
    CHECK(written["inputs"]["size"] == nlohmann::json::array({7.0, 8.0, 9.0}));
    const auto& texture = textureAt(setDoc, written["inputs"]["emitters"][0]["texture"]);
    CHECK(texture["payload"]["uri"] == "../textures/dust1.png");
    CHECK(fs::exists(dir / "textures" / "dust1.png"));
    CHECK(textureAt(setDoc, written["inputs"]["emitters"][1]["texture"])["state"] == "known_unresolved");
    CHECK(written["style"]["volume"]["particulate"]["max_particles"] == 500);
    REQUIRE(written["style"]["emitter"].size() == 2);
    CHECK(written["style"]["emitter"][0]["draw"]["blend"] == "add");
    CHECK_FALSE(written["style"]["emitter"][1]["draw"].contains("blend"));
}

TEST_CASE("writeBundle: an ambient box instance writes list and record inputs and its colours as style") {
    canon::Model model = buildSyntheticModel();
    canon::PlacedInstance box;
    box.id = 3;
    box.asset = canon::Ref{canon::None{}, "classic_ambient_box", canon::NameSource::Synthesized};
    canon::EffectPlacement effect;
    std::vector<canon::InputValue> planes;
    for (int k = 0; k < 6; ++k) {
        planes.push_back({std::vector<canon::NamedInput>{{"normal", {canon::Vec3{0, 0, k % 2 ? 1.0f : -1.0f}}},
                                                         {"distance", {2.0}}}});
    }
    effect.inputs = {{"planes", {planes}}};
    canon::StyleBlock style;
    style.ambientColors = std::array<std::array<float, 4>, 3>{{{0.2f, 0.4f, 0.6f, 1.0f}, {}, {}}};
    effect.style = {{"ambient", {style}}};
    effect.unresolved = {{"end", canon::UnresolvedField::Kind::Scalar, 1.5, "region.edge"}};
    effect.origin = {"MBVD", 0};
    box.effect = effect;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {box};
    model.placementSets = {set};
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-ambient";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    parseManifest(dir);  // validates the set's inputs against the effect too

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    const auto& instance = setDoc["instances"][0];
    REQUIRE(instance["inputs"]["planes"].size() == 6);
    CHECK(instance["inputs"]["planes"][1]["normal"] == nlohmann::json::array({0.0, 0.0, 1.0}));
    CHECK(instance["inputs"]["planes"][1]["distance"] == 2.0);
    CHECK(instance["style"]["ambient"]["ambient_color"]["colors"][0][1].get<double>() == doctest::Approx(0.4));
    CHECK(instance["unresolved"]["fields"][0]["candidate_role"] == "region.edge");
}

TEST_CASE("writeBundle: fog instances in an owned set, the effects they place, and the parts that name them") {
    canon::Model model = buildSyntheticModel();
    canon::PlacedInstance fog;
    fog.id = 7;
    fog.asset = canon::Ref{canon::None{}, "classic_local_fog", canon::NameSource::Synthesized};
    fog.translation = {1, 2, 3};
    fog.flags = 0x1000;
    canon::EffectPlacement effect;
    effect.inputs = {{"inner_radius", 5.0}, {"outer_radius", 20.0}};
    canon::StyleBlock style;
    style.distanceFog = canon::DistanceFog{canon::LinearFog{25, 100, {0.2f, 0.4f, 0.6f}}};
    style.underwater = canon::DistanceFog{canon::LinearFog{-25, 50, {0, 0, 1}}};
    effect.style = {{"fog", {style}}};
    effect.unresolved = {{"flags", canon::UnresolvedField::Kind::Bits, uint32_t{0x1000}, std::nullopt}};
    effect.origin = {"MFOG", 1};
    fog.effect = effect;
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {fog};
    model.placementSets = {set};
    model.defaultFog = canon::InstanceRef{0, 7};
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{0}, "hall", canon::NameSource::WmoEmbedded}, 0, {}, {},
                                {canon::InstanceRef{0, 7}}});
    model.mesh.parts.push_back({canon::Ref{canon::RecordIndex{1}, "", canon::NameSource::None}, 0, {}, {}, {}});
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-fogs";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    CHECK(manifest["resources"]["scene"]["default_fog"] == nlohmann::json({{"set", 0}, {"id", 7}}));
    const auto& parts = manifest["resources"]["geometry"]["mesh"]["parts"];
    CHECK(parts[0]["fog_volumes"] == nlohmann::json::array({{{"set", 0}, {"id", 7}}}));
    CHECK_FALSE(parts[1].contains("fog_volumes"));

    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    CHECK(setDoc["references"][0]["kind"] == "effect");
    CHECK(setDoc["references"][0]["asset"]["uri"] == "../effects/classic_local_fog.json");
    const auto& instance = setDoc["instances"][0];
    CHECK(instance["translation"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(instance["inputs"]["outer_radius"] == 20.0);
    CHECK(instance["style"]["fog"]["distance_fog"]["linear"]["start"] == 25.0);
    CHECK(instance["style"]["fog"]["looks"]["underwater"]["linear"]["end"] == 50.0);
    CHECK(instance["unresolved"]["fields"][0]["value"] == "0x1000");
    CHECK(instance["source"]["record"]["table"] == "MFOG");
    CHECK_FALSE(instance.contains("selection"));
    CHECK(fs::exists(dir / "effects" / "classic_local_fog.json"));

    // parseManifest checked the documents; the placeholder effect is a warning.
    std::vector<bundle_schema::Problem> warnings;
    bundle_schema::validateDocument(dir, nullptr, &warnings);
    CHECK(warnings.size() == 1);
}

TEST_CASE("writeWorldBundle: tile grid, and the map's volumes as effect sets with linked fog shapes") {
    canon::World world;
    world.subject = canon::Ref{canon::FileDataId{775971}, "azeroth", canon::NameSource::Listfile};
    world.cells.push_back({32, 49, canon::Ref{canon::FileDataId{778032}, "azeroth_32_49", canon::NameSource::Listfile},
                           {-533.3f, 0, -10}, {0, 533.3f, 90}});
    world.cells.push_back({33, 49, canon::Ref{canon::None{}, "azeroth_33_49", canon::NameSource::Listfile}, {}, {}});
    wdt::FogsFile fogs;
    wdt::Fog fog;
    fog.id = 1659;
    fog.flags = 0x11;
    fog.position = {1, 2, 3};
    fog.rotation = {0, 0, 0, 1};
    fog.modelFileDataId = 1728356;
    fogs.fogs = {fog};
    wdt::MpvFile mpv;
    wdt::MapParticulateVolume v;
    v.id = 185;
    v.volume.extent = {10, 10, 10};
    v.boundsMin = {-5, -5, -5};
    v.boundsMax = {5, 5, 5};
    mpv.volumes = {v};
    mapinput::MapSceneInputs in;
    in.fogs = &fogs;
    in.mpv = &mpv;
    in.assetNames.emplace(1728356, "fogvolume_sphere");
    world.placements = mapinput::buildMapEffects(in);

    fs::path root = fs::temp_directory_path() / "husk-test-world";
    fs::remove_all(root);
    fs::create_directories(root / "azeroth_32_49.bundle");
    std::ofstream(root / "azeroth_32_49.bundle" / "manifest.json") << "{}";
    fs::create_directories(root / "fogvolume_sphere.bundle");
    std::ofstream(root / "fogvolume_sphere.bundle" / "manifest.json") << "{}";
    fs::path dir = root / "azeroth.world.bundle";
    writers::Provenance provenance{"husk test", "wdt", {{"_fogs.wdt", "fog volumes", true, ""}}};
    writers::writeWorldBundle(world, dir, provenance, {"../azeroth_32_49.bundle/manifest.json", ""},
                              {{1728356, "../fogvolume_sphere.bundle/manifest.json"}});
    nlohmann::json manifest = parseManifest(dir);  // validates the sets and their effects too

    CHECK(manifest["kind"] == "world");
    CHECK(manifest["subject"]["name"] == "azeroth");
    CHECK_FALSE(manifest.contains("scene"));
    const auto& grid = manifest["grid"];
    CHECK(grid["cell_size"].get<double>() == doctest::Approx(533.3333));
    CHECK(grid["origin"][0].get<double>() == doctest::Approx(32 * 533.3333));
    CHECK(grid["axis_i"] == nlohmann::json::array({0.0, -1.0, 0.0}));
    CHECK(grid["axis_j"] == nlohmann::json::array({-1.0, 0.0, 0.0}));
    REQUIRE(grid["cells"].size() == 2);
    CHECK(grid["cells"][0]["i"] == 32);
    CHECK(grid["cells"][0]["j"] == 49);
    CHECK(grid["cells"][0]["bounds_max"][2] == 90.0);
    const auto& tile = manifest["references"][grid["cells"][0]["tile"].get<size_t>()];
    CHECK(tile["kind"] == "model");
    CHECK(tile["asset"]["uri"] == "../azeroth_32_49.bundle/manifest.json");
    CHECK_FALSE(manifest["references"][1]["asset"].contains("uri"));

    // The off-grid sets follow the tiles: fogs, then particulate volumes.
    REQUIRE(manifest["placements"].size() == 2);
    CHECK(manifest["references"][2]["asset"]["name"] == "fogs");
    CHECK(manifest["references"][3]["asset"]["name"] == "particulate_volumes");
    nlohmann::json fogSet = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    const auto& f = fogSet["instances"][0];
    CHECK(f["id"] == 1659);
    CHECK(f["translation"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(f["inputs"]["shape"]["name"] == "fogvolume_sphere");
    CHECK(f["inputs"]["shape"]["uri"] == "../../fogvolume_sphere.bundle/manifest.json");
    CHECK(f["source"]["flags"] == "0x11");
    nlohmann::json particleSet = nlohmann::json::parse(std::ifstream(dir / "sets" / "1.json"));
    CHECK(particleSet["instances"][0]["id"] == 185);
    CHECK(particleSet["instances"][0]["inputs"]["pieces"].size() == 1);
    CHECK(fs::exists(dir / "effects" / "classic_volumetric_fog.json"));
    CHECK(fs::exists(dir / "effects" / "classic_map_particulate_volume.json"));
    CHECK_FALSE(fs::exists(dir / "world.bin"));

    CHECK_THROWS(writers::writeWorldBundle(world, root / "mismatch.bundle", provenance, {""}));
}

TEST_CASE("writeWorldBundle: zones carry stable ids and name their parent by id") {
    canon::World world;
    world.subject = canon::Ref{canon::FileDataId{775971}, "azeroth", canon::NameSource::Listfile};
    world.cells.push_back({32, 49, canon::Ref{canon::None{}, "azeroth_32_49", canon::NameSource::Listfile}, {}, {}});
    canon::Zone elwynn;
    elwynn.id = nameUuid("zone/azeroth/AreaTable/12");
    elwynn.ref = canon::Ref{canon::Db2Row{"AreaTable", 12}, "Elwynn Forest", canon::NameSource::Db2};
    canon::Zone northshire;
    northshire.id = nameUuid("zone/azeroth/AreaTable/9");
    northshire.ref = canon::Ref{canon::Db2Row{"AreaTable", 9}, "Northshire", canon::NameSource::Db2};
    northshire.parent = 0;
    northshire.cells.push_back({32, 49, 0.5f});
    world.zones = {elwynn, northshire};

    fs::path root = fs::temp_directory_path() / "husk-test-world-zones";
    fs::remove_all(root);
    fs::path dir = root / "azeroth.world.bundle";
    writers::writeWorldBundle(world, dir, testProvenance(), {""});
    nlohmann::json manifest = parseManifest(dir);

    const auto& zones = manifest["zones"];
    REQUIRE(zones.size() == 2);
    CHECK(zones[0]["id"] == nameUuid("zone/azeroth/AreaTable/12"));
    CHECK_FALSE(zones[0].contains("parent"));
    CHECK(zones[1]["parent"] == zones[0]["id"]);

    // A parent that names no zone's id fails validation, wherever it sits in the array.
    manifest["zones"][1]["parent"] = nameUuid("zone/azeroth/AreaTable/999");
    std::ofstream(dir / "manifest.json") << manifest.dump();
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir);
    REQUIRE_FALSE(problems.empty());
    CHECK(problems[0].where.find("/zones/1/parent") != std::string::npos);
}

TEST_CASE("nameUuid: deterministic version-3 UUID text") {
    std::string a = nameUuid("zone/azeroth/AreaTable/12");
    CHECK(a == nameUuid("zone/azeroth/AreaTable/12"));
    CHECK(a != nameUuid("zone/azeroth/AreaTable/13"));
    REQUIRE(a.size() == 36);
    CHECK(a[8] == '-');
    CHECK(a[14] == '3');
    CHECK(std::string("89ab").find(a[19]) != std::string::npos);
}

TEST_CASE("writeWorldBundle: zone lights index the set's light_params, each set written once") {
    canon::World world;
    world.subject = canon::Ref{canon::FileDataId{775971}, "azeroth", canon::NameSource::Listfile};
    lightdb::Data lights;
    lights.mapIds["azeroth"] = 0;
    lightdb::Zone global;
    global.id = 1;
    global.params = {12, 12, 0, 0, 0, 0, 0, 0};
    lightdb::Zone local = global;
    local.id = 2;
    local.position = {1, 2, 3};
    local.falloffEnd = 10;
    lights.zones = {global, local};
    lightdb::Key noon;
    noon.time = 1440;
    noon.directColor = 0x01FF8000u;
    noon.colorGradingFileDataId = 1140733;
    noon.field_12_0_0_63854_043 = 1;
    lights.keysByParams[12] = {noon};
    lightdb::Params params;
    params.id = 12;
    params.flags = 0x4;
    params.lightSkyboxId = 81;
    params.unnamed = {0x3F800000, 0x3F800000, 0, 0, 0, 0, 0, 0, 0, 0x3F800000, 0x3F800000, 0, 0x3F800000,
                      std::bit_cast<uint32_t>(0.35f), 0x3F800000, std::bit_cast<uint32_t>(0.7f)};
    lights.params[12] = params;
    lights.skyboxes[81] = {81, "ENVIRONMENTS\\Stars\\DeathSkybox.mdx", 0x2, 235313, 0};
    mapinput::MapSceneInputs in;
    in.map = "azeroth";
    in.lights = &lights;
    in.assetNames.emplace(235313, "deathskybox");
    canon::TextureRef lut;
    lut.state = canon::TextureRef::State::Resolved;
    lut.resolved = canon::Ref{canon::FileDataId{1140733}, "colorgrading_identity", canon::NameSource::Listfile};
    in.textures.emplace(1140733, lut);
    world.placements = mapinput::buildMapEffects(in);

    fs::path root = fs::temp_directory_path() / "husk-test-world-lights";
    fs::remove_all(root);
    fs::create_directories(root / "deathskybox.bundle");
    std::ofstream(root / "deathskybox.bundle" / "manifest.json") << "{}";
    fs::path dir = root / "azeroth.world.bundle";
    writers::Provenance provenance{"husk test", "db2", {{"light.db2", "zone lights", true, ""}}};
    writers::writeWorldBundle(world, dir, provenance, {}, {{235313, "../deathskybox.bundle/manifest.json"}});
    parseManifest(dir);  // validates the set, its indices and its effects

    nlohmann::json set = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    CHECK(set["subject"]["name"] == "zone_lights");
    REQUIRE(set["instances"].size() == 2);
    CHECK(set["instances"][1]["style"]["light"]["zone_light"] == nlohmann::json{{"clear", 0}, {"clear_underwater", 0}});
    const auto& table = set["resources"]["light_params"];
    REQUIRE(table.size() == 1);
    const auto& settings = table[0]["settings"];
    CHECK(settings["hide_sun"] == true);
    CHECK(settings["skybox"]["model"]["uri"] == "../../deathskybox.bundle/manifest.json");
    CHECK(settings["skybox"]["source"]["flags"] == "0x2");
    CHECK_FALSE(settings.contains("unresolved"));
    const auto& key = table[0]["keys"][0];
    CHECK(key["time"] == 43200.0);
    CHECK(key["direct_color"][0].get<double>() == doctest::Approx(1.0));
    CHECK(key["unresolved"]["fields"][0]["field"] == "LightData DirectColor high byte");
    const auto& texture = set["resources"]["textures"][key["color_grading"].get<size_t>()];
    CHECK(texture["identity"]["name"] == "colorgrading_identity");
    CHECK(fs::exists(dir / "effects" / "classic_zone_light.json"));
    CHECK(fs::exists(dir / "effects" / "classic_zone_light_global.json"));
}
