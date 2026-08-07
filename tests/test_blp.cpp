// Ports blp/tests/test_decode.py's fixtures directly -- husk-blp (the
// separate Python tool this project already ships) remains the ground
// truth this decoder is checked against, not a second, independently
// derived spec transcription. DXT1/DXT3/DXT5 block bytes below are
// constructed directly from the standard, publicly documented S3TC/BC1-3
// bit layout, same as the Python fixtures' own comment explains.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/blp.hpp"

namespace {

void putU32(std::vector<uint8_t>& buf, size_t offset, uint32_t v) {
    std::memcpy(buf.data() + offset, &v, 4);
}

constexpr size_t kHeaderSize = 1172;  // 0x494, husk::blp's own fixed header + palette region

std::vector<uint8_t> buildHeader(uint8_t colorEncoding = 2, uint8_t alphaBitDepth = 8,
                                  uint8_t preferredFormat = 7, uint32_t width = 64,
                                  uint32_t height = 32,
                                  std::vector<uint32_t> mipOffsets = std::vector<uint32_t>(16, 0),
                                  std::vector<uint8_t> palette = std::vector<uint8_t>(1024, 0)) {
    std::vector<uint8_t> buf(kHeaderSize, 0);
    std::memcpy(buf.data(), "BLP2", 4);
    putU32(buf, 0x04, 1);  // version
    buf[0x08] = colorEncoding;
    buf[0x09] = alphaBitDepth;
    buf[0x0A] = preferredFormat;
    buf[0x0B] = 1;  // hasMipmaps
    putU32(buf, 0x0C, width);
    putU32(buf, 0x10, height);
    mipOffsets.resize(16, 0);  // callers pass only the leading entries they care about
    for (int i = 0; i < 16; ++i) putU32(buf, 0x14 + 4 * i, mipOffsets[i]);
    REQUIRE(palette.size() == 1024);
    std::memcpy(buf.data() + 0x94, palette.data(), 1024);
    return buf;
}

uint16_t rgb565(uint8_t r5, uint8_t g6, uint8_t b5) {
    return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
}

void putU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

// One 4x4 block, opaque, every pixel the same color: color0 == color1 so
// index 0 always resolves to color0 regardless of which of DXT1's two
// interpolation modes the decoder picks.
std::vector<uint8_t> dxt1SolidBlock(uint8_t r5, uint8_t g6, uint8_t b5) {
    std::vector<uint8_t> b;
    uint16_t c = rgb565(r5, g6, b5);
    putU16LE(b, c);
    putU16LE(b, c);
    b.insert(b.end(), {0, 0, 0, 0});
    return b;
}

std::vector<uint8_t> dxt5SolidBlock(uint8_t r5, uint8_t g6, uint8_t b5, uint8_t alpha) {
    std::vector<uint8_t> b = {alpha, 0, 0, 0, 0, 0, 0, 0};
    auto color = dxt1SolidBlock(r5, g6, b5);
    b.insert(b.end(), color.begin(), color.end());
    return b;
}

std::vector<uint8_t> dxt3SolidBlock(uint8_t r5, uint8_t g6, uint8_t b5, uint8_t alpha4) {
    uint8_t nibblePair = static_cast<uint8_t>(alpha4 | (alpha4 << 4));
    std::vector<uint8_t> b(8, nibblePair);
    auto color = dxt1SolidBlock(r5, g6, b5);
    b.insert(b.end(), color.begin(), color.end());
    return b;
}

}  // namespace

TEST_CASE("blp::decode: DXT1 solid red block") {
    auto header = buildHeader(2, 8, 0, 4, 4, {static_cast<uint32_t>(kHeaderSize)});
    auto block = dxt1SolidBlock(31, 0, 0);
    auto file = header;
    file.insert(file.end(), block.begin(), block.end());

    auto img = husk::blp::decode(file);
    REQUIRE(img.width == 4);
    REQUIRE(img.height == 4);
    CHECK(img.rgba[0] == 255);
    CHECK(img.rgba[1] == 0);
    CHECK(img.rgba[2] == 0);
    CHECK(img.rgba[3] == 255);
    size_t last = (4 * 4 - 1) * 4;
    CHECK(img.rgba[last + 0] == 255);
    CHECK(img.rgba[last + 3] == 255);
}

TEST_CASE("blp::decode: DXT5 solid blue, half alpha block") {
    auto header = buildHeader(2, 8, 7, 4, 4, {static_cast<uint32_t>(kHeaderSize)});
    auto block = dxt5SolidBlock(0, 0, 31, 128);
    auto file = header;
    file.insert(file.end(), block.begin(), block.end());

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[0] == 0);
    CHECK(img.rgba[1] == 0);
    CHECK(img.rgba[2] == 255);
    CHECK(img.rgba[3] == 128);
}

TEST_CASE("blp::decode: DXT3 solid green, explicit alpha block") {
    // preferredFormat 1 = DXT3 -- 6,759 real BLP2 files in a local corpus
    // scan carry this (see husk-blp's own README/WIKI_FINDINGS.md history).
    auto header = buildHeader(2, 8, 1, 4, 4, {static_cast<uint32_t>(kHeaderSize)});
    auto block = dxt3SolidBlock(0, 63, 0, 8);  // green, alpha nibble 8 -> 136
    auto file = header;
    file.insert(file.end(), block.begin(), block.end());

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[0] == 0);
    CHECK(img.rgba[1] == 255);
    CHECK(img.rgba[2] == 0);
    CHECK(img.rgba[3] == 136);
    size_t last = (4 * 4 - 1) * 4;
    CHECK(img.rgba[last + 1] == 255);
    CHECK(img.rgba[last + 3] == 136);
}

TEST_CASE("blp::decode: DXT unsupported preferredFormat throws") {
    auto header = buildHeader(2, 8, 5, 4, 4, {static_cast<uint32_t>(kHeaderSize)});  // RGB565
    auto file = header;
    file.insert(file.end(), 8, 0);
    CHECK_THROWS_WITH_AS(husk::blp::decode(file), doctest::Contains("DXT variant"),
                          husk::blp::ParseError);
}

TEST_CASE("blp::decode: palette, alpha depth 0 is fully opaque") {
    std::vector<uint8_t> palette(1024, 0);
    palette[5 * 4 + 0] = 0;
    palette[5 * 4 + 1] = 255;
    palette[5 * 4 + 2] = 0;  // BGRX(0,255,0) -> green
    auto header = buildHeader(1, 0, 7, 2, 2, {static_cast<uint32_t>(kHeaderSize)}, palette);
    auto file = header;
    file.insert(file.end(), {5, 5, 5, 5});

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[0] == 0);
    CHECK(img.rgba[1] == 255);
    CHECK(img.rgba[2] == 0);
    CHECK(img.rgba[3] == 255);
}

TEST_CASE("blp::decode: palette, alpha depth 8 reads explicit per-pixel alpha") {
    std::vector<uint8_t> palette(1024, 0);
    palette[9 * 4 + 0] = 0;
    palette[9 * 4 + 1] = 0;
    palette[9 * 4 + 2] = 255;  // BGRX(0,0,255) -> RGB red
    auto header = buildHeader(1, 8, 7, 2, 1, {static_cast<uint32_t>(kHeaderSize)}, palette);
    auto file = header;
    file.insert(file.end(), {9, 9});     // indices
    file.insert(file.end(), {10, 200});  // alpha

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[3] == 10);
    CHECK(img.rgba[7] == 200);
    CHECK(img.rgba[0] == 255);
    CHECK(img.rgba[1] == 0);
    CHECK(img.rgba[2] == 0);
}

TEST_CASE("blp::decode: palette, alpha depth 1 unpacks bits LSB-first") {
    std::vector<uint8_t> palette(1024, 0);
    palette[0] = 255;
    palette[1] = 255;
    palette[2] = 255;  // white
    auto header = buildHeader(1, 1, 7, 8, 1, {static_cast<uint32_t>(kHeaderSize)}, palette);
    auto file = header;
    file.insert(file.end(), 8, 0);          // indices, all palette entry 0
    file.insert(file.end(), {0b00000001});  // bit0=1(opaque) bit1=0(transparent) rest 0

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[3] == 255);
    CHECK(img.rgba[7] == 0);
}

TEST_CASE("blp::decode: BGRA channel order") {
    auto header = buildHeader(3, 8, 7, 1, 1, {static_cast<uint32_t>(kHeaderSize)});
    auto file = header;
    file.insert(file.end(), {10, 20, 30, 40});  // B, G, R, A

    auto img = husk::blp::decode(file);
    CHECK(img.rgba[0] == 30);
    CHECK(img.rgba[1] == 20);
    CHECK(img.rgba[2] == 10);
    CHECK(img.rgba[3] == 40);
}

TEST_CASE("blp::decode: mip level 0 not present throws") {
    auto header = buildHeader(2, 8, 0, 4, 4);  // mipOffsets all 0
    CHECK_THROWS_WITH_AS(husk::blp::decode(header), doctest::Contains("isn't present"),
                          husk::blp::ParseError);
}

TEST_CASE("blp::decode: data running past end of file throws") {
    auto header = buildHeader(2, 8, 0, 4, 4, {static_cast<uint32_t>(kHeaderSize)});
    auto file = header;
    file.insert(file.end(), 4, 0);  // DXT1 needs 8 bytes, only 4 present
    CHECK_THROWS_WITH_AS(husk::blp::decode(file), doctest::Contains("needs 8 bytes"),
                          husk::blp::ParseError);
}

TEST_CASE("blp::decode: unsupported color encoding throws") {
    auto header = buildHeader(0, 8, 7, 4, 4, {static_cast<uint32_t>(kHeaderSize)});  // JPEG
    auto file = header;
    file.insert(file.end(), 100, 0);
    CHECK_THROWS_WITH_AS(husk::blp::decode(file), doctest::Contains("colorEncoding 0"),
                          husk::blp::ParseError);
}

TEST_CASE("blp::decode: bad magic throws") {
    auto header = buildHeader();
    std::memcpy(header.data(), "XXXX", 4);
    CHECK_THROWS_WITH_AS(husk::blp::decode(header), doctest::Contains("BLP2"),
                          husk::blp::ParseError);
}

TEST_CASE("blp::encodePng: round-trips through stb_image_write without throwing") {
    husk::blp::Image img;
    img.width = 2;
    img.height = 2;
    img.rgba = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    auto png = husk::blp::encodePng(img);
    REQUIRE(png.size() > 8);
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    CHECK(png[0] == 0x89);
    CHECK(png[1] == 'P');
    CHECK(png[2] == 'N');
    CHECK(png[3] == 'G');
}
