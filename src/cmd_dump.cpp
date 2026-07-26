#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "bone.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "json_writer.hpp"
#include "m2.hpp"

// `husk dump-chunks`: extracts the M2 chunks that don't feed into `husk
// export`'s .glb output (they're mostly rendering-effect/gameplay-metadata
// concerns glTF's own material model has no real equivalent for -- edge
// fade, PBR-ish waterfall shading, parent-model animation overrides,
// player-housing collision, ...) into readable JSON on stdout instead of
// leaving them silently unread. This is deliberately a *separate*
// intermediary format, not a step toward richer glTF export -- see
// README.md's format matrix and Design notes for why these chunks aren't
// "simple" the way attachments/events/lights were (roadmap stage 6's
// follow-on): several have version-ambiguous or explicitly
// wiki-acknowledged-uncertain layouts, and this command's job is to get
// what *is* well-documented into a usable form without pretending the rest
// is understood too -- those are still included, as a raw hex dump plus a
// note explaining why, rather than silently dropped.
namespace husk::commands {

namespace {

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk dump-chunks <file.m2>|<file.bone>\n"
           "\n"
           "Extracts the M2 chunks husk doesn't fold into `export`'s glTF\n"
           "output -- TXAC/EXPT/PABC/PADC/PSBC/PEDC/RPID/GPID/PGD1/WFV3/NERF/\n"
           "EDGF/DBOC/TEXL, all reasonably well documented on wowdev.wiki -- into\n"
           "readable JSON on stdout. Chunks with no documented byte layout, or\n"
           "an internally-inconsistent one (WFV1/WFV2/DPIV/AFRA/DETL/PFDC/PCOL/\n"
           "EXP2) are still included, as a raw hex dump plus a note, not\n"
           "silently skipped. Only applies to Legion+ chunked files -- these\n"
           "chunks don't exist in a pre-Legion flat MD20 file at all.\n"
           "\n"
           "A .bone file (see M2/.skel's BFID chunk) is also accepted --\n"
           "husk dumps its per-bone correction matrices (see src/bone.hpp;\n"
           "this shape isn't documented on wowdev.wiki at all, reverse\n"
           "engineered from real files, so treat field names as inferred).\n";
}

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("couldn't open '" + path + "' for reading");
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        throw std::runtime_error("error reading '" + path + "'");
    }
    return bytes;
}

// Bounds-checked read primitives, same discipline as every other parser in
// this codebase (src/m2.cpp, src/skel.cpp) -- duplicated locally rather
// than exposed from m2.cpp, since these chunks' internal layout is a
// cmd_dump.cpp-only concern (m2::Header doesn't surface any of them, and
// none of them feed husk export -- see this file's own doc comment above).
uint32_t readU32(const uint8_t* d, size_t n, size_t off) {
    if (off + 4 > n) {
        throw std::runtime_error("chunk field at offset " + std::to_string(off) +
                                  " needs 4 bytes but the chunk is only " + std::to_string(n) +
                                  " bytes");
    }
    uint32_t v;
    std::memcpy(&v, d + off, 4);
    return v;
}

uint16_t readU16(const uint8_t* d, size_t n, size_t off) {
    if (off + 2 > n) {
        throw std::runtime_error("chunk field at offset " + std::to_string(off) +
                                  " needs 2 bytes but the chunk is only " + std::to_string(n) +
                                  " bytes");
    }
    uint16_t v;
    std::memcpy(&v, d + off, 2);
    return v;
}

float readF32(const uint8_t* d, size_t n, size_t off) {
    uint32_t bits = readU32(d, n, off);
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

struct ChunkArray {
    uint32_t count = 0;
    uint32_t offset = 0;
};

// Reads an M2Array<T>-shaped (count, offset) pair at `off` -- for the
// chunks whose *entire* documented content is "M2Array<T> fieldname;"
// (PABC/PADC/PSBC/PEDC/PGD1), `off` is 0 (the chunk payload starts with
// this descriptor) and the actual records live at `offset` bytes into this
// same chunk's payload -- the same "chunk carries its own small header
// then raw data, offsets relative to the chunk's own start" shape
// src/skel.cpp already uses for .skel's SKB1 chunk.
ChunkArray readChunkArray(const uint8_t* d, size_t n, size_t off) {
    return {readU32(d, n, off), readU32(d, n, off + 4)};
}

std::string hexDump(const uint8_t* d, size_t n) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) {
        os << std::setw(2) << static_cast<int>(d[i]);
    }
    return os.str();
}

void writeVec3(json::Writer& w, const m2::Vec3& v) {
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(v.x));
    w.key("y");
    w.value(static_cast<double>(v.y));
    w.key("z");
    w.value(static_cast<double>(v.z));
    w.endObject();
}

void dumpRawFallback(json::Writer& w, const Chunk& c, const char* note) {
    w.beginObject();
    w.key("note");
    w.value(note);
    w.key("size");
    w.value(static_cast<int64_t>(c.size));
    w.key("raw_hex");
    w.value(hexDump(c.data, c.size));
    w.endObject();
}

// TXAC (wowdev.wiki M2#TXAC), a flat array of 2-byte records, count =
// materials.count + particle_emitters.count -- husk doesn't validate that
// expected count against the chunk's own actual size (chunk.size / 2), on
// the same "trust the chunk's own byte length over a cross-referenced
// count from elsewhere" principle findFileDataIdArrayChunk (m2.cpp) uses
// for BFID/TXID.
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

// EXPT (wowdev.wiki M2#EXPT): struct{float zSource,colorMult,alphaMult;}
// per particle_emitters entry, 12 bytes/record, count = chunk.size / 12.
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

// PABC (wowdev.wiki M2#PABC): the whole chunk is one M2Array<uint16_t> --
// reuses m2::parseUint16Array directly on the chunk's own payload bytes,
// same as PGD1 below (both are "an array of one uint16 field" in different
// clothes).
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

// PADC (wowdev.wiki M2#PADC): the whole chunk is one
// M2Array<M2TextureWeight> -- reuses m2::parseTextureWeights directly, same
// static-value-only caveat as the header's own texture_weights array (see
// m2::TextureWeight's doc comment).
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

// PSBC (wowdev.wiki M2#PSBC): the whole chunk is one M2Array<M2Bounds>.
// M2Bounds's own field layout isn't individually named anywhere on the
// wiki -- what's used here (a CAaBox min/max plus a float radius, 28 bytes
// total) is inferred by analogy to the header's own identically-shaped
// bounding_box+bounding_sphere_radius pair, and independently confirmed to
// be the right *size* (not necessarily field order/meaning) by the
// M2Sequence stride investigation (see m2.hpp's Sequence doc comment) --
// flagged as an inference, not asserted as confirmed field semantics.
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

// PEDC (wowdev.wiki M2#PEDC): the whole chunk is one M2Array<M2TrackBase>
// -- an M2TrackBase (wowdev.wiki M2#Interpolation) is 12 bytes:
// interpolation_type(u16)/global_sequence(u16)/timestamps
// (M2Array<M2Array<uint32_t>>, 8 bytes). Only the two scalar header fields
// are surfaced -- the nested per-sequence timestamp sub-arrays themselves
// ("every timestamp is an implicit 'fire now'", wowdev.wiki M2#Events)
// aren't resolved, consistent with this command's "readable summary, not
// full resolution" scope.
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

// RPID/GPID (wowdev.wiki M2#RPID/#GPID): flat uint32_t FileDataID arrays,
// one entry per particle_emitters entry -- same shape TXID/BFID already
// use (m2.cpp's findFileDataIdArrayChunk), just not centralized there
// since this file doesn't otherwise touch m2.cpp's internals.
void dumpFileDataIdArrayChunk(json::Writer& w, const Chunk& c) {
    size_t n = c.size / 4;
    w.beginArray();
    for (size_t i = 0; i < n; ++i) {
        w.value(static_cast<int64_t>(readU32(c.data, c.size, i * 4)));
    }
    w.endArray();
}

// WFV3 (wowdev.wiki M2#WFV3), WaterFallDataV3 -- one fixed 80-byte struct
// per chunk (not an array). Field names/order transcribed verbatim from
// the wiki; most are documented only as "passed directly to fragment
// shader" with no further meaning given.
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
    w.key("unk1");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x40)));
    w.key("unk2");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x44)));
    w.key("unk3");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x48)));
    w.key("unk4");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x4C)));
    w.endObject();
}

// NERF (wowdev.wiki M2#NERF): one fixed 8-byte struct, `C2Vector coefs`.
void dumpNerf(json::Writer& w, const Chunk& c) {
    w.beginObject();
    w.key("coefs_x");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x00)));
    w.key("coefs_y");
    w.value(static_cast<double>(readF32(c.data, c.size, 0x04)));
    w.endObject();
}

// EDGF (wowdev.wiki M2#EDGF): 24-byte records, count = chunk.size / 24 --
// the wiki doesn't tie the count to any header array, unlike TXAC/EXPT.
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

// DBOC (wowdev.wiki M2#DBOC): 16-byte records, count = chunk.size / 16 --
// the wiki itself notes ambiguity about whether a given file's DBOC is one
// 16-byte entry or several ("might be 2 entries, might not be"); treating
// it as chunk.size/16 handles both without guessing at semantics beyond
// the record boundary itself.
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

// TEXL (wowdev.wiki M2#TEXL, Midnight/12.0+): 16-byte records, one per
// `lights.count` entry -- unlike DETL's internally-inconsistent offsets or
// WFV1/AFRA's outright-undocumented layout, TEXL's struct (two floats, then
// an index into TXID for the light cookie texture, then one more unknown
// int) is unambiguous, so it gets real structural parsing rather than the
// raw-hex-plus-note fallback below -- was a complete blind spot before
// (FAILURES2.md #5): recognized by cmd_info.cpp's documentedM2ChunkTags
// (so it never tripped the "undocumented chunk" note) but absent from both
// of this file's own lists, meaning a real file's TEXL data was invisible
// to every husk command, not just unparsed.
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

}  // namespace

int dumpChunks(int argc, char** args) {
    if (argc >= 1 && isHelpFlag(args[0])) {
        printUsage(std::cout);
        return 0;
    }
    if (argc != 1) {
        printUsage();
        return 1;
    }

    std::string path = args[0];
    try {
        auto fileBytes = readFileBytes(path);

        // .bone files have no MD20/MD21 magic of their own (see bone.hpp) --
        // sniff for that first so a .bone path doesn't hit m2::parseHeader's
        // "bad magic" error instead of actually being read.
        bool looksLikeM2 = fileBytes.size() >= 4 &&
                            (std::memcmp(fileBytes.data(), "MD20", 4) == 0 ||
                             std::memcmp(fileBytes.data(), "MD21", 4) == 0);
        if (!looksLikeM2) {
            auto corrections = bone::parse(fileBytes);
            json::Writer w(std::cout);
            w.beginArray();
            for (const auto& c : corrections) {
                w.beginObject();
                w.key("bone_index");
                w.value(static_cast<int64_t>(c.boneIndex));
                w.key("matrix_row_major_inferred");
                w.beginArray();
                for (float f : c.matrix) w.value(static_cast<double>(f));
                w.endArray();
                w.endObject();
            }
            w.endArray();
            std::cout << "\n";
            return 0;
        }

        auto header = m2::parseHeader(fileBytes);
        if (!header.chunked) {
            std::cerr << "husk: '" << path
                      << "' is a pre-Legion flat MD20 file -- none of these chunks exist outside "
                         "the Legion+ chunked container\n";
            return 0;
        }
        auto chunks = readChunks(fileBytes.data(), fileBytes.size());

        json::Writer w(std::cout);
        w.beginObject();

        struct Entry {
            const char* tag;
            void (*dump)(json::Writer&, const Chunk&);
        };
        static const Entry kDocumented[] = {
            {"TXAC", dumpTxac},          {"EXPT", dumpExpt},   {"PABC", dumpU16ArrayChunk},
            {"PADC", dumpPadc},          {"PSBC", dumpPsbc},   {"PEDC", dumpPedc},
            {"RPID", dumpFileDataIdArrayChunk}, {"GPID", dumpFileDataIdArrayChunk},
            {"PGD1", dumpU16ArrayChunk}, {"WFV3", dumpWfv3},   {"NERF", dumpNerf},
            {"EDGF", dumpEdgf},          {"DBOC", dumpDboc},   {"TEXL", dumpTexl},
        };
        for (const auto& e : kDocumented) {
            auto c = findChunk(chunks, e.tag);
            if (!c) continue;
            w.key(e.tag);
            e.dump(w, *c);
        }

        struct FallbackEntry {
            const char* tag;
            const char* note;
        };
        static const FallbackEntry kFallback[] = {
            {"WFV1", "structure not documented on wowdev.wiki (\"// unknown\") as of the "
                     "2026-07-25 fetch"},
            {"WFV2", "structure not documented on wowdev.wiki (\"// unknown\") as of the "
                     "2026-07-25 fetch"},
            {"DPIV", "structure not documented on wowdev.wiki (\"Unknown, seemingly always 32 "
                     "bytes, mostly empty\") as of the 2026-07-25 fetch"},
            {"AFRA", "structure not documented on wowdev.wiki (\"Not observed in any files yet\") "
                     "as of the 2026-07-25 fetch"},
            {"DETL", "wowdev.wiki's own byte offsets for this struct don't add up to its stated "
                     "field list (0x0a end offset vs. fields summing to 0x0c) -- not parsed "
                     "structurally without real data to resolve the discrepancy against"},
            {"PFDC", "embeds a .phys-shaped PHYS sub-structure that itself has no documented byte "
                     "layout on wowdev.wiki"},
            {"PCOL", "wowdev.wiki flags this struct as \"Preliminary structure as per Zee's "
                     "research\" -- not treated as settled enough to parse structurally"},
            {"EXP2", "contains a nested M2PartTrack<fixed16> whose own byte layout isn't given on "
                     "wowdev.wiki"},
        };
        for (const auto& e : kFallback) {
            auto c = findChunk(chunks, e.tag);
            if (!c) continue;
            w.key(e.tag);
            dumpRawFallback(w, *c, e.note);
        }

        w.endObject();
        std::cout << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: dump-chunks failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
