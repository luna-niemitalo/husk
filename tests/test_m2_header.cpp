// Tests for husk::m2's header module (src/m2_header.hpp/.cpp): parseTextures/parseMaterials/billboardModeName.
// Split out of the former tests/test_m2.cpp -- see FILE_SPLIT_TODO.md Item 5.

#include "test_m2_fixtures.hpp"

TEST_CASE("billboardModeName: names each of the four billboard bits, per wowdev.wiki M2#Bones") {
    CHECK(std::string(husk::m2::billboardModeName(0x8)) == "spherical");
    CHECK(std::string(husk::m2::billboardModeName(0x10)) == "cylindrical_lock_x");
    CHECK(std::string(husk::m2::billboardModeName(0x20)) == "cylindrical_lock_y");
    CHECK(std::string(husk::m2::billboardModeName(0x40)) == "cylindrical_lock_z");
}


TEST_CASE("billboardModeName: no billboard bit set returns nullptr, even with unrelated bits set") {
    CHECK(husk::m2::billboardModeName(0x0) == nullptr);
    // ignoreParentTranslate|ignoreParentScale|ignoreParentRotation (0x1|0x2|0x4) --
    // real, unrelated bone flags, not billboard bits.
    CHECK(husk::m2::billboardModeName(0x7) == nullptr);
}


TEST_CASE("billboardModeName: a bone with a billboard bit set alongside unrelated bits is still "
          "recognized") {
    CHECK(std::string(husk::m2::billboardModeName(0x1 | 0x8)) == "spherical");
}

// M2Texture (wowdev.wiki M2#Textures), 16 bytes: 0x00 type (u32), 0x04
// flags (u32), 0x08 filename (M2Array<char>).
void putTexture(std::vector<uint8_t>& buf, size_t off, uint32_t type, uint32_t flags,
                 const std::string& filename) {
    if (buf.size() < off + 0x10) buf.resize(off + 0x10, 0);
    putU32(buf, off + 0x00, type);
    putU32(buf, off + 0x04, flags);
    if (filename.empty()) {
        putArray(buf, off + 0x08, 0, 0);
        return;
    }
    size_t nameOff = buf.size();
    buf.resize(nameOff + filename.size());
    std::memcpy(buf.data() + nameOff, filename.data(), filename.size());
    putArray(buf, off + 0x08, static_cast<uint32_t>(filename.size()),
             static_cast<uint32_t>(nameOff));
}

// M2Material (wowdev.wiki M2#Render_flags_and_blending_modes), 4 bytes:
// 0x00 flags (u16), 0x02 blending_mode (u16).
void putMaterial(std::vector<uint8_t>& buf, size_t off, uint16_t flags, uint16_t blendMode) {
    if (buf.size() < off + 0x04) buf.resize(off + 0x04, 0);
    uint16_t f = flags, b = blendMode;
    std::memcpy(buf.data() + off + 0x00, &f, 2);
    std::memcpy(buf.data() + off + 0x02, &b, 2);
}


TEST_CASE("parseTextures: reads type/flags/filename for every entry") {
    size_t off = 500;
    // Both fixed-size records reserved up front -- putTexture appends
    // variable-length filename data at the *current* end of the buffer, so
    // writing record 0's name before record 1's fixed bytes exist would
    // otherwise let record 1 clobber it.
    std::vector<uint8_t> blob(off + 2 * 0x10, 0);
    putTexture(blob, off, /*type=*/0, /*flags=*/0x1, "Textures\\foo.blp");
    putTexture(blob, off + 0x10, /*type=*/6, /*flags=*/0, "");  // char hair, no embedded name

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto textures = husk::m2::parseTextures(blob, array);

    REQUIRE(textures.size() == 2);
    CHECK(textures[0].type == 0);
    CHECK(textures[0].flags == 0x1);
    CHECK(textures[0].filename == "Textures\\foo.blp");
    CHECK(textures[1].type == 6);
    CHECK(textures[1].flags == 0);
    CHECK(textures[1].filename.empty());
}


TEST_CASE("parseTextures: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 999;
    CHECK(husk::m2::parseTextures(blob, array).empty());
}


TEST_CASE("parseTextures: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(10, 0);  // 0x10 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseTextures(blob, array), husk::m2::ParseError);
}


TEST_CASE("parseMaterials: reads flags/blendMode for every entry") {
    size_t off = 300;
    std::vector<uint8_t> blob(off, 0);
    putMaterial(blob, off, /*flags=*/0x04, /*blendMode=*/0);
    putMaterial(blob, off + 0x04, /*flags=*/0x11, /*blendMode=*/4);

    husk::m2::Array array;
    array.count = 2;
    array.offset = static_cast<uint32_t>(off);
    auto materials = husk::m2::parseMaterials(blob, array);

    REQUIRE(materials.size() == 2);
    CHECK(materials[0].flags == 0x04);
    CHECK(materials[0].blendMode == 0);
    CHECK(materials[1].flags == 0x11);
    CHECK(materials[1].blendMode == 4);
}


TEST_CASE("parseMaterials: empty array returns an empty vector without touching the blob") {
    std::vector<uint8_t> blob;
    husk::m2::Array array;
    array.count = 0;
    array.offset = 777;
    CHECK(husk::m2::parseMaterials(blob, array).empty());
}


TEST_CASE("parseMaterials: array running past the end of the blob throws") {
    std::vector<uint8_t> blob(2, 0);  // 4 bytes needed for one entry
    husk::m2::Array array;
    array.count = 1;
    array.offset = 0;
    CHECK_THROWS_AS(husk::m2::parseMaterials(blob, array), husk::m2::ParseError);
}


