#pragma once

#include <cstdint>
#include <vector>

#include "../canon_material.hpp"

// BUNDLE_FORMAT.md's "Texture encoding -- settled": a texture's bundle
// payload is its source BLP's own compressed blocks rehoused in a DDS
// container, never a re-encode. Shared by every canon producer that turns a
// resolved BLP into a TextureRef payload (cmd_export_canon.cpp for models,
// cmd_export_terrain.cpp for terrain).
namespace husk::sources {

// Throws blp::ParseError when the source encoding cannot be rehoused
// (palettized, JPEG -- blp::extractRawPayload's own doc comment); a caller
// that still wants pixels falls back to `pngPayloadFromBlp`.
canon::TextureRef::Payload ddsPayloadFromBlp(const std::vector<uint8_t>& blpBytes);

// The decoded-pixels fallback: the whole BLP decoded and written as PNG.
// Throws blp::ParseError when the BLP cannot be decoded at all.
canon::TextureRef::Payload pngPayloadFromBlp(const std::vector<uint8_t>& blpBytes);

}  // namespace husk::sources
