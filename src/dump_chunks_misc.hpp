#pragma once

#include <cstdint>

#include "chunk.hpp"
#include "json_writer.hpp"

// The ~30 small, independent per-tag `dumpXxx(Writer&, Chunk&)` dumpers
// `husk dump-chunks` uses for Legion+ chunk tags with no shared state
// beyond dump_writer_utils.hpp's small read/write helpers -- see
// FILE_SPLIT_TODO.md's Item 4 and cmd_dump.cpp's own top doc comment for
// the full "why these chunks, why JSON not glTF" rationale.
namespace husk::commands {

// TXAC (wowdev.wiki M2#TXAC), a flat array of 2-byte records, count =
// materials.count + particle_emitters.count -- husk doesn't validate that
// expected count against the chunk's own actual size (chunk.size / 2), on
// the same "trust the chunk's own byte length over a cross-referenced
// count from elsewhere" principle findFileDataIdArrayChunk (m2.cpp) uses
// for BFID/TXID.
void dumpTxac(json::Writer& w, const Chunk& c);

// EXPT (wowdev.wiki M2#EXPT): struct{float zSource,colorMult,alphaMult;}
// per particle_emitters entry, 12 bytes/record, count = chunk.size / 12.
void dumpExpt(json::Writer& w, const Chunk& c);

// PABC (wowdev.wiki M2#PABC)/PGD1: the whole chunk is one M2Array<uint16_t>
// -- reuses m2::parseUint16Array directly on the chunk's own payload bytes.
void dumpU16ArrayChunk(json::Writer& w, const Chunk& c);

// PADC (wowdev.wiki M2#PADC): the whole chunk is one
// M2Array<M2TextureWeight> -- reuses m2::parseTextureWeights directly, same
// static-value-only caveat as the header's own texture_weights array (see
// m2::TextureWeight's doc comment).
void dumpPadc(json::Writer& w, const Chunk& c);

// PSBC (wowdev.wiki M2#PSBC): the whole chunk is one M2Array<M2Bounds>.
// M2Bounds's own field layout isn't individually named anywhere on the
// wiki -- what's used here (a CAaBox min/max plus a float radius, 28 bytes
// total) is inferred by analogy to the header's own identically-shaped
// bounding_box+bounding_sphere_radius pair, and independently confirmed to
// be the right *size* (not necessarily field order/meaning) by the
// M2Sequence stride investigation (see m2.hpp's Sequence doc comment) --
// flagged as an inference, not asserted as confirmed field semantics.
void dumpPsbc(json::Writer& w, const Chunk& c);

// PEDC (wowdev.wiki M2#PEDC): the whole chunk is one M2Array<M2TrackBase>
// -- an M2TrackBase (wowdev.wiki M2#Interpolation) is 12 bytes:
// interpolation_type(u16)/global_sequence(u16)/timestamps
// (M2Array<M2Array<uint32_t>>, 8 bytes). Only the two scalar header fields
// are surfaced -- the nested per-sequence timestamp sub-arrays themselves
// ("every timestamp is an implicit 'fire now'", wowdev.wiki M2#Events)
// aren't resolved, consistent with this command's "readable summary, not
// full resolution" scope.
void dumpPedc(json::Writer& w, const Chunk& c);

// RPID/GPID (wowdev.wiki M2#RPID/#GPID): flat uint32_t FileDataID arrays,
// one entry per particle_emitters entry -- same shape TXID/BFID already
// use (m2.cpp's findFileDataIdArrayChunk), just not centralized there
// since this file doesn't otherwise touch m2.cpp's internals.
void dumpFileDataIdArrayChunk(json::Writer& w, const Chunk& c);

// WFV3 (wowdev.wiki M2#WFV3), WaterFallDataV3 -- one fixed 80-byte struct
// per chunk (not an array), *except* a real, consistent 64-byte variant
// (Shadowlands "Maw" zone waterfall doodads,
// world/expansion08/doodads/maw/*.m2): every one is missing exactly the
// trailing 4 floats (unk1-unk4, the last 16 bytes of the 80-byte struct),
// never truncated anywhere else. Reads those four conditionally on
// `c.size >= 80`, emitting `null` (same "genuinely absent, not a parse
// failure" treatment dumpTextureWeights's optional weight/alpha fields
// already use) for the shorter variant instead of throwing. Field
// names/order for the rest transcribed verbatim from the wiki; most are
// documented only as "passed directly to fragment shader" with no further
// meaning given.
// TODO: Remove: 64-byte variant found across all 9 real corpus hits, see
// `WIKI_FINDINGS/M2.md`.
void dumpWfv3(json::Writer& w, const Chunk& c);

// NERF (wowdev.wiki M2#NERF): one fixed 8-byte struct, `C2Vector coefs`.
void dumpNerf(json::Writer& w, const Chunk& c);

// EDGF (wowdev.wiki M2#EDGF): 24-byte records, count = chunk.size / 24 --
// the wiki doesn't tie the count to any header array, unlike TXAC/EXPT.
void dumpEdgf(json::Writer& w, const Chunk& c);

// DBOC (wowdev.wiki M2#DBOC): 16-byte records, count = chunk.size / 16 --
// the wiki itself notes ambiguity about whether a given file's DBOC is one
// 16-byte entry or several ("might be 2 entries, might not be"); treating
// it as chunk.size/16 handles both without guessing at semantics beyond
// the record boundary itself.
void dumpDboc(json::Writer& w, const Chunk& c);

// TEXL (wowdev.wiki M2#TEXL, Midnight/12.0+): 16-byte records, one per
// `lights.count` entry -- an unambiguous wiki struct (two floats, then an
// index into TXID for the light cookie texture, then one more unknown int).
// TODO: Remove: was a complete blind spot before (FAILURES2.md #5) -- known
// to cmd_info.cpp's chunk-tag list but absent from this file's own dump
// tables, so real TEXL data was invisible everywhere, not just unparsed.
void dumpTexl(json::Writer& w, const Chunk& c);

// AFRA (wowdev.wiki M2#AFRA -- no wiki struct documented). Every real
// sample decodes cleanly as exactly 16 bytes: a float32 in [0.2, 0.98] at
// offset 0x00, followed by 12 bytes that are zero in every sample. Field
// names below are deliberately generic (`value`/`unk1..3`, not a guessed
// semantic like "alpha") -- the wiki gives no name for this chunk's field,
// and the filenames it turns up on (aura/void-portal VFX doodads) only
// weakly suggest an opacity-like role, not enough to assert as fact.
// TODO: Remove: byte-decoded from scratch against 32 real corpus hits
// (afra_files_for_exploration.txt); wiki said "not observed in any files
// yet" as of the 2026-07-25 fetch, see WIKI_FINDINGS.md's AFRA section.
void dumpAfra(json::Writer& w, const Chunk& c);

// DPIV (wowdev.wiki M2#DPIV): chunk size is always an exact multiple of 32
// bytes (1-4 records seen), so this is a real record array, not a single
// fixed struct as the wiki's own text implies. Per-record layout: 8x
// float32; the last 4 floats (offsets 0x10-0x1F) are zero in every real
// record seen -- kept as real fields rather than assumed-reserved, since a
// future file could populate them. Field names are deliberately generic
// (`field_0..7`, not a guessed semantic) -- no wiki text describes what any
// of these represent, and one field (offset 0x0C) decodes to suspicious
// small-integer-as-float denormals (raw bits 1 or 2), suggesting it may
// actually be an integer count/type rather than a float; exposed as a
// plain float here rather than guessing its real type.
// TODO: Remove: byte-decoded from scratch against 2,632 real corpus hits
// (dpiv_files_for_exploration.txt, 2,951 records); wiki said "seemingly
// always 32 bytes, mostly empty," see WIKI_FINDINGS.md's DPIV section.
void dumpDpiv(json::Writer& w, const Chunk& c);

// WFV1 (wowdev.wiki M2#WFV1 -- no struct documented). A genuinely thin,
// 2-file sample, both byte-identical (two Nazjatar-zone waterfall
// doodads) -- flagged tentative since there's no cross-file variation to
// confirm field boundaries against. Decodes as one real float32 (10.0) at
// offset 0x00 followed by 12 zero bytes -- same shape as AFRA, generic
// field names for the same reason.
// TODO: Remove: byte-decoded from scratch against both real corpus hits
// (wfv1_files_for_exploration.txt); wiki said "// unknown" as of the
// 2026-07-25 fetch.
void dumpWfv1(json::Writer& w, const Chunk& c);

// WFV2 (wowdev.wiki M2#WFV2 -- no struct documented). Same 2-file,
// byte-identical-content sample as WFV1 (same two Nazjatar waterfall
// doodads' sibling "_custom_" variants) -- tentative for the same reason.
// Decodes cleanly as 64 bytes / 16x float32 with no leftover bytes, but two
// of the sixteen (offsets 0x2C/0x30) show signs of not really being
// floats: 0x2C's raw bytes read as a plausible packed RGBA color (0xff,
// 0x9b, 0x8d, 0x72) rather than a sane float magnitude, and 0x30 decodes to
// a small-integer-as-float denormal pattern (raw bits = 3) also seen in
// DPIV's field_3 -- exposed here as plain floats rather than guessing a
// color/int reinterpretation from a 2-file sample.
// TODO: Remove: wiki said "// unknown" as of the 2026-07-25 fetch.
void dumpWfv2(json::Writer& w, const Chunk& c);

// DETL (wowdev.wiki M2#DETL, >= 9.0.1.34365): per-light shadow-RT-scale/
// diffuse-color-multiplier data, one 12-byte record per `lights.count`
// entry, correcting the wiki's own internally-inconsistent trailing
// `/*0x0a*/` end-offset comment (0x0c is right, matching the field list --
// the comment is simply wrong). `lightCount` (the header's own
// `lights.count`) is the authoritative record count; `chunk.size / kSize`
// is only a defensive floor against a foreign/corrupted file's
// `lights.count` disagreeing with its own chunk's real size -- trusting
// `lightCount` alone would silently read past a short chunk, and trusting
// `chunk.size / kSize` alone can overcount by exactly one when the
// alignment padding happens to be >= 12 bytes (real corpus data: a real
// 3-light file pads to 48 bytes, and 48 / 12 == 4, not 3). The padding
// itself is never read as data.
// TODO: Remove: real stride and the chunk's own 16-byte alignment padding
// both confirmed against all 1,043 real DETL-bearing files in the corpus,
// `WIKI_FINDINGS/M2.md`.
void dumpDetl(json::Writer& w, const Chunk& c, uint32_t lightCount);

// PFDC (wowdev.wiki M2#PFDC, >= 9.0.1.33978): inline physics data, byte-
// for-byte the same chunked container a standalone .phys file's own bytes
// are -- see dump_writer_utils.hpp's physPayloadRealLength doc comment for
// the padding-trim rationale.
void dumpPfdc(json::Writer& w, const Chunk& c);

// EXP2 (wowdev.wiki M2#EXP2, >= 7.3.0): M2Array<M2ExtendedParticle>
// content, same "chunk carries its own small header then raw data" shape
// PABC/PADC/PSBC/PEDC/PGD1 already use (readChunkArray at chunk-payload
// offset 0) -- records dereferenced via m2::parseExtendedParticles against
// a local "blob" that's really just this chunk's own payload bytes (same
// pattern dumpPadc/dumpU16ArrayChunk already establish for reusing
// m2.cpp's blob+Array-shaped parsers against chunk-local data, rather than
// the model's full MD20 blob). One entry expected per particle_emitters
// entry (per the wiki) -- not cross-checked here, same "trust this
// chunk's own byte length" policy dumpTxac already uses. Supersedes EXPT
// when both are present (wiki: "if EXP2 doesn't exist, the client tries
// to reconstruct it with data from the EXPT chunk"): zSource/colorMult/
// alphaMult are duplicated here rather than living only in EXPT, plus the
// new alphaCutoff curve EXPT has no room for at all. Unverified against
// any real file -- see ExtendedParticle's own doc comment in m2.hpp.
void dumpExp2(json::Writer& w, const Chunk& c);

// PCOL (wowdev.wiki M2#PCOL, >= 11.1.7.60520): player-housing collision
// mesh. Four independent M2Array-shaped (count, offset) regions in a fixed
// 32-byte header -- vertexPositions/faceNormals/indices/flags -- but,
// unlike PSBC/PEDC/PADC's single chunk-relative array, *not* one combined
// M2Array-of-structs: each region has its own count+offset pair, and the
// wiki explicitly warns "there can be extra bytes between the data, use
// the offsets" -- regions are read independently via their own offset, not
// accumulated sequentially the way PLYT's header+data walk is (src/
// phys.cpp's parsePolytopes). The two regions are *not* always contiguous
// (a real file was seen with an 8-byte gap between the faceNormals
// region's end and indices' own offset), consistent with the wiki's own
// warning, so no "regions exactly fill the chunk" cross-check is
// meaningful here the way PLYT's is. `flags`' per-record meaning is
// undocumented (wiki gives no field name beyond "short flags[flagsCount]")
// -- exposed raw, not interpreted. Diagnostic-only (`dump-chunks`), same
// class as EXP2/PFDC/DETL: no glTF slot, since this is niche (War Within
// 11.1.7+ player-housing furniture only) sidecar-shaped collision data,
// not core render geometry -- see M2_COMPLETENESS.md.
// TODO: Remove: confirmed empirically against all 2,354 real PCOL-bearing
// files (every region in-bounds, every index in range, indexCount ==
// faceNormCount * 3) -- corrected from an earlier scanner bug's false
// "zero real files," see `WIKI_FINDINGS/M2.md`.
void dumpPcol(json::Writer& w, const Chunk& c);

}  // namespace husk::commands
