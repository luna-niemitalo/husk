#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "bone.hpp"
#include "chunk.hpp"
#include "commands.hpp"
#include "json_writer.hpp"
#include "m2.hpp"
#include "phys.hpp"

// `husk dump-chunks`: extracts M2 data that doesn't feed into `husk
// export`'s .glb output into readable JSON on stdout instead of leaving it
// silently unread. Two categories, both real:
//
// - Legion+-only chunks (TXAC/EXPT/PABC/PADC/PSBC/PEDC/RPID/GPID/PGD1/
//   WFV3/NERF/EDGF/DBOC/TEXL, all reasonably well documented on
//   wowdev.wiki) -- rendering-effect/gameplay-metadata concerns glTF's own
//   material model has no real equivalent for (edge fade, PBR-ish
//   waterfall shading, parent-model animation overrides, player-housing
//   collision, ...). Chunks with no documented byte layout, or an
//   internally-inconsistent one (WFV1/WFV2/DPIV/AFRA/DETL/PFDC/PCOL/EXP2),
//   are still included as a raw hex dump plus a note, not silently
//   dropped. Only present in Legion+ chunked files.
// - `ribbon_emitters`/`particle_emitters` -- core MD20 header arrays
//   present in every version, not Legion+ chunks, but the same "no glTF
//   slot" rationale applies: procedural emitter systems, not renderable
//   geometry (see DESIGN.md's Key design decisions). Originally out of
//   scope for this command (chunk tags only); broadened once real
//   particle/ribbon-bearing files (weapon models) made full-field/curve
//   parsing possible to verify -- see WIKI_FINDINGS.md. `husk export`
//   still attaches a minimal position/bone anchor to the .glb itself (see
//   gltf::Skeleton::RibbonAnchor/ParticleAnchor), so a consumer that only
//   needs placement doesn't need this command at all; this is the home for
//   every other field and fully-resolved animation curve.
//
// A `.phys` file, passed directly (like `.bone`), also gets its full
// body/shape/joint/PHYV record set dumped here -- same ".glb gets a
// minimal placement anchor, this command gets everything else" split as
// ribbon/particle emitters above (see gltf::Skeleton::PhysicsBody's doc
// comment, DESIGN.md's Key design decisions).
//
// This is deliberately a *separate* intermediary format, not a step toward
// richer glTF export -- see README.md's format matrix and Design notes.
namespace husk::commands {

namespace {

void printUsage(std::ostream& out = std::cerr) {
    out << "usage: husk dump-chunks <file.m2>|<file.bone>|<file.phys>\n"
           "\n"
           "Extracts M2 data husk doesn't fold into `export`'s glTF output --\n"
           "into readable JSON on stdout:\n"
           "\n"
           "  ribbon_emitters/particle_emitters: every field, and every fully\n"
           "  resolved animation curve, for the model's own M2Ribbon/M2Particle\n"
           "  records (procedural emitter data -- no native glTF representation).\n"
           "  Present in every M2 version. particle_emitters is count-only below\n"
           "  Cataclysm (see m2::kMinVerifiedParticleVersion).\n"
           "\n"
           "  Legion+ chunk tags -- TXAC/EXPT/PABC/PADC/PSBC/PEDC/RPID/GPID/PGD1/\n"
           "  WFV3/NERF/EDGF/DBOC/TEXL, all reasonably well documented on\n"
           "  wowdev.wiki. Chunks with no documented byte layout, or an\n"
           "  internally-inconsistent one (WFV1/WFV2/DPIV/AFRA/DETL/PFDC/PCOL/\n"
           "  EXP2), are still included, as a raw hex dump plus a note, not\n"
           "  silently skipped. Only present in Legion+ chunked files.\n"
           "\n"
           "A .bone file (see M2/.skel's BFID chunk) is also accepted --\n"
           "husk dumps its per-bone correction matrices (see src/bone.hpp;\n"
           "this shape isn't documented on wowdev.wiki at all, reverse\n"
           "engineered from real files, so treat field names as inferred).\n"
           "\n"
           "A .phys file (see M2's PFID chunk) is also accepted -- husk dumps\n"
           "every body/shape/joint/PHYV record, each shape/joint resolved to\n"
           "its real type-specific data inline (see src/phys.hpp; the byte\n"
           "layout is documented on wowdev.wiki, verified against real files,\n"
           "see WIKI_FINDINGS.md §9). `husk export --phys` attaches only a\n"
           "minimal per-body placement anchor to the .glb itself -- this is\n"
           "the home for everything else.\n";
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
// per chunk (not an array), *except* a real, consistent 64-byte variant
// found across all 9 real corpus files that carry it (Shadowlands "Maw"
// zone waterfall doodads, world/expansion08/doodads/maw/*.m2, see
// WIKI_FINDINGS.md §8): every one is missing exactly the trailing 4 floats
// (unk1-unk4, the last 16 bytes of the 80-byte struct), never truncated
// anywhere else. Reads those four conditionally on `c.size >= 80`,
// emitting `null` (same "genuinely absent, not a parse failure" treatment
// dumpTextureWeights's optional weight/alpha fields already use) for the
// shorter variant instead of throwing -- consistent with this codebase's
// "don't guess, dump what data actually exists" rule. Field names/order
// for the rest transcribed verbatim from the wiki; most are documented
// only as "passed directly to fragment shader" with no further meaning
// given.
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

// Shared "write one M2Track<T>'s fully-resolved curve" shape: interpolation
// type + global sequence, then either every non-empty M2Sequence's own
// keyframes (indexed 0..sequenceCount-1, skipping sequences with no data
// for this specific track -- most don't) or, for a global-sequence-driven
// track, its single independent curve. `writeValue` serializes one decoded
// keyframe value; `resolveSeq`/`resolveGlobal` are one of
// resolveVec3TrackSequence/resolveFloatTrackSequence/
// resolveRawIntTrackSequence and its GlobalSequenceTrack counterpart.
template <typename T, typename ResolveSeqFn, typename ResolveGlobalFn, typename WriteValueFn>
void writeTrackCurve(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                      uint32_t sequenceCount, ResolveSeqFn resolveSeq, ResolveGlobalFn resolveGlobal,
                      WriteValueFn writeValue) {
    m2::TrackMeta meta = m2::readTrackMeta(blob, trackOffset);
    w.beginObject();
    w.key("interpolation_type");
    w.value(static_cast<int64_t>(meta.interpolationType));

    auto writeKeyframes = [&](const std::vector<std::pair<uint32_t, T>>& kfs) {
        w.beginArray();
        for (const auto& [ts, v] : kfs) {
            w.beginObject();
            w.key("time_ms");
            w.value(static_cast<int64_t>(ts));
            w.key("value");
            writeValue(w, v);
            w.endObject();
        }
        w.endArray();
    };

    if (meta.globalSequence == m2::TrackMeta::kNoGlobalSequence) {
        w.key("global_sequence");
        w.nullValue();
        w.key("sequences");
        w.beginArray();
        for (uint32_t seq = 0; seq < sequenceCount; ++seq) {
            auto kfs = resolveSeq(blob, trackOffset, seq, nullptr);
            if (kfs.empty()) continue;
            w.beginObject();
            w.key("sequence_index");
            w.value(static_cast<int64_t>(seq));
            w.key("keyframes");
            writeKeyframes(kfs);
            w.endObject();
        }
        w.endArray();
    } else {
        w.key("global_sequence");
        w.value(static_cast<int64_t>(meta.globalSequence));
        w.key("keyframes");
        writeKeyframes(resolveGlobal(blob, trackOffset, nullptr));
    }
    w.endObject();
}

void writeFloatTrack(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                      uint32_t sequenceCount) {
    writeTrackCurve<float>(
        w, blob, trackOffset, sequenceCount, m2::resolveFloatTrackSequence,
        m2::resolveFloatGlobalSequenceTrack,
        [](json::Writer& w2, float v) { w2.value(static_cast<double>(v)); });
}

void writeVec3Track(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                     uint32_t sequenceCount) {
    writeTrackCurve<m2::Vec3>(w, blob, trackOffset, sequenceCount, m2::resolveVec3TrackSequence,
                               m2::resolveVec3GlobalSequenceTrack,
                               [](json::Writer& w2, const m2::Vec3& v) { writeVec3(w2, v); });
}

// `elementSize` is 1 (uint8_t) or 2 (uint16_t/fixed16); `writeValue` decides
// how the raw zero-extended uint32_t gets serialized -- as-is for a plain
// index/flag (texSlotTrack/visibilityTrack/enabledIn), or scaled to a
// 0.0..1.0 float the same way readFixed16TrackValue does (alphaTrack).
void writeRawIntTrack(json::Writer& w, const std::vector<uint8_t>& blob, uint32_t trackOffset,
                       uint32_t sequenceCount, size_t elementSize,
                       const std::function<void(json::Writer&, uint32_t)>& writeValue) {
    auto resolveSeq = [elementSize](const std::vector<uint8_t>& b, uint32_t off, uint32_t seq,
                                     const std::vector<uint8_t>*) {
        return m2::resolveRawIntTrackSequence(b, off, seq, elementSize);
    };
    auto resolveGlobal = [elementSize](const std::vector<uint8_t>& b, uint32_t off,
                                        const std::vector<uint8_t>*) {
        return m2::resolveRawIntGlobalSequenceTrack(b, off, elementSize);
    };
    writeTrackCurve<uint32_t>(w, blob, trackOffset, sequenceCount, resolveSeq, resolveGlobal, writeValue);
}

void writeFixed16AsFloat(json::Writer& w, uint32_t raw) {
    int16_t signedRaw;
    uint16_t bits = static_cast<uint16_t>(raw);
    std::memcpy(&signedRaw, &bits, sizeof(signedRaw));
    float v = std::clamp(static_cast<float>(signedRaw) / 32767.0f, 0.0f, 1.0f);
    w.value(static_cast<double>(v));
}

void writeRawIntAsIs(json::Writer& w, uint32_t raw) { w.value(static_cast<int64_t>(raw)); }

template <typename T, typename WriteValueFn>
void writeFBlockCurve(json::Writer& w, const std::vector<std::pair<uint16_t, T>>& keyframes,
                       WriteValueFn writeValue) {
    w.beginArray();
    for (const auto& [ts, v] : keyframes) {
        w.beginObject();
        // Not milliseconds -- see m2::FBlockMeta's doc comment for why this
        // is exposed raw rather than guessing a 0.0..1.0 rescale.
        w.key("timestamp_raw_u16");
        w.value(static_cast<int64_t>(ts));
        w.key("value");
        writeValue(w, v);
        w.endObject();
    }
    w.endArray();
}

void writeRibbon(json::Writer& w, const std::vector<uint8_t>& blob, const m2::Ribbon& r,
                  uint32_t sequenceCount) {
    w.beginObject();
    w.key("ribbon_id");
    w.value(static_cast<int64_t>(r.ribbonId));
    w.key("bone");
    w.value(static_cast<int64_t>(r.boneIndex));
    w.key("position");
    writeVec3(w, r.position);
    w.key("texture_indices");
    w.beginArray();
    for (uint16_t v : r.textureIndices) w.value(static_cast<int64_t>(v));
    w.endArray();
    w.key("material_indices");
    w.beginArray();
    for (uint16_t v : r.materialIndices) w.value(static_cast<int64_t>(v));
    w.endArray();
    w.key("edges_per_second");
    w.value(static_cast<double>(r.edgesPerSecond));
    w.key("edge_lifetime");
    w.value(static_cast<double>(r.edgeLifetime));
    w.key("gravity");
    w.value(static_cast<double>(r.gravity));
    w.key("texture_rows");
    w.value(static_cast<int64_t>(r.textureRows));
    w.key("texture_cols");
    w.value(static_cast<int64_t>(r.textureCols));
    w.key("priority_plane");
    w.value(static_cast<int64_t>(r.priorityPlane));
    w.key("ribbon_color_index");
    w.value(static_cast<int64_t>(r.ribbonColorIndex));
    w.key("texture_transform_lookup_index");
    w.value(static_cast<int64_t>(r.textureTransformLookupIndex));

    w.key("color_track");
    writeVec3Track(w, blob, r.colorTrackOffset, sequenceCount);
    w.key("alpha_track");
    writeRawIntTrack(w, blob, r.alphaTrackOffset, sequenceCount, 2, writeFixed16AsFloat);
    w.key("height_above_track");
    writeFloatTrack(w, blob, r.heightAboveTrackOffset, sequenceCount);
    w.key("height_below_track");
    writeFloatTrack(w, blob, r.heightBelowTrackOffset, sequenceCount);
    w.key("tex_slot_track");
    writeRawIntTrack(w, blob, r.texSlotTrackOffset, sequenceCount, 2, writeRawIntAsIs);
    w.key("visibility_track");
    writeRawIntTrack(w, blob, r.visibilityTrackOffset, sequenceCount, 1, writeRawIntAsIs);
    w.endObject();
}

void writeParticle(json::Writer& w, const std::vector<uint8_t>& blob, const m2::ParticleEmitter& p,
                    uint32_t sequenceCount) {
    w.beginObject();
    w.key("particle_id");
    w.value(static_cast<int64_t>(p.particleId));
    w.key("flags");
    w.value(static_cast<int64_t>(p.flags));
    w.key("position");
    writeVec3(w, p.position);
    w.key("bone");
    w.value(static_cast<int64_t>(p.boneId));
    w.key("texture_id_raw");
    w.value(static_cast<int64_t>(p.textureId));
    w.key("particle_model_filename");
    w.value(p.particleModelFilename);
    w.key("child_emitters_model_filename");
    w.value(p.childEmittersModelFilename);
    w.key("blending_type");
    w.value(static_cast<int64_t>(p.blendingType));
    w.key("emitter_type");
    w.value(static_cast<int64_t>(p.emitterType));
    w.key("particle_color_index");
    w.value(static_cast<int64_t>(p.particleColorIndex));
    w.key("multi_tex_scale");
    w.beginArray();
    w.value(static_cast<double>(p.multiTexScale[0]));
    w.value(static_cast<double>(p.multiTexScale[1]));
    w.endArray();
    w.key("priority_plane");
    w.value(static_cast<int64_t>(p.priorityPlane));
    w.key("rows");
    w.value(static_cast<int64_t>(p.rows));
    w.key("columns");
    w.value(static_cast<int64_t>(p.columns));

    w.key("emission_speed_track");
    writeFloatTrack(w, blob, p.emissionSpeedTrackOffset, sequenceCount);
    w.key("speed_variation_track");
    writeFloatTrack(w, blob, p.speedVariationTrackOffset, sequenceCount);
    w.key("vertical_range_track");
    writeFloatTrack(w, blob, p.verticalRangeTrackOffset, sequenceCount);
    w.key("horizontal_range_track");
    writeFloatTrack(w, blob, p.horizontalRangeTrackOffset, sequenceCount);
    w.key("gravity_track");
    writeFloatTrack(w, blob, p.gravityTrackOffset, sequenceCount);
    w.key("lifespan_track");
    writeFloatTrack(w, blob, p.lifespanTrackOffset, sequenceCount);
    w.key("lifespan_variation");
    w.value(static_cast<double>(p.lifespanVariation));
    w.key("emission_rate_track");
    writeFloatTrack(w, blob, p.emissionRateTrackOffset, sequenceCount);
    w.key("emission_rate_variation");
    w.value(static_cast<double>(p.emissionRateVariation));
    w.key("emission_area_width_track");
    writeFloatTrack(w, blob, p.emissionAreaWidthTrackOffset, sequenceCount);
    w.key("emission_area_length_track");
    writeFloatTrack(w, blob, p.emissionAreaLengthTrackOffset, sequenceCount);
    w.key("z_source_track");
    writeFloatTrack(w, blob, p.zSourceTrackOffset, sequenceCount);

    w.key("color_track");
    writeFBlockCurve(w, m2::resolveFBlockVec3(blob, p.colorTrackBlockOffset),
                      [](json::Writer& w2, const m2::Vec3& v) { writeVec3(w2, v); });
    w.key("alpha_track");
    writeFBlockCurve(w, m2::resolveFBlockFixed16(blob, p.alphaTrackBlockOffset),
                      [](json::Writer& w2, float v) { w2.value(static_cast<double>(v)); });
    w.key("scale_track");
    writeFBlockCurve(w, m2::resolveFBlockVec2(blob, p.scaleTrackBlockOffset),
                      [](json::Writer& w2, const m2::Vec2& v) {
                          w2.beginObject();
                          w2.key("x");
                          w2.value(static_cast<double>(v.x));
                          w2.key("y");
                          w2.value(static_cast<double>(v.y));
                          w2.endObject();
                      });
    w.key("scale_vary");
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(p.scaleVary.x));
    w.key("y");
    w.value(static_cast<double>(p.scaleVary.y));
    w.endObject();
    w.key("head_uv_anim_track");
    writeFBlockCurve(w, m2::resolveFBlockUint16(blob, p.headUVAnimBlockOffset), writeRawIntAsIs);
    w.key("tail_uv_anim_track");
    writeFBlockCurve(w, m2::resolveFBlockUint16(blob, p.tailUVAnimBlockOffset), writeRawIntAsIs);

    w.key("tail_length");
    w.value(static_cast<double>(p.tailLength));
    w.key("twinkle_speed");
    w.value(static_cast<double>(p.twinkleSpeed));
    w.key("twinkle_percent");
    w.value(static_cast<double>(p.twinklePercent));
    w.key("twinkle_scale_min");
    w.value(static_cast<double>(p.twinkleScaleMin));
    w.key("twinkle_scale_max");
    w.value(static_cast<double>(p.twinkleScaleMax));
    w.key("inherit_velocity_scale");
    w.value(static_cast<double>(p.inheritVelocityScale));
    w.key("drag");
    w.value(static_cast<double>(p.drag));
    w.key("base_spin");
    w.value(static_cast<double>(p.baseSpin));
    w.key("base_spin_variation");
    w.value(static_cast<double>(p.baseSpinVariation));
    w.key("spin_speed");
    w.value(static_cast<double>(p.spinSpeed));
    w.key("spin_speed_variation");
    w.value(static_cast<double>(p.spinSpeedVariation));
    w.key("tumble_min");
    writeVec3(w, p.tumbleMin);
    w.key("tumble_max");
    writeVec3(w, p.tumbleMax);
    w.key("wind_vector");
    writeVec3(w, p.windVector);
    w.key("wind_time");
    w.value(static_cast<double>(p.windTime));
    w.key("follow_speed1");
    w.value(static_cast<double>(p.followSpeed1));
    w.key("follow_scale1");
    w.value(static_cast<double>(p.followScale1));
    w.key("follow_speed2");
    w.value(static_cast<double>(p.followSpeed2));
    w.key("follow_scale2");
    w.value(static_cast<double>(p.followScale2));
    w.key("spline_points");
    w.beginArray();
    for (const auto& pt : p.splinePoints) writeVec3(w, pt);
    w.endArray();
    w.key("enabled_in_track");
    writeRawIntTrack(w, blob, p.enabledInTrackOffset, sequenceCount, 1, writeRawIntAsIs);
    w.key("multi_tex_scroll_mid");
    w.beginArray();
    for (float v : p.multiTexScrollMid) w.value(static_cast<double>(v));
    w.endArray();
    w.key("multi_tex_scroll_range");
    w.beginArray();
    for (float v : p.multiTexScrollRange) w.value(static_cast<double>(v));
    w.endArray();
    w.endObject();
}

void writePhysVec3(json::Writer& w, const phys::Vec3& v) {
    w.beginObject();
    w.key("x");
    w.value(static_cast<double>(v.x));
    w.key("y");
    w.value(static_cast<double>(v.y));
    w.key("z");
    w.value(static_cast<double>(v.z));
    w.endObject();
}

void writeMat3x4(json::Writer& w, const std::array<float, 12>& m) {
    w.beginArray();
    for (float f : m) w.value(static_cast<double>(f));
    w.endArray();
}

// Resolves shape `s` (a body's shape entry) to its full type-specific
// record -- `f.boxes`/`capsules`/`spheres`/`polytopes[s.index]`, already
// bounds-checked by husk::phys::parse itself (WIKI_FINDINGS.md §9's own
// index/bounds cross-check found zero violations across 103 real files, so
// this is trusted, not re-checked here).
void writePhysShape(json::Writer& w, const phys::File& f, const phys::Shape& s) {
    w.beginObject();
    w.key("type");
    w.value(static_cast<int64_t>(s.type));
    w.key("index");
    w.value(static_cast<int64_t>(s.index));
    w.key("friction");
    w.value(static_cast<double>(s.friction));
    w.key("restitution");
    w.value(static_cast<double>(s.restitution));
    w.key("density");
    w.value(static_cast<double>(s.density));
    w.key("resolved");
    switch (s.type) {
        case 0: {
            const auto& b = f.boxes[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("box");
            w.key("a");
            writeMat3x4(w, b.a);
            w.key("c");
            writePhysVec3(w, b.c);
            w.endObject();
            break;
        }
        case 1: {
            const auto& c = f.capsules[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("capsule");
            w.key("local_position1");
            writePhysVec3(w, c.localPosition1);
            w.key("local_position2");
            writePhysVec3(w, c.localPosition2);
            w.key("radius");
            w.value(static_cast<double>(c.radius));
            w.endObject();
            break;
        }
        case 2: {
            const auto& sp = f.spheres[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("sphere");
            w.key("local_position");
            writePhysVec3(w, sp.localPosition);
            w.key("radius");
            w.value(static_cast<double>(sp.radius));
            w.endObject();
            break;
        }
        case 3: {
            const auto& p = f.polytopes[static_cast<size_t>(s.index)];
            w.beginObject();
            w.key("kind");
            w.value("polytope");
            w.key("vertex_count");
            w.value(static_cast<int64_t>(p.vertexCount));
            w.key("count_10");
            w.value(static_cast<int64_t>(p.count10));
            w.key("node_count");
            w.value(static_cast<int64_t>(p.nodeCount));
            w.key("vertices");
            w.beginArray();
            for (const auto& v : p.vertices) writePhysVec3(w, v);
            w.endArray();
            w.key("unk1_hex");
            w.beginArray();
            for (const auto& u : p.unk1) w.value(hexDump(u.data(), u.size()));
            w.endArray();
            w.key("unk2");
            w.beginArray();
            for (uint8_t u : p.unk2) w.value(static_cast<int64_t>(u));
            w.endArray();
            w.key("nodes");
            w.beginArray();
            for (const auto& n : p.nodes) {
                w.beginObject();
                w.key("unk");
                w.value(static_cast<int64_t>(n[0]));
                w.key("vertex_index");
                w.value(static_cast<int64_t>(n[1]));
                w.key("unk_index0");
                w.value(static_cast<int64_t>(n[2]));
                w.key("unk_index1");
                w.value(static_cast<int64_t>(n[3]));
                w.endObject();
            }
            w.endArray();
            w.endObject();
            break;
        }
        default:
            w.nullValue();
    }
    w.endObject();
}

void writePhysWeldJoint(json::Writer& w, const phys::WeldJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("weld");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("angular_frequency_hz");
    w.value(static_cast<double>(j.angularFrequencyHz));
    w.key("angular_damping_ratio");
    w.value(static_cast<double>(j.angularDampingRatio));
    w.key("linear_frequency_hz");
    w.value(static_cast<double>(j.linearFrequencyHz));
    w.key("linear_damping_ratio");
    w.value(static_cast<double>(j.linearDampingRatio));
    w.key("unk70");
    w.value(static_cast<double>(j.unk70));
    w.endObject();
}

void writePhysSphericalJoint(json::Writer& w, const phys::SphericalJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("spherical");
    w.key("anchor_a");
    writePhysVec3(w, j.anchorA);
    w.key("anchor_b");
    writePhysVec3(w, j.anchorB);
    w.key("friction_torque");
    w.value(static_cast<double>(j.frictionTorque));
    w.endObject();
}

void writePhysShoulderJoint(json::Writer& w, const phys::ShoulderJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("shoulder");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_twist_angle");
    w.value(static_cast<double>(j.lowerTwistAngle));
    w.key("upper_twist_angle");
    w.value(static_cast<double>(j.upperTwistAngle));
    w.key("cone_angle");
    w.value(static_cast<double>(j.coneAngle));
    w.key("max_motor_torque");
    w.value(static_cast<double>(j.maxMotorTorque));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysPrismaticJoint(json::Writer& w, const phys::PrismaticJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("prismatic");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_limit");
    w.value(static_cast<double>(j.lowerLimit));
    w.key("upper_limit");
    w.value(static_cast<double>(j.upperLimit));
    w.key("max_motor_force");
    w.value(static_cast<double>(j.maxMotorForce));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysRevoluteJoint(json::Writer& w, const phys::RevoluteJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("revolute");
    w.key("frame_a");
    writeMat3x4(w, j.frameA);
    w.key("frame_b");
    writeMat3x4(w, j.frameB);
    w.key("lower_angle");
    w.value(static_cast<double>(j.lowerAngle));
    w.key("upper_angle");
    w.value(static_cast<double>(j.upperAngle));
    w.key("max_motor_torque");
    w.value(static_cast<double>(j.maxMotorTorque));
    w.key("motor_mode");
    w.value(static_cast<int64_t>(j.motorMode));
    w.key("motor_frequency_hz");
    w.value(static_cast<double>(j.motorFrequencyHz));
    w.key("motor_damping_ratio");
    w.value(static_cast<double>(j.motorDampingRatio));
    w.endObject();
}

void writePhysDistanceJoint(json::Writer& w, const phys::DistanceJoint& j) {
    w.beginObject();
    w.key("kind");
    w.value("distance");
    w.key("local_anchor_a");
    writePhysVec3(w, j.localAnchorA);
    w.key("local_anchor_b");
    writePhysVec3(w, j.localAnchorB);
    w.key("some_distance_factor");
    w.value(static_cast<double>(j.someDistanceFactor));
    w.endObject();
}

// `j.bodyA`/`bodyB` are left as plain indices (not resolved to the body's
// own data) -- that's how the source data itself relates bodies to each
// other, and a caller already has the `bodies` array from the same JSON
// document to look them up in.
void writePhysJoint(json::Writer& w, const phys::File& f, const phys::Joint& j) {
    w.beginObject();
    w.key("body_a");
    w.value(static_cast<int64_t>(j.bodyA));
    w.key("body_b");
    w.value(static_cast<int64_t>(j.bodyB));
    w.key("type");
    w.value(static_cast<int64_t>(j.type));
    w.key("index");
    w.value(static_cast<int64_t>(j.index));
    w.key("resolved");
    switch (j.type) {
        case 0: writePhysSphericalJoint(w, f.sphericalJoints[static_cast<size_t>(j.index)]); break;
        case 1: writePhysShoulderJoint(w, f.shoulderJoints[static_cast<size_t>(j.index)]); break;
        case 2: writePhysWeldJoint(w, f.weldJoints[static_cast<size_t>(j.index)]); break;
        case 3: writePhysRevoluteJoint(w, f.revoluteJoints[static_cast<size_t>(j.index)]); break;
        case 4: writePhysPrismaticJoint(w, f.prismaticJoints[static_cast<size_t>(j.index)]); break;
        case 5: writePhysDistanceJoint(w, f.distanceJoints[static_cast<size_t>(j.index)]); break;
        default: w.nullValue();
    }
    w.endObject();
}

// Full `.phys` dump -- every body/shape/joint/PHYV record, each shape/joint
// resolved to its real type-specific data inline (see this file's own top
// doc comment, DESIGN.md's Key design decisions). `husk export --phys`
// attaches only a minimal per-body placement anchor to the .glb itself
// (gltf::Skeleton::PhysicsBody) -- this is the home for everything else.
void writePhysFile(json::Writer& w, const phys::File& f) {
    w.beginObject();
    w.key("version");
    w.value(static_cast<int64_t>(f.version));
    w.key("phyt");
    w.value(static_cast<int64_t>(f.phyt));

    w.key("bodies");
    w.beginArray();
    for (size_t i = 0; i < f.bodies.size(); ++i) {
        const auto& b = f.bodies[i];
        w.beginObject();
        w.key("index");
        w.value(static_cast<int64_t>(i));
        w.key("type");
        w.value(static_cast<int64_t>(b.type));
        w.key("bone");
        w.value(static_cast<int64_t>(b.boneIndex));
        w.key("position");
        writePhysVec3(w, b.position);
        w.key("shape_base");
        w.value(static_cast<int64_t>(b.shapeBase));
        w.key("shape_count");
        w.value(static_cast<int64_t>(b.shapeCount));
        w.key("unk0");
        w.value(static_cast<double>(b.unk0));
        w.key("x1c");
        w.value(static_cast<double>(b.x1c));
        w.key("drag");
        w.value(static_cast<double>(b.drag));
        w.key("unk1");
        w.value(static_cast<double>(b.unk1));
        w.key("x28");
        w.value(static_cast<double>(b.x28));
        w.key("shapes");
        w.beginArray();
        for (int32_t si = b.shapeBase; si < b.shapeBase + b.shapeCount; ++si) {
            writePhysShape(w, f, f.shapes[static_cast<size_t>(si)]);
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();

    w.key("joints");
    w.beginArray();
    for (const auto& j : f.joints) writePhysJoint(w, f, j);
    w.endArray();

    w.key("phyv");
    w.beginArray();
    for (const auto& v : f.phyv) {
        w.beginArray();
        for (float x : v.values) w.value(static_cast<double>(x));
        w.endArray();
    }
    w.endArray();

    w.endObject();
}

// Writes `ribbon_emitters`/`particle_emitters` -- unlike every other key
// this file writes, these come from the model's core MD20 header arrays
// (present in every version, including pre-Legion flat files), not a
// Legion+ chunk, so they're written unconditionally rather than gated
// behind `header.chunked`. Broadens this command's own stated scope (see
// this file's top doc comment): the "no glTF slot" rationale that already
// applied to the Legion+ side-chunks (TXAC/EXPT/RPID/GPID/PGD1) applies
// just as much to the parsed M2Ribbon/M2Particle records themselves --
// procedural emitter data, not renderable geometry -- while `husk export`
// still attaches a minimal position/bone anchor to the .glb's skin extras
// (see gltf::Skeleton::RibbonAnchor/ParticleAnchor) for placement without
// needing this JSON at all.
void dumpEmitters(json::Writer& w, const std::vector<uint8_t>& blob, const m2::Header& header) {
    w.key("ribbon_emitters");
    w.beginArray();
    for (const auto& r : m2::parseRibbons(blob, header.ribbonEmitters)) {
        writeRibbon(w, blob, r, header.sequences.count);
    }
    w.endArray();

    w.key("particle_emitters");
    if (header.particleEmitters.count > 0 && header.version < m2::kMinVerifiedParticleVersion) {
        w.beginObject();
        w.key("note");
        w.value("version " + std::to_string(header.version) + " is below Cataclysm (" +
                std::to_string(m2::kMinVerifiedParticleVersion) +
                ") -- M2Particle's record shape is only documented and verified for Cata+; not "
                "parsed structurally to avoid a silent misread");
        w.key("count");
        w.value(static_cast<int64_t>(header.particleEmitters.count));
        w.endObject();
    } else {
        w.beginArray();
        for (const auto& p : m2::parseParticles(blob, header.particleEmitters)) {
            writeParticle(w, blob, p, header.sequences.count);
        }
        w.endArray();
    }
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

        // .phys files start with a PHYS chunk whose tag is byte-reversed on
        // disk ("SYHP" -- see phys.hpp's doc comment), unambiguous against
        // both M2's own MD20/MD21 magic and .bone's leading version field --
        // sniffed first, most specific check first.
        bool looksLikePhys =
            fileBytes.size() >= 4 && std::memcmp(fileBytes.data(), "SYHP", 4) == 0;
        if (looksLikePhys) {
            auto physFile = phys::parse(fileBytes);
            json::Writer w(std::cout);
            writePhysFile(w, physFile);
            std::cout << "\n";
            return 0;
        }

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
        auto blob = m2::extractBlob(fileBytes);

        json::Writer w(std::cout);
        w.beginObject();

        // ribbon_emitters/particle_emitters are core MD20 header arrays,
        // present in every version -- written unconditionally, unlike the
        // Legion+-only chunk tags below (see dumpEmitters's doc comment).
        dumpEmitters(w, blob, header);

        if (!header.chunked) {
            std::cerr << "husk: '" << path
                      << "' is a pre-Legion flat MD20 file -- the Legion+-only chunks below "
                         "(TXAC/EXPT/.../TEXL) don't exist outside the chunked container, so only "
                         "ribbon_emitters/particle_emitters are populated above\n";
            w.endObject();
            std::cout << "\n";
            return 0;
        }
        auto chunks = readChunks(fileBytes.data(), fileBytes.size());

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
