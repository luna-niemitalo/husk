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

// REFACTOR/BUNDLE_FORMAT.md's "Texture encoding -- settled": canonical
// texture storage is the source *payload* (compressed GPU blocks),
// rehoused verbatim into an open container, never a decode. The four real
// compressed/raw shapes this decoder already distinguishes while parsing
// (decode()'s own colorEncoding/preferredFormat switch) -- Palette is
// deliberately excluded, see extractRawPayload's own doc comment.
enum class RawEncoding { Bc1, Bc2, Bc3, Bgra };

struct RawPayload {
    uint32_t width = 0;
    uint32_t height = 0;
    RawEncoding encoding = RawEncoding::Bc1;
    std::vector<uint8_t> bytes;  // mip level 0's real file bytes, verbatim -- never decoded to pixels
};

// Extracts mip level 0's real compressed/raw bytes verbatim, without ever
// decoding to pixels -- the lossless half of decode()'s own header/mip-
// offset parsing, stopped one step earlier. Throws ParseError on the same
// conditions decode() does (bad header, missing mip0), plus when
// colorEncoding is Palette, JPEG, or ARGB8888_DUP: JPEG/ARGB8888_DUP
// aren't supported by this decoder at all (same restriction decode() has);
// Palette specifically has real bytes to extract (the index buffer +
// palette table decode() already reads) but wrapping an 8-bit paletted
// image in a DDS container that a broad set of public tools actually
// opens is a real, separate design question this project hasn't settled
// (legacy D3DFMT_P8 has thin modern tool support, unlike the BC1-3/BGRA
// cases below) -- not guessed at here. Use decode()+encodePng() for a
// Palette source today.
RawPayload extractRawPayload(const std::vector<uint8_t>& fileBytes);

// Wraps `payload`'s bytes in a minimal, standard Microsoft DDS container
// ("DDS " magic + a 124-byte DDS_HEADER + the bytes unchanged) -- a header
// swap, not a transcode, matching BUNDLE_FORMAT.md's own framing exactly.
// BC1/BC2/BC3 write a compressed (FourCC "DXT1"/"DXT3"/"DXT5") header;
// Bgra writes an uncompressed 32-bit RGB+alpha header (BLP's own in-file
// byte order, B,G,R,A per pixel, is already the standard DDS A8R8G8B8
// memory layout -- no channel reordering needed). Opens directly in
// Blender, GIMP, and any other DDS-aware tool.
std::vector<uint8_t> encodeDds(const RawPayload& payload);

}  // namespace husk::blp
