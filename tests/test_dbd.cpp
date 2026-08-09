// Tests for the WoWDBDefs .dbd text-format parser (src/dbd.hpp/.cpp).
// Synthetic-text tests exercise the grammar unconditionally; the real-data
// test resolves husk::test::testDbdDir() (reference/WoWDBDefs, gitignored,
// dev-only) and skips cleanly if that checkout isn't present -- same
// discipline every other real-fixture test in this suite uses.

#include <doctest/doctest.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "../src/db2.hpp"
#include "../src/dbd.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

// A synthetic field_storage_info vector for the tests below that only care
// about name resolution, not the shape cross-check -- field_compression_none
// with fieldSizeBits == bits, matching every synthetic layout's own plain
// "<32>"-declared (unpacked, non-array) fields exactly, so it never trips
// resolveFieldNames' new per-field shape check.
std::vector<db2::FieldStorageInfo> noneFields(size_t count, uint16_t bits = 32) {
    std::vector<db2::FieldStorageInfo> info(count);
    for (db2::FieldStorageInfo& f : info) {
        f.storageType = db2::FieldCompression::None;
        f.fieldSizeBits = bits;
    }
    return info;
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("dbd::parseDbd: COLUMNS block resolves name + type, '?' suffix stripped") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID?\n"
        "float Chance // a comment\n"
        "string Name\n"
        "locstring Description\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.columns.size() == 5);
    CHECK(table.columns[0].name == "ID");
    CHECK(table.columns[0].type == dbd::ColumnType::Int);
    CHECK_FALSE(table.columns[0].relation.has_value());
    CHECK(table.columns[1].name == "CharComponentTextureLayoutsID");
    CHECK(table.columns[1].type == dbd::ColumnType::Int);
    REQUIRE(table.columns[1].relation.has_value());
    CHECK(table.columns[1].relation->targetTable == "CharComponentTextureLayouts");
    CHECK(table.columns[1].relation->targetColumn == "ID");
    CHECK(table.columns[2].name == "Chance");
    CHECK(table.columns[2].type == dbd::ColumnType::Float);
    CHECK(table.columns[3].name == "Name");
    CHECK(table.columns[3].type == dbd::ColumnType::String);
    CHECK(table.columns[4].name == "Description");
    CHECK(table.columns[4].type == dbd::ColumnType::LocString);
}

TEST_CASE("dbd::parseDbd: resolveFieldNames carries the relation target through to the resolved Column") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID?\n"
        "\n"
        "LAYOUT AABBCCDD\n"
        "BUILD 1.0.0.1\n"
        "$noninline,id$ID<32>\n"
        "CharComponentTextureLayoutsID<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];
    auto resolved = dbd::resolveFieldNames(table, layout, noneFields(1));
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 1);
    CHECK((*resolved)[0].name == "CharComponentTextureLayoutsID");
    REQUIRE((*resolved)[0].relation.has_value());
    CHECK((*resolved)[0].relation->targetTable == "CharComponentTextureLayouts");
    CHECK((*resolved)[0].relation->targetColumn == "ID");
}

TEST_CASE("dbd::parseDbd: one LAYOUT block with a single hash, all fields inline") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int Width\n"
        "int Height\n"
        "\n"
        "LAYOUT D4ED338C\n"
        "BUILD 9.0.1.34365, 9.0.1.34392\n"
        "$id$ID<32>\n"
        "Width<32>\n"
        "Height<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.layouts.size() == 1);
    const dbd::Layout& layout = table.layouts[0];
    REQUIRE(layout.hashes.size() == 1);
    CHECK(layout.hashes[0] == 0xD4ED338C);
    REQUIRE(layout.fields.size() == 3);
    CHECK(layout.fields[0].name == "ID");
    CHECK_FALSE(layout.fields[0].nonInline);
    CHECK(layout.fields[1].name == "Width");
    CHECK(layout.fields[2].name == "Height");
}

TEST_CASE("dbd::parseDbd: a LAYOUT line listing multiple hashes shares one field list") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int Value\n"
        "\n"
        "LAYOUT AAAAAAAA, BBBBBBBB, CCCCCCCC\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "Value<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.layouts.size() == 1);
    REQUIRE(table.layouts[0].hashes.size() == 3);
    CHECK(table.layouts[0].hashes[0] == 0xAAAAAAAA);
    CHECK(table.layouts[0].hashes[1] == 0xBBBBBBBB);
    CHECK(table.layouts[0].hashes[2] == 0xCCCCCCCC);
}

TEST_CASE("dbd::parseDbd: multiple LAYOUT blocks parse independently, separated by a blank line") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "int B\n"
        "\n"
        "LAYOUT 11111111\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n"
        "\n"
        "LAYOUT 22222222\n"
        "BUILD 2.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n"
        "B<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.layouts.size() == 2);
    CHECK(table.layouts[0].hashes[0] == 0x11111111);
    CHECK(table.layouts[0].fields.size() == 2);
    CHECK(table.layouts[1].hashes[0] == 0x22222222);
    CHECK(table.layouts[1].fields.size() == 3);
}

TEST_CASE("dbd::parseDbd: 'noninline' annotation is recorded and excluded by resolveFieldNames") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int Width\n"
        "\n"
        "LAYOUT AABBCCDD\n"
        "BUILD 1.0.0.1\n"
        "$noninline,id$ID<32>\n"
        "Width<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.layouts.size() == 1);
    const dbd::Layout& layout = table.layouts[0];
    REQUIRE(layout.fields.size() == 2);
    CHECK(layout.fields[0].name == "ID");
    CHECK(layout.fields[0].nonInline);
    CHECK_FALSE(layout.fields[1].nonInline);

    // The real WDC5 field array only has 1 inline slot (Width) -- ID is
    // stored out-of-band, so resolveFieldNames against a 1-entry
    // field_storage_info must find exactly Width, skipping the noninline ID
    // entirely.
    auto resolved = dbd::resolveFieldNames(table, layout, noneFields(1));
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 1);
    CHECK((*resolved)[0].name == "Width");
}

TEST_CASE("dbd::resolveFieldNames: a real field-count mismatch returns nullopt, never a partial/guessed result") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "int B\n"
        "\n"
        "LAYOUT DEADBEEF\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n"
        "B<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];
    CHECK_FALSE(dbd::resolveFieldNames(table, layout, noneFields(2)).has_value());
    CHECK_FALSE(dbd::resolveFieldNames(table, layout, noneFields(99)).has_value());
    CHECK(dbd::resolveFieldNames(table, layout, noneFields(3)).has_value());
}

TEST_CASE(
    "dbd::resolveFieldNames: a field_storage_info shape that disagrees with the .dbd's own declared "
    "size fails closed, even though the field count matches") {
    // Same shape as the field-count test above (3 inline int<32> fields),
    // but this time the count matches and the *shape* doesn't -- field 'A'
    // is declared <32> in the .dbd, real field_storage_info says 16. This
    // is exactly the class of bug the count-only check couldn't catch: a
    // coincidentally-right field count with the wrong per-field shape (e.g.
    // matched against the wrong layout hash).
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "int B\n"
        "\n"
        "LAYOUT DEADBEEF\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n"
        "B<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];

    std::vector<db2::FieldStorageInfo> mismatched = noneFields(3);
    mismatched[1].fieldSizeBits = 16;  // real file disagrees with the .dbd's declared A<32>
    CHECK_FALSE(dbd::resolveFieldNames(table, layout, mismatched).has_value());

    std::vector<db2::FieldStorageInfo> agreeing = noneFields(3);
    CHECK(dbd::resolveFieldNames(table, layout, agreeing).has_value());
}

TEST_CASE(
    "dbd::resolveFieldNames: bitpacked storage may use fewer bits than the .dbd's declared size, "
    "never more") {
    // Real files (chrmodelmaterial.db2, this session's own verification --
    // see the real-data test further below) routinely bitpack a <32>-declared
    // int down to a handful of bits. That's real compression, not a
    // mismatch -- only a bitpacked width *larger* than the declared type
    // can't be a legitimate compression of it.
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "\n"
        "LAYOUT AABBCCDD\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];

    std::vector<db2::FieldStorageInfo> smaller(2);
    smaller[0].storageType = db2::FieldCompression::Bitpacked;
    smaller[0].fieldSizeBits = 10;
    smaller[1].storageType = db2::FieldCompression::BitpackedSigned;
    smaller[1].fieldSizeBits = 6;
    CHECK(dbd::resolveFieldNames(table, layout, smaller).has_value());

    std::vector<db2::FieldStorageInfo> larger = smaller;
    larger[1].fieldSizeBits = 40;  // can't be a compression of a 32-bit value
    CHECK_FALSE(dbd::resolveFieldNames(table, layout, larger).has_value());
}

TEST_CASE(
    "dbd::resolveFieldNames: bitpacked_indexed_array's array_count is checked exactly against the "
    "declared [Length]") {
    // Real chrmodeltexturelayer.db2 (layout D0583FB4) has exactly this
    // shape: a [3]-declared field decoding as bitpacked_indexed_array with
    // array_count 3 (this session's own real-data verification).
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "\n"
        "LAYOUT AABBCCDD\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>[3]\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];

    // Two inline fields (ID, A) -- field_storage_info needs an entry for
    // each, ID's own included, even though only A's shape is under test.
    std::vector<db2::FieldStorageInfo> agreeing = noneFields(2);
    agreeing[1].storageType = db2::FieldCompression::BitpackedIndexedArray;
    agreeing[1].arrayCount = 3;
    CHECK(dbd::resolveFieldNames(table, layout, agreeing).has_value());

    std::vector<db2::FieldStorageInfo> disagreeing = agreeing;
    disagreeing[1].arrayCount = 2;
    CHECK_FALSE(dbd::resolveFieldNames(table, layout, disagreeing).has_value());
}

TEST_CASE(
    "dbd::resolveFieldNames: common_data/bitpacked_indexed carry no comparable fact, never flagged as "
    "a mismatch") {
    // Both storage types' field_size_bits describes something other than
    // the field's own logical value width (0 for CommonData -- no inline
    // storage at all; an index's own width for BitpackedIndexed) -- neither
    // is checkable against a .dbd-declared type/size, so an extreme,
    // deliberately-wrong-looking value here must NOT fail the match.
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int A\n"
        "int B\n"
        "\n"
        "LAYOUT AABBCCDD\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "A<32>\n"
        "B<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    const dbd::Layout& layout = table.layouts[0];

    // Three inline fields (ID, A, B) -- field_storage_info needs an entry
    // for each, ID's own included, even though only A/B's shapes are under
    // test.
    std::vector<db2::FieldStorageInfo> info = noneFields(3);
    info[1].storageType = db2::FieldCompression::CommonData;
    info[1].fieldSizeBits = 0;
    info[2].storageType = db2::FieldCompression::BitpackedIndexed;
    info[2].fieldSizeBits = 3;  // an index width, unrelated to B's own <32>
    CHECK(dbd::resolveFieldNames(table, layout, info).has_value());
}

TEST_CASE("dbd::findLayout: matches by any hash in a multi-hash LAYOUT line, nullptr on no match") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "\n"
        "LAYOUT 12345678, 87654321\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    CHECK(dbd::findLayout(table, 0x12345678) != nullptr);
    CHECK(dbd::findLayout(table, 0x87654321) != nullptr);
    CHECK(dbd::findLayout(table, 0xFFFFFFFF) == nullptr);
}

TEST_CASE(
    "dbd: real reference/WoWDBDefs checkout resolves ChrModelMaterial's real column names for the "
    "real chrmodelmaterial.db2 file's own layout_hash" *
    doctest::skip(husk::test::testDbdDir().empty() ||
                   !std::filesystem::exists("/media/luna/data/wow_export/dbfilesclient/chrmodelmaterial.db2"))) {
    // Real values from `husk db2-info /media/luna/data/wow_export/dbfilesclient/chrmodelmaterial.db2`
    // at test-writing time: table_hash=0xfa82a022, layout_hash=0x22469480, 7 fields.
    std::optional<dbd::Table> table = dbd::loadTableForHash(husk::test::testDbdDir(), 0xfa82a022);
    REQUIRE(table.has_value());
    CHECK(table->tableName == "ChrModelMaterial");

    const dbd::Layout* layout = dbd::findLayout(*table, 0x22469480);
    REQUIRE(layout != nullptr);

    // Real field_storage_info, not a bare count, now that resolveFieldNames
    // cross-validates per-field shape too -- exercises that check against
    // real bitpacked/bitpacked_indexed_array storage, not just the
    // synthetic None-only cases above.
    db2::File file =
        db2::parse(readFileBytes("/media/luna/data/wow_export/dbfilesclient/chrmodelmaterial.db2"));
    auto resolved = dbd::resolveFieldNames(*table, *layout, file.fieldStorageInfo);
    REQUIRE(resolved.has_value());
    REQUIRE(resolved->size() == 7);
    CHECK((*resolved)[0].name == "ID");
    CHECK((*resolved)[1].name == "CharComponentTextureLayoutsID");
    REQUIRE((*resolved)[1].relation.has_value());
    CHECK((*resolved)[1].relation->targetTable == "CharComponentTextureLayouts");
    CHECK((*resolved)[1].relation->targetColumn == "ID");
    CHECK((*resolved)[2].name == "TextureType");
    CHECK((*resolved)[3].name == "Width");
    CHECK((*resolved)[4].name == "Height");
    CHECK((*resolved)[5].name == "Flags");
    CHECK((*resolved)[6].name == "Field_9_0_1_34615_006");
}

TEST_CASE("dbd::findIdFieldName: finds a $noninline,id$ field, ignores an inline $id$ field") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int Value\n"
        "\n"
        "LAYOUT AAAAAAAA\n"
        "BUILD 1.0.0.1\n"
        "$noninline,id$ID<32>\n"
        "Value<32>\n"
        "\n"
        "LAYOUT BBBBBBBB\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "Value<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    REQUIRE(table.layouts.size() == 2);

    // Non-inline id: has a name, since resolveFieldNames' own by-position
    // scan can never surface it (no field-array slot).
    auto nonInlineName = dbd::findIdFieldName(table.layouts[0]);
    REQUIRE(nonInlineName.has_value());
    CHECK(*nonInlineName == "ID");

    // Inline id: findIdFieldName deliberately returns nullopt here --
    // resolveFieldNames' normal by-position result already covers it, so a
    // second, separate "ID" column would just be a duplicate.
    CHECK_FALSE(dbd::findIdFieldName(table.layouts[1]).has_value());
}

TEST_CASE("dbd::findIdFieldName: a layout with no id annotation at all returns nullopt") {
    std::string text =
        "COLUMNS\n"
        "int Value\n"
        "\n"
        "LAYOUT CCCCCCCC\n"
        "BUILD 1.0.0.1\n"
        "Value<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    CHECK_FALSE(dbd::findIdFieldName(table.layouts[0]).has_value());
}

TEST_CASE("dbd::findNonInlineNonIdFieldNames: finds a $noninline,relation$ field, excludes the id field") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
        "int Value\n"
        "\n"
        "LAYOUT AAAAAAAA\n"
        "BUILD 1.0.0.1\n"
        "$noninline,id$ID<32>\n"
        "$noninline,relation$CharComponentTextureLayoutsID<32>\n"
        "Value<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    auto names = dbd::findNonInlineNonIdFieldNames(table.layouts[0]);
    REQUIRE(names.size() == 1);
    CHECK(names[0] == "CharComponentTextureLayoutsID");
}

TEST_CASE("dbd::findNonInlineNonIdFieldNames: an inline $relation$ field (no noninline tag) is not returned") {
    std::string text =
        "COLUMNS\n"
        "int ID\n"
        "int<CharComponentTextureLayouts::ID> CharComponentTextureLayoutsID\n"
        "\n"
        "LAYOUT BBBBBBBB\n"
        "BUILD 1.0.0.1\n"
        "$id$ID<32>\n"
        "$relation$CharComponentTextureLayoutsID<32>\n";
    dbd::Table table = dbd::parseDbd(text, "Test");
    CHECK(dbd::findNonInlineNonIdFieldNames(table.layouts[0]).empty());
}

TEST_CASE(
    "dbd: an unknown tableHash returns nullopt, not a throw" *
    doctest::skip(husk::test::testDbdDir().empty())) {
    CHECK_FALSE(dbd::loadTableForHash(husk::test::testDbdDir(), 0xFFFFFFFF).has_value());
}
