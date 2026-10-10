#include "wdt_volumes.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include "chunk.hpp"

namespace husk::wdt {

namespace {

constexpr size_t kVfogSize = 104;
constexpr size_t kVfexSize = 96;
constexpr size_t kVfe2Size = 176;
constexpr size_t kVfexFogIdOffset = 0x44;
constexpr size_t kVfe2FogIdOffset = 0x4C;
constexpr uint32_t kMpvVersion = 3;
constexpr size_t kPvmiSize = wmo::kParticulateVolumeSize + 40;
constexpr size_t kPvpdSize = 16;
constexpr size_t kPvbdSize = 64;
constexpr uint32_t kPvbdMaxChildren = 8;

template <typename T>
T read(const uint8_t* data, size_t size, size_t off, const char* what) {
    if (off > size || size - off < sizeof(T)) {
        throw ParseError(std::string(what) + ": read of " + std::to_string(sizeof(T)) + " bytes at offset " +
                         std::to_string(off) + " runs past end (size " + std::to_string(size) + ")");
    }
    T v;
    std::memcpy(&v, data + off, sizeof(T));
    return v;
}

Vec3 readVec3(const uint8_t* data, size_t size, size_t off, const char* what) {
    return read<Vec3>(data, size, off, what);
}

std::vector<Chunk> readChunksReversed(const std::vector<uint8_t>& bytes) {
    std::vector<Chunk> chunks = readChunks(bytes.data(), bytes.size());
    for (Chunk& c : chunks) std::reverse(c.tag.begin(), c.tag.end());
    return chunks;
}

size_t recordCount(const Chunk& c, size_t recordSize) {
    if (c.size % recordSize != 0) {
        throw ParseError(c.tag + ": expected a multiple of " + std::to_string(recordSize) + " bytes, got " +
                         std::to_string(c.size));
    }
    return c.size / recordSize;
}

uint32_t readVersion(const std::vector<Chunk>& chunks, const char* file) {
    std::optional<Chunk> mver = findChunk(chunks, "MVER");
    if (!mver) throw ParseError(std::string(file) + ": no MVER chunk");
    return read<uint32_t>(mver->data, mver->size, 0, "MVER");
}

Fog parseFog(const Chunk& c, size_t o) {
    Fog f;
    f.color = readVec3(c.data, c.size, o + 0x00, "VFOG");
    f.intensity = read<std::array<float, 3>>(c.data, c.size, o + 0x0C, "VFOG");
    f.unknown18 = read<float>(c.data, c.size, o + 0x18, "VFOG");
    f.position = readVec3(c.data, c.size, o + 0x1C, "VFOG");
    f.rotation = read<std::array<float, 4>>(c.data, c.size, o + 0x2C, "VFOG");
    f.radius = read<std::array<float, 3>>(c.data, c.size, o + 0x3C, "VFOG");
    f.animationPeriods = read<std::array<int32_t, 4>>(c.data, c.size, o + 0x48, "VFOG");
    f.flags = read<uint32_t>(c.data, c.size, o + 0x58, "VFOG");
    f.modelFileDataId = read<uint32_t>(c.data, c.size, o + 0x5C, "VFOG");
    f.fogLevel = read<uint32_t>(c.data, c.size, o + 0x60, "VFOG");
    f.id = read<uint32_t>(c.data, c.size, o + 0x64, "VFOG");
    return f;
}

// Attaches each `recordSize` record of `c` to the fog its id at `idOffset`
// names, through `slot`.
void attachExtras(const Chunk& c, size_t recordSize, size_t idOffset, std::vector<Fog>& fogs,
                  std::vector<uint8_t> Fog::*slot) {
    std::map<uint32_t, Fog*> byId;
    for (Fog& f : fogs) byId[f.id] = &f;
    for (size_t i = 0; i < recordCount(c, recordSize); ++i) {
        size_t o = i * recordSize;
        auto id = read<uint32_t>(c.data, c.size, o + idOffset, c.tag.c_str());
        auto it = byId.find(id);
        if (it == byId.end()) {
            throw ParseError(c.tag + "[" + std::to_string(i) + "]: expected the id of a VFOG record, got " +
                             std::to_string(id));
        }
        std::vector<uint8_t>& target = it->second->*slot;
        if (!target.empty()) {
            throw ParseError(c.tag + "[" + std::to_string(i) + "]: expected at most one per VFOG, got a second for id " +
                             std::to_string(id));
        }
        target.assign(c.data + o, c.data + o + recordSize);
    }
}

void requireGroupCounts(const MapParticulateVolume& v, uint32_t declaredPoints, size_t group) {
    if (declaredPoints != v.points.size()) {
        throw ParseError("PVMI group " + std::to_string(group) + ": expected the trailer's PVPD count " +
                         std::to_string(declaredPoints) + " to match the group's " + std::to_string(v.points.size()) +
                         " PVPD records");
    }
    for (size_t b = 0; b < v.boxes.size(); ++b) {
        for (uint32_t index : v.boxes[b].children) {
            if (index >= v.boxes.size()) {
                throw ParseError("PVMI group " + std::to_string(group) + " PVBD[" + std::to_string(b) +
                                 "]: expected child box indices < " + std::to_string(v.boxes.size()) + ", got " +
                                 std::to_string(index));
            }
        }
    }
}

}  // namespace

FogsFile parseFogs(const std::vector<uint8_t>& fileBytes) {
    std::vector<Chunk> chunks = readChunksReversed(fileBytes);
    FogsFile out;
    out.version = readVersion(chunks, "_fogs.wdt");
    for (const Chunk& c : chunks) {
        if (c.tag != "VFOG") continue;
        for (size_t i = 0; i < recordCount(c, kVfogSize); ++i) out.fogs.push_back(parseFog(c, i * kVfogSize));
    }
    for (const Chunk& c : chunks) {
        if (c.tag == "VFEX") attachExtras(c, kVfexSize, kVfexFogIdOffset, out.fogs, &Fog::extra);
        if (c.tag == "VFE2") attachExtras(c, kVfe2Size, kVfe2FogIdOffset, out.fogs, &Fog::extra2);
    }
    for (Fog& f : out.fogs) {
        if (f.extra.empty()) continue;
        const uint8_t* d = f.extra.data();
        size_t n = f.extra.size();
        Fog::Extra extra;
        extra.enabled = read<uint32_t>(d, n, 0x00, "VFEX");
        extra.extent = readVec3(d, n, 0x04, "VFEX");
        extra.parameters = read<std::array<float, 13>>(d, n, 0x10, "VFEX");
        f.extraFields = extra;
    }
    for (Fog& f : out.fogs) {
        if (f.extra2.empty()) continue;
        const uint8_t* d = f.extra2.data();
        size_t n = f.extra2.size();
        Fog::Palette palette;
        palette.colors = {readVec3(d, n, 0x00, "VFE2"), readVec3(d, n, 0x20, "VFE2"), readVec3(d, n, 0x2C, "VFE2")};
        palette.mode = read<float>(d, n, 0x3C, "VFE2");
        f.palette = palette;
    }
    return out;
}

MpvFile parseMpv(const std::vector<uint8_t>& fileBytes) {
    std::vector<Chunk> chunks = readChunksReversed(fileBytes);
    MpvFile out;
    out.version = readVersion(chunks, "_mpv.wdt");
    std::vector<uint32_t> declaredPoints;
    for (const Chunk& c : chunks) {
        if (c.tag == "PVMI") {
            if (out.version != kMpvVersion) {
                throw ParseError("_mpv.wdt MVER: expected " + std::to_string(kMpvVersion) +
                                 " (the only PVMI layout husk reads), got " + std::to_string(out.version));
            }
            if (c.size != kPvmiSize) {
                throw ParseError("PVMI: expected " + std::to_string(kPvmiSize) + " bytes, got " + std::to_string(c.size));
            }
            std::string what = "PVMI[" + std::to_string(out.volumes.size()) + "]";
            MapParticulateVolume v;
            v.volume = wmo::parseParticulateVolume(c.data, c.size, 0, what);
            size_t t = wmo::kParticulateVolumeSize;
            v.boundsMin = readVec3(c.data, c.size, t + 0x00, "PVMI");
            v.boundsMax = readVec3(c.data, c.size, t + 0x0C, "PVMI");
            v.id = read<uint32_t>(c.data, c.size, t + 0x18, "PVMI");
            declaredPoints.push_back(read<uint32_t>(c.data, c.size, t + 0x1C, "PVMI"));
            out.volumes.push_back(std::move(v));
            continue;
        }
        if (c.tag != "PVPD" && c.tag != "PVBD") continue;
        if (out.volumes.empty()) throw ParseError(c.tag + ": expected a PVMI before it, got none");
        MapParticulateVolume& v = out.volumes.back();
        if (c.tag == "PVPD") {
            for (size_t i = 0; i < recordCount(c, kPvpdSize); ++i) {
                size_t o = i * kPvpdSize;
                v.points.push_back({read<std::array<float, 2>>(c.data, c.size, o, "PVPD"),
                                    read<float>(c.data, c.size, o + 0x08, "PVPD"),
                                    read<float>(c.data, c.size, o + 0x0C, "PVPD")});
            }
        } else {
            for (size_t i = 0; i < recordCount(c, kPvbdSize); ++i) {
                size_t o = i * kPvbdSize;
                ParticulateBox box;
                auto count = read<uint32_t>(c.data, c.size, o, "PVBD");
                if (count > kPvbdMaxChildren) {
                    throw ParseError("PVBD[" + std::to_string(i) + "] child count: expected <= " +
                                     std::to_string(kPvbdMaxChildren) + ", got " + std::to_string(count));
                }
                box.min = readVec3(c.data, c.size, o + 0x04, "PVBD");
                box.max = readVec3(c.data, c.size, o + 0x10, "PVBD");
                for (uint32_t k = 0; k < count; ++k) {
                    box.children.push_back(read<uint32_t>(c.data, c.size, o + 0x1C + 4 * k, "PVBD"));
                }
                box.complete = read<uint32_t>(c.data, c.size, o + 0x3C, "PVBD");
                v.boxes.push_back(std::move(box));
            }
        }
    }
    for (size_t g = 0; g < out.volumes.size(); ++g) requireGroupCounts(out.volumes[g], declaredPoints[g], g);
    return out;
}

}  // namespace husk::wdt
