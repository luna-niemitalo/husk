#include "dump_chunks_misc.hpp"

#include <algorithm>

#include "dump_phys.hpp"
#include "dump_writer_utils.hpp"
#include "m2.hpp"
#include "phys.hpp"

namespace husk::commands {

void dumpTxac(json::Writer& w, const Chunk& c) {
    size_t n = c.size / 2;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        w.beginObject();
        w.key("unk0");
        w.value(static_cast<int64_t>(c.data[i * 2 + 0]));
        w.key("unk1");
        w.value(static_cast<int64_t>(c.data[i * 2 + 1]));
        w.endObject();
    }
    w.endArray();
}

void dumpExpt(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 12;
    size_t n = c.size / kSize;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        w.key("zSource");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x00)));
        w.key("colorMult");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x04)));
        w.key("alphaMult");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x08)));
        w.endObject();
    }
    w.endArray();
}

void dumpU16ArrayChunk(json::Writer& w, const Chunk& c) {
    ChunkArray arr = readChunkArray(c.data, c.size, 0);
    std::vector<uint8_t> payload(c.data, c.data + c.size);
    auto values = m2::parseUint16Array(payload, m2::Array{arr.count, arr.offset});
    w.beginArray();
    for (uint16_t v : values) {
        w.value(static_cast<int64_t>(v));
    }
    w.endArray();
}

void dumpPadc(json::Writer& w, const Chunk& c) {
    ChunkArray arr = readChunkArray(c.data, c.size, 0);
    std::vector<uint8_t> payload(c.data, c.data + c.size);
    auto weights = m2::parseTextureWeights(payload, m2::Array{arr.count, arr.offset});
    w.beginArray();
    for (const auto& tw : weights) {
        w.beginObject();
        w.key("weight");
        if (tw.weight) {
            w.value(static_cast<double>(*tw.weight));
        } else {
            w.nullValue();
        }
        w.endObject();
    }
    w.endArray();
}

void dumpPsbc(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 28;  // CAaBox (24) + float radius (4)
    ChunkArray arr = readChunkArray(c.data, c.size, 0);
    w.beginArray();
    for (uint32_t i = 0; i < arr.count; ++i) {
        size_t off = static_cast<size_t>(arr.offset) + static_cast<size_t>(i) * kSize;
        m2::Vec3 min{readF32(c.data, c.size, off + 0x00), readF32(c.data, c.size, off + 0x04),
                     readF32(c.data, c.size, off + 0x08)};
        m2::Vec3 max{readF32(c.data, c.size, off + 0x0C), readF32(c.data, c.size, off + 0x10),
                     readF32(c.data, c.size, off + 0x14)};
        float radius = readF32(c.data, c.size, off + 0x18);
        w.beginObject();
        w.key("min_inferred");
        writeVec3(w, min);
        w.key("max_inferred");
        writeVec3(w, max);
        w.key("radius_inferred");
        w.value(static_cast<double>(radius));
        w.endObject();
    }
    w.endArray();
}

void dumpPedc(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 12;
    ChunkArray arr = readChunkArray(c.data, c.size, 0);
    w.beginArray();
    for (uint32_t i = 0; i < arr.count; ++i) {
        size_t off = static_cast<size_t>(arr.offset) + static_cast<size_t>(i) * kSize;
        w.beginObject();
        w.key("interpolation_type");
        w.value(static_cast<int64_t>(readU16(c.data, c.size, off + 0x00)));
        w.key("global_sequence");
        w.value(static_cast<int64_t>(readU16(c.data, c.size, off + 0x02)));
        w.endObject();
    }
    w.endArray();
}

void dumpFileDataIdArrayChunk(json::Writer& w, const Chunk& c) {
    size_t n = c.size / 4;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        w.value(static_cast<int64_t>(readU32(c.data, c.size, i * 4)));
    }
    w.endArray();
}

void dumpWfv3(json::Writer& w, const Chunk& c) {
    w.beginObject();
    w.key("bumpScale");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x00)));
    w.key("value0_x");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x04)));
    w.key("value0_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x08)));
    w.key("value0_z");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x0C)));
    w.key("value1_w");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x10)));
    w.key("value0_w");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x14)));
    w.key("value1_x");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x18)));
    w.key("value1_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x1C)));
    w.key("value2_w");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x20)));
    w.key("value3_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x24)));
    w.key("value3_x");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x28)));
    w.key("basecolor_rgba");
    w.beginArray();
    for (int i = 0; i < 4; ++i) w.value(static_cast<int64_t>(c.data[0x2C + i]));
    w.endArray();
    w.key("flags");
    w.value(static_cast<int64_t>(readU16(c.data, c.size, 0x30)));
    w.key("unk0");
    w.value(static_cast<int64_t>(readU16(c.data, c.size, 0x32)));
    w.key("values3_w");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x34)));
    w.key("values3_z");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x38)));
    w.key("values4_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x3C)));
    auto writeOptionalF32 = [&](const char* key, size_t off) {
        w.key(key);
        if (c.size >= 0x50) {
            w.value(static_cast<double>(readF32(c.data, c.size, off)));
        } else {
            w.nullValue();
        }
    };
    writeOptionalF32("unk1", 0x40);
    writeOptionalF32("unk2", 0x44);
    writeOptionalF32("unk3", 0x48);
    writeOptionalF32("unk4", 0x4C);
    w.endObject();
}

void dumpNerf(json::Writer& w, const Chunk& c) {
    w.beginObject();
    w.key("coefs_x");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x00)));
    w.key("coefs_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x04)));
    w.endObject();
}

void dumpEdgf(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 24;
    size_t n = c.size / kSize;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        w.key("_0x0_0");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x00)));
        w.key("_0x0_1");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x04)));
        w.key("_0x8");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x08)));
        w.key("_0xC_hex");
        w.value(hexDump(c.data + off + 0x0C, 0x0C));
        w.endObject();
    }
    w.endArray();
}

void dumpDboc(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 16;
    size_t n = c.size / kSize;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        w.key("unk1_1");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x00)));
        w.key("unk1_2");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x04)));
        w.key("unk1_3");
        w.value(static_cast<int64_t>(readU32(c.data, c.size, off + 0x08)));
        w.key("unk1_4");
        w.value(static_cast<int64_t>(readU32(c.data, c.size, off + 0x0C)));
        w.endObject();
    }
    w.endArray();
}

void dumpTexl(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 16;
    size_t n = c.size / kSize;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        w.key("unk0");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x00)));
        w.key("unk1");
        w.value(static_cast<double>(readF32(c.data, c.size, off + 0x04)));
        w.key("texture_lookup");
        w.value(static_cast<int64_t>(readU32(c.data, c.size, off + 0x08)));
        w.key("unk2");
        w.value(static_cast<int64_t>(readU32(c.data, c.size, off + 0x0C)));
        w.endObject();
    }
    w.endArray();
}

void dumpAfra(json::Writer& w, const Chunk& c) {
    w.beginObject();
    w.key("value");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x00)));
    w.key("unk1");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x04)));
    w.key("unk2");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x08)));
    w.key("unk3");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x0C)));
    w.endObject();
}

void dumpDpiv(json::Writer& w, const Chunk& c) {
    constexpr size_t kSize = 32;
    size_t n = c.size / kSize;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        for (int f = 0; f < 8; ++f) {
            w.key("field_" + std::to_string(f));
            w.value(static_cast<double>(readF32(c.data, c.size, off + f * 4)));
        }
        w.endObject();
    }
    w.endArray();
}

void dumpWfv1(json::Writer& w, const Chunk& c) {
    w.beginObject();
    w.key("value");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x00)));
    w.key("unk1");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x04)));
    w.key("unk2");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x08)));
    w.key("unk3");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x0C)));
    w.endObject();
}

void dumpWfv2(json::Writer& w, const Chunk& c) {
    constexpr size_t kFloatCount = 16;
    w.beginArray();
    for (size_t i = 0; i < kFloatCount; ++i) {
        w.value(static_cast<double>(readF32(c.data, c.size, i * 4)));
    }
    w.endArray();
}

void dumpDetl(json::Writer& w, const Chunk& c, uint32_t lightCount) {
    constexpr size_t kSize = 12;
    size_t n = std::min(static_cast<size_t>(lightCount), c.size / kSize);
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        size_t off = i * kSize;
        w.beginObject();
        w.key("flags");
        w.value(static_cast<int64_t>(readU16(c.data, c.size, off + 0x00)));
        w.key("scale");
        w.value(static_cast<double>(readHalfFloat(readU16(c.data, c.size, off + 0x02))));
        w.key("diffuse_color_multiplier");
        w.value(static_cast<double>(readHalfFloat(readU16(c.data, c.size, off + 0x04))));
        w.key("unk0");
        w.value(static_cast<int64_t>(readU16(c.data, c.size, off + 0x06)));
        w.key("unk1");
        w.value(static_cast<int64_t>(readU32(c.data, c.size, off + 0x08)));
        w.endObject();
    }
    w.endArray();
}

void dumpPfdc(json::Writer& w, const Chunk& c) {
    size_t len = physPayloadRealLength(c.data, c.size);
    std::vector<uint8_t> physBytes(c.data, c.data + len);
    auto physFile = phys::parse(physBytes);
    writePhysFile(w, physFile);
}

void dumpExp2(json::Writer& w, const Chunk& c) {
    ChunkArray arr = readChunkArray(c.data, c.size, 0);
    std::vector<uint8_t> payload(c.data, c.data + c.size);
    auto particles = m2::parseExtendedParticles(payload, m2::Array{arr.count, arr.offset});
    w.beginArray();
    for (const auto& p : particles) {
        w.beginObject();
        w.key("z_source");
        w.value(static_cast<double>(p.zSource));
        w.key("color_mult");
        w.value(static_cast<double>(p.colorMult));
        w.key("alpha_mult");
        w.value(static_cast<double>(p.alphaMult));
        w.key("alpha_cutoff_track");
        w.beginArray();
        for (const auto& [lifeFraction, cutoff] :
             m2::resolveFixed16PartTrack(payload, p.alphaCutoffOffset)) {
            w.beginObject();
            w.key("life_fraction");
            w.value(static_cast<double>(lifeFraction));
            w.key("alpha_cutoff");
            w.value(static_cast<double>(cutoff));
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
}

void dumpPcol(json::Writer& w, const Chunk& c) {
    uint32_t vertexPosCount = readU32(c.data, c.size, 0x00);
    uint32_t vertexPosOffset = readU32(c.data, c.size, 0x04);
    uint32_t faceNormCount = readU32(c.data, c.size, 0x08);
    uint32_t faceNormOffset = readU32(c.data, c.size, 0x0C);
    uint32_t indexCount = readU32(c.data, c.size, 0x10);
    uint32_t indexOffset = readU32(c.data, c.size, 0x14);
    uint32_t flagsCount = readU32(c.data, c.size, 0x18);
    uint32_t flagsOffset = readU32(c.data, c.size, 0x1C);

    w.beginObject();

    w.key("vertex_positions");
    w.beginArray();
    for (uint32_t i = 0; i < vertexPosCount; ++i) {
        size_t off = static_cast<size_t>(vertexPosOffset) + static_cast<size_t>(i) * 12;
        writeVec3(w, m2::Vec3{readF32(c.data, c.size, off + 0x00), readF32(c.data, c.size, off + 0x04),
                               readF32(c.data, c.size, off + 0x08)});
    }
    w.endArray();

    w.key("face_normals");
    w.beginArray();
    for (uint32_t i = 0; i < faceNormCount; ++i) {
        size_t off = static_cast<size_t>(faceNormOffset) + static_cast<size_t>(i) * 12;
        writeVec3(w, m2::Vec3{readF32(c.data, c.size, off + 0x00), readF32(c.data, c.size, off + 0x04),
                               readF32(c.data, c.size, off + 0x08)});
    }
    w.endArray();

    w.key("indices");
    w.beginArray();
    for (uint32_t i = 0; i < indexCount; ++i) {
        size_t off = static_cast<size_t>(indexOffset) + static_cast<size_t>(i) * 2;
        w.value(static_cast<int64_t>(static_cast<int16_t>(readU16(c.data, c.size, off))));
    }
    w.endArray();

    w.key("flags");
    w.beginArray();
    for (uint32_t i = 0; i < flagsCount; ++i) {
        size_t off = static_cast<size_t>(flagsOffset) + static_cast<size_t>(i) * 2;
        w.value(static_cast<int64_t>(static_cast<int16_t>(readU16(c.data, c.size, off))));
    }
    w.endArray();

    w.endObject();
}

}  // namespace husk::commands
