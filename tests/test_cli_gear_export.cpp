// CLI tier: `husk export --appearance` -- exercises the real compiled
// binary (see run_husk.hpp) against a small synthetic M2/.skel fixture plus
// a synthetic WoWDBDefs/.db2 fixture set (same shapes tests/
// test_cli_appearance.cpp's own gear-resolution tests already use for
// `husk appearance-string`), verifying the real `gear_items` (case 1,
// standalone-geometry items)/`gear_section_overlays` (case 2, object-skin
// section overlay) glTF extras land in the actual `husk export` output
// .glb -- TODO/EQUIPPED_GEAR_RENDER_TODO.md. `src/itemappearance_db2.hpp`'s
// own DB2-chain-resolution correctness is tests/test_cli_appearance.cpp's
// job, not re-tested here -- this file is specifically about
// `attachGearAppearance`'s (`src/export_extras.cpp`) wiring into `husk
// export`'s own skin extras, and the `--appearance`/
// `--customization-choice-ids` flag-conflict check.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tiny_gltf.h>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

namespace {

// Same buildFlatDb2/writeTextFile/writeItemAppearanceDbd shape as
// tests/test_cli_appearance.cpp's own gear-resolution fixture -- kept as
// its own local copy per this project's own per-file-fixture convention
// (that file's own comment documents why: the resolution logic under test
// only cares about column *names* resolving correctly, not sharing a real
// WDC5 storage layout builder across files).
void putU16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void putU32Db2(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }
void putU64(std::vector<uint8_t>& buf, size_t offset, uint64_t v) { std::memcpy(buf.data() + offset, &v, 8); }

constexpr size_t kHeaderSize = 204;
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

std::vector<uint8_t> buildFlatDb2(uint32_t tableHash, uint32_t layoutHash,
                                   const std::vector<std::vector<uint32_t>>& rows) {
    uint32_t fieldCount = static_cast<uint32_t>(rows.empty() ? 0 : rows[0].size());
    size_t recordSize = fieldCount * 4;
    size_t stringTableSize = 1;
    size_t sectionFileOffset =
        kHeaderSize + kSectionHeaderSize + fieldCount * kFieldStructureSize + fieldCount * kFieldStorageInfoSize;
    size_t total = sectionFileOffset + rows.size() * recordSize + stringTableSize;

    std::vector<uint8_t> buf(total, 0);
    std::memcpy(buf.data(), "WDC5", 4);
    putU32Db2(buf, 4, 5);

    size_t p = 8 + 128;
    putU32Db2(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32Db2(buf, p, fieldCount); p += 4;
    putU32Db2(buf, p, static_cast<uint32_t>(recordSize)); p += 4;
    putU32Db2(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32Db2(buf, p, tableHash); p += 4;
    putU32Db2(buf, p, layoutHash); p += 4;
    putU32Db2(buf, p, 1); p += 4;
    putU32Db2(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU16(buf, p, 0); p += 2;
    putU16(buf, p, 0); p += 2;
    putU32Db2(buf, p, fieldCount); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, fieldCount * kFieldStorageInfoSize); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 1); p += 4;
    REQUIRE(p == kHeaderSize);

    putU64(buf, p, 0); p += 8;
    putU32Db2(buf, p, static_cast<uint32_t>(sectionFileOffset)); p += 4;
    putU32Db2(buf, p, static_cast<uint32_t>(rows.size())); p += 4;
    putU32Db2(buf, p, static_cast<uint32_t>(stringTableSize)); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    putU32Db2(buf, p, 0); p += 4;
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

    for (uint32_t i = 0; i < fieldCount; ++i) { putU16(buf, p, 0); p += 2; putU16(buf, p, static_cast<uint16_t>(i * 4)); p += 2; }
    for (uint32_t i = 0; i < fieldCount; ++i) {
        putU16(buf, p, static_cast<uint16_t>(i * 32)); p += 2;
        putU16(buf, p, 32); p += 2;
        putU32Db2(buf, p, 0); p += 4;
        putU32Db2(buf, p, 0); p += 4;
        putU32Db2(buf, p, 0); p += 4;
        putU32Db2(buf, p, 0); p += 4;
        putU32Db2(buf, p, 0); p += 4;
    }
    REQUIRE(p == sectionFileOffset);

    for (const auto& row : rows) {
        for (uint32_t v : row) { putU32Db2(buf, p, v); p += 4; }
    }
    p += stringTableSize;
    REQUIRE(p == total);
    return buf;
}

void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}

void writeItemAppearanceDbd(const fs::path& dbdDir) {
    fs::create_directories(dbdDir / "definitions");
    writeTextFile(dbdDir / "manifest.json",
                  "[\n"
                  "  {\"tableName\": \"ItemModifiedAppearance\", \"tableHash\": \"a0000001\"},\n"
                  "  {\"tableName\": \"ItemAppearance\", \"tableHash\": \"a0000002\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfo\", \"tableHash\": \"a0000003\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfoModelMatRes\", \"tableHash\": \"a0000004\"},\n"
                  "  {\"tableName\": \"ModelFileData\", \"tableHash\": \"a0000005\"},\n"
                  "  {\"tableName\": \"TextureFileData\", \"tableHash\": \"a0000006\"},\n"
                  "  {\"tableName\": \"ItemDisplayInfoMaterialRes\", \"tableHash\": \"a0000007\"}\n"
                  "]\n");
    writeTextFile(dbdDir / "definitions" / "ItemModifiedAppearance.dbd",
                  "COLUMNS\nint ID\nint ItemAppearanceID\n\n"
                  "LAYOUT b0000001\nBUILD 1.0.0.1\n$id$ID<32>\nItemAppearanceID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemAppearance.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\n\n"
                  "LAYOUT b0000002\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfo.dbd",
                  "COLUMNS\nint ID\nint ModelResourcesID\n\n"
                  "LAYOUT b0000003\nBUILD 1.0.0.1\n$id$ID<32>\nModelResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfoModelMatRes.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\nint MaterialResourcesID\nint TextureType\nint ModelIndex\n\n"
                  "LAYOUT b0000004\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\nMaterialResourcesID<32>\n"
                  "TextureType<32>\nModelIndex<32>\n");
    writeTextFile(dbdDir / "definitions" / "ModelFileData.dbd",
                  "COLUMNS\nint FileDataID\nint ModelResourcesID\n\n"
                  "LAYOUT b0000005\nBUILD 1.0.0.1\n$id$FileDataID<32>\nModelResourcesID<32>\n");
    writeTextFile(dbdDir / "definitions" / "TextureFileData.dbd",
                  "COLUMNS\nint FileDataID\nint MaterialResourcesID\nint UsageType\n\n"
                  "LAYOUT b0000006\nBUILD 1.0.0.1\n$id$FileDataID<32>\nMaterialResourcesID<32>\nUsageType<32>\n");
    writeTextFile(dbdDir / "definitions" / "ItemDisplayInfoMaterialRes.dbd",
                  "COLUMNS\nint ID\nint ItemDisplayInfoID\nint ComponentSection\nint MaterialResourcesID\n\n"
                  "LAYOUT b0000007\nBUILD 1.0.0.1\n$id$ID<32>\nItemDisplayInfoID<32>\nComponentSection<32>\n"
                  "MaterialResourcesID<32>\n");
}

}  // namespace

TEST_CASE("husk export: --appearance and --customization-choice-ids are mutually exclusive") {
    auto dir = defaultsDir("gearconflict");
    writeFile(dir / "gearconflict.m2", tinyValidM2());
    writeFile(dir / "gearconflict00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "gearconflict.m2").string() +
                           " --appearance \"husk-appearance/1 gear=MAINHAND:15\""
                           " --customization-choice-ids 1");
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("mutually exclusive") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a malformed husk-appearance/1 string fails cleanly, not a crash") {
    auto dir = defaultsDir("gearmalformed");
    writeFile(dir / "gearmalformed.m2", tinyValidM2());
    writeFile(dir / "gearmalformed00.skin", tinyMatchingSkin());

    auto result = runHusk("export " + (dir / "gearmalformed.m2").string() +
                           " --appearance \"not-a-real-appearance-string\"");
    CHECK(result.exitCode != 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: 'gear' entries without --db2-dir/--dbd-dir are reported and "
          "skipped, no gear extras attached, export still succeeds") {
    auto dir = defaultsDir("gearnodb2");
    writeFile(dir / "gearnodb2.m2", tinyValidM2());
    writeFile(dir / "gearnodb200.skin", tinyMatchingSkin());
    writeFile(dir / "gearnodb2.skel", boneCorrectionSkel());

    auto result = runHusk("export " + (dir / "gearnodb2.m2").string() +
                           " --appearance \"husk-appearance/1 gear=MAINHAND:15\"");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("--db2-dir/--dbd-dir are required") != std::string::npos);

    fs::path glbPath = dir / "gearnodb2.glb";
    REQUIRE(fs::exists(glbPath));
    std::ifstream glb(glbPath, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(glb)), std::istreambuf_iterator<char>());
    CHECK(bytes.find("gear_items") == std::string::npos);
    CHECK(bytes.find("gear_section_overlays") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a case-1 (standalone-geometry) gear entry resolves through "
          "the real ItemModifiedAppearance -> ItemAppearance -> ItemDisplayInfo -> "
          "ItemDisplayInfoModelMatRes chain to real model/texture FileDataIDs, attached as "
          "gear_items skin extras on the root joint node") {
    auto dir = defaultsDir("gearcase1");
    writeFile(dir / "gearcase1.m2", tinyValidM2());
    writeFile(dir / "gearcase100.skin", tinyMatchingSkin());
    writeFile(dir / "gearcase1.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);

    // Same real-shaped chain as tests/test_cli_appearance.cpp's own case-1
    // test: ItemModifiedAppearanceID 15 -> ItemAppearanceID 154 ->
    // ItemDisplayInfoID 1542 -> ModelResourcesID 160 -> real .m2 FileDataID
    // 370361 (confirmed against real local DB2 data this session --
    // creature/pygmy/pygmyshaman.m2), MaterialResourcesID 22758 -> real
    // texture FileDataID 148134.
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{15, 154}}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {{154, 1542}}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {{1542, 160}}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2",
              buildFlatDb2(0xa0000004, 0xb0000004, {{1, 1542, 22758, 2, 0}}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {{370361, 160}}));
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xa0000006, 0xb0000006, {{148134, 22758, 0}}));
    writeFile(db2Dir / "itemdisplayinfomaterialres.db2", buildFlatDb2(0xa0000007, 0xb0000007, {}));

    std::ostringstream cmd;
    cmd << "export " << (dir / "gearcase1.m2").string()
        << " --appearance \"husk-appearance/1 gear=MAINHAND:15\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 gear item entry(ies) (case 1)") != std::string::npos);

    fs::path glbPath = dir / "gearcase1.glb";
    REQUIRE(fs::exists(glbPath));
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, glbPath.string()));
    REQUIRE(!model.skins.empty());
    const auto& extras = model.nodes[model.skins[0].joints[0]].extras;
    REQUIRE(extras.Has("gear_items"));
    const auto& items = extras.Get("gear_items");
    REQUIRE(items.IsArray());
    REQUIRE(items.ArrayLen() == 1);
    const auto& item0 = items.Get(0);
    CHECK(item0.Get("slot").Get<std::string>() == "MAINHAND");
    CHECK(item0.Get("item_modified_appearance_id").Get<int>() == 15);
    const auto& modelIds = item0.Get("model_file_data_ids");
    REQUIRE(modelIds.IsArray());
    REQUIRE(modelIds.ArrayLen() == 1);
    CHECK(modelIds.Get(0).Get<int>() == 370361);
    const auto& materials = item0.Get("materials");
    REQUIRE(materials.IsArray());
    REQUIRE(materials.ArrayLen() == 1);
    CHECK(materials.Get(0).Get("texture_type").Get<int>() == 2);
    CHECK(materials.Get(0).Get("file_data_id").Get<int>() == 148134);
    CHECK_FALSE(extras.Has("gear_section_overlays"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a case-1 gear item, given --listfile/--listfile-root, gets "
          "its own real .m2 resolved and exported to a real 'aux_models/<slot>_<fdid>.glb' "
          "sibling file, with the RELATIVE path baked into gear_items' own aux_glb_path -- the "
          "Blender-side consumer needs zero listfile/husk-subprocess access of its own") {
    auto dir = defaultsDir("gearauxexport");
    writeFile(dir / "gearauxexport.m2", tinyValidM2());
    writeFile(dir / "gearauxexport00.skin", tinyMatchingSkin());
    writeFile(dir / "gearauxexport.skel", boneCorrectionSkel());

    // A second, real local .m2 the --listfile row below points at --
    // stands in for the equipped item's own model. Lives under a
    // corpus-shaped subdirectory so --listfile-root's relative-path
    // resolution is genuinely exercised, not just a same-directory
    // coincidence.
    fs::path corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "item" / "objectcomponents" / "weapon");
    writeFile(corpusRoot / "item" / "objectcomponents" / "weapon" / "fakesword.m2", tinyValidM2());
    writeFile(corpusRoot / "item" / "objectcomponents" / "weapon" / "fakesword00.skin", tinyMatchingSkin());
    writeTextFile(dir / "listfile.csv", "370361;item/objectcomponents/weapon/fakesword.m2\n");

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{15, 154}}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {{154, 1542}}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {{1542, 160}}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2", buildFlatDb2(0xa0000004, 0xb0000004, {}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {{370361, 160}}));
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xa0000006, 0xb0000006, {}));
    writeFile(db2Dir / "itemdisplayinfomaterialres.db2", buildFlatDb2(0xa0000007, 0xb0000007, {}));

    std::ostringstream cmd;
    cmd << "export " << (dir / "gearauxexport.m2").string()
        << " --appearance \"husk-appearance/1 gear=MAINHAND:15\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string()
        << " --listfile " << (dir / "listfile.csv").string()
        << " --listfile-root " << corpusRoot.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("item model exported to") != std::string::npos);

    fs::path auxGlbPath = dir / "aux_models" / "mainhand_370361.glb";
    CHECK(fs::exists(auxGlbPath));

    fs::path glbPath = dir / "gearauxexport.glb";
    REQUIRE(fs::exists(glbPath));
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, glbPath.string()));
    REQUIRE(!model.skins.empty());
    const auto& extras = model.nodes[model.skins[0].joints[0]].extras;
    const auto& item0 = extras.Get("gear_items").Get(0);
    REQUIRE(item0.Has("aux_glb_path"));
    CHECK(item0.Get("aux_glb_path").Get<std::string>() == "aux_models/mainhand_370361.glb");

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: a case-2 (object-skin section overlay) gear entry (e.g. "
          "boots spanning LEG_LOWER + FOOT) resolves real ComponentSection -> texture FileDataID "
          "pairs, attached as gear_section_overlays skin extras -- multi-section, not collapsed "
          "to one") {
    auto dir = defaultsDir("gearcase2");
    writeFile(dir / "gearcase2.m2", tinyValidM2());
    writeFile(dir / "gearcase200.skin", tinyMatchingSkin());
    writeFile(dir / "gearcase2.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);

    // Same real ItemDisplayInfoID 233 (boots) shape as
    // tests/test_cli_appearance.cpp's own case-2 test.
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {{367, 495}}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {{495, 233}}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {{233, 0}}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2", buildFlatDb2(0xa0000004, 0xb0000004, {}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {}));
    writeFile(db2Dir / "texturefiledata.db2",
              buildFlatDb2(0xa0000006, 0xb0000006, {{500001, 34187, 0}, {500002, 27801, 0}}));
    writeFile(db2Dir / "itemdisplayinfomaterialres.db2",
              buildFlatDb2(0xa0000007, 0xb0000007, {{1, 233, 6, 34187}, {2, 233, 7, 27801}}));

    std::ostringstream cmd;
    cmd << "export " << (dir / "gearcase2.m2").string()
        << " --appearance \"husk-appearance/1 gear=FEET:367\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 gear section-overlay entry(ies) (case 2)") != std::string::npos);

    fs::path glbPath = dir / "gearcase2.glb";
    REQUIRE(fs::exists(glbPath));
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, glbPath.string()));
    REQUIRE(!model.skins.empty());
    const auto& extras = model.nodes[model.skins[0].joints[0]].extras;
    REQUIRE(extras.Has("gear_section_overlays"));
    const auto& overlays = extras.Get("gear_section_overlays");
    REQUIRE(overlays.IsArray());
    REQUIRE(overlays.ArrayLen() == 1);
    const auto& overlay0 = overlays.Get(0);
    CHECK(overlay0.Get("slot").Get<std::string>() == "FEET");
    const auto& sections = overlay0.Get("sections");
    REQUIRE(sections.IsArray());
    REQUIRE(sections.ArrayLen() == 2);
    CHECK(sections.Get(0).Get("component_section").Get<int>() == 6);
    CHECK(sections.Get(0).Get("file_data_id").Get<int>() == 500001);
    CHECK(sections.Get(1).Get("component_section").Get<int>() == 7);
    CHECK(sections.Get(1).Get("file_data_id").Get<int>() == 500002);
    CHECK_FALSE(extras.Has("gear_items"));

    fs::remove_all(dir);
}

TEST_CASE("husk export --appearance: 'cust' entries feed the same customization-choice resolution "
          "--customization-choice-ids drives, via one flag") {
    auto dir = defaultsDir("gearcust");
    writeFile(dir / "gearcust.m2", tinyValidM2());
    writeFile(dir / "gearcust00.skin", tinyMatchingSkin());
    writeFile(dir / "gearcust.skel", boneCorrectionSkel());

    fs::path db2Dir = dir / "db2";
    fs::path dbdDir = dir / "dbd";
    fs::create_directories(db2Dir);
    writeItemAppearanceDbd(dbdDir);
    // No real chrcustomization*.db2 fixture here -- just confirms
    // --appearance's own 'cust' field reaches attachCustomizationChoices
    // at all (it reports "no customization-choice DB2 data resolved",
    // proving the ID list was parsed and handed off, not silently
    // dropped) without needing a second, larger fixture set duplicating
    // tests/test_cli_chrcustomization.cpp's own coverage of that chain's
    // actual resolution correctness.
    writeFile(db2Dir / "itemmodifiedappearance.db2", buildFlatDb2(0xa0000001, 0xb0000001, {}));
    writeFile(db2Dir / "itemappearance.db2", buildFlatDb2(0xa0000002, 0xb0000002, {}));
    writeFile(db2Dir / "itemdisplayinfo.db2", buildFlatDb2(0xa0000003, 0xb0000003, {}));
    writeFile(db2Dir / "itemdisplayinfomodelmatres.db2", buildFlatDb2(0xa0000004, 0xb0000004, {}));
    writeFile(db2Dir / "modelfiledata.db2", buildFlatDb2(0xa0000005, 0xb0000005, {}));
    writeFile(db2Dir / "texturefiledata.db2", buildFlatDb2(0xa0000006, 0xb0000006, {}));
    writeFile(db2Dir / "itemdisplayinfomaterialres.db2", buildFlatDb2(0xa0000007, 0xb0000007, {}));

    std::ostringstream cmd;
    cmd << "export " << (dir / "gearcust.m2").string()
        << " --appearance \"husk-appearance/1 cust=7,9\""
        << " --db2-dir " << db2Dir.string() << " --dbd-dir " << dbdDir.string();
    auto result = runHusk(cmd.str());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("no customization-choice DB2 data resolved") != std::string::npos);

    fs::remove_all(dir);
}
