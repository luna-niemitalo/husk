#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

// .bone file parsing -- NOT documented on wowdev.wiki (as of this writing,
// the M2#BFID section only describes the FileDataID *array* an M2/.skel's
// BFID chunk holds, nothing about what's inside the files those ids point
// at). Reverse-engineered from real Legion+ character files (see
// tests/test_bone.cpp for the exact fixtures/reasoning) rather than a spec
// -- treat every field name below as inferred, not sourced.
//
// Layout: a 4-byte little-endian version (1 in every file this was tested
// against) followed by the same generic chunked container husk::readChunks
// already reads for M2/.skel (see chunk.hpp) -- two chunks, `BIDA` and
// `BOMT`, always the same length apart from each other (one entry each, in
// lockstep): BIDA is a flat array of uint16 bone indices (into the owning
// model's `bones` array, same indexing as M2Vertex::bone_indices); BOMT is a
// flat array of 4x4 row-major float matrices, one per BIDA entry, always
// observed with (0,0,0,1) as its last column and a near-identity 3x3 --
// i.e. a small corrective delta transform for that bone, not a full
// replacement pose. Which LOD/context each `.bone` file (of the several a
// single model's BFID array lists) applies to isn't documented or inferred
// here -- husk surfaces the raw (boneIndex, matrix) pairs and stops there.
namespace husk::bone {

struct Correction {
    uint16_t boneIndex = 0;
    std::array<float, 16> matrix{};  // row-major, translation in row 3 (indices 12/13/14)
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses a complete .bone file already read into memory. Throws ParseError
// if it's shorter than the leading version field, has no BIDA or BOMT
// chunk, or BIDA's entry count doesn't match BOMT's -- every real file this
// was checked against has the two in lockstep, so a mismatch is treated as
// foreign/corrupted input rather than guessed at.
std::vector<Correction> parse(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::bone
