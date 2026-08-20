#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

// BLP2 texture decode -- NOT documented on wowdev.wiki as a C++ struct, but
// the container/pixel-format layout is: see https://wowdev.wiki/BLP.
// Mirrors blp/src/husk_blp/{header,decode}.py (the separate Python
// husk-blp tool this project already ships) field-for-field and offset-for-
// offset, deliberately -- that tool remains the ground truth this decoder
// is checked against (tests/test_blp.cpp ports its fixtures directly), not
// a second, independently-derived spec transcription.
//
// Scope: BLP2 only (every Cataclysm+ M2 uses BLP2; BLP0/BLP1 have a
// different header and are out of scope, same restriction husk-blp has).
// DXT1/DXT3/DXT5 block decode and PNG encoding are both hand-rolled/
// delegated to already-linked code here in C++, instead of Pillow --
// see DESIGN.md for why: husk-blp's own Pillow dependency was the one
// remaining reason `husk export` couldn't be a single self-contained tool
// end to end.
namespace husk::blp {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// RGBA8, row-major, top-to-bottom, 4 bytes/pixel -- the layout
// stbi_write_png_to_mem expects directly.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4 bytes
};

// Decodes mip level 0 of a complete .blp file already read into memory.
// Throws ParseError on a bad magic/version, a mip claiming more bytes than
// the file has, or an unsupported encoding (JPEG and the undocumented
// ARGB8888_DUP aren't supported -- same restriction husk-blp has).
Image decode(const std::vector<uint8_t>& fileBytes);

// Encodes `img` as PNG bytes in memory (stbi_write_png_to_mem under the
// hood, already linked in via tinygltf's own vendored stb_image_write.h --
// no new dependency). Throws ParseError if encoding fails.
std::vector<uint8_t> encodePng(const Image& img);

// Decodes already-in-memory PNG bytes to the same Image shape decode()
// returns (stbi_load_from_memory, same already-linked tinygltf-vendored
// stb_image.h as encodePng's write-side counterpart -- no new dependency).
// Needed by TODO/CHAR_TEXTURE_COMPOSITING_TODO.md's Stage 4 real pixel
// compositor (char_composite.hpp): every other consumer of a resolved
// texture file in this codebase treats "PNG bytes" as an opaque blob to
// re-embed as-is (readTextureFileBytes, export_texture_resolution.cpp) --
// this is the first real PNG *pixel* decode husk has needed. Throws
// ParseError if the bytes aren't a decodable PNG.
Image decodePng(const std::vector<uint8_t>& fileBytes);

}  // namespace husk::blp
