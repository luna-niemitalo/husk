#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "json_writer.hpp"
#include "m2.hpp"

// Small bounds-checked read primitives and JSON-writer helpers shared
// across `husk dump-chunks`'s split-out dumpers (dump_emitters.cpp,
// dump_phys.cpp, dump_chunks_misc.cpp, cmd_dump.cpp itself) -- see
// FILE_SPLIT_TODO.md's Item 4. No shared state, just leaf helpers.
namespace husk::commands {

// Bounds-checked read primitives, same discipline as every other parser in
// this codebase (src/m2.cpp, src/skel.cpp) -- duplicated locally rather
// than exposed from m2.cpp, since these chunks' internal layout is a
// dump-chunks-only concern (m2::Header doesn't surface any of them, and
// none of them feed husk export -- see cmd_dump.cpp's own doc comment).
uint32_t readU32(const uint8_t* d, size_t n, size_t off);

uint16_t readU16(const uint8_t* d, size_t n, size_t off);

float readF32(const uint8_t* d, size_t n, size_t off);

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
ChunkArray readChunkArray(const uint8_t* d, size_t n, size_t off);

std::string hexDump(const uint8_t* d, size_t n);

void writeVec3(json::Writer& w, const m2::Vec3& v);

void writeFixed16AsFloat(json::Writer& w, uint32_t raw);

void writeRawIntAsIs(json::Writer& w, uint32_t raw);

// Decodes an IEEE-754 binary16 (half-precision) value -- distinct from this
// codebase's other "16-bit float-ish" type, fixed16 (M2Track<fixed16>,
// readFixed16TrackValue/decodeFixed16Element): fixed16 is a *linear*
// 0x0000..0x7FFF -> 0.0..1.0 fixed-point fraction, not a real float, while
// DETL's scale/diffuseColorMultiplier are genuine half-precision floats --
// wire value 0x231c decodes to 0.013885498046875 and 0x3c00 to exactly
// 1.0, neither of which makes sense under fixed16's linear scaling
// (0x231c/32767 would be ~0.276, 0x3c00/32767 would be > 1.0 and get
// clamped, matching neither observed constant).
// TODO: Remove: confirmed against real bytes, `WIKI_FINDINGS/M2.md`.
float readHalfFloat(uint16_t bits);

// PFDC (wowdev.wiki M2#PFDC, >= 9.0.1.33978): inline physics data, byte-
// for-byte the same chunked container a standalone .phys file's own bytes
// are (wiki: "PHYS physics; char PADDING[6];"), plus up to 6 bytes of
// trailing zero padding to the next alignment boundary. husk already has a
// full .phys parser (src/phys.hpp/phys.cpp) -- this just points it at a
// different byte range. husk::readChunks (which phys::parse calls
// internally) throws if trailing bytes can't form another full chunk
// header, so the padding has to be trimmed first, not handed to
// phys::parse verbatim. physPayloadRealLength walks the same chunk
// sequence husk::readChunks does but stops cleanly (rather than throwing)
// once fewer than 8 bytes remain -- exactly the shape PADDING[6] produces.
// See M2_COMPLETENESS.md.
// TODO: Remove: confirmed empirically (tests/test_dump.cpp) against a real
// committed .phys fixture wrapped in a synthetic PFDC chunk. husk's local
// extraction corpus has zero PFDC-bearing files (a local-extraction gap,
// not a real absence) -- a live-CASC scan found 2,430 real PFDC files, one
// pulled directly (FileDataID 1003471) decodes cleanly, see
// `WIKI_FINDINGS/PHYS.md`.
size_t physPayloadRealLength(const uint8_t* d, size_t n);

}  // namespace husk::commands
