#include "chunk.hpp"

#include <cstring>

namespace husk {

std::vector<Chunk> readChunks(const uint8_t* buf, size_t size) {
    std::vector<Chunk> chunks;
    size_t pos = 0;

    while (pos < size) {
        if (size - pos < 8) {
            throw ChunkError(
                "truncated chunk header: expected 8 bytes (4-byte tag + "
                "uint32 size) at offset " +
                std::to_string(pos) + ", only " + std::to_string(size - pos) +
                " remain");
        }

        Chunk chunk;
        chunk.tag.assign(reinterpret_cast<const char*>(buf + pos), 4);

        uint32_t chunkSize;
        std::memcpy(&chunkSize, buf + pos + 4, sizeof(chunkSize));
        chunk.size = chunkSize;

        size_t payloadStart = pos + 8;
        if (chunkSize > size - payloadStart) {
            throw ChunkError("chunk '" + chunk.tag + "' at offset " + std::to_string(pos) +
                              " declares size " + std::to_string(chunkSize) +
                              " but only " + std::to_string(size - payloadStart) +
                              " bytes remain in the buffer");
        }

        chunk.data = buf + payloadStart;
        chunks.push_back(chunk);
        pos = payloadStart + chunkSize;
    }

    return chunks;
}

std::optional<Chunk> findChunk(const std::vector<Chunk>& chunks, std::string_view tag) {
    for (const auto& chunk : chunks) {
        if (chunk.tag == tag) {
            return chunk;
        }
    }
    return std::nullopt;
}

}  // namespace husk
