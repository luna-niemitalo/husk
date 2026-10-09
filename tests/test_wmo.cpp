// husk::wmo (src/wmo.hpp): synthetic chunk buffers for each parse rule and
// boundary check, plus real fixtures whose expected values were decoded by an
// independent Python walker (not by this parser).

#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_data_paths.hpp"
#include "wmo.hpp"

using namespace husk;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f.good());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

template <typename T>
void put(std::vector<uint8_t>& buf, size_t off, T v) {
    if (buf.size() < off + sizeof(T)) buf.resize(off + sizeof(T), 0);
    std::memcpy(buf.data() + off, &v, sizeof(T));
}

// Appends a chunk with its tag byte-reversed, as WMO stores it.
void appendChunk(std::vector<uint8_t>& out, const std::string& tag, const std::vector<uint8_t>& payload) {
    out.insert(out.end(), tag.rbegin(), tag.rend());
    put(out, out.size(), static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> version17() {
    std::vector<uint8_t> v;
    put<uint32_t>(v, 0, 17);
    return v;
}

std::vector<uint8_t> minimalRoot(uint32_t groupCount, std::vector<std::pair<std::string, std::vector<uint8_t>>> extra) {
    std::vector<uint8_t> mohd(64, 0);
    put<uint32_t>(mohd, 0x04, groupCount);
    std::vector<uint8_t> file;
    appendChunk(file, "MVER", version17());
    appendChunk(file, "MOHD", mohd);
    for (const auto& [tag, payload] : extra) appendChunk(file, tag, payload);
    return file;
}

// A group file holding `sub` inside MOGP after its 68-byte header.
std::vector<uint8_t> groupFile(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& sub) {
    std::vector<uint8_t> mogp(0x44, 0);
    put<uint32_t>(mogp, 0x08, 0x1234);
    for (const auto& [tag, payload] : sub) appendChunk(mogp, tag, payload);
    std::vector<uint8_t> file;
    appendChunk(file, "MVER", version17());
    appendChunk(file, "MOGP", mogp);
    return file;
}

std::vector<uint8_t> floats(std::initializer_list<float> values) {
    std::vector<uint8_t> out;
    for (float v : values) put(out, out.size(), v);
    return out;
}

std::vector<uint8_t> u16s(std::initializer_list<uint16_t> values) {
    std::vector<uint8_t> out;
    for (uint16_t v : values) put(out, out.size(), v);
    return out;
}

std::vector<uint8_t> batch(uint32_t start, uint16_t count, uint8_t flags, uint8_t smallMaterial,
                           uint16_t largeMaterial) {
    std::vector<uint8_t> b(24, 0);
    put(b, 0x0A, largeMaterial);
    put(b, 0x0C, start);
    put(b, 0x10, count);
    put<uint8_t>(b, 0x16, flags);
    put<uint8_t>(b, 0x17, smallMaterial);
    return b;
}

std::string groupPath(const std::string& rootPath, int index) {
    std::filesystem::path p(rootPath);
    std::string suffix = std::to_string(index);
    suffix.insert(0, 3 - suffix.size(), '0');
    return (p.parent_path() / (p.stem().string() + "_" + suffix + ".wmo")).string();
}

}  // namespace

TEST_CASE("wmo::parseRoot: MVER and MOHD are required, MOHD is exactly 64 bytes") {
    std::vector<uint8_t> noHeader;
    appendChunk(noHeader, "MVER", version17());
    CHECK_THROWS_WITH_AS(wmo::parseRoot(noHeader), "WMO root: no MOHD chunk", wmo::ParseError);

    std::vector<uint8_t> shortHeader;
    appendChunk(shortHeader, "MVER", version17());
    appendChunk(shortHeader, "MOHD", std::vector<uint8_t>(60, 0));
    CHECK_THROWS_WITH_AS(wmo::parseRoot(shortHeader), "MOHD: expected 64 bytes, got 60", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MODI and MODD are sized off their chunks, not MOHD's counts") {
    std::vector<uint8_t> modi;
    for (uint32_t id : {11u, 0u, 33u}) put(modi, modi.size(), id);
    std::vector<uint8_t> modd(40, 0);
    put<uint32_t>(modd, 0x00, 2u | (0x05u << 24));
    put(modd, 0x04, 1.0f);
    put(modd, 0x20, 2.5f);
    put<uint32_t>(modd, 0x24, 0xFF102030u);
    std::vector<uint8_t> mods(32, 0);
    std::memcpy(mods.data(), "Set_$DefaultGlobal", 18);
    put<uint32_t>(mods, 0x18, 1);

    wmo::RootFile root = wmo::parseRoot(minimalRoot(0, {{"MODI", modi}, {"MODD", modd}, {"MODS", mods}}));

    CHECK(root.doodadFileDataIds == std::vector<uint32_t>{11, 0, 33});
    REQUIRE(root.doodads.size() == 1);
    CHECK(root.doodads[0].nameIndex == 2);
    CHECK(root.doodads[0].flags == 0x05);
    CHECK(root.doodads[0].position[0] == 1.0f);
    CHECK(root.doodads[0].scale == 2.5f);
    CHECK(root.doodads[0].color == 0xFF102030u);
    REQUIRE(root.doodadSets.size() == 1);
    CHECK(root.doodadSets[0].name == "Set_$DefaultGlobal");
    CHECK(root.doodadSets[0].count == 1);
}

TEST_CASE("wmo::parseRoot: a doodad set reaching past MODD throws with both ranges") {
    std::vector<uint8_t> mods(32, 0);
    std::memcpy(mods.data(), "Set_A", 5);
    put<uint32_t>(mods, 0x14, 1);
    put<uint32_t>(mods, 0x18, 2);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MODD", std::vector<uint8_t>(40, 0)}, {"MODS", mods}})),
                         "MODS 'Set_A': expected doodads [1, 3) within 1 MODD entries", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MOUV must have one entry per material") {
    CHECK_THROWS_WITH_AS(
        wmo::parseRoot(minimalRoot(0, {{"MOMT", std::vector<uint8_t>(64, 0)}, {"MOUV", std::vector<uint8_t>(32, 0)}})),
        "MOUV: expected one entry per MOMT material (1), got 2", wmo::ParseError);
}

TEST_CASE("wmo::groupFileDataId: GFID is row-major [lodTier][group], 0 for holes and out of range") {
    std::vector<uint8_t> gfid;
    for (uint32_t id : {10u, 11u, 20u, 0u}) put(gfid, gfid.size(), id);
    wmo::RootFile root = wmo::parseRoot(minimalRoot(2, {{"GFID", gfid}}));
    CHECK(wmo::groupFileDataId(root, 0, 1) == 11);
    CHECK(wmo::groupFileDataId(root, 1, 0) == 20);
    CHECK(wmo::groupFileDataId(root, 1, 1) == 0);
    CHECK(wmo::groupFileDataId(root, 2, 0) == 0);
    CHECK(wmo::groupFileDataId(root, 0, 2) == 0);
}

TEST_CASE("wmo::parseGroup: reads the MOGP header and every sub-chunk after it") {
    std::vector<uint8_t> movt = floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    std::vector<uint8_t> mocv(12, 0x7F);
    std::vector<uint8_t> moba = batch(0, 3, 0x02, 0, 7);
    std::vector<uint8_t> file = groupFile({{"MOPY", {0x20, 7}},
                                           {"MOVI", u16s({0, 1, 2})},
                                           {"MOVT", movt},
                                           {"MONR", floats({0, 0, 1, 0, 0, 1, 0, 0, 1})},
                                           {"MOTV", floats({0, 0, 1, 0, 0, 1})},
                                           {"MOTV", floats({0, 0, 2, 0, 0, 2})},
                                           {"MOCV", mocv},
                                           {"MOBA", moba}});

    wmo::GroupFile g = wmo::parseGroup(file);

    CHECK(g.flags == 0x1234);
    CHECK(g.indices == std::vector<uint32_t>{0, 1, 2});
    REQUIRE(g.positions.size() == 3);
    CHECK(g.positions[1][0] == 1.0f);
    CHECK(g.uvSets.size() == 2);
    CHECK(g.uvSets[1][1][0] == 2.0f);
    CHECK(g.colorSets.size() == 1);
    REQUIRE(g.triangles.size() == 1);
    CHECK(g.triangles[0].flags == 0x20);
    CHECK(g.triangles[0].material == 7);
    REQUIRE(g.batches.size() == 1);
    CHECK(g.batches[0].material == 7);  // the large-id flag picks material_id_large
}

TEST_CASE("wmo::parseGroup: without the large-id flag a batch uses material_id") {
    std::vector<uint8_t> file = groupFile({{"MOVI", u16s({0, 0, 0})},
                                           {"MOVT", floats({0, 0, 0})},
                                           {"MOBA", batch(0, 3, 0x00, 4, 99)}});
    CHECK(wmo::parseGroup(file).batches[0].material == 4);
}

TEST_CASE("wmo::parseGroup: per-vertex chunks, indices and batches are checked against MOVT and MOVI") {
    std::vector<uint8_t> oneVertex = floats({0, 0, 0});
    CHECK_THROWS_WITH_AS(wmo::parseGroup(groupFile({{"MOVT", oneVertex}, {"MOTV", floats({0, 0, 1, 1})}})),
                         "MOTV: expected 1 entries (one per MOVT vertex), got 2", wmo::ParseError);
    CHECK_THROWS_WITH_AS(wmo::parseGroup(groupFile({{"MOVT", oneVertex}, {"MOVI", u16s({0, 0, 1})}})),
                         "MOVI: expected indices < 1 vertices, got 1", wmo::ParseError);
    CHECK_THROWS_WITH_AS(
        wmo::parseGroup(groupFile({{"MOVT", oneVertex}, {"MOVI", u16s({0, 0, 0})}, {"MOBA", batch(3, 3, 0, 0, 0)}})),
        "MOBA batch 0: expected indices [3, 6) within 3 MOVI indices", wmo::ParseError);
    std::vector<uint8_t> shortMogp;
    appendChunk(shortMogp, "MVER", version17());
    appendChunk(shortMogp, "MOGP", std::vector<uint8_t>(0x40, 0));
    CHECK_THROWS_WITH_AS(wmo::parseGroup(shortMogp), "MOGP: expected >= 68-byte header, got 64 bytes",
                         wmo::ParseError);
}

TEST_CASE("wmo: real cameron.wmo -- one group, one FileDataID material, a batch shorter than MOVI" *
          doctest::skip(test::testWmoCameron().empty())) {
    wmo::RootFile root = wmo::parseRoot(readFile(test::testWmoCameron()));
    CHECK(root.header.groupCount == 1);
    REQUIRE(root.materials.size() == 1);
    CHECK(root.materials[0].textures[0] == 130069);
    CHECK(root.textureNames.empty());
    CHECK(root.groupFileDataIds == std::vector<uint32_t>{108237});

    wmo::GroupFile g = wmo::parseGroup(readFile(groupPath(test::testWmoCameron(), 0)));
    CHECK(g.positions.size() == 154);
    CHECK(g.indices.size() == 708);
    REQUIRE(g.triangles.size() == 236);
    CHECK(g.triangles[0].flags == 0x64);
    REQUIRE(g.batches.size() == 1);
    CHECK(g.batches[0].startIndex == 0);
    CHECK(g.batches[0].indexCount == 672);
    CHECK(g.batches[0].maxVertex == 153);
}

TEST_CASE("wmo: real guardtower.wmo -- doodad sets, and MOHD's doodad count disagreeing with MODD" *
          doctest::skip(test::testWmoGuardTower().empty())) {
    wmo::RootFile root = wmo::parseRoot(readFile(test::testWmoGuardTower()));
    CHECK(root.materials.size() == 20);
    REQUIRE(root.doodadSets.size() == 3);
    CHECK(root.doodadSets[1].name == "Set_A");
    CHECK(root.doodadSets[2].startIndex == 62);
    CHECK(root.doodadSets[2].count == 108);
    CHECK(root.header.doodadDefCount == 173);
    CHECK(root.doodads.size() == 170);
    CHECK(root.doodadFileDataIds.size() == 71);
    CHECK(root.groupFileDataIds == std::vector<uint32_t>{106870, 106871});

    // Z up: the tower group is tallest on the third component.
    wmo::GroupFile tower = wmo::parseGroup(readFile(groupPath(test::testWmoGuardTower(), 1)));
    float height = tower.bounds.max[2] - tower.bounds.min[2];
    CHECK(height > tower.bounds.max[0] - tower.bounds.min[0]);
    CHECK(height > tower.bounds.max[1] - tower.bounds.min[1]);
}

TEST_CASE("wmo: real chambercap01 -- 4-tier GFID with holes, material_id_large batches, two UV and colour sets" *
          doctest::skip(test::testWmoChamberCap().empty())) {
    wmo::RootFile root = wmo::parseRoot(readFile(test::testWmoChamberCap()));
    CHECK(root.groupFileDataIds == std::vector<uint32_t>{2991660, 3171241, 3574067, 3171239, 3171242, 0, 3171240,
                                                         3171243, 0, 0, 3544636, 0});
    CHECK(wmo::groupFileDataId(root, 3, 1) == 3544636);
    CHECK(root.doodadSets.size() == 37);
    CHECK(root.doodads.size() == 181);
    CHECK(root.doodadColorMultipliers.size() == 181);

    wmo::GroupFile g = wmo::parseGroup(readFile(groupPath(test::testWmoChamberCap(), 0)));
    std::vector<uint32_t> materials;
    for (const wmo::Batch& b : g.batches) materials.push_back(b.material);
    CHECK(materials == std::vector<uint32_t>{2, 3, 4, 6, 8, 9, 1, 5, 7});
    CHECK(g.positions.size() == 1348);
    CHECK(g.uvSets.size() == 2);
    CHECK(g.colorSets.size() == 2);
}
