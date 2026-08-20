#include "blp.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

// stb_image_write.h ships inside the tinygltf package this project already
// links (tinygltf::tinygltf's INTERFACE_INCLUDE_DIRECTORIES, confirmed via
// `find /nix/store/.../tinygltf-*/include`) and its implementation --
// including stbi_write_png_to_mem -- is already compiled into libtinygltf.a
// (tinygltf's own TINYGLTF_IMPLEMENTATION translation unit defines
// STB_IMAGE_WRITE_IMPLEMENTATION internally). The header only declares this
// particular function inside its own implementation-only block, so
// including it plainly wouldn't expose the prototype, and defining
// STB_IMAGE_WRITE_IMPLEMENTATION here too would duplicate every symbol
// already in libtinygltf.a at link time. Declaring the prototype directly
// (extern "C", matching the unmangled symbol `nm libtinygltf.a` shows) is
// the narrowest way to reach an already-linked function -- no new
// dependency, no second implementation.
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes,
                                                 int x, int y, int n, int* out_len);
// Same already-linked-in-libtinygltf.a story as stbi_write_png_to_mem above,
// this time stb_image.h's read side (tinygltf needs to decode embedded/
// external glTF images itself, so STB_IMAGE_IMPLEMENTATION is already
// compiled in) -- decodePng's own doc comment has the rest.
extern "C" unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len, int* x, int* y,
                                                 int* channels_in_file, int desired_channels);
extern "C" void stbi_image_free(void* retval_from_stbi_load);

namespace husk::blp {

namespace {

// --- Header, mirrors blp/src/husk_blp/header.py's offsets exactly --------

constexpr size_t kHeaderMagicOff = 0x00;
constexpr size_t kHeaderVersionOff = 0x04;
constexpr size_t kColorEncodingOff = 0x08;
constexpr size_t kAlphaBitDepthOff = 0x09;
constexpr size_t kPreferredFormatOff = 0x0A;
constexpr size_t kWidthOff = 0x0C;
constexpr size_t kHeightOff = 0x10;
constexpr size_t kMipOffsetsOff = 0x14;
constexpr size_t kMipSizesOff = 0x54;
constexpr size_t kPaletteOff = 0x94;
constexpr size_t kPaletteRegionSize = 1024;
constexpr size_t kHeaderSize = kPaletteOff + kPaletteRegionSize;  // 0x494 = 1172
constexpr int kMipmapCount = 16;

enum ColorEncoding : uint8_t {
    kJpeg = 0,
    kPalette = 1,
    kDxt = 2,
    kBgra = 3,
    kArgb8888Dup = 4,
};

enum PixelFormat : uint8_t {
    kDxt1 = 0,
    kDxt3 = 1,
    kDxt5 = 7,
};

struct Header {
    uint8_t colorEncoding = 0;
    uint8_t alphaBitDepth = 0;
    uint8_t preferredFormat = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::array<uint32_t, kMipmapCount> mipOffsets{};
    const uint8_t* palette = nullptr;  // points into fileBytes, kPaletteRegionSize bytes
};

uint32_t readU32(const uint8_t* d, size_t off) {
    uint32_t v;
    std::memcpy(&v, d + off, sizeof(v));
    return v;
}

Header parseHeader(const std::vector<uint8_t>& f) {
    if (f.size() < kHeaderSize) {
        throw ParseError("BLP header is " + std::to_string(f.size()) + " bytes, need at least " +
                          std::to_string(kHeaderSize) +
                          " for the fixed header + palette/JPEG-header region");
    }
    if (std::memcmp(f.data() + kHeaderMagicOff, "BLP2", 4) != 0) {
        throw ParseError("expected 'BLP2' magic, got '" +
                          std::string(f.begin() + kHeaderMagicOff, f.begin() + kHeaderMagicOff + 4) +
                          "' -- not a BLP2 file (BLP0/BLP1 use a different header layout, not "
                          "supported here)");
    }
    uint32_t version = readU32(f.data(), kHeaderVersionOff);
    if (version != 1) {
        throw ParseError("expected BLP2 version 1, got " + std::to_string(version));
    }

    Header h;
    h.colorEncoding = f[kColorEncodingOff];
    h.alphaBitDepth = f[kAlphaBitDepthOff];
    h.preferredFormat = f[kPreferredFormatOff];
    h.width = readU32(f.data(), kWidthOff);
    h.height = readU32(f.data(), kHeightOff);
    for (int i = 0; i < kMipmapCount; ++i) {
        h.mipOffsets[i] = readU32(f.data(), kMipOffsetsOff + 4 * i);
    }
    h.palette = f.data() + kPaletteOff;
    return h;
}

const uint8_t* sliceOrThrow(const std::vector<uint8_t>& f, size_t offset, size_t size,
                             const char* what) {
    size_t end = offset + size;
    if (end < offset || end > f.size()) {
        throw ParseError(std::string(what) + " needs " + std::to_string(size) + " bytes at offset " +
                          std::to_string(offset) + " (through " + std::to_string(end) +
                          "), but the file is only " + std::to_string(f.size()) + " bytes");
    }
    return f.data() + offset;
}

// --- DXT1/DXT3/DXT5 (BC1/BC2/BC3) block decode ----------------------------
// Standard, publicly documented S3TC/BC1-3 bit layout (not WoW-specific) --
// see e.g. the Khronos Data Format spec's "BC1/BC2/BC3" sections. Hand-
// rolled here rather than delegated to a library, same treatment husk gives
// every other small, fully-specified binary format.

void unpack565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5) & 0x3F;
    uint8_t b5 = c & 0x1F;
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

// Decodes one 4x4 BC1-shaped color block (8 bytes: color0, color1, then 4
// bytes of 2-bit-per-pixel indices) into 16 RGB triples. `punchThrough`
// reports which interpolation mode was used (color0 <= color1): DXT1 alone
// treats index 3 as transparent black in that mode, so the caller needs
// this bit to get alpha right (DXT3/DXT5 supply alpha from a separate block
// and ignore it). BC2/BC3 share this exact color-block layout.
void decodeColorBlock(const uint8_t* block, std::array<std::array<uint8_t, 3>, 4>& colors,
                       std::array<uint8_t, 16>& indices, bool& punchThrough) {
    uint16_t c0, c1;
    std::memcpy(&c0, block, 2);
    std::memcpy(&c1, block + 2, 2);

    unpack565(c0, colors[0][0], colors[0][1], colors[0][2]);
    unpack565(c1, colors[1][0], colors[1][1], colors[1][2]);

    punchThrough = c0 <= c1;
    if (!punchThrough) {
        for (int ch = 0; ch < 3; ++ch) {
            colors[2][ch] = static_cast<uint8_t>((2 * colors[0][ch] + colors[1][ch]) / 3);
            colors[3][ch] = static_cast<uint8_t>((colors[0][ch] + 2 * colors[1][ch]) / 3);
        }
    } else {
        for (int ch = 0; ch < 3; ++ch) {
            colors[2][ch] = static_cast<uint8_t>((colors[0][ch] + colors[1][ch]) / 2);
        }
        colors[3] = {0, 0, 0};
    }

    for (int byteIdx = 0; byteIdx < 4; ++byteIdx) {
        uint8_t b = block[4 + byteIdx];
        for (int p = 0; p < 4; ++p) {
            indices[byteIdx * 4 + p] = (b >> (2 * p)) & 0x3;
        }
    }
}

// DXT3's explicit alpha block: 8 bytes, 16x 4-bit values, low nibble first,
// raster order. Scaled 0-15 -> 0-255 by *17 (0x11), the standard nibble ->
// byte expansion (0xF * 17 == 255).
void decodeExplicitAlphaBlock(const uint8_t* block, std::array<uint8_t, 16>& alpha) {
    for (int i = 0; i < 16; ++i) {
        uint8_t byte = block[i / 2];
        uint8_t nibble = (i % 2 == 0) ? (byte & 0xF) : (byte >> 4);
        alpha[i] = static_cast<uint8_t>(nibble * 17);
    }
}

// DXT5's interpolated alpha block: alpha0, alpha1, then 6 bytes packed as a
// 48-bit little-endian field, 3 bits/pixel, LSB-first.
void decodeInterpolatedAlphaBlock(const uint8_t* block, std::array<uint8_t, 16>& alpha) {
    uint8_t a0 = block[0];
    uint8_t a1 = block[1];
    std::array<uint8_t, 8> table{};
    table[0] = a0;
    table[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            table[1 + i] = static_cast<uint8_t>(((6 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            table[1 + i] = static_cast<uint8_t>(((4 - i) * a0 + i * a1) / 5);
        }
        table[6] = 0;
        table[7] = 255;
    }

    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }
    for (int i = 0; i < 16; ++i) {
        alpha[i] = table[(bits >> (3 * i)) & 0x7];
    }
}

void writeBlockPixels(std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, uint32_t bx,
                       uint32_t by, const std::array<std::array<uint8_t, 3>, 4>& colors,
                       const std::array<uint8_t, 16>& colorIndices,
                       const std::array<uint8_t, 16>& alpha) {
    for (uint32_t row = 0; row < 4; ++row) {
        uint32_t y = by * 4 + row;
        if (y >= height) continue;
        for (uint32_t col = 0; col < 4; ++col) {
            uint32_t x = bx * 4 + col;
            if (x >= width) continue;
            uint8_t idx = colorIndices[row * 4 + col];
            uint8_t a = alpha[row * 4 + col];
            const auto& c = colors[idx];
            size_t off = (static_cast<size_t>(y) * width + x) * 4;
            rgba[off + 0] = c[0];
            rgba[off + 1] = c[1];
            rgba[off + 2] = c[2];
            rgba[off + 3] = a;
        }
    }
}

size_t dxtBlockCount(uint32_t width, uint32_t height) {
    return static_cast<size_t>((width + 3) / 4) * ((height + 3) / 4);
}

size_t dxtBlockSize(uint8_t preferredFormat) {
    switch (preferredFormat) {
        case kDxt1:
            return 8;
        case kDxt3:
        case kDxt5:
            return 16;
        default:
            throw ParseError("colorEncoding is DXT but preferredFormat " +
                              std::to_string(preferredFormat) +
                              " isn't a supported DXT variant (only DXT1=0, DXT3=1, DXT5=7 are)");
    }
}

Image decodeDxt(const Header& h, const uint8_t* data) {
    uint8_t fmt = h.preferredFormat;
    size_t blockSize = dxtBlockSize(fmt);  // throws if unsupported

    Image img;
    img.width = h.width;
    img.height = h.height;
    img.rgba.assign(static_cast<size_t>(img.width) * img.height * 4, 0);

    uint32_t blocksX = (h.width + 3) / 4;
    uint32_t blocksY = (h.height + 3) / 4;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            const uint8_t* block = data + (static_cast<size_t>(by) * blocksX + bx) * blockSize;
            std::array<uint8_t, 16> alpha{};
            const uint8_t* colorBlock = block;
            if (fmt == kDxt1) {
                alpha.fill(255);
            } else if (fmt == kDxt3) {
                decodeExplicitAlphaBlock(block, alpha);
                colorBlock = block + 8;
            } else {  // kDxt5
                decodeInterpolatedAlphaBlock(block, alpha);
                colorBlock = block + 8;
            }

            std::array<std::array<uint8_t, 3>, 4> colors{};
            std::array<uint8_t, 16> colorIndices{};
            bool punchThrough = false;
            decodeColorBlock(colorBlock, colors, colorIndices, punchThrough);

            // DXT1's punch-through mode (color0 <= color1) makes index 3
            // transparent black, not just RGB-black -- DXT3/DXT5 ignore this
            // entirely, their alpha always comes from the separate block
            // decoded above.
            if (fmt == kDxt1 && punchThrough) {
                for (int i = 0; i < 16; ++i) {
                    if (colorIndices[i] == 3) alpha[i] = 0;
                }
            }
            writeBlockPixels(img.rgba, img.width, img.height, bx, by, colors, colorIndices, alpha);
        }
    }
    return img;
}

// --- Palettized ------------------------------------------------------------

size_t alphaByteSize(uint8_t alphaBitDepth, uint32_t width, uint32_t height) {
    size_t n = static_cast<size_t>(width) * height;
    switch (alphaBitDepth) {
        case 0:
            return 0;
        case 1:
            return (n + 7) / 8;
        case 8:
            return n;
        default:
            throw ParseError("alpha bit depth " + std::to_string(alphaBitDepth) +
                              " isn't supported -- BLP2's own wiki spec doesn't clearly document "
                              "this value's bit layout (only 0, 1, and 8 are handled)");
    }
}

Image decodePalette(const Header& h, const uint8_t* indexData, const uint8_t* alphaData) {
    Image img;
    img.width = h.width;
    img.height = h.height;
    size_t n = static_cast<size_t>(img.width) * img.height;
    img.rgba.resize(n * 4);

    for (size_t i = 0; i < n; ++i) {
        uint8_t palIdx = indexData[i];
        const uint8_t* entry = h.palette + palIdx * 4;  // BGRX
        img.rgba[i * 4 + 0] = entry[2];
        img.rgba[i * 4 + 1] = entry[1];
        img.rgba[i * 4 + 2] = entry[0];

        uint8_t a;
        switch (h.alphaBitDepth) {
            case 0:
                a = 255;
                break;
            case 8:
                a = alphaData[i];
                break;
            case 1: {
                uint8_t byte = alphaData[i / 8];
                a = ((byte >> (i % 8)) & 1) ? 255 : 0;
                break;
            }
            default:
                throw ParseError("alpha bit depth " + std::to_string(h.alphaBitDepth) +
                                  " isn't supported");
        }
        img.rgba[i * 4 + 3] = a;
    }
    return img;
}

// --- Uncompressed BGRA ------------------------------------------------------

Image decodeBgra(const Header& h, const uint8_t* data) {
    Image img;
    img.width = h.width;
    img.height = h.height;
    size_t n = static_cast<size_t>(img.width) * img.height;
    img.rgba.resize(n * 4);
    for (size_t i = 0; i < n; ++i) {
        img.rgba[i * 4 + 0] = data[i * 4 + 2];  // R <- B
        img.rgba[i * 4 + 1] = data[i * 4 + 1];  // G
        img.rgba[i * 4 + 2] = data[i * 4 + 0];  // B <- R
        img.rgba[i * 4 + 3] = data[i * 4 + 3];  // A
    }
    return img;
}

}  // namespace

Image decode(const std::vector<uint8_t>& fileBytes) {
    Header h = parseHeader(fileBytes);

    constexpr int kLevel = 0;
    uint32_t offset = h.mipOffsets[kLevel];
    if (offset == 0) {
        throw ParseError("mip level 0 isn't present in this file (offset 0)");
    }

    switch (h.colorEncoding) {
        case kDxt: {
            size_t blockSize = dxtBlockSize(h.preferredFormat);
            size_t size = dxtBlockCount(h.width, h.height) * blockSize;
            const uint8_t* data = sliceOrThrow(fileBytes, offset, size, "mip level 0 (DXT)");
            return decodeDxt(h, data);
        }
        case kPalette: {
            size_t indexSize = static_cast<size_t>(h.width) * h.height;
            const uint8_t* indexData =
                sliceOrThrow(fileBytes, offset, indexSize, "mip level 0 (palette indices)");
            size_t alphaSize = alphaByteSize(h.alphaBitDepth, h.width, h.height);
            const uint8_t* alphaData =
                sliceOrThrow(fileBytes, offset + indexSize, alphaSize, "mip level 0 (alpha)");
            return decodePalette(h, indexData, alphaData);
        }
        case kBgra: {
            size_t size = static_cast<size_t>(h.width) * h.height * 4;
            const uint8_t* data = sliceOrThrow(fileBytes, offset, size, "mip level 0 (BGRA)");
            return decodeBgra(h, data);
        }
        default:
            throw ParseError("colorEncoding " + std::to_string(h.colorEncoding) +
                              " isn't supported yet (JPEG=0 and ARGB8888_DUP=4 -- see the repo "
                              "README's format matrix)");
    }
}

std::vector<uint8_t> encodePng(const Image& img) {
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(img.rgba.data(), static_cast<int>(img.width) * 4,
                                                static_cast<int>(img.width),
                                                static_cast<int>(img.height), 4, &len);
    if (!png) {
        throw ParseError("stbi_write_png_to_mem failed to encode a " + std::to_string(img.width) +
                          "x" + std::to_string(img.height) + " image");
    }
    std::vector<uint8_t> out(png, png + len);
    std::free(png);  // stb_image_write's default allocator (STBIW_MALLOC/FREE unset -> malloc/free)
    return out;
}

Image decodePng(const std::vector<uint8_t>& fileBytes) {
    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &width, &height,
                               &channels, /*desired_channels=*/4);
    if (!pixels) {
        throw ParseError("stbi_load_from_memory failed to decode " + std::to_string(fileBytes.size()) +
                          " byte(s) as a PNG");
    }
    Image img;
    img.width = static_cast<uint32_t>(width);
    img.height = static_cast<uint32_t>(height);
    img.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return img;
}

}  // namespace husk::blp
