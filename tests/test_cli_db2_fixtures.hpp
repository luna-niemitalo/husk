// Synthetic DB2 tables (WDC5) plus the WoWDBDefs checkout naming their
// columns, for the `husk export --db2-dir/--dbd-dir` CLI tests. Every
// column is one inline 32-bit field, so one writer covers int and string
// columns alike. Anonymous namespace: each including TU gets its own copy.
#pragma once

#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace db2fx {

namespace fs = std::filesystem;

struct Column {
    std::string name;
    bool text = false;
};

struct Cell {
    uint32_t number = 0;
    std::optional<std::string> text;
    Cell(int v) : number(static_cast<uint32_t>(v)) {}
    Cell(uint32_t v) : number(v) {}
    Cell(const char* s) : text(s) {}
};

using Row = std::vector<Cell>;

uint32_t f32(float v) { return std::bit_cast<uint32_t>(v); }

void put16(std::vector<uint8_t>& buf, size_t offset, uint16_t v) { std::memcpy(buf.data() + offset, &v, 2); }
void put32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) { std::memcpy(buf.data() + offset, &v, 4); }

constexpr size_t kHeaderSize = 204;
constexpr size_t kSectionHeaderSize = 40;
constexpr size_t kFieldStructureSize = 4;
constexpr size_t kFieldStorageInfoSize = 24;

// One section, no offset map, the first column the record id. A string
// cell holds its string's offset from the cell itself (WDC5 inline
// strings); the string table opens with an empty string.
std::vector<uint8_t> buildTable(uint32_t tableHash, uint32_t layoutHash, const std::vector<Column>& columns,
                                const std::vector<Row>& rows) {
    auto fieldCount = static_cast<uint32_t>(columns.size());
    size_t recordSize = fieldCount * 4;
    size_t recordsAt = kHeaderSize + kSectionHeaderSize + fieldCount * (kFieldStructureSize + kFieldStorageInfoSize);
    size_t stringsAt = recordsAt + rows.size() * recordSize;

    std::string strings(1, '\0');
    std::vector<uint8_t> records(rows.size() * recordSize, 0);
    for (size_t r = 0; r < rows.size(); ++r) {
        REQUIRE(rows[r].size() == columns.size());
        for (size_t c = 0; c < columns.size(); ++c) {
            const Cell& cell = rows[r][c];
            REQUIRE(cell.text.has_value() == columns[c].text);
            size_t cellAt = r * recordSize + c * 4;
            uint32_t value = cell.number;
            if (cell.text) {
                value = static_cast<uint32_t>(stringsAt + strings.size() - (recordsAt + cellAt));
                strings += *cell.text;
                strings += '\0';
            }
            put32(records, cellAt, value);
        }
    }

    std::vector<uint8_t> buf(stringsAt + strings.size(), 0);
    std::memcpy(buf.data(), "WDC5", 4);
    put32(buf, 4, 5);
    const uint32_t header[] = {static_cast<uint32_t>(rows.size()), fieldCount, static_cast<uint32_t>(recordSize),
                               static_cast<uint32_t>(strings.size()), tableHash, layoutHash,
                               1, static_cast<uint32_t>(rows.size()), 0};
    size_t p = 8 + 128;
    for (uint32_t v : header) { put32(buf, p, v); p += 4; }
    put16(buf, p, 0); p += 2;  // flags
    put16(buf, p, 0); p += 2;  // id index
    const uint32_t tail[] = {fieldCount, 0, 0, fieldCount * static_cast<uint32_t>(kFieldStorageInfoSize), 0, 0, 1};
    for (uint32_t v : tail) { put32(buf, p, v); p += 4; }
    REQUIRE(p == kHeaderSize);

    p += 8;  // tact key hash
    const uint32_t section[] = {static_cast<uint32_t>(recordsAt), static_cast<uint32_t>(rows.size()),
                                static_cast<uint32_t>(strings.size()), 0, 0, 0, 0, 0};
    for (uint32_t v : section) { put32(buf, p, v); p += 4; }
    REQUIRE(p == kHeaderSize + kSectionHeaderSize);

    for (uint32_t i = 0; i < fieldCount; ++i) {
        put16(buf, p, 0);
        put16(buf, p + 2, static_cast<uint16_t>(i * 4));
        p += kFieldStructureSize;
    }
    for (uint32_t i = 0; i < fieldCount; ++i) {
        put16(buf, p, static_cast<uint16_t>(i * 32));
        put16(buf, p + 2, 32);
        p += kFieldStorageInfoSize;
    }
    REQUIRE(p == recordsAt);
    std::memcpy(buf.data() + recordsAt, records.data(), records.size());
    std::memcpy(buf.data() + stringsAt, strings.data(), strings.size());
    return buf;
}

std::string lowercase(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// A --db2-dir/--dbd-dir pair: each table() writes `<db2Dir>/<table>.db2`
// (lowercase, as casc-tool names them) and its definition, and rewrites
// manifest.json to list every table so far.
class Tables {
public:
    Tables(fs::path db2Dir, fs::path dbdDir) : db2Dir_(std::move(db2Dir)), dbdDir_(std::move(dbdDir)) {
        fs::create_directories(db2Dir_);
        fs::create_directories(dbdDir_ / "definitions");
    }

    void table(const std::string& name, const std::vector<Column>& columns, const std::vector<Row>& rows) {
        uint32_t tableHash = 0x7a000001u + static_cast<uint32_t>(manifest_.size());
        uint32_t layoutHash = 0x7b000001u + static_cast<uint32_t>(manifest_.size());

        std::ostringstream dbd;
        dbd << "COLUMNS\n";
        for (const Column& c : columns) dbd << (c.text ? "locstring " : "int ") << c.name << "\n";
        dbd << "\nLAYOUT " << std::hex << layoutHash << std::dec << "\nBUILD 1.0.0.1\n";
        for (size_t i = 0; i < columns.size(); ++i) dbd << (i == 0 ? "$id$" : "") << columns[i].name << "<32>\n";
        std::ofstream(dbdDir_ / "definitions" / (name + ".dbd")) << dbd.str();

        std::vector<uint8_t> bytes = buildTable(tableHash, layoutHash, columns, rows);
        std::ofstream out(db2Dir_ / (lowercase(name) + ".db2"), std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

        std::ostringstream entry;
        entry << "  {\"tableName\": \"" << name << "\", \"tableHash\": \"" << std::hex << tableHash << "\"}";
        manifest_.push_back(entry.str());
        std::ofstream manifest(dbdDir_ / "manifest.json");
        manifest << "[\n";
        for (size_t i = 0; i < manifest_.size(); ++i) manifest << manifest_[i] << (i + 1 < manifest_.size() ? ",\n" : "\n");
        manifest << "]\n";
    }

    std::string flags() const { return " --db2-dir " + db2Dir_.string() + " --dbd-dir " + dbdDir_.string(); }

private:
    fs::path db2Dir_;
    fs::path dbdDir_;
    std::vector<std::string> manifest_;
};

// Rows for every table `husk export` reads for creature options and
// character customization (src/creature_display_db2.cpp,
// chrcustomization_db2.cpp, chrmodel_db2.cpp, chrrace_db2.cpp). Column
// order is the order each comment lists; a table left empty is written
// with no rows.
struct CharacterTables {
    std::vector<Row> creatureModelData;    // ID, FileDataID, ModelScale (f32), CreatureGeosetDataID
    std::vector<Row> creatureDisplayInfo;  // ID, ModelID, CreatureModelScale (f32), CreatureModelAlpha,
                                           // ParticleColorID, ExtendedDisplayInfoID, TextureVariationFileDataID
    std::vector<Row> displayGeosets;       // CreatureDisplayInfoID, GeosetIndex, GeosetValue
    std::vector<Row> chrModel;             // ID, DisplayID, CharComponentTextureLayoutID
    std::vector<Row> chrRaces;             // ID, ClientFileString
    std::vector<Row> chrRaceXChrModel;     // ID, ChrRacesID, Sex, ChrModelID
    std::vector<Row> options;              // ID, ChrModelID, OrderIndex, ChrCustomizationCategoryID, Name_lang
    std::vector<Row> choices;              // ID, ChrCustomizationOptionID, OrderIndex, Name_lang
    std::vector<Row> categories;           // ID, OrderIndex, CategoryName_lang
    std::vector<Row> elements;             // ID, ChrCustomizationChoiceID, ChrCustomizationGeosetID,
                                           // ChrCustomizationBoneSetID, ChrCustomizationMaterialID,
                                           // RelatedChrCustomizationChoiceID
    std::vector<Row> geosets;              // ID, GeosetType, GeosetID
    std::vector<Row> boneSets;             // ID, BoneFileDataID, ModelFileDataID
    std::vector<Row> materials;            // ID, ChrModelTextureTargetID, MaterialResourcesID
    std::vector<Row> textureFileData;      // FileDataID, MaterialResourcesID, UsageType
    std::vector<Row> chrModelMaterial;     // ID, CharComponentTextureLayoutsID, TextureType, Width, Height, Flags
    std::vector<Row> layouts;              // ID, Width, Height
    std::vector<Row> sections;             // ID, CharComponentTextureLayoutID, SectionType, X, Y, Width, Height,
                                           // OverlapSectionMask
    std::vector<Row> layers;               // ID, CharComponentTextureLayoutsID, TextureType, Layer, Flags,
                                           // BlendMode, TextureSectionTypeBitMask, ChrModelTextureTargetID
};

std::vector<Column> ints(std::initializer_list<const char*> names) {
    std::vector<Column> out;
    for (const char* n : names) out.push_back({n, false});
    return out;
}

void writeCharacterTables(Tables& t, CharacterTables rows) {
    // texturefiledata::load, chrcustomization::load and chrmodel::load each
    // treat an all-empty table set as absent, and creaturedisplay::load
    // needs all three, so each gets one row nothing refers to.
    if (rows.textureFileData.empty()) rows.textureFileData.push_back({1, 1, 0});
    if (rows.options.empty() && rows.choices.empty() && rows.categories.empty() && rows.elements.empty() &&
        rows.geosets.empty() && rows.boneSets.empty() && rows.materials.empty()) {
        rows.geosets.push_back({0xFFFF, 0, 0});
    }
    if (rows.chrModelMaterial.empty() && rows.layouts.empty() && rows.sections.empty() && rows.layers.empty()) {
        rows.layouts.push_back({0xFFFF, 0, 0});
    }
    t.table("CreatureModelData", ints({"ID", "FileDataID", "ModelScale", "CreatureGeosetDataID"}), rows.creatureModelData);
    t.table("CreatureDisplayInfo",
            ints({"ID", "ModelID", "CreatureModelScale", "CreatureModelAlpha", "ParticleColorID", "ExtendedDisplayInfoID",
                  "TextureVariationFileDataID"}),
            rows.creatureDisplayInfo);
    t.table("CreatureDisplayInfoGeosetData", ints({"CreatureDisplayInfoID", "GeosetIndex", "GeosetValue"}),
            rows.displayGeosets);
    t.table("ParticleColor", ints({"ID", "Start", "MID", "End"}), {});
    t.table("CreatureDisplayInfoExtra", ints({"ID", "BakeMaterialResourcesID", "HDBakeMaterialResourcesID"}), {});
    t.table("CreatureDisplayInfoOption",
            ints({"ID", "CreatureDisplayInfoExtraID", "ChrCustomizationOptionID", "ChrCustomizationChoiceID"}), {});
    t.table("NPCModelItemSlotDisplayInfo", ints({"ID", "NpcModelID", "ItemDisplayInfoID", "ItemSlot"}), {});
    t.table("Creature", {{"ID"}, {"DisplayID"}, {"Name_lang", true}, {"Title_lang", true}}, {});
    t.table("ChrModel", ints({"ID", "DisplayID", "CharComponentTextureLayoutID"}), rows.chrModel);
    t.table("ChrRaces", {{"ID"}, {"ClientFileString", true}}, rows.chrRaces);
    t.table("ChrRaceXChrModel", ints({"ID", "ChrRacesID", "Sex", "ChrModelID"}), rows.chrRaceXChrModel);
    t.table("ChrCustomizationOption",
            {{"ID"}, {"ChrModelID"}, {"OrderIndex"}, {"ChrCustomizationCategoryID"}, {"Name_lang", true}}, rows.options);
    t.table("ChrCustomizationChoice", {{"ID"}, {"ChrCustomizationOptionID"}, {"OrderIndex"}, {"Name_lang", true}},
            rows.choices);
    t.table("ChrCustomizationCategory", {{"ID"}, {"OrderIndex"}, {"CategoryName_lang", true}}, rows.categories);
    t.table("ChrCustomizationElement",
            ints({"ID", "ChrCustomizationChoiceID", "ChrCustomizationGeosetID", "ChrCustomizationBoneSetID",
                  "ChrCustomizationMaterialID", "RelatedChrCustomizationChoiceID"}),
            rows.elements);
    t.table("ChrCustomizationGeoset", ints({"ID", "GeosetType", "GeosetID"}), rows.geosets);
    t.table("ChrCustomizationBoneSet", ints({"ID", "BoneFileDataID", "ModelFileDataID"}), rows.boneSets);
    t.table("ChrCustomizationMaterial", ints({"ID", "ChrModelTextureTargetID", "MaterialResourcesID"}), rows.materials);
    t.table("TextureFileData", ints({"FileDataID", "MaterialResourcesID", "UsageType"}), rows.textureFileData);
    t.table("ChrModelMaterial", ints({"ID", "CharComponentTextureLayoutsID", "TextureType", "Width", "Height", "Flags"}),
            rows.chrModelMaterial);
    t.table("CharComponentTextureLayouts", ints({"ID", "Width", "Height"}), rows.layouts);
    t.table("CharComponentTextureSections",
            ints({"ID", "CharComponentTextureLayoutID", "SectionType", "X", "Y", "Width", "Height", "OverlapSectionMask"}),
            rows.sections);
    t.table("ChrModelTextureLayer",
            ints({"ID", "CharComponentTextureLayoutsID", "TextureType", "Layer", "Flags", "BlendMode",
                  "TextureSectionTypeBitMask", "ChrModelTextureTargetID"}),
            rows.layers);
}

// `modelFileDataId`'s model drawn by one creature display, the shape the
// client gives a character model: CreatureModelData -> CreatureDisplayInfo
// -> ChrModel.DisplayID.
void drawModelAs(CharacterTables& rows, uint32_t modelFileDataId, uint32_t chrModelId, uint32_t layoutId) {
    uint32_t modelData = 5000 + chrModelId;
    uint32_t display = 6000 + chrModelId;
    rows.creatureModelData.push_back({modelData, modelFileDataId, f32(1.0f), 0});
    rows.creatureDisplayInfo.push_back({display, modelData, f32(1.0f), 255, 0, 0, 0});
    rows.chrModel.push_back({chrModelId, display, layoutId});
}

}  // namespace db2fx

}  // namespace
