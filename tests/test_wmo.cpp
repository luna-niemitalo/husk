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

TEST_CASE("wmo::parseRoot: MNLD reads every field at its wiki offset, scale and multiplier as half floats") {
    std::vector<uint8_t> mnld(184, 0);
    put<int32_t>(mnld, 0x00, 1);
    put<int32_t>(mnld, 0x04, 0);
    put<int32_t>(mnld, 0x08, 0x3);
    put<int32_t>(mnld, 0x0C, 1);
    put<uint32_t>(mnld, 0x10, 0xFF102030);
    put(mnld, 0x14, 1.0f);
    put(mnld, 0x18, 2.0f);
    put(mnld, 0x1C, 3.0f);
    put(mnld, 0x20, -3.0f);
    put(mnld, 0x2C, 4.0f);
    put(mnld, 0x30, 9.0f);
    put(mnld, 0x34, 1.5f);
    put<uint32_t>(mnld, 0x38, 0xFF405060);
    put(mnld, 0x3C, 0.5f);
    put(mnld, 0x40, 6.0f);
    put(mnld, 0x48, 0.25f);
    put(mnld, 0x4C, 2.0f);
    put<int32_t>(mnld, 0x50, 2);
    put<uint32_t>(mnld, 0x64, 3618219);
    put(mnld, 0x7C, 5.0f);
    put(mnld, 0x80, 0.5f);
    put(mnld, 0x84, 1.75f);
    put<uint16_t>(mnld, 0x88, 0x3800);  // 0.5
    put<uint16_t>(mnld, 0x8A, 0x3E00);  // 1.5
    std::vector<uint8_t> mods(64, 0);
    wmo::RootFile root = wmo::parseRoot(minimalRoot(0, {{"MODS", mods}, {"MNLD", mnld}}));
    REQUIRE(root.newLights.size() == 1);
    const wmo::NewLight& l = root.newLights[0];
    CHECK(l.type == 1);
    CHECK(l.flags == 0x3);
    CHECK(l.doodadSet == 1);
    CHECK(l.innerColor == 0xFF102030);
    CHECK(l.position == wmo::Vec3{1, 2, 3});
    CHECK(l.rotation == wmo::Vec3{-3, 0, 0});
    CHECK(l.attenuationStart == 4.0f);
    CHECK(l.attenuationEnd == 9.0f);
    CHECK(l.intensity == 1.5f);
    CHECK(l.outerColor == 0xFF405060);
    CHECK(l.blendStart == 0.5f);
    CHECK(l.blendEnd == 6.0f);
    CHECK(l.flickerIntensity == 0.25f);
    CHECK(l.flickerSpeed == 2.0f);
    CHECK(l.flickerMode == 2);
    CHECK(l.cookieFileDataId == 3618219);
    CHECK(l.falloff == 5.0f);
    CHECK(l.innerAngle == 0.5f);
    CHECK(l.outerAngle == 1.75f);
    CHECK(l.scale == 0.5f);
    CHECK(l.intensityMultiplier == 1.5f);
}

TEST_CASE("wmo::parseRoot: an MNLD light naming a doodad set past MODS throws with both values") {
    std::vector<uint8_t> mnld(184, 0);
    put<int32_t>(mnld, 0x0C, 2);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MODS", std::vector<uint8_t>(64, 0)}, {"MNLD", mnld}})),
                         "MNLD[0].doodadSet: expected a MODS index < 2, got 2", wmo::ParseError);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MNLD", std::vector<uint8_t>(183, 0)}})),
                         "MNLD: expected a multiple of 184 bytes, got 183", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MOLT reads type, attenuation flag, colour, position, rotation and range") {
    std::vector<uint8_t> molt(48, 0);
    put<uint8_t>(molt, 0x00, 2);
    put<uint8_t>(molt, 0x01, 1);
    put<uint8_t>(molt, 0x02, 1);
    put<uint32_t>(molt, 0x04, 0xFF052C25);
    put(molt, 0x08, 7.0f);
    put(molt, 0x14, 1.0f);
    put(molt, 0x24, 1.0f);
    put(molt, 0x28, 2.0f);
    put(molt, 0x2C, 4.0f);
    wmo::RootFile root = wmo::parseRoot(minimalRoot(0, {{"MOLT", molt}}));
    REQUIRE(root.legacyLights.size() == 1);
    const wmo::LegacyLight& l = root.legacyLights[0];
    CHECK(l.type == 2);
    CHECK(l.useAttenuation == 1);
    CHECK(l.unknown == std::array<uint8_t, 2>{1, 0});
    CHECK(l.color == 0xFF052C25);
    CHECK(l.position == wmo::Vec3{7, 0, 0});
    CHECK(l.intensity == 1.0f);
    CHECK(l.rotation == std::array<float, 4>{0, 0, 0, 1});
    CHECK(l.attenuationStart == 2.0f);
    CHECK(l.attenuationEnd == 4.0f);
}

namespace {

// One MPVD record with emitter slots 0 and 2 used.
std::vector<uint8_t> mpvdRecord(uint32_t keyCount) {
    std::vector<uint8_t> r(4272, 0);
    put<uint32_t>(r, 0x00, 21);
    put(r, 0x08, 1.0f);
    put(r, 0x18, 7.0f);
    put(r, 0x28, -3.0f);
    put<uint32_t>(r, 0x30, 16386);
    put<uint32_t>(r, 0x34, 500);
    put(r, 0x3C, 0.5f);
    for (uint32_t k = 0; k < keyCount && k < 40; ++k) {
        put(r, 0x40 + 12 * k, 0.25f);
        put(r, 0x44 + 12 * k, 1.0f);
        put(r, 0x48 + 12 * k, static_cast<float>(k));
    }
    put<uint32_t>(r, 0x298, keyCount);
    for (uint32_t slot : {0u, 2u}) {
        size_t e = 0x2D0 + slot * 844;
        put(r, e + 0x10, -0.5f);
        put(r, e + 0x80, 1.0f);
        put(r, e + 0x8C, 0.5f);
        put<uint32_t>(r, e + 0x94, 100 + slot);
        put<uint32_t>(r, e + 0x98, 3);
        put<uint32_t>(r, e + 0x9C, 4);
        put<uint32_t>(r, e + 0xA0, 2);
        put<uint32_t>(r, e + 0xA4, 8);
        put(r, e + 0xA8, 12.0f);
    }
    put<uint32_t>(r, 0x10AC, 2);  // emitter-entry count
    return r;
}

}  // namespace

TEST_CASE("wmo::parseRoot: MPVD reads header, envelope and the used emitter slots; undescribed ranges only when set") {
    wmo::RootFile root = wmo::parseRoot(minimalRoot(0, {{"MPVD", mpvdRecord(2)}}));
    REQUIRE(root.particulateVolumes.size() == 1);
    const wmo::ParticulateVolume& v = root.particulateVolumes[0];
    CHECK(v.flags == 21);
    CHECK(v.position == wmo::Vec3{1, 0, 0});
    CHECK(v.extent == wmo::Vec3{0, 7, 0});
    CHECK(v.rotation == wmo::Vec3{0, 0, -3});
    CHECK(v.presetId == 16386);
    CHECK(v.particleCount == 500);
    CHECK(v.opacity == 0.5f);
    REQUIRE(v.envelope.size() == 2);
    CHECK(v.envelope[1] == std::array<float, 3>{0.25f, 1.0f, 1.0f});
    REQUIRE(v.emitters.size() == 2);
    const wmo::ParticulateEmitter& e = v.emitters[1];
    CHECK(e.slot == 2);
    CHECK(e.textureFileDataId == 102);
    CHECK(e.parameters[4] == -0.5f);
    CHECK(e.color == std::array<float, 4>{1, 0, 0, 0.5f});
    CHECK(e.blendMode == 3);
    CHECK(e.tilesX == 4);
    CHECK(e.tilesY == 2);
    CHECK(e.frames == 8);
    CHECK(e.unknownA8 == 12.0f);
    CHECK(v.unknownRanges.empty());

    std::vector<uint8_t> gap = mpvdRecord(2);
    put<uint32_t>(gap, 0x274, 7);           // inside the undescribed +0x270..+0x298
    put<uint32_t>(gap, 0x2D0 + 844 + 4, 9);  // inside the unused emitter slot 1
    root = wmo::parseRoot(minimalRoot(0, {{"MPVD", gap}}));
    const std::vector<wmo::UnknownRange>& ranges = root.particulateVolumes[0].unknownRanges;
    REQUIRE(ranges.size() == 2);
    CHECK(ranges[0].offset == 0x270);
    CHECK(ranges[0].words[1] == 7);
    CHECK(ranges[1].offset == 0x2D0 + 844);
    CHECK(ranges[1].words.size() == 844 / 4);
}

TEST_CASE("wmo::parseRoot: an MPVD emitter count that disagrees with the textured slots throws") {
    std::vector<uint8_t> r = mpvdRecord(0);
    put<uint32_t>(r, 0x10AC, 3);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MPVD", r}})),
                         "MPVD[0] emitter count: expected the +0x10AC count to match the 2 slots with a texture, got 3",
                         wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MPVD with more than 40 keyframes or a partial record throws") {
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MPVD", mpvdRecord(41)}})),
                         "MPVD[0] keyframe count: expected <= 40, got 41", wmo::ParseError);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MPVD", std::vector<uint8_t>(4000, 0)}})),
                         "MPVD: expected a multiple of 4272 bytes, got 4000", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MAVG/MAVD read 48-byte records, MBVD 128-byte boxes, each naming a doodad set") {
    std::vector<uint8_t> mavd(48, 0);
    put(mavd, 0x00, 5.0f);
    put(mavd, 0x0C, 10.0f);
    put(mavd, 0x10, 20.0f);
    put<uint32_t>(mavd, 0x14, 0xFF102030);
    put<uint32_t>(mavd, 0x18, 0xFF405060);
    put<uint32_t>(mavd, 0x1C, 0xFF708090);
    put<uint32_t>(mavd, 0x20, 1);
    put<uint16_t>(mavd, 0x24, 1);
    std::vector<uint8_t> mbvd(128, 0);
    float planes[6][4] = {{-1, 0, 0, 2}, {1, 0, 0, 3}, {0, -1, 0, 4}, {0, 1, 0, 5}, {0, 0, -1, 6}, {0, 0, 1, 7}};
    for (size_t k = 0; k < 6; ++k) {
        for (size_t c = 0; c < 4; ++c) put(mbvd, 16 * k + 4 * c, planes[k][c]);
    }
    put(mbvd, 0x60, 1.5f);
    put<uint32_t>(mbvd, 0x64, 0xFF000001);
    put<uint32_t>(mbvd, 0x70, 1);
    std::vector<uint8_t> mods(64, 0);
    wmo::RootFile root = wmo::parseRoot(
        minimalRoot(0, {{"MODS", mods}, {"MAVG", std::vector<uint8_t>(48, 0)}, {"MAVD", mavd}, {"MBVD", mbvd}}));

    REQUIRE(root.globalAmbientVolumes.size() == 1);
    REQUIRE(root.ambientVolumes.size() == 1);
    const wmo::AmbientVolume& v = root.ambientVolumes[0];
    CHECK(v.position == wmo::Vec3{5, 0, 0});
    CHECK(v.start == 10.0f);
    CHECK(v.end == 20.0f);
    CHECK(v.colors == std::array<uint32_t, 3>{0xFF102030, 0xFF405060, 0xFF708090});
    CHECK(v.flags == 1);
    CHECK(v.doodadSet == 1);
    REQUIRE(root.ambientBoxes.size() == 1);
    const wmo::AmbientBox& b = root.ambientBoxes[0];
    CHECK(b.planes[3] == std::array<float, 4>{0, 1, 0, 5});
    CHECK(b.end == 1.5f);
    CHECK(b.colors[0] == 0xFF000001);
    CHECK(b.flags == 1);
}

TEST_CASE("wmo::parseRoot: an ambient volume naming a doodad set past MODS throws with both values") {
    std::vector<uint8_t> mavd(48, 0);
    put<uint16_t>(mavd, 0x24, 3);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MODS", std::vector<uint8_t>(32, 0)}, {"MAVD", mavd}})),
                         "MAVD[0].doodadSet: expected a MODS index < 1, got 3", wmo::ParseError);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MBVD", std::vector<uint8_t>(100, 0)}})),
                         "MBVD: expected a multiple of 128 bytes, got 100", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MFOG reads 48-byte fogs, MFED gives each its doodad set") {
    std::vector<uint8_t> mfog(96, 0);
    put<uint32_t>(mfog, 48 + 0x00, 0x1);
    put(mfog, 48 + 0x04, 7.0f);
    put(mfog, 48 + 0x10, 5.0f);
    put(mfog, 48 + 0x14, 20.0f);
    put(mfog, 48 + 0x18, 100.0f);
    put(mfog, 48 + 0x1C, 0.25f);
    put<uint32_t>(mfog, 48 + 0x20, 0xFF102030);
    put(mfog, 48 + 0x24, 50.0f);
    put(mfog, 48 + 0x28, -0.5f);
    put<uint32_t>(mfog, 48 + 0x2C, 0xFF0000FF);
    std::vector<uint8_t> mfed(32, 0);
    put<uint16_t>(mfed, 16, 1);

    wmo::RootFile withExtra =
        wmo::parseRoot(minimalRoot(0, {{"MODS", std::vector<uint8_t>(64, 0)}, {"MFOG", mfog}, {"MFED", mfed}}));
    REQUIRE(withExtra.fogs.size() == 2);
    const wmo::Fog& f = withExtra.fogs[1];
    CHECK(f.flags == 0x1);
    CHECK(f.position == wmo::Vec3{7, 0, 0});
    CHECK(f.smallerRadius == 5.0f);
    CHECK(f.largerRadius == 20.0f);
    CHECK(f.fog.end == 100.0f);
    CHECK(f.fog.startScalar == 0.25f);
    CHECK(f.fog.color == 0xFF102030);
    CHECK(f.underwater.end == 50.0f);
    CHECK(f.underwater.startScalar == -0.5f);
    CHECK(f.underwater.color == 0xFF0000FF);
    CHECK(withExtra.fogs[0].doodadSet == 0);
    CHECK(f.doodadSet == 1);

    // An empty MFED, the common real case, leaves every fog without a set.
    wmo::RootFile emptyExtra = wmo::parseRoot(minimalRoot(0, {{"MFOG", mfog}, {"MFED", {}}}));
    CHECK_FALSE(emptyExtra.fogs[1].doodadSet.has_value());
}

TEST_CASE("wmo::parseRoot: an MFED that isn't one per MFOG, or names a set past MODS, throws") {
    std::vector<uint8_t> mfog(96, 0);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MFOG", mfog}, {"MFED", std::vector<uint8_t>(16, 0)}})),
                         "MFED: expected one entry per MFOG fog (2), got 1", wmo::ParseError);
    std::vector<uint8_t> mfed(32, 0);
    put<uint16_t>(mfed, 16, 2);
    CHECK_THROWS_WITH_AS(
        wmo::parseRoot(minimalRoot(0, {{"MODS", std::vector<uint8_t>(64, 0)}, {"MFOG", mfog}, {"MFED", mfed}})),
        "MFED[1].doodadSet: expected a MODS index < 2, got 2", wmo::ParseError);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MFOG", std::vector<uint8_t>(40, 0)}})),
                         "MFOG: expected a multiple of 48 bytes, got 40", wmo::ParseError);
}

TEST_CASE("wmo::parseRoot: MOPV/MOPT/MOPR read portal polygons and the groups' links, MOPE raw") {
    std::vector<uint8_t> mopv = floats({0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1});
    std::vector<uint8_t> mopt(20, 0);
    put<uint16_t>(mopt, 0x00, 0);
    put<uint16_t>(mopt, 0x02, 4);
    put(mopt, 0x04, 1.0f);
    put(mopt, 0x10, -2.5f);
    std::vector<uint8_t> mopr(16, 0);
    put<uint16_t>(mopr, 0x02, 1);
    put<int16_t>(mopr, 0x04, 1);
    put<uint16_t>(mopr, 0x0A, 0);
    put<int16_t>(mopr, 0x0C, -1);
    std::vector<uint8_t> mogi(64, 0);
    std::vector<uint8_t> mope(16, 0x7A);

    wmo::RootFile root = wmo::parseRoot(
        minimalRoot(2, {{"MOGI", mogi}, {"MOPV", mopv}, {"MOPT", mopt}, {"MOPR", mopr}, {"MOPE", mope}}));
    REQUIRE(root.portalVertices.size() == 4);
    REQUIRE(root.portals.size() == 1);
    CHECK(root.portals[0].count == 4);
    CHECK(root.portals[0].normal == wmo::Vec3{1, 0, 0});
    CHECK(root.portals[0].distance == -2.5f);
    REQUIRE(root.portalRefs.size() == 2);
    CHECK(root.portalRefs[0].group == 1);
    CHECK(root.portalRefs[0].side == 1);
    CHECK(root.portalRefs[1].side == -1);
    REQUIRE(root.portalExtras.size() == 1);
    CHECK(root.portalExtras[0][15] == 0x7A);
}

TEST_CASE("wmo::parseRoot: portal ranges and links past their tables throw with both values") {
    std::vector<uint8_t> mopt(20, 0);
    put<uint16_t>(mopt, 0x02, 4);
    CHECK_THROWS_WITH_AS(wmo::parseRoot(minimalRoot(0, {{"MOPV", floats({0, 0, 0})}, {"MOPT", mopt}})),
                         "MOPT[0]: expected vertices [0, 4) within 1 MOPV vertices", wmo::ParseError);
    std::vector<uint8_t> mopr(8, 0);
    put<uint16_t>(mopr, 0x02, 2);
    CHECK_THROWS_WITH_AS(
        wmo::parseRoot(minimalRoot(2, {{"MOGI", std::vector<uint8_t>(64, 0)}, {"MOPT", std::vector<uint8_t>(20, 0)},
                                       {"MOPR", mopr}})),
        "MOPR[0]: expected portal < 1 and group < 2, got portal 0, group 2", wmo::ParseError);
    CHECK_THROWS_WITH_AS(
        wmo::parseRoot(minimalRoot(0, {{"MOPT", std::vector<uint8_t>(40, 0)}, {"MOPE", std::vector<uint8_t>(16, 0)}})),
        "MOPE: expected one entry per MOPT portal (2), got 1", wmo::ParseError);
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

namespace {

// An MLIQ chunk with vertex i's height = i and data bytes {i, 0, 0, 0}, and
// tile i's byte = tiles[i].
std::vector<uint8_t> mliq(int32_t vertsX, int32_t vertsY, const std::vector<uint8_t>& tiles,
                          int32_t tilesX = -1, int32_t tilesY = -1) {
    std::vector<uint8_t> out;
    put<int32_t>(out, 0x00, vertsX);
    put<int32_t>(out, 0x04, vertsY);
    put<int32_t>(out, 0x08, tilesX < 0 ? vertsX - 1 : tilesX);
    put<int32_t>(out, 0x0C, tilesY < 0 ? vertsY - 1 : tilesY);
    put(out, 0x10, 10.0f);
    put(out, 0x14, 20.0f);
    put(out, 0x18, -5.0f);
    put<uint16_t>(out, 0x1C, 3);
    for (int32_t i = 0; i < vertsX * vertsY; ++i) {
        put<uint8_t>(out, out.size(), static_cast<uint8_t>(i));
        put<uint8_t>(out, out.size(), 0);
        put<uint16_t>(out, out.size(), 0);
        put(out, out.size(), static_cast<float>(i));
    }
    out.insert(out.end(), tiles.begin(), tiles.end());
    return out;
}

}  // namespace

TEST_CASE("wmo::parseGroup: MLIQ reads its header, row-major vertices and raw tile bytes") {
    wmo::GroupFile g = wmo::parseGroup(groupFile({{"MLIQ", mliq(3, 2, {0x40, 0x0F})}}));
    REQUIRE(g.liquid.has_value());
    const wmo::Liquid& l = *g.liquid;
    CHECK(l.vertsX == 3);
    CHECK(l.vertsY == 2);
    CHECK(l.corner == wmo::Vec3{10, 20, -5});
    CHECK(l.material == 3);
    CHECK(l.heights == std::vector<float>{0, 1, 2, 3, 4, 5});
    CHECK(l.vertexData[4] == std::array<uint8_t, 4>{4, 0, 0, 0});
    CHECK(l.tiles == std::vector<uint8_t>{0x40, 0x0F});
    CHECK_FALSE(wmo::parseGroup(groupFile({})).liquid.has_value());
}

TEST_CASE("wmo::parseGroup: MLIQ counts that disagree with each other or the chunk size throw") {
    CHECK_THROWS_WITH_AS(wmo::parseGroup(groupFile({{"MLIQ", mliq(3, 2, {0, 0}, 3, 1)}})),
                         "MLIQ: expected tiles = verts - 1 >= 0 per axis, got verts (3, 2) tiles (3, 1)",
                         wmo::ParseError);
    std::vector<uint8_t> shortTiles = mliq(3, 2, {0});
    CHECK_THROWS_WITH_AS(wmo::parseGroup(groupFile({{"MLIQ", shortTiles}})),
                         "MLIQ: expected 80 bytes for 3x2 vertices, got 79", wmo::ParseError);
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
    CHECK_THROWS_WITH_AS(wmo::parseGroup(groupFile({{"MOPY", {0x20, 0, 0x20, 0}},
                                                    {"MOVI", u16s({0, 0, 0})},
                                                    {"MOVT", oneVertex}})),
                         "MOPY/MPY2: expected one entry per MOVI triangle (1), got 2", wmo::ParseError);
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
    // The wiki's "empty fog entry", byte for byte.
    REQUIRE(root.fogs.size() == 1);
    CHECK(root.fogs[0].largerRadius == 0.0f);
    CHECK(root.fogs[0].fog.end == doctest::Approx(444.4445));
    CHECK(root.fogs[0].fog.startScalar == 0.25f);
    CHECK(root.fogs[0].underwater.end == doctest::Approx(222.2222));
    CHECK(root.fogs[0].underwater.startScalar == -0.5f);

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

TEST_CASE("wmo: real ruinedkeep_crypt2_015 -- the wiki's MLIQ example, liquid tiles over the group" *
          doctest::skip(test::testWmoLiquidGroup().empty())) {
    wmo::GroupFile g = wmo::parseGroup(readFile(test::testWmoLiquidGroup()));
    REQUIRE(g.liquid.has_value());
    const wmo::Liquid& l = *g.liquid;
    CHECK(l.vertsX == 10);
    CHECK(l.vertsY == 13);
    CHECK(l.material == 46);
    CHECK(l.corner[0] == doctest::Approx(91.6667f));
    CHECK(l.corner[1] == 75.0f);
    CHECK(l.heights[0] == doctest::Approx(-80.3648f));
    // Grid +X/+Y from the corner at kLiquidTileSize. The grid snaps to world
    // units and overhangs the group's walls by under a tile, so each liquid
    // tile overlaps the group's bounds rather than sitting inside them.
    for (uint32_t y = 0; y + 1 < l.vertsY; ++y) {
        for (uint32_t x = 0; x + 1 < l.vertsX; ++x) {
            if ((l.tiles[y * (l.vertsX - 1) + x] & 0x0F) == 0x0F) continue;
            float x0 = l.corner[0] + static_cast<float>(x) * wmo::kLiquidTileSize;
            float y0 = l.corner[1] + static_cast<float>(y) * wmo::kLiquidTileSize;
            CAPTURE(x);
            CAPTURE(y);
            CHECK(x0 + wmo::kLiquidTileSize > g.bounds.min[0]);
            CHECK(x0 < g.bounds.max[0]);
            CHECK(y0 + wmo::kLiquidTileSize > g.bounds.min[1]);
            CHECK(y0 < g.bounds.max[1]);
        }
    }
}

TEST_CASE("wmo: real 10xt_exterior_tunnel01 -- the corpus's one two-emitter MPVD record" *
          doctest::skip(test::testWmoParticulate().empty())) {
    wmo::RootFile root = wmo::parseRoot(readFile(test::testWmoParticulate()));
    REQUIRE(root.particulateVolumes.size() == 1);
    const wmo::ParticulateVolume& v = root.particulateVolumes[0];
    CHECK(v.extent == wmo::Vec3{86, 74, 109});
    REQUIRE(v.emitters.size() == 2);
    CHECK(v.emitters[0].textureFileDataId == 2058148);
    CHECK(v.emitters[1].slot == 1);
    CHECK(v.emitters[1].textureFileDataId == 3509651);
    // Flipbook ints line up across both slots: the 844-byte stride.
    CHECK(v.emitters[0].tilesX * v.emitters[0].tilesY == v.emitters[0].frames);
    CHECK(v.emitters[1].tilesX * v.emitters[1].tilesY == v.emitters[1].frames);
}

namespace {

std::vector<uint8_t> u32s(std::initializer_list<uint32_t> values) {
    std::vector<uint8_t> out;
    for (uint32_t v : values) put(out, out.size(), v);
    return out;
}

// One MOLS record: index 7, BGRA red, at (1, 2, 3), attenuation 4..10,
// intensity 0.5, rotation (0.1, 0.2, 0.3), unknown 1.0, cone 0.25..0.75.
std::vector<uint8_t> mols() {
    std::vector<uint8_t> out = u32s({7, 0xFFFF0000u});
    for (float v : {1.0f, 2.0f, 3.0f, 4.0f, 10.0f, 0.5f, 0.1f, 0.2f, 0.3f, 1.0f, 0.25f, 0.75f}) put(out, out.size(), v);
    return out;
}

// One MOLP record: end 9 at +0x18, start 3 at +0x1C (the wiki's labels swapped).
std::vector<uint8_t> molp() {
    std::vector<uint8_t> out = u32s({0, 0xFF00FF00u});
    for (float v : {5.0f, 6.0f, 7.0f, 2.0f, 9.0f, 3.0f, 0.0f, -0.0f, -1.0f}) put(out, out.size(), v);
    return out;
}

}  // namespace

TEST_CASE("wmo::parseGroup: light refs, light-set lights at their corpus-verified offsets, and set windows") {
    std::vector<uint8_t> molsTwo = mols();
    std::vector<uint8_t> second = mols();
    molsTwo.insert(molsTwo.end(), second.begin(), second.end());
    std::vector<uint8_t> file = groupFile({{"MOLR", u16s({2, 0})},
                                           {"MNLR", u16s({5})},
                                           {"MOLS", molsTwo},
                                           {"MOLP", molp()},
                                           {"MLSS", u32s({0, 2})},
                                           {"MLSP", u32s({0, 1})}});

    wmo::GroupFile g = wmo::parseGroup(file);

    CHECK(g.legacyLightRefs == std::vector<uint16_t>{2, 0});
    CHECK(g.newLightRefs == std::vector<uint16_t>{5});
    REQUIRE(g.spotLights.size() == 2);
    CHECK(g.spotLights[0].lightIndex == 7);
    CHECK(g.spotLights[0].position[2] == 3.0f);
    CHECK(g.spotLights[0].attenuationEnd == 10.0f);
    CHECK(g.spotLights[0].rotation[1] == 0.2f);
    CHECK(g.spotLights[0].outerAngle == 0.75f);
    REQUIRE(g.pointLights.size() == 1);
    CHECK(g.pointLights[0].intensity == 2.0f);
    CHECK(g.pointLights[0].attenuationStart == 3.0f);
    CHECK(g.pointLights[0].attenuationEnd == 9.0f);
    CHECK(g.pointLights[0].unknown28 == -1.0f);
    REQUIRE(g.spotLightSets.size() == 1);
    CHECK(g.spotLightSets[0].count == 2);
}

TEST_CASE("wmo::parseGroup: a light-set window past its records throws with both ranges") {
    std::vector<uint8_t> file = groupFile({{"MOLP", molp()}, {"MLSP", u32s({0, 2})}});
    CHECK_THROWS_WITH_AS(wmo::parseGroup(file), doctest::Contains("expected a window within 1 MOLP records, got offset 0 count 2"),
                         wmo::ParseError);
}
