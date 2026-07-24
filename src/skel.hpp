#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "m2.hpp"

// .skel file parsing, per https://wowdev.wiki/M2/.skel (fetched 2026-07-24).
//
// Legion+ (7.3+) can move an M2's `bones` array out of the M2 entirely: an
// `SKID` chunk in the M2 (see husk::m2::Header::skeletonFileId) points at a
// separate .skel file instead. husk doesn't resolve that FileDataID to a
// path itself (no CASC/listfile access) -- give this module a .skel file
// you already have a path to (e.g. via `husk export`'s optional 4th
// argument).
//
// A .skel file is itself chunked -- the same generic (4-byte tag, uint32
// size, payload) container husk::readChunks already reads for M2 itself
// (wowdev.wiki M2/.skel's own words: "These files replace some blocks from
// the M2 MD20 data. The chunks doing that have a fixed header followed by
// raw data, as with MD20."). This module only reads the `SKB1` chunk
// (bones): its `bones` field is `M2Array<M2CompBone>`, byte-for-byte the
// same struct husk::m2::parseBones already reads out of an M2's own inline
// `bones` array -- just relocated here, with offsets relative to this
// chunk's own payload start instead of the MD20 blob's. `SKB1`'s
// `key_bone_lookup` field, and every other .skel chunk (`SKL1`, `SKA1`,
// `SKS1`, `SKPD`), are out of scope for now -- not needed for a bind-pose
// skeleton.
namespace husk::skel {

struct BoneHeader {
    m2::Array bones;  // -> M2CompBone records, same shape as M2's own `bones` array
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Finds the SKB1 chunk in a .skel file already read into memory and reads
// its 16-byte header. Throws ParseError if there's no SKB1 chunk, or its
// payload is too short to hold the header.
BoneHeader parseBoneHeader(const std::vector<uint8_t>& fileBytes);

// Resolves SKB1's `bones` array into actual M2CompBone records, via
// husk::m2::parseBones. Throws ParseError under the same conditions as
// parseBoneHeader, or m2::ParseError if the bones array itself runs past
// the end of the SKB1 chunk's payload.
std::vector<m2::Bone> parseBones(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::skel
