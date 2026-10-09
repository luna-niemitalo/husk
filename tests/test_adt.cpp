// husk::adt -- raw ADT/WDT parsing, against synthetic files built from
// ADT/v18.md (tests/test_adt_fixtures.hpp). Meaning (world frame, decoded
// normals, hole masks) is test_adt_canon_input.cpp's job; this file only
// checks that bytes land in the right raw fields and that every malformed
// shape is a ParseError, never a silent misread.

#include <doctest/doctest.h>

#include "../src/adt.hpp"
#include "test_adt_fixtures.hpp"

using namespace adtfx;
namespace adt = husk::adt;

TEST_CASE("adt::parseRoot reads every MCNK header field and MCVT/MCNR, in on-disk order") {
    RootSpec spec;
    spec.chunk = [](uint32_t ix, uint32_t iy) {
        ChunkSpec c;
        c.areaId = 100 + ix;
        c.baseZ = static_cast<float>(iy);
        c.height = [ix](int i) { return static_cast<float>(i) * 0.5f + static_cast<float>(ix); };
        c.normal = {10, -20, 120};
        c.holesHighRes = {1, 2, 3, 4, 5, 6, 7, 8};
        c.holesLowRes = 0xBEEF;
        c.predominantTexture[0] = 0xE4;
        c.noEffectDoodad[7] = 0x80;
        return c;
    };
    adt::RootFile root = adt::parseRoot(rootFile(spec));

    REQUIRE(root.chunks.size() == 256);
    CHECK_FALSE(root.liquid.has_value());
    const adt::MapChunk& mc = root.chunks[16 * 3 + 5];
    CHECK(mc.indexX == 5);
    CHECK(mc.indexY == 3);
    CHECK(mc.flags == 0x10000);
    CHECK(mc.areaId == 105);
    CHECK(mc.position[0] == doctest::Approx(chunkOrigin(32, 48, 5, 3)[0]));
    CHECK(mc.position[1] == doctest::Approx(chunkOrigin(32, 48, 5, 3)[1]));
    CHECK(mc.position[2] == 3.0f);
    CHECK(mc.heights[0] == 5.0f);
    CHECK(mc.heights[144] == 72.0f + 5.0f);
    CHECK(mc.normals[17][0] == 10);
    CHECK(mc.normals[17][1] == -20);
    CHECK(mc.normals[17][2] == 120);
    CHECK(mc.holesHighRes == std::array<uint8_t, 8>{1, 2, 3, 4, 5, 6, 7, 8});
    CHECK(mc.holesLowRes == 0xBEEF);
    CHECK(mc.predominantTexture[0] == 0xE4);
    CHECK(mc.noEffectDoodad[7] == 0x80);
}

TEST_CASE("adt::parseRoot rejects a tile that is not exactly 256 MCNKs in index order") {
    RootSpec spec;
    spec.chunkCount = 255;
    CHECK_THROWS_WITH_AS(adt::parseRoot(rootFile(spec)), doctest::Contains("expected 256 MCNK chunks, got 255"),
                         adt::ParseError);

    RootSpec swapped;
    swapped.swapFirstTwoChunks = true;
    CHECK_THROWS_WITH_AS(adt::parseRoot(rootFile(swapped)), doctest::Contains("MCNK #0 expected index (0,0), got (1,0)"),
                         adt::ParseError);
}

TEST_CASE("adt::parseRoot rejects an MCNK missing MCVT or MCNR") {
    RootSpec spec;
    spec.chunk = [](uint32_t ix, uint32_t iy) {
        ChunkSpec c;
        if (ix == 2 && iy == 1) c.includeMcnr = false;
        return c;
    };
    CHECK_THROWS_WITH_AS(adt::parseRoot(rootFile(spec)), doctest::Contains("MCNK (2,1): expected MCVT and MCNR"),
                         adt::ParseError);
}

TEST_CASE("adt::parseRoot rejects a truncated MCNK header") {
    std::vector<uint8_t> file = mverChunk();
    appendChunk(file, "MCNK", std::vector<uint8_t>(64, 0));
    CHECK_THROWS_WITH_AS(adt::parseRoot(file), doctest::Contains("expected >= 128-byte header, got 64"),
                         adt::ParseError);
}

namespace {

// (w + 1) * (h + 1) heights, then the same count of 2x u16 UVs and/or u8 depths.
std::vector<uint8_t> vertexBlock(uint8_t w, uint8_t h, bool heights, bool uvs, bool depths) {
    size_t n = static_cast<size_t>(w + 1) * (h + 1);
    std::vector<uint8_t> out;
    if (heights) {
        for (size_t i = 0; i < n; ++i) putF32(out, 50.0f + static_cast<float>(i));
    }
    if (uvs) {
        for (size_t i = 0; i < n; ++i) {
            putU16(out, static_cast<uint16_t>(i * 8));
            putU16(out, static_cast<uint16_t>(i * 16));
        }
    }
    if (depths) {
        for (size_t i = 0; i < n; ++i) putU8(out, static_cast<uint8_t>(i));
    }
    return out;
}

adt::RootFile rootWithLiquids(std::vector<LiquidSpec> liquids) {
    RootSpec spec;
    spec.liquids = std::move(liquids);
    return adt::parseRoot(rootFile(spec));
}

}  // namespace

TEST_CASE("adt::parseRoot MH2O infers each of the four vertex layouts from the gap to the next block") {
    LiquidSpec heightDepth{0, 1, 0, 1.0f, 2.0f, 0, 0, 1, 1, {}, vertexBlock(1, 1, true, false, true)};
    LiquidSpec heightUv{1, 2, 0, 0, 0, 2, 3, 2, 1, {}, vertexBlock(2, 1, true, true, false)};
    LiquidSpec depthOnly{2, 3, 0, 0, 0, 0, 0, 1, 2, {}, vertexBlock(1, 2, false, false, true)};
    LiquidSpec heightUvDepth{3, 4, 0, 0, 0, 0, 0, 1, 1, {}, vertexBlock(1, 1, true, true, true)};
    adt::RootFile root = rootWithLiquids({heightDepth, heightUv, depthOnly, heightUvDepth});
    REQUIRE(root.liquid.has_value());
    const auto& liquid = *root.liquid;

    const adt::LiquidInstance& a = liquid[0].instances.at(0);
    CHECK(a.vertexFormat == adt::LiquidVertexFormat::HeightDepth);
    CHECK(a.liquidType == 1);
    CHECK(a.minHeight == 1.0f);
    CHECK(a.maxHeight == 2.0f);
    CHECK(a.vertexHeights == std::vector<float>{50, 51, 52, 53});
    CHECK(a.vertexDepths == std::vector<uint8_t>{0, 1, 2, 3});
    CHECK(a.vertexUvs.empty());

    const adt::LiquidInstance& b = liquid[1].instances.at(0);
    CHECK(b.vertexFormat == adt::LiquidVertexFormat::HeightUv);
    CHECK(b.xOffset == 2);
    CHECK(b.yOffset == 3);
    CHECK(b.vertexHeights.size() == 6);
    REQUIRE(b.vertexUvs.size() == 6);
    CHECK(b.vertexUvs[1] == std::array<uint16_t, 2>{8, 16});
    CHECK(b.vertexDepths.empty());

    const adt::LiquidInstance& c = liquid[2].instances.at(0);
    CHECK(c.vertexFormat == adt::LiquidVertexFormat::DepthOnly);
    CHECK(c.vertexHeights.empty());
    CHECK(c.vertexDepths.size() == 6);

    const adt::LiquidInstance& d = liquid[3].instances.at(0);
    CHECK(d.vertexFormat == adt::LiquidVertexFormat::HeightUvDepth);
    CHECK(d.vertexHeights.size() == 4);
    CHECK(d.vertexUvs.size() == 4);
    CHECK(d.vertexDepths.size() == 4);

    CHECK(liquid[4].instances.empty());
}

TEST_CASE("adt::parseRoot MH2O keeps the exists bitmap and leaves the format unset with no vertex data") {
    LiquidSpec ocean{7, 2, 2, 0.0f, 0.0f, 0, 0, 8, 8, {0xFF, 0, 0, 0, 0, 0, 0, 0x81}, {}};
    adt::RootFile root = rootWithLiquids({ocean});
    const adt::LiquidInstance& inst = (*root.liquid)[7].instances.at(0);
    CHECK_FALSE(inst.vertexFormat.has_value());
    CHECK(inst.existsBitmap == std::vector<uint8_t>{0xFF, 0, 0, 0, 0, 0, 0, 0x81});
    CHECK(inst.liquidObjectOrLvf == 2);
}

TEST_CASE("adt::parseRoot MH2O with an MH2O chunk but no instances is an empty liquid table, not nullopt") {
    RootSpec spec;
    spec.writeMh2o = true;
    adt::RootFile root = adt::parseRoot(rootFile(spec));
    REQUIRE(root.liquid.has_value());
    for (const adt::LiquidChunk& c : *root.liquid) CHECK(c.instances.empty());
}

TEST_CASE("adt::parseRoot MH2O rejects an instance rect outside the chunk's 8x8 grid") {
    CHECK_THROWS_WITH_AS(rootWithLiquids({LiquidSpec{0, 1, 0, 0, 0, 4, 0, 5, 1, {}, {}}}),
                         doctest::Contains("expected rect inside 8x8 with nonzero size"), adt::ParseError);
    CHECK_THROWS_WITH_AS(rootWithLiquids({LiquidSpec{0, 1, 0, 0, 0, 0, 0, 0, 1, {}, {}}}),
                         doctest::Contains("expected rect inside 8x8 with nonzero size"), adt::ParseError);
}

TEST_CASE("adt::parseRoot MH2O rejects vertex data whose size matches no known layout") {
    // 4 vertices, 7 bytes each: divisible, but not 5/8/1/9.
    CHECK_THROWS_WITH_AS(rootWithLiquids({LiquidSpec{0, 1, 0, 0, 0, 0, 0, 1, 1, {}, std::vector<uint8_t>(28, 0)}}),
                         doctest::Contains("expected 5/8/1/9 bytes per vertex, got 7"), adt::ParseError);
    // 4 vertices, 21 bytes: not divisible at all.
    CHECK_THROWS_WITH_AS(rootWithLiquids({LiquidSpec{0, 1, 0, 0, 0, 0, 0, 1, 1, {}, std::vector<uint8_t>(21, 0)}}),
                         doctest::Contains("expected gap divisible by 4 vertices"), adt::ParseError);
}

TEST_CASE("adt::parseObj reads MDDF and MODF records field by field") {
    ObjSpec spec;
    spec.doodads.push_back({123, 7, {1, 2, 3}, {4, 5, 6}, 2048, 0x40});
    spec.mapObjects.push_back({456, 8, {10, 20, 30}, {0, 90, 0}, 0x8 | 0x4, 3, 1, 512});
    adt::ObjFile obj = adt::parseObj(objFile(spec));

    REQUIRE(obj.doodads.size() == 1);
    const adt::DoodadPlacement& d = obj.doodads[0];
    CHECK(d.nameId == 123);
    CHECK(d.uniqueId == 7);
    CHECK(d.position == std::array<float, 3>{1, 2, 3});
    CHECK(d.rotation == std::array<float, 3>{4, 5, 6});
    CHECK(d.scale == 2048);
    CHECK(d.flags == 0x40);

    REQUIRE(obj.mapObjects.size() == 1);
    const adt::MapObjectPlacement& m = obj.mapObjects[0];
    CHECK(m.nameId == 456);
    CHECK(m.uniqueId == 8);
    CHECK(m.position == std::array<float, 3>{10, 20, 30});
    CHECK(m.rotation == std::array<float, 3>{0, 90, 0});
    CHECK(m.extentsMin == std::array<float, 3>{-1, -1, -1});
    CHECK(m.extentsMax == std::array<float, 3>{1, 1, 1});
    CHECK(m.flags == 0xC);
    CHECK(m.doodadSet == 3);
    CHECK(m.nameSet == 1);
    CHECK(m.scale == 512);
}

TEST_CASE("adt::parseObj resolves MMDX/MMID and MWMO/MWID into per-index name lists") {
    ObjSpec spec;
    spec.doodadNames = {"World\\Azeroth\\Elwynn\\Tree01.mdx", "world/generic/rock.m2"};
    spec.mapObjectNames = {"World\\wmo\\Azeroth\\Buildings\\Inn.wmo"};
    spec.doodads.push_back({1, 1, {}, {}, 1024, 0});
    spec.mapObjects.push_back({0, 2, {}, {}, 0, 0, 0, 1024});
    adt::ObjFile obj = adt::parseObj(objFile(spec));
    CHECK(obj.doodadNames ==
          std::vector<std::string>{"World\\Azeroth\\Elwynn\\Tree01.mdx", "world/generic/rock.m2"});
    CHECK(obj.mapObjectNames == std::vector<std::string>{"World\\wmo\\Azeroth\\Buildings\\Inn.wmo"});
    CHECK(obj.doodads[0].nameId == 1);
}

TEST_CASE("adt::parseObj rejects a name-table placement whose nameId has no MMID/MWID entry") {
    ObjSpec spec;
    spec.doodadNames = {"a.m2"};
    spec.doodads.push_back({1, 1, {}, {}, 1024, 0});
    CHECK_THROWS_WITH_AS(adt::parseObj(objFile(spec)), doctest::Contains("MDDF: name-table placement expected nameId < 1"),
                         adt::ParseError);

    ObjSpec noTable;
    noTable.mapObjects.push_back({0, 2, {}, {}, 0, 0, 0, 1024});
    CHECK_THROWS_WITH_AS(adt::parseObj(objFile(noTable)), doctest::Contains("MODF: name-table placement expected nameId < 0"),
                         adt::ParseError);
}

TEST_CASE("adt::parseObj rejects a broken name table") {
    std::vector<uint8_t> loneBlock = mverChunk();
    appendChunk(loneBlock, "MMDX", std::vector<uint8_t>{'a', 0});
    CHECK_THROWS_WITH_AS(adt::parseObj(loneBlock), doctest::Contains("MMDX/MMID: expected both chunks or neither"),
                         adt::ParseError);

    std::vector<uint8_t> badOffset = mverChunk();
    appendChunk(badOffset, "MMDX", std::vector<uint8_t>{'a', 0});
    std::vector<uint8_t> ids;
    putU32(ids, 9);
    appendChunk(badOffset, "MMID", ids);
    CHECK_THROWS_WITH_AS(adt::parseObj(badOffset), doctest::Contains("MMDX: expected string offset < 2, got 9"),
                         adt::ParseError);

    std::vector<uint8_t> unterminated = mverChunk();
    appendChunk(unterminated, "MMDX", std::vector<uint8_t>{'a', 'b'});
    std::vector<uint8_t> zero;
    putU32(zero, 0);
    appendChunk(unterminated, "MMID", zero);
    CHECK_THROWS_WITH_AS(adt::parseObj(unterminated), doctest::Contains("not NUL-terminated"), adt::ParseError);
}

TEST_CASE("adt::parseObj rejects MDDF/MODF sizes that are not whole records") {
    std::vector<uint8_t> file = mverChunk();
    appendChunk(file, "MDDF", std::vector<uint8_t>(35, 0));
    CHECK_THROWS_WITH_AS(adt::parseObj(file), doctest::Contains("MDDF: expected size multiple of 36, got 35"),
                         adt::ParseError);

    std::vector<uint8_t> modf = mverChunk();
    appendChunk(modf, "MODF", std::vector<uint8_t>(65, 0));
    CHECK_THROWS_WITH_AS(adt::parseObj(modf), doctest::Contains("MODF: expected size multiple of 64, got 65"),
                         adt::ParseError);
}

TEST_CASE("adt::parseTex reads MDID/MHID/MTXP and every chunk's MCLY/MCAL") {
    TexSpec spec;
    spec.diffuseIds = {1001, 1002};
    spec.heightIds = {0, 2002};
    spec.textureParams = {0x10, 0x30};
    spec.layers = [](uint32_t c) {
        std::vector<LayerSpec> layers{LayerSpec{0, 0, 5, {}}};
        if (c == 9) layers.push_back(LayerSpec{1, 0x100, 0xFFFFFFFF, std::vector<uint8_t>(4096, 0x7F)});
        return layers;
    };
    adt::TexFile tex = adt::parseTex(texFile(spec));

    CHECK(tex.diffuseIds == std::vector<uint32_t>{1001, 1002});
    CHECK(tex.heightIds == std::vector<uint32_t>{0, 2002});
    REQUIRE(tex.params.has_value());
    REQUIRE(tex.params->size() == 2);
    CHECK((*tex.params)[1].flags == 0x30);
    CHECK((*tex.params)[1].heightScale == 0.5f);
    CHECK((*tex.params)[1].heightOffset == 2.0f);
    CHECK(tex.chunks[0].layers.size() == 1);
    CHECK(tex.chunks[0].layers[0].effectId == 5);
    REQUIRE(tex.chunks[9].layers.size() == 2);
    CHECK(tex.chunks[9].layers[1].textureId == 1);
    CHECK(tex.chunks[9].layers[1].flags == 0x100);
    CHECK(tex.chunks[9].mcal.size() == 4096);
}

TEST_CASE("adt::parseTex treats an MTEX holding only an empty string as an untextured tile") {
    TexSpec spec;
    spec.emptyMtex = true;
    spec.layers = [](uint32_t) { return std::vector<LayerSpec>{}; };
    adt::TexFile tex = adt::parseTex(texFile(spec));
    CHECK(tex.diffuseIds.empty());
    CHECK(tex.diffuseNames.empty());
    CHECK(tex.chunks[0].layers.empty());
}

TEST_CASE("adt::parseTex reads a non-empty MTEX as a filename table") {
    TexSpec spec;
    spec.textureNames = {"tileset/elwynn/elwynngrass.blp", "tileset/elwynn/elwynndirt.blp"};
    spec.layers = [](uint32_t) { return std::vector<LayerSpec>{LayerSpec{1, 0, 0xFFFFFFFF, {}}}; };
    adt::TexFile tex = adt::parseTex(texFile(spec));
    CHECK(tex.diffuseIds.empty());
    CHECK(tex.diffuseNames ==
          std::vector<std::string>{"tileset/elwynn/elwynngrass.blp", "tileset/elwynn/elwynndirt.blp"});
    CHECK(tex.textureCount() == 2);
    CHECK(tex.chunks[3].layers[0].textureId == 1);
}

TEST_CASE("adt::parseTex rejects a _tex0 with neither MDID nor MTEX") {
    std::vector<uint8_t> file = mverChunk();
    CHECK_THROWS_WITH_AS(adt::parseTex(file), doctest::Contains("expected an MDID or MTEX chunk"), adt::ParseError);
}

TEST_CASE("adt::parseTex rejects an MHID count that disagrees with the texture list") {
    TexSpec spec;
    spec.diffuseIds = {1, 2};
    spec.heightIds = {3};
    CHECK_THROWS_WITH_AS(adt::parseTex(texFile(spec)), doctest::Contains("MHID: expected 2 entries"),
                         adt::ParseError);
}

TEST_CASE("adt::parseTex rejects a layer pointing past the texture list") {
    TexSpec spec;
    spec.layers = [](uint32_t c) { return std::vector<LayerSpec>{LayerSpec{c == 200 ? 1u : 0u, 0, 0xFFFFFFFF, {}}}; };
    CHECK_THROWS_WITH_AS(adt::parseTex(texFile(spec)), doctest::Contains("MCLY in MCNK #200: expected textureId < 1, got 1"),
                         adt::ParseError);
}

TEST_CASE("adt::parseWdtFlags reads MPHD.flags and requires MPHD") {
    CHECK(adt::parseWdtFlags(wdtFile(0x3CA)) == 0x3CA);
    CHECK_THROWS_WITH_AS(adt::parseWdtFlags(mverChunk()), doctest::Contains("expected an MPHD chunk"),
                         adt::ParseError);
}

namespace {

adt::TextureLayer layerAt(uint32_t offset, uint32_t flags) { return adt::TextureLayer{0, flags | 0x100, offset, 0}; }

}  // namespace

TEST_CASE("adt::decodeAlphaMap: uncompressed 8-bit (4096) reads straight through from the layer's offset") {
    std::vector<uint8_t> mcal(10, 0xEE);
    for (int i = 0; i < 4096; ++i) mcal.push_back(static_cast<uint8_t>(i));
    auto alpha = adt::decodeAlphaMap(mcal, layerAt(10, 0), true, false);
    CHECK(alpha[0] == 0);
    CHECK(alpha[255] == 255);
    CHECK(alpha[4095] == static_cast<uint8_t>(4095));
}

TEST_CASE("adt::decodeAlphaMap: RLE fill and copy runs expand to 4096 bytes") {
    std::vector<uint8_t> mcal;
    // fill 127 x 0x10, copy 3 literal bytes, then fill the remainder with 0xFF.
    mcal.push_back(0x80 | 127);
    mcal.push_back(0x10);
    mcal.push_back(3);
    mcal.insert(mcal.end(), {1, 2, 3});
    size_t remaining = 4096 - 130;
    while (remaining > 0) {
        size_t run = std::min<size_t>(remaining, 127);
        mcal.push_back(static_cast<uint8_t>(0x80 | run));
        mcal.push_back(0xFF);
        remaining -= run;
    }
    auto alpha = adt::decodeAlphaMap(mcal, layerAt(0, adt::kLayerAlphaCompressed), true, false);
    CHECK(alpha[0] == 0x10);
    CHECK(alpha[126] == 0x10);
    CHECK(alpha[127] == 1);
    CHECK(alpha[129] == 3);
    CHECK(alpha[130] == 0xFF);
    CHECK(alpha[4095] == 0xFF);
}

TEST_CASE("adt::decodeAlphaMap: an RLE run past 4096 stops at the map's end instead of overrunning") {
    std::vector<uint8_t> mcal;
    for (int i = 0; i < 33; ++i) {  // 33 * 127 = 4191 > 4096
        mcal.push_back(0x80 | 127);
        mcal.push_back(0x42);
    }
    auto alpha = adt::decodeAlphaMap(mcal, layerAt(0, adt::kLayerAlphaCompressed), true, false);
    CHECK(alpha[4095] == 0x42);
}

TEST_CASE("adt::decodeAlphaMap: compressed alpha under a 4-bit WDT is a ParseError") {
    std::vector<uint8_t> mcal{0x81, 0};
    CHECK_THROWS_WITH_AS(adt::decodeAlphaMap(mcal, layerAt(0, adt::kLayerAlphaCompressed), false, false),
                         doctest::Contains("compressed alpha flag set but WDT MPHD selects 4-bit alpha"),
                         adt::ParseError);
}

TEST_CASE("adt::decodeAlphaMap: 4-bit (2048) expands each nibble low-first, v | v << 4") {
    std::vector<uint8_t> mcal(2048, 0);
    mcal[0] = 0xA1;  // pixel 0 = 0x1 -> 0x11, pixel 1 = 0xA -> 0xAA
    mcal[31] = 0x50;  // pixels 62, 63 of row 0: 0x00, 0x55
    auto fixedUp = adt::decodeAlphaMap(mcal, layerAt(0, 0), false, false);
    CHECK(fixedUp[0] == 0x11);
    CHECK(fixedUp[1] == 0xAA);
    // Not do_not_fix: the 63x63 map's last column/row duplicate their neighbour.
    CHECK(fixedUp[63] == fixedUp[62]);
    CHECK(fixedUp[63 * 64 + 5] == fixedUp[62 * 64 + 5]);

    auto raw = adt::decodeAlphaMap(mcal, layerAt(0, 0), false, true);
    CHECK(raw[62] == 0x00);
    CHECK(raw[63] == 0x55);
}

TEST_CASE("adt::decodeAlphaMap: a truncated MCAL is a ParseError, not a short read") {
    std::vector<uint8_t> mcal(4000, 0);
    CHECK_THROWS_WITH_AS(adt::decodeAlphaMap(mcal, layerAt(0, 0), true, false), doctest::Contains("MCAL 4096"),
                         adt::ParseError);
    CHECK_THROWS_WITH_AS(adt::decodeAlphaMap(std::vector<uint8_t>(10, 0), layerAt(0, 0), false, false),
                         doctest::Contains("MCAL 2048"), adt::ParseError);
}
