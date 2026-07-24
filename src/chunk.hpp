#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Generic reader for WoW's chunked file container format: a repeating
// sequence of (4-byte tag, little-endian uint32 size, `size` bytes of
// payload) records running to the end of the buffer.
//
// M2-specific gotcha (wowdev.wiki M2#Chunks): unlike WMO/ADT, M2 chunk tags
// are NOT byte-reversed. A chunk tagged "MD21" reads as the literal bytes
// 'M','D','2','1' in the file, not "12DM". Callers compare against tags
// as written here, un-reversed.
namespace husk {

struct Chunk {
    std::string tag;  // exactly 4 bytes, not null-terminated in the file
    const uint8_t* data;
    uint32_t size;
};

struct ChunkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Splits `buf` into chunk records. Throws ChunkError if a chunk header or
// its declared payload runs past the end of the buffer -- truncated/foreign
// input is reported, never silently under-read.
std::vector<Chunk> readChunks(const uint8_t* buf, size_t size);

// Returns the first chunk in `chunks` whose tag matches, if any.
std::optional<Chunk> findChunk(const std::vector<Chunk>& chunks, std::string_view tag);

}  // namespace husk
