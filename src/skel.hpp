#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include "m2.hpp"

// .skel file parsing, per https://wowdev.wiki/M2/.skel (fetched 2026-07-24
// and again 2026-07-25 for the SKS1/AFID sections below).
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
// raw data, as with MD20."). This module reads three of its chunks:
//   - `SKB1` (bones): `bones` field is `M2Array<M2CompBone>`, byte-for-byte
//     the same struct husk::m2::parseBones already reads out of an M2's own
//     inline `bones` array -- just relocated here, with offsets relative to
//     this chunk's own payload start instead of the MD20 blob's. Verified
//     against a real bloodelffemale_hd.skel: every M2Track outer array
//     index inside SKB1's bone records lines up 1:1 with SKS1's own
//     `sequences` array position (same convention as an M2's own inline
//     bones+sequences) -- e.g. bone 0's translation track outer array has
//     count 396, exactly SKS1's sequence count.
//   - `SKS1` (sequences): `sequences` field is `M2Array<M2Sequence>`, same
//     struct/stride as husk::m2::parseSequences already reads inline.
//     `global_loops`/`sequence_lookups` are skipped, same "extend as later
//     commands need more" policy as elsewhere in this codebase.
//   - `AFID`: same 8-byte-per-entry (anim_id, sub_anim_id, file_id) shape as
//     an M2's own AFID chunk (husk::m2::Header::AnimFileEntry) -- but this
//     is the *.skel's own* table, with its own FileDataIDs, not the owning
//     M2's (confirmed against real data: the same (animId, subAnimId) pair
//     resolves to a completely different fileId in each).
// `SKB1`'s `key_bone_lookup` field, and every other .skel chunk (`SKL1`,
// `SKA1`, `SKPD`, `BFID`), are out of scope for now.
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

// Returns the SKB1 chunk's own payload bytes -- the *blob* every
// m2::Bone::translationTrackOffset/rotationTrackOffset/scaleTrackOffset
// parseBones just returned is relative to (see parseBones's doc comment
// above), needed as m2::resolveVec3TrackSequence/resolveQuatTrackSequence's
// `blob` argument to actually resolve a .skel-sourced bone's keyframes.
// Throws ParseError under the same conditions as parseBoneHeader.
std::vector<uint8_t> boneTrackBlob(const std::vector<uint8_t>& fileBytes);

// Resolves SKS1's `sequences` array into actual M2Sequence records, via
// husk::m2::parseSequences. Throws ParseError if there's no SKS1 chunk, or
// its payload is too short to hold the 32-byte header (global_loops +
// sequences + sequence_lookups + 8 bytes of trailing padding), or
// m2::ParseError if the sequences array itself runs past the end of the
// SKS1 chunk's payload.
std::vector<m2::Sequence> parseSequences(const std::vector<uint8_t>& fileBytes);

// Reads the .skel's own AFID chunk, if present -- same shape as
// husk::m2::Header::animFileIds, but this table's FileDataIDs are this
// .skel file's own, not the owning M2's (see this file's doc comment).
// Returns nullopt if there's no AFID chunk at all (not every .skel has one
// -- wowdev.wiki's SKPD section: a child skeleton that shares its parent's
// animations "does not even have an AFID chunk").
std::optional<std::vector<m2::Header::AnimFileEntry>> findAnimFileIds(
    const std::vector<uint8_t>& fileBytes);

}  // namespace husk::skel
