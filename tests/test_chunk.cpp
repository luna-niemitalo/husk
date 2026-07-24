// Spec source: https://wowdev.wiki/Chunk (generic container) and
// https://wowdev.wiki/M2#Chunks (M2's specific use of it, fetched
// 2026-07-24): a chunk is a 4-byte tag followed by a little-endian uint32
// size followed by exactly `size` bytes of payload, repeated to the end of
// the buffer -- and, unlike WMO/ADT, M2's chunk tags are NOT byte-reversed.

#include <cstring>
#include <doctest/doctest.h>

#include "../src/chunk.hpp"

namespace {

// Appends one chunk record (tag + LE size + payload) to `buf`.
void appendChunk(std::vector<uint8_t>& buf, const char tag[4], const std::vector<uint8_t>& payload) {
    buf.insert(buf.end(), tag, tag + 4);
    uint32_t size = static_cast<uint32_t>(payload.size());
    uint8_t sizeBytes[4];
    std::memcpy(sizeBytes, &size, 4);
    buf.insert(buf.end(), sizeBytes, sizeBytes + 4);
    buf.insert(buf.end(), payload.begin(), payload.end());
}

}  // namespace

TEST_CASE("readChunks: empty buffer yields no chunks") {
    std::vector<uint8_t> buf;
    auto chunks = husk::readChunks(buf.data(), buf.size());
    CHECK(chunks.empty());
}

TEST_CASE("readChunks: single chunk, tag read literally (not reversed)") {
    std::vector<uint8_t> buf;
    appendChunk(buf, "MD21", {0xAA, 0xBB, 0xCC});

    auto chunks = husk::readChunks(buf.data(), buf.size());
    REQUIRE(chunks.size() == 1);
    CHECK(chunks[0].tag == "MD21");  // not "12DM" -- M2 does not reverse tags
    CHECK(chunks[0].size == 3);
    CHECK(chunks[0].data[0] == 0xAA);
    CHECK(chunks[0].data[1] == 0xBB);
    CHECK(chunks[0].data[2] == 0xCC);
}

TEST_CASE("readChunks: multiple chunks in sequence, arbitrary order preserved") {
    std::vector<uint8_t> buf;
    appendChunk(buf, "SFID", {1, 2});
    appendChunk(buf, "MD21", {9});
    appendChunk(buf, "AFID", {});

    auto chunks = husk::readChunks(buf.data(), buf.size());
    REQUIRE(chunks.size() == 3);
    CHECK(chunks[0].tag == "SFID");
    CHECK(chunks[1].tag == "MD21");
    CHECK(chunks[2].tag == "AFID");
    CHECK(chunks[2].size == 0);
}

TEST_CASE("readChunks: truncated chunk header throws") {
    std::vector<uint8_t> buf = {'M', 'D', '2', '1', 0x05, 0x00};  // only 6 of 8 header bytes
    CHECK_THROWS_AS(husk::readChunks(buf.data(), buf.size()), husk::ChunkError);
}

TEST_CASE("readChunks: declared size longer than remaining data throws") {
    std::vector<uint8_t> buf;
    appendChunk(buf, "MD21", {1, 2, 3});
    buf.resize(buf.size() - 1);  // chop off the last payload byte
    CHECK_THROWS_AS(husk::readChunks(buf.data(), buf.size()), husk::ChunkError);
}

TEST_CASE("findChunk: locates by tag, ignores others") {
    std::vector<uint8_t> buf;
    appendChunk(buf, "SFID", {1});
    appendChunk(buf, "MD21", {2, 3});

    auto chunks = husk::readChunks(buf.data(), buf.size());
    auto found = husk::findChunk(chunks, "MD21");
    REQUIRE(found.has_value());
    CHECK(found->size == 2);
    CHECK(found->data[0] == 2);
}

TEST_CASE("findChunk: returns nullopt when tag absent") {
    std::vector<uint8_t> buf;
    appendChunk(buf, "SFID", {1});

    auto chunks = husk::readChunks(buf.data(), buf.size());
    CHECK_FALSE(husk::findChunk(chunks, "MD21").has_value());
}
