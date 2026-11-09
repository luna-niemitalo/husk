// husk::writers::writeBundle's appearance output: texture payloads and the
// texture table, materials and their documents, uv maps, texture slots,
// customization options and presets. Synthetic models; shared helpers in
// test_bundle_fixtures.hpp.

#include <doctest/doctest.h>

#include "test_bundle_fixtures.hpp"

TEST_CASE("writeBundle: a Resolved TextureRef carrying a real payload writes a real "
          "textures/<name>.<ext> file and records a real uri (AUDIT.md §7.4); a Resolved "
          "TextureRef with no payload writes no uri and no file, and two layers sharing the "
          "same resolved name dedupe to one written file") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].layers.size() == 1);

    std::vector<uint8_t> pngBytes = {0x01, 0x02, 0x03, 0x04};  // arbitrary bytes -- this writer never decodes them, just stores them verbatim

    canon::TextureRef resolvedWithPayload;
    resolvedWithPayload.state = canon::TextureRef::State::Resolved;
    resolvedWithPayload.resolved.id = canon::FileDataId{99};
    resolvedWithPayload.resolved.name = "shared_texture";
    canon::TextureRef::Payload payload;
    payload.bytes = pngBytes;
    payload.encoding = canon::TextureEncoding::Png;
    resolvedWithPayload.payload = payload;
    model.materials[0].layers[0].texture = resolvedWithPayload;

    // A second layer on a second material, same resolved name -- exercises
    // the writtenTextures dedup (one file written, not two).
    canon::Material material2 = model.materials[0];
    material2.ref.id = canon::RecordIndex{1};
    model.materials.push_back(material2);
    model.primitiveMaterials.push_back(canon::Identity{canon::RecordIndex{1}});
    model.mesh.primitives.push_back(model.mesh.primitives[0]);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-texture-payload";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());

    fs::path textureFile = dir / "textures" / "shared_texture.png";
    REQUIRE(fs::exists(textureFile));
    std::vector<uint8_t> writtenBytes = readFile(textureFile.string());
    CHECK(writtenBytes == pngBytes);

    nlohmann::json manifest = parseManifest(dir);
    auto& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);
    // Both layers name the one table entry.
    CHECK(materials[0]["layers"][0]["texture"] == materials[1]["layers"][0]["texture"]);
    CHECK(manifest["resources"]["textures"].size() == 1);
    const auto& shared = textureAt(manifest, materials[0]["layers"][0]["texture"]);
    CHECK(shared["state"] == "resolved");
    CHECK(shared["payload"]["uri"] == "textures/shared_texture.png");

    // Only one real file on disk despite two referencing layers.
    size_t textureFileCount = 0;
    for (const auto& entry : fs::directory_iterator(dir / "textures")) {
        (void)entry;
        ++textureFileCount;
    }
    CHECK(textureFileCount == 1);

    // Resolved but no payload -- no uri, no textures/ directory created at all.
    fs::path dirNoPayload = fs::temp_directory_path() / "husk-test-bundle-texture-no-payload";
    fs::remove_all(dirNoPayload);
    canon::Model modelNoPayload = buildSyntheticModel();
    canon::TextureRef resolvedNoPayload;
    resolvedNoPayload.state = canon::TextureRef::State::Resolved;
    resolvedNoPayload.resolved.id = canon::FileDataId{99};
    resolvedNoPayload.resolved.name = "unfetched_texture";
    modelNoPayload.materials[0].layers[0].texture = resolvedNoPayload;
    writers::writeBundle(modelNoPayload, dirNoPayload, testProvenance());

    nlohmann::json manifestNoPayload = parseManifest(dirNoPayload);
    auto& layer0NoPayload = manifestNoPayload["resources"]["materials"][0]["layers"][0];
    CHECK(textureAt(manifestNoPayload, layer0NoPayload["texture"])["state"] == "resolved");
    CHECK_FALSE(textureAt(manifestNoPayload, layer0NoPayload["texture"]).contains("payload"));
    CHECK_FALSE(fs::exists(dirNoPayload / "textures"));
}

TEST_CASE("writeBundle: rawPayload (DDS-housed source blocks) wins over payload (PNG) when both are "
          "present, and a payload-only TextureRef still falls back to writing PNG (BUNDLE_FORMAT.md's "
          "'Texture encoding -- settled')") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].layers.size() == 1);

    // Deliberately not a real DDS byte layout -- this writer never parses
    // rawPayload's bytes, just stores them verbatim under a .dds name, same
    // "arbitrary bytes" convention the payload-only test above already uses.
    std::vector<uint8_t> ddsBytes = {0x44, 0x44, 0x53, 0x20, 0xAA, 0xBB};
    std::vector<uint8_t> pngBytes = {0x01, 0x02, 0x03, 0x04};

    canon::TextureRef bothPayloads;
    bothPayloads.state = canon::TextureRef::State::Resolved;
    bothPayloads.resolved.id = canon::FileDataId{101};
    bothPayloads.resolved.name = "both_variants";
    canon::TextureRef::Payload png;
    png.bytes = pngBytes;
    png.encoding = canon::TextureEncoding::Png;
    bothPayloads.payload = png;
    canon::TextureRef::Payload dds;
    dds.bytes = ddsBytes;
    dds.encoding = canon::TextureEncoding::Bc3;
    bothPayloads.rawPayload = dds;
    model.materials[0].layers[0].texture = bothPayloads;

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());

    // rawPayload wins: a real .dds file with rawPayload's own bytes, not
    // payload's PNG bytes, and no .png file written at all for this texture.
    fs::path ddsFile = dir / "textures" / "both_variants.dds";
    REQUIRE(fs::exists(ddsFile));
    CHECK(readFile(ddsFile.string()) == ddsBytes);
    CHECK_FALSE(fs::exists(dir / "textures" / "both_variants.png"));

    nlohmann::json manifest = parseManifest(dir);
    auto& layer0 = manifest["resources"]["materials"][0]["layers"][0];
    CHECK(textureAt(manifest, layer0["texture"])["state"] == "resolved");
    CHECK(textureAt(manifest, layer0["texture"])["payload"]["uri"] == "textures/both_variants.dds");

    // payload-only (no rawPayload) -- falls back to writing the .png
    // variant (e.g. a source that was never a compressed BLP, or one where
    // DDS extraction declined).
    fs::path dirPngOnly = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload-fallback";
    fs::remove_all(dirPngOnly);
    canon::Model modelPngOnly = buildSyntheticModel();
    canon::TextureRef pngOnly;
    pngOnly.state = canon::TextureRef::State::Resolved;
    pngOnly.resolved.id = canon::FileDataId{102};
    pngOnly.resolved.name = "png_only";
    pngOnly.payload = png;
    modelPngOnly.materials[0].layers[0].texture = pngOnly;
    writers::writeBundle(modelPngOnly, dirPngOnly, testProvenance());

    fs::path pngFile = dirPngOnly / "textures" / "png_only.png";
    REQUIRE(fs::exists(pngFile));
    CHECK(readFile(pngFile.string()) == pngBytes);
    CHECK_FALSE(fs::exists(dirPngOnly / "textures" / "png_only.dds"));

    nlohmann::json manifestPngOnly = parseManifest(dirPngOnly);
    CHECK(textureAt(manifestPngOnly, manifestPngOnly["resources"]["materials"][0]["layers"][0]["texture"])["payload"]["uri"] ==
          "textures/png_only.png");

    // Neither present -- no uri, no textures/ directory, same as the
    // existing no-payload case above.
    fs::path dirNeither = fs::temp_directory_path() / "husk-test-bundle-texture-rawpayload-neither";
    fs::remove_all(dirNeither);
    canon::Model modelNeither = buildSyntheticModel();
    canon::TextureRef neither;
    neither.state = canon::TextureRef::State::Resolved;
    neither.resolved.id = canon::FileDataId{103};
    neither.resolved.name = "neither_variant";
    modelNeither.materials[0].layers[0].texture = neither;
    writers::writeBundle(modelNeither, dirNeither, testProvenance());

    nlohmann::json manifestNeither = parseManifest(dirNeither);
    CHECK_FALSE(manifestNeither["resources"]["materials"][0]["layers"][0]["texture"].contains("uri"));
    CHECK_FALSE(fs::exists(dirNeither / "textures"));
}

TEST_CASE("writeBundle: a material's framebuffer blend is written by name, and omitted when unknown") {
    canon::Model model = buildSyntheticModel();
    REQUIRE(model.materials.size() == 1);
    model.materials[0].framebufferBlend = canon::FramebufferBlend::AlphaKey;
    canon::Material unknown = model.materials[0];
    unknown.ref.id = canon::RecordIndex{1};
    unknown.framebufferBlend.reset();
    model.materials.push_back(unknown);
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-framebuffer-blend";
    fs::remove_all(dir);

    writers::writeBundle(model, dir, testProvenance());

    nlohmann::json manifest = parseManifest(dir);
    const nlohmann::json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);
    CHECK(materials[0]["framebuffer_blend"] == "alpha_key");
    CHECK_FALSE(materials[1].contains("framebuffer_blend"));
}

TEST_CASE("writeBundle: texture slot options and particle colours in the definition, presets in the selection") {
    canon::Model model = buildSyntheticModel();
    canon::TextureRef skin;
    skin.state = canon::TextureRef::State::KnownUnresolved;
    skin.resolved.id = canon::FileDataId{123};
    skin.unresolvedReason = "not extracted";
    model.textureSlots = {{"monster_1", 11, {skin}, std::nullopt}, {"variation_4", std::nullopt, {skin}, "test inference"}};
    model.materials[0].layers[0].slot = 0;
    model.particleColorOptions.push_back(
        {canon::Ref{canon::Db2Row{"ParticleColor", 5}, "", canon::NameSource::None},
         {{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}}});
    canon::Preset preset;
    preset.textures.push_back({0, 0});
    preset.shownGeosets = std::vector<uint32_t>{0, 203};
    preset.particleColors = 0;
    preset.opacity = 0.5f;
    canon::Definition customization;
    customization.chrModelId = 45;
    canon::Option hair;
    hair.ref = canon::Ref{canon::Db2Row{"ChrCustomizationOption", 3}, "Hair Style", canon::NameSource::Db2};
    hair.category.ref = canon::Ref{canon::Db2Row{"ChrCustomizationCategory", 1}, "", canon::NameSource::None};
    canon::Choice longHair;
    longHair.ref = canon::Ref{canon::Db2Row{"ChrCustomizationChoice", 30}, "", canon::NameSource::None};
    longHair.geoset = canon::Geoset::fromRawId(203);
    hair.choices = {longHair};
    customization.options = {hair};
    model.customization = {customization};
    model.items = {{canon::Ref{canon::Db2Row{"ItemDisplayInfo", 9000}, "", canon::NameSource::None}, {}, {}, {}},
                   {canon::Ref{canon::Db2Row{"ItemDisplayInfo", 9011}, "", canon::NameSource::None}, {}, {}, {}}};
    preset.customization.push_back({0, 0});
    preset.items.push_back({"head", 0, 0});
    preset.items.push_back({std::nullopt, 11, 1});
    preset.sources.push_back({canon::Ref{canon::Db2Row{"CreatureDisplayInfo", 7}, "", canon::NameSource::None}, 1.5f, 2});
    preset.unresolved.push_back({"TextureVariationFileDataID[3]", canon::UnresolvedField::Kind::Id, 99.0, std::nullopt});
    model.presets.push_back(preset);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-options";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& definition = manifest["definition"];
    CHECK(definition["texture_slots"][0]["name"] == "monster_1");
    CHECK(definition["texture_slots"][0]["source"]["texture_type"] == 11);
    CHECK(textureAt(manifest, definition["texture_slots"][0]["options"][0])["state"] == "known_unresolved");
    CHECK(textureAt(manifest, definition["texture_slots"][0]["options"][0])["identity"]["id"]["value"] == 123);
    CHECK_FALSE(definition["texture_slots"][0].contains("inferred_from"));
    CHECK_FALSE(definition["texture_slots"][1].contains("source"));
    CHECK(definition["texture_slots"][1]["inferred_from"] == "test inference");
    CHECK(definition["particle_colors"][0]["ramps"][0]["mid"] == nlohmann::json::array({0.0, 1.0, 0.0, 1.0}));
    CHECK(manifest["resources"]["materials"][0]["layers"][0]["slot"] == 0);
    const auto& p = manifest["selection"]["presets"][0];
    CHECK(p["textures"][0]["slot"] == 0);
    CHECK(p["textures"][0]["option"] == 0);
    CHECK(p["shown_geosets"] == nlohmann::json::array({0, 203}));
    CHECK(p["particle_colors"] == 0);
    CHECK(p["opacity"] == 0.5);
    CHECK(p["customization"][0]["option"] == 0);
    CHECK(p["customization"][0]["choice"] == 0);
    const auto& option = definition["customization"]["options"][0];
    CHECK(option["ref"]["name"] == "Hair Style");
    CHECK(option["chr_model"]["id"]["row"] == 45);
    CHECK(option["choices"][0]["ref"]["id"]["row"] == 30);
    CHECK(option["choices"][0]["geoset_id"] == 203);
    CHECK(p["items"][0]["slot"] == "head");
    CHECK(p["items"][1]["item"] == 1);
    CHECK(definition["items"][1]["ref"]["id"]["row"] == 9011);
    CHECK_FALSE(p["items"][1].contains("slot"));
    CHECK(p["items"][1]["source"]["item_slot"] == 11);
    CHECK(p["sources"][0]["ref"]["id"]["row"] == 7);
    CHECK(p["sources"][0]["scale"] == 1.5);
    CHECK(p["sources"][0]["base_scale"] == 2);
    CHECK(p["unresolved"]["fields"][0]["value"] == 99);
}

TEST_CASE("writeBundle: physics, correction sets a choice selects, and an item's models, textures and overlays") {
    canon::Model model = buildSyntheticModel();
    auto unresolvedTexture = [](uint32_t fdid) {
        canon::TextureRef ref;
        ref.state = canon::TextureRef::State::KnownUnresolved;
        ref.resolved.id = canon::FileDataId{fdid};
        ref.unresolvedReason = "not extracted";
        return ref;
    };
    canon::Physics physics;
    physics.bodies.push_back({0, 0, {1, 2, 3}, 0, 1, 0, 1, 0.5f, 0, 0.9f});
    physics.shapes.push_back({1, 0, 0.5f, 0.1f, 1.0f});
    physics.capsules.push_back({{0, 0, 0}, {0, 0, 1}, 0.25f});
    physics.joints.push_back({0, 0, 1, 0});
    physics.shoulderJoints.push_back({});
    physics.shoulderJoints[0].coneAngle = 60;
    model.physics = physics;

    canon::CorrectionSet set;
    set.ref = canon::Ref{canon::FileDataId{555}, "", canon::NameSource::None};
    set.corrections.push_back({0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}});
    model.correctionSets = {set};
    canon::Definition customization;
    customization.chrModelId = 1;
    canon::Option jaw;
    jaw.ref = canon::Ref{canon::Db2Row{"ChrCustomizationOption", 8}, "Jaw", canon::NameSource::Db2};
    jaw.category.ref = canon::Ref{canon::Db2Row{"ChrCustomizationCategory", 1}, "", canon::NameSource::None};
    canon::Choice wide;
    wide.ref = canon::Ref{canon::Db2Row{"ChrCustomizationChoice", 80}, "", canon::NameSource::None};
    jaw.choices = {wide};
    customization.options = {jaw};
    model.customization = {customization};
    model.choiceCorrectionSets = {{{0, 0}, 0}};

    canon::ItemEntry item;
    item.ref = canon::Ref{canon::Db2Row{"ItemModifiedAppearance", 15}, "", canon::NameSource::None};
    item.models.push_back({canon::Ref{canon::FileDataId{148132}, "", canon::NameSource::None}, "items/148132.bundle/manifest.json"});
    item.textures.push_back({2, unresolvedTexture(700)});
    item.overlays.push_back({3, unresolvedTexture(701)});
    model.items = {item};
    canon::Preset preset;
    preset.items.push_back({"main_hand", std::nullopt, 0, "MAIN_HAND"});
    preset.sources.push_back({canon::Ref{canon::None{}, "husk-appearance/1 gear=MAIN_HAND:15", canon::NameSource::Synthesized}, 1, 1});
    model.presets.push_back(preset);

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-sidecars";
    fs::remove_all(dir);
    writers::writeBundle(buildSyntheticModel(), dir / "items" / "148132.bundle", testProvenance());
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);

    const auto& p = manifest["resources"]["physics"];
    CHECK(p["bodies"][0]["position"] == nlohmann::json::array({1.0, 2.0, 3.0}));
    CHECK(p["bodies"][0]["shapes"] == nlohmann::json::array({0}));
    CHECK(p["shapes"][0]["kind"] == "capsule");
    CHECK(p["shapes"][0]["radius"] == 0.25);
    CHECK(p["joints"][0]["kind"] == "shoulder");
    CHECK(p["joints"][0]["source"]["cone_angle"] == 60);

    CHECK(manifest["resources"]["rig"]["correction_sets"][0]["ref"]["id"]["value"] == 555);
    CHECK(manifest["resources"]["rig"]["correction_sets"][0]["corrections"][0]["matrix"].size() == 16);
    CHECK(manifest["definition"]["customization"]["options"][0]["choices"][0]["correction_set"] == 0);

    const auto& i = manifest["definition"]["items"][0];
    CHECK(i["models"][0]["uri"] == "items/148132.bundle/manifest.json");
    CHECK(i["textures"][0]["texture_type"] == 2);
    CHECK(i["overlays"][0]["component_section"] == 3);
    bool referenced = false;
    for (const auto& r : manifest["references"]) referenced |= r["asset"].value("uri", "") == "items/148132.bundle/manifest.json";
    CHECK(referenced);
    const auto& pi = manifest["selection"]["presets"][0]["items"][0];
    CHECK(pi["slot"] == "main_hand");
    CHECK(pi["source"]["slot_token"] == "MAIN_HAND");
    CHECK_FALSE(pi["source"].contains("item_slot"));
}

TEST_CASE("writeBundle: a model without options writes no texture_slots, particle_colors or presets") {
    canon::Model model = buildSyntheticModel();
    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-no-options";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    CHECK_FALSE(manifest["definition"].contains("texture_slots"));
    CHECK_FALSE(manifest["definition"].contains("particle_colors"));
    CHECK_FALSE(manifest["selection"].contains("presets"));
}

TEST_CASE("externalizeTextures: payloads go to the shared store once; the model and its set files point there") {
    canon::Model model = buildSyntheticModel();
    canon::TextureRef skin;
    skin.state = canon::TextureRef::State::Resolved;
    skin.resolved = canon::Ref{canon::FileDataId{101}, "skin", canon::NameSource::Listfile};
    skin.rawPayload = canon::TextureRef::Payload{{9, 9, 9}, canon::TextureEncoding::Bc1};
    model.materials[0].layers[0].texture = skin;
    model.textureSlots = {{"monster_1", 11, {skin}, std::nullopt}};
    wmo::ParticulateVolume v;
    v.extent = {1, 1, 1};
    wmo::ParticulateEmitter dust;
    dust.textureFileDataId = 101;
    v.emitters = {dust};
    canon::Model::OwnedSet set;
    set.set.ref = canon::Ref{canon::RecordIndex{0}, "Set_$DefaultGlobal", canon::NameSource::WmoEmbedded};
    set.alwaysOn = true;
    set.set.instances = {wmoinput::particulateInstance(v, {{101, skin}}, {"MPVD", 0})};
    model.placementSets = {set};

    fs::path root = fs::temp_directory_path() / "husk-test-texture-store";
    fs::remove_all(root);
    fs::create_directories(root / "textures");
    writers::TextureStore store(root / "textures");
    writers::AssetUris uris;
    writers::externalizeTextures(model, store, "../../textures/", uris);
    CHECK_FALSE(model.materials[0].layers[0].texture.rawPayload.has_value());
    CHECK(uris.at(101) == "../../textures/101.dds");
    CHECK(readFile((root / "textures" / "101.dds").string()) == std::vector<uint8_t>{9, 9, 9});
    CHECK_FALSE(fs::exists(root / "textures" / "101.dds.partial"));

    fs::path dir = root / "models" / "500.canon.bundle";
    writers::writeBundle(model, dir, testProvenance(), {}, uris);
    nlohmann::json manifest = parseManifest(dir);  // also checks the shared file exists
    CHECK_FALSE(fs::exists(dir / "textures"));
    REQUIRE(manifest["resources"]["textures"].size() == 1);
    CHECK(textureAt(manifest, manifest["resources"]["materials"][0]["layers"][0]["texture"])["payload"]["uri"] ==
          "../../textures/101.dds");
    nlohmann::json setDoc = nlohmann::json::parse(std::ifstream(dir / "sets" / "0.json"));
    CHECK(textureAt(setDoc, setDoc["instances"][0]["inputs"]["emitters"][0]["texture"])["payload"]["uri"] ==
          "../../../textures/101.dds");
}

TEST_CASE("writeBundle: material documents beside the model; a slot's composite and a choice's textures point at them") {
    canon::Model model = buildSyntheticModel();
    canon::MaterialDocument skin;
    skin.subject = canon::Ref{canon::Db2Row{"ChrModelMaterial", 1}, "skin_layout_103", canon::NameSource::Synthesized};
    skin.imageInputs = {{"target_4", "The face."}};
    canon::CompositeTexture composite;
    composite.width = 64;
    composite.height = 32;
    composite.layers.push_back({std::string("target_4"), {32, 0, 32, 32}, canon::CompositeBlend::Overlay, 6, 2});
    skin.textures.push_back(composite);
    skin.layers.push_back({canon::SurfaceRole::Base, canon::TextureSource{0u}, canon::UvSetIndex{0}, canon::Combine::Replace});
    model.materialDocuments = {skin};
    model.textureSlots = {{"skin", 1, {}, std::nullopt, 0}};
    canon::Definition customization;
    canon::Option face;
    face.ref = canon::Ref{canon::Db2Row{"ChrCustomizationOption", 3}, "Face", canon::NameSource::Db2};
    face.category.ref = canon::Ref{canon::Db2Row{"ChrCustomizationCategory", 1}, "", canon::NameSource::None};
    canon::Choice first;
    first.ref = canon::Ref{canon::Db2Row{"ChrCustomizationChoice", 30}, "", canon::NameSource::None};
    face.choices = {first};
    customization.options = {face};
    model.customization = {customization};
    canon::TextureRef faceTexture;
    faceTexture.state = canon::TextureRef::State::KnownUnresolved;
    faceTexture.resolved.id = canon::FileDataId{1700};
    model.choiceTextures.push_back({{0, 0}, 0, "target_4", faceTexture, canon::CustomizationPick{0, 0}});

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-materials";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);  // follows the material reference too
    const auto& ref = manifest["references"][0];
    CHECK(ref["kind"] == "material");
    CHECK(ref["asset"]["uri"] == "materials/0.json");
    CHECK(manifest["definition"]["texture_slots"][0]["composite"] == 0);
    const auto& bound = manifest["definition"]["customization"]["options"][0]["choices"][0]["textures"][0];
    CHECK(bound["material"] == 0);
    CHECK(bound["input"] == "target_4");
    CHECK(bound["only_with"]["option"] == 0);
    CHECK(textureAt(manifest, bound["texture"])["identity"]["id"]["value"] == 1700);
    nlohmann::json material = nlohmann::json::parse(std::ifstream(dir / "materials" / "0.json"));
    CHECK(material["kind"] == "material");
    CHECK(material["definition"]["inputs"]["target_4"]["kind"] == "image");
    const auto& drawn = material["resources"]["textures"][0]["composite"];
    CHECK(drawn["blend_mapping"] == "unverified");
    CHECK(drawn["layers"][0]["blend"] == "overlay");
    CHECK(drawn["layers"][0]["rect"] == nlohmann::json::array({32, 0, 32, 32}));
}

TEST_CASE("validateDocument: a choice binding an input its material doesn't declare is a problem") {
    canon::Model model = buildSyntheticModel();
    canon::MaterialDocument skin;
    skin.subject = canon::Ref{canon::Db2Row{"ChrModelMaterial", 1}, "skin", canon::NameSource::Synthesized};
    skin.imageInputs = {{"target_4", "The face."}};
    canon::CompositeTexture composite;
    composite.width = 4;
    composite.height = 4;
    skin.textures.push_back(composite);
    skin.layers.push_back({canon::SurfaceRole::Base, canon::TextureSource{0u}, canon::UvSetIndex{0}, canon::Combine::Replace});
    model.materialDocuments = {skin};
    canon::Definition customization;
    canon::Option face;
    face.ref = canon::Ref{canon::Db2Row{"ChrCustomizationOption", 3}, "Face", canon::NameSource::Db2};
    face.category.ref = canon::Ref{canon::Db2Row{"ChrCustomizationCategory", 1}, "", canon::NameSource::None};
    face.choices = {canon::Choice{canon::Ref{canon::Db2Row{"ChrCustomizationChoice", 30}, "", canon::NameSource::None}, 0, std::nullopt}};
    customization.options = {face};
    model.customization = {customization};
    canon::TextureRef texture;
    model.choiceTextures.push_back({{0, 0}, 0, "target_9", texture, std::nullopt});

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-material-bad-input";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].message.find("target_9") != std::string::npos);
}

TEST_CASE("writeBundle: layers sampling alike share one uv map; others get their own") {
    canon::Model model = buildSyntheticModel();
    canon::MaterialLayer same = model.materials[0].layers[0];
    canon::MaterialLayer scrolled = same;
    scrolled.uvScroll = canon::Vec2{0.25f, 0};
    canon::MaterialLayer environment = same;
    environment.uv = canon::EnvironmentMapped{};
    model.materials[0].layers = {same, same, scrolled, environment};

    fs::path dir = fs::temp_directory_path() / "husk-test-bundle-uv-maps";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());
    nlohmann::json manifest = parseManifest(dir);
    const auto& resources = manifest["resources"];
    const auto& layers = resources["materials"][0]["layers"];

    REQUIRE(resources["uv_maps"].size() == 3);
    CHECK(layers[0]["uv_map"] == layers[1]["uv_map"]);
    CHECK(layers[2]["uv_map"] != layers[0]["uv_map"]);
    CHECK(resources["uv_maps"][layers[2]["uv_map"].get<size_t>()]["scroll"] == nlohmann::json::array({0.25, 0.0}));
    CHECK(resources["uv_maps"][layers[3]["uv_map"].get<size_t>()]["set"] == "environment");
    CHECK(resources["uv_maps"][layers[0]["uv_map"].get<size_t>()]["set"] == 0);
    CHECK(bundle_schema::validateDocument((dir / "manifest.json").string()).empty());
}
