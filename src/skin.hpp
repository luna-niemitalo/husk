#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "m2.hpp"

// .skin file parsing, per https://wowdev.wiki/M2/.skin (fetched 2026-07-24).
//
// A .skin file is one LOD/submesh "view" onto an M2's vertex list. M2 files
// carry no triangle indices themselves (wowdev.wiki M2#Views_(LOD)) -- those
// live here instead, as two levels of lookup table:
//   skin.vertices[i]  -> an index into the M2's own global vertex list
//   skin.indices[i]   -> an index into *this* skin's `vertices` array above
// So a drawable triangle-index buffer of M2 global vertex indices is
// `vertices[indices[0]], vertices[indices[1]], vertices[indices[2]], ...`.
//
// Unlike M2 itself, .skin files are never chunked -- flat header, offsets
// relative to the start of the .skin file. Only the fields needed to build
// that triangle-index buffer are read here (magic, vertices, indices); the
// header continues further (bones, submeshes, batches, ...) for later work
// (per-material/per-submesh export, skinning).
namespace husk::skin {

struct Header {
    uint32_t magic = 0;   // "SKIN" once read, checked against the literal bytes
    m2::Array vertices;   // -> M2's global vertex list
    m2::Array indices;    // -> this skin's `vertices` array above
    m2::Array submeshes;  // -> M2SkinSection records, see Submesh below
    m2::Array batches;    // -> M2Batch records, see Batch below
};

// M2SkinSection, per wowdev.wiki M2/.skin#Submeshes -- 48 bytes on disk.
// Unparsed fields (centerPosition/sortCenterPosition/sortRadius/boneCount/
// boneComboIndex/boneInfluences/centerBoneIndex) and `skinSectionId`'s own
// unfiltered-passthrough behavior: see DESIGN.md's "Deliberately unparsed
// fields" ledger.
//
// `Level` (offset 0x02) is NOT LOD/culling metadata despite the field name
// and wowdev.wiki's own placement of it near those concerns -- confirmed
// against wow.export's Skin.js: it's the *high 16 bits* of `indexStart`
// (`triangleStart += level << 16`), needed once a model's resolved
// triangle-index buffer exceeds 65,535 entries (real, not rare -- 77/114
// submeshes on `bloodelffemale_hd.m2` need it). `indexStart` below is
// already the corrected 32-bit value; callers never see `Level` directly.
// Trap: treating this as a plain LOD marker and leaving it unread makes
// husk silently alias into the *wrong* region of the triangle-index
// buffer for any submesh needing correction, no error, no crash -- only
// visible as missing geometry in a real render.
struct Submesh {
    uint16_t skinSectionId = 0;
    uint16_t vertexStart = 0;
    uint16_t vertexCount = 0;
    uint32_t indexStart = 0;  // into the skin's resolved triangle-index buffer; see Level note above
    uint16_t indexCount = 0;
};

// M2Batch (aka "texture unit"), per wowdev.wiki M2/.skin#Texture_units --
// 24 bytes on disk. Unparsed fields (priorityPlane/geosetIndex/
// materialLayer): see DESIGN.md's "Deliberately unparsed fields" ledger.
struct Batch {
    uint8_t flags = 0;
    // wowdev.wiki M2/.skin#shader_id_and_textureCount: on Cata+ content
    // (husk's own Legion+ scope), a mostly-direct on-disk shader selector
    // -- 0x8000 set means "index into the real s_modelShaderEffect table",
    // clear means "derive from the low bits + textureCount". Resolved into
    // a real Combiners_*/Diffuse_* name pair by m2::resolveShaderNames
    // (src/m2_shader_names.hpp), not by this struct itself.
    uint16_t shaderId = 0;
    uint16_t skinSectionIndex = 0;  // -> Header::submeshes
    uint16_t materialIndex = 0;     // -> M2's own `materials` array (m2::Material)
    uint16_t textureCount = 0;      // 1..4; only the first texture is used here
    // -> M2's own `textureCombos` array (m2::parseUint16Array on
    // Header::textureCombos), which in turn holds an index into M2's
    // `textures` array. See src/cmd_export.cpp for the full chain.
    uint16_t textureComboIndex = 0;
    // -> M2's own `textureCoordCombos` array (m2::parseUint16Array on
    // Header::textureCoordCombos): documented as -1 (0xFFFF)/0/1, selecting
    // environment mapping (unhandled, falls back to UV set 0)/UV set 0/UV
    // set 1 for this batch's texture -- see src/cmd_export.cpp's fuller
    // doc comment on this same field.
    // TODO: Remove: a real file's table holds values outside that
    // documented range (likely vestigial, safely ignored either way), see
    // `WIKI_FINDINGS/M2/skin.md`.
    uint16_t textureCoordComboIndex = 0;
    // -1 (0xFFFF) if none, else an index into M2's own `colors` array
    // (m2::Color) -- a per-batch tint/fade, see src/cmd_export.cpp.
    uint16_t colorIndex = 0xFFFF;
    // -> M2's own `textureWeightCombos` array (m2::parseUint16Array on
    // Header::textureWeightCombos), which in turn holds an index into M2's
    // `textureWeights` array (m2::TextureWeight) -- an additional,
    // separately-animated transparency multiplier. See src/cmd_export.cpp.
    uint16_t textureWeightComboIndex = 0;
    // -1 (0xFFFF) if none (inferred by convention, same sentinel shape as
    // colorIndex above -- not an explicit wowdev.wiki quote for this
    // specific field), else -> M2's own `textureTransformCombos` array
    // (m2::parseUint16Array on Header::textureTransformCombos), which in
    // turn holds an index into M2's `textureTransforms` array
    // (m2::TextureTransform) -- UV scroll/rotate/scale animation. See
    // src/cmd_export.cpp.
    uint16_t textureTransformComboIndex = 0xFFFF;
};

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses the fixed header out of a complete .skin file already read into
// memory. Throws ParseError if the file is too short or the magic isn't
// "SKIN".
Header parseHeader(const std::vector<uint8_t>& fileBytes);

// Reads `array.count` little-endian uint16 values out of `fileBytes` at
// `array.offset`. Throws ParseError if that range runs past the end of the
// file. Shared by both the `vertices` and `indices` lookup tables, which
// have identical on-disk shape (M2Array<uint16>).
std::vector<uint16_t> parseU16Array(const std::vector<uint8_t>& fileBytes, const m2::Array& array);

// Resolves `header.indices` all the way through to M2 global vertex
// indices -- see the module comment above for the two-level lookup this
// collapses. The result is a flat triangle-corner index buffer the same
// length as `header.indices.count`; every 3 consecutive entries are one
// triangle (wowdev.wiki M2/.skin#Indices). Throws ParseError if any index
// in `header.indices` is out of range for `header.vertices`.
std::vector<uint32_t> resolveTriangleIndices(const std::vector<uint8_t>& fileBytes,
                                              const Header& header);

// Reads `array.count` M2SkinSection records out of `fileBytes` at
// `array.offset`. Throws ParseError if that range runs past the end of the
// file. An empty array (count 0) returns an empty vector without touching
// `array.offset` at all.
std::vector<Submesh> parseSubmeshes(const std::vector<uint8_t>& fileBytes, const m2::Array& array);

// Reads `array.count` M2Batch records out of `fileBytes` at `array.offset`.
// Throws ParseError if that range runs past the end of the file. An empty
// array (count 0) returns an empty vector without touching `array.offset`
// at all.
std::vector<Batch> parseBatches(const std::vector<uint8_t>& fileBytes, const m2::Array& array);

}  // namespace husk::skin
