// wdt::parseFogs / wdt::parseMpv: synthetic files built from the layouts
// in WIKI_FINDINGS/WORLD.md, plus the real map 2656 sidecars.

#include <doctest/doctest.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "test_data_paths.hpp"
#include "wdt_volumes.hpp"

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

// Appends a chunk with its tag byte-reversed, as WDT stores it.
void appendChunk(std::vector<uint8_t>& out, const std::string& tag, const std::vector<uint8_t>& payload) {
    out.insert(out.end(), tag.rbegin(), tag.rend());
    put(out, out.size(), static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> file(uint32_t version, const std::vector<std::pair<std::string, std::vector<uint8_t>>>& chunks) {
    std::vector<uint8_t> out;
    std::vector<uint8_t> mver;
    put(mver, 0, version);
    appendChunk(out, "MVER", mver);
    for (const auto& [tag, payload] : chunks) appendChunk(out, tag, payload);
    return out;
}

std::vector<uint8_t> vfog(uint32_t id, uint32_t model) {
    std::vector<uint8_t> r(104, 0);
    put(r, 0x00, 0.5f);
    put(r, 0x1C, 100.0f);
    put(r, 0x38, 1.0f);  // rotation w
    put(r, 0x3C, 950.0f);
    put<int32_t>(r, 0x48, 3000);
    put<uint32_t>(r, 0x58, 3);
    put<uint32_t>(r, 0x5C, model);
    put<uint32_t>(r, 0x60, 1);
    put<uint32_t>(r, 0x64, id);
    return r;
}

std::vector<uint8_t> withId(size_t size, size_t idOffset, uint32_t id) {
    std::vector<uint8_t> r(size, 0);
    put(r, idOffset, id);
    return r;
}

std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// A PVMI blob: one emitter (texture 77), trailer AABB, id and PVPD count.
std::vector<uint8_t> pvmi(uint32_t id, uint32_t pointCount) {
    std::vector<uint8_t> r(4312, 0);
    put<uint32_t>(r, 0x00, 3);
    put(r, 0x08, 10.0f);
    put<uint32_t>(r, 0x2D0 + 0x94, 77);
    put<uint32_t>(r, 0x2D0 + 0x98, 2);
    put<uint32_t>(r, 0x10AC, 1);
    put(r, 0x10B0, -5.0f);
    put(r, 0x10BC, 25.0f);
    put<uint32_t>(r, 0x10C8, id);
    put<uint32_t>(r, 0x10CC, pointCount);
    return r;
}

std::vector<uint8_t> pvpd(size_t count) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < count; ++i) {
        put(out, out.size(), 1.0f);
        put(out, out.size(), 0.0f);
        put(out, out.size(), -0.0f);
        put(out, out.size(), static_cast<float>(i));
    }
    return out;
}

// A PVBD record naming `children`.
std::vector<uint8_t> pvbd(const std::vector<uint32_t>& children, uint32_t complete = 1) {
    std::vector<uint8_t> r(64, 0);
    put(r, 0x00, static_cast<uint32_t>(children.size()));
    put(r, 0x04, -1.0f);
    put(r, 0x10, 1.0f);
    for (size_t k = 0; k < children.size(); ++k) put(r, 0x1C + 4 * k, children[k]);
    put(r, 0x3C, complete);
    return r;
}

}  // namespace

TEST_CASE("wdt::parseFogs: VFOG fields, with VFEX and VFE2 attached by VFOG id, not by position") {
    std::vector<uint8_t> vfe2 = withId(176, 0x4C, 11);
    put(vfe2, 0x00, 1.0f);
    put(vfe2, 0x20, 0.5f);
    put(vfe2, 0x34, 0.25f);
    put(vfe2, 0x3C, 2.0f);
    std::vector<uint8_t> vfex = withId(96, 0x44, 10);
    put<uint32_t>(vfex, 0x00, 1);
    put(vfex, 0x04, 600.0f);
    put(vfex, 0x10, 0.4f);
    put(vfex, 0x14, -65.0f);
    put(vfex, 0x28, 0.1f);
    put(vfex, 0x2C, 25.0f);
    put(vfex, 0x40, 10.0f);
    wdt::FogsFile f = wdt::parseFogs(file(2, {{"VFOG", concat(vfog(10, 0), vfog(11, 1728356))},
                                              {"VFEX", concat(withId(96, 0x44, 11), vfex)},
                                              {"VFE2", vfe2}}));
    CHECK(f.version == 2);
    REQUIRE(f.fogs.size() == 2);
    const wdt::Fog& a = f.fogs[0];
    CHECK(a.id == 10);
    CHECK(a.color == wdt::Vec3{0.5f, 0, 0});
    CHECK(a.position == wdt::Vec3{100, 0, 0});
    CHECK(a.rotation == std::array<float, 4>{0, 0, 0, 1});
    CHECK(a.radius[0] == 950.0f);
    CHECK(a.animationPeriods[0] == 3000);
    CHECK(a.flags == 3);
    CHECK(a.fogLevel == 1);
    REQUIRE(a.extra.size() == 96);
    REQUIRE(a.extraFields.has_value());
    CHECK(a.extraFields->enabled == 1);
    CHECK(a.extraFields->extent == wdt::Vec3{600, 0, 0});
    CHECK(a.extraFields->parameters[0] == 0.4f);
    CHECK(a.extraFields->parameters[1] == -65.0f);
    CHECK(a.extraFields->parameters[6] == 0.1f);
    CHECK(a.extraFields->parameters[7] == 25.0f);
    CHECK(a.extraFields->parameters[12] == 10.0f);
    CHECK(a.extra2.empty());
    CHECK_FALSE(a.palette.has_value());
    const wdt::Fog& b = f.fogs[1];
    CHECK(b.modelFileDataId == 1728356);
    CHECK(b.extra.size() == 96);
    REQUIRE(b.palette.has_value());
    CHECK(b.palette->colors[0] == wdt::Vec3{1, 0, 0});
    CHECK(b.palette->colors[1] == wdt::Vec3{0.5f, 0, 0});
    CHECK(b.palette->colors[2] == wdt::Vec3{0, 0, 0.25f});
    CHECK(b.palette->mode == 2.0f);
}

TEST_CASE("wdt::parseFogs: an extra naming no VFOG, or a second extra for one VFOG, throws") {
    CHECK_THROWS_WITH_AS(wdt::parseFogs(file(2, {{"VFOG", vfog(10, 0)}, {"VFEX", withId(96, 0x44, 12)}})),
                         "VFEX[0]: expected the id of a VFOG record, got 12", wdt::ParseError);
    CHECK_THROWS_WITH_AS(
        wdt::parseFogs(file(2, {{"VFOG", vfog(10, 0)}, {"VFE2", concat(withId(176, 0x4C, 10), withId(176, 0x4C, 10))}})),
        "VFE2[1]: expected at most one per VFOG, got a second for id 10", wdt::ParseError);
    CHECK_THROWS_WITH_AS(wdt::parseFogs(file(2, {{"VFOG", std::vector<uint8_t>(100, 0)}})),
                         "VFOG: expected a multiple of 104 bytes, got 100", wdt::ParseError);
}

TEST_CASE("wdt::parseMpv: repeating PVMI/PVPD/PVBD groups, each PVMI the shared record plus its map trailer") {
    wdt::MpvFile m = wdt::parseMpv(file(3, {{"PVMI", pvmi(185, 2)},
                                            {"PVPD", pvpd(2)},
                                            {"PVBD", concat(concat(pvbd({}), pvbd({})), pvbd({0, 1}))},
                                            {"PVMI", pvmi(186, 1)},
                                            {"PVPD", pvpd(1)},
                                            {"PVBD", concat(pvbd({}), pvbd({0}))}}));
    REQUIRE(m.volumes.size() == 2);
    const wdt::MapParticulateVolume& v = m.volumes[0];
    CHECK(v.id == 185);
    CHECK(v.boundsMin == wdt::Vec3{-5, 0, 0});
    CHECK(v.boundsMax == wdt::Vec3{25, 0, 0});
    CHECK(v.volume.position == wmo::Vec3{10, 0, 0});
    REQUIRE(v.volume.emitters.size() == 1);
    CHECK(v.volume.emitters[0].textureFileDataId == 77);
    REQUIRE(v.points.size() == 2);
    CHECK(v.points[1].direction == std::array<float, 2>{1, 0});
    CHECK(v.points[1].unknown0C == 1.0f);
    REQUIRE(v.boxes.size() == 3);
    CHECK(v.boxes[0].children.empty());
    CHECK(v.boxes[2].children == std::vector<uint32_t>{0, 1});
    CHECK(v.boxes[2].min == wdt::Vec3{-1, 0, 0});
    CHECK(m.volumes[1].points.size() == 1);
    CHECK(m.volumes[1].boxes.size() == 2);
}

TEST_CASE("wdt::parseMpv: an empty stub reads under any MVER; a PVMI needs MVER 3") {
    CHECK(wdt::parseMpv(file(1, {})).volumes.empty());
    CHECK_THROWS_WITH_AS(wdt::parseMpv(file(2, {{"PVMI", pvmi(1, 0)}})),
                         "_mpv.wdt MVER: expected 3 (the only PVMI layout husk reads), got 2", wdt::ParseError);
}

TEST_CASE("wdt::parseMpv: structural violations throw with expected and actual") {
    CHECK_THROWS_WITH_AS(wdt::parseMpv(file(3, {{"PVPD", pvpd(1)}})), "PVPD: expected a PVMI before it, got none",
                         wdt::ParseError);
    CHECK_THROWS_WITH_AS(wdt::parseMpv(file(3, {{"PVMI", std::vector<uint8_t>(4272, 0)}})),
                         "PVMI: expected 4312 bytes, got 4272", wdt::ParseError);
    CHECK_THROWS_WITH_AS(wdt::parseMpv(file(3, {{"PVMI", pvmi(1, 3)}, {"PVPD", pvpd(2)}})),
                         "PVMI group 0: expected the trailer's PVPD count 3 to match the group's 2 PVPD records",
                         wdt::ParseError);
    CHECK_THROWS_WITH_AS(
        wdt::parseMpv(file(3, {{"PVMI", pvmi(1, 1)}, {"PVPD", pvpd(1)}, {"PVBD", concat(pvbd({}), pvbd({0, 2}))}})),
        "PVMI group 0 PVBD[1]: expected child box indices < 2, got 2", wdt::ParseError);
    std::vector<uint8_t> tooMany = pvbd({});
    put<uint32_t>(tooMany, 0, 9);
    CHECK_THROWS_WITH_AS(wdt::parseMpv(file(3, {{"PVMI", pvmi(1, 0)}, {"PVBD", tooMany}})),
                         "PVBD[0] child count: expected <= 8, got 9", wdt::ParseError);
}

TEST_CASE("wdt: real map 2656 -- six paired fogs, five particulate groups, one shared-leaf hierarchy" *
          doctest::skip(test::testMapFogs().empty() || test::testMapMpv().empty())) {
    wdt::FogsFile fogs = wdt::parseFogs(readFile(test::testMapFogs()));
    REQUIRE(fogs.fogs.size() == 6);
    for (const wdt::Fog& f : fogs.fogs) {
        CAPTURE(f.id);
        CHECK(f.extra.size() == 96);
        REQUIRE(f.extraFields.has_value());
        for (int axis = 0; axis < 3; ++axis) CHECK(f.extraFields->extent[axis] > 0.0f);
        REQUIRE(f.palette.has_value());
        CHECK((f.palette->mode == 1.0f || f.palette->mode == 2.0f));
    }

    wdt::MpvFile mpv = wdt::parseMpv(readFile(test::testMapMpv()));
    REQUIRE(mpv.volumes.size() == 5);
    for (const wdt::MapParticulateVolume& v : mpv.volumes) {
        CAPTURE(v.id);
        // Leaves first, one per point; every later box has children.
        REQUIRE(v.boxes.size() > v.points.size());
        for (size_t b = 0; b < v.boxes.size(); ++b) CHECK(v.boxes[b].children.empty() == (b < v.points.size()));
        for (int axis = 0; axis < 3; ++axis) CHECK(v.boundsMin[axis] <= v.boundsMax[axis]);
    }
    // Group 1: root box 10 joins inner boxes 11..14, which can only be
    // boxes -- the group has 10 points.
    const wdt::MapParticulateVolume& joined = mpv.volumes[1];
    REQUIRE(joined.points.size() == 10);
    CHECK(joined.boxes[10].children == std::vector<uint32_t>{11, 12, 13, 14});
    CHECK(joined.boxes[10].complete == 0);
}
