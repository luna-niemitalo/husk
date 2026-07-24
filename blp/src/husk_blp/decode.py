"""Pixel decoding for a parsed BLP2 header, per https://wowdev.wiki/BLP.

Split of responsibility, deliberately: the container format (this module's
own offset math, mip-level sizing) is hand-rolled and spec-transcribed like
the rest of husk, since it's plain structured data with no real ambiguity.
Actual DXT1/DXT3/DXT5 block decoding is delegated to Pillow instead --
wrapping the raw compressed bytes in a minimal synthetic DDS container
(bit-for-bit the same block layout BLP uses) and handing that to Pillow's
own DDS reader -- rather than hand-rolling the color/alpha interpolation
math ourselves. Verified against hand-built single-block fixtures in
tests/test_decode.py that Pillow decodes them pixel-correctly, not just
parses the wrapper header.
"""

from __future__ import annotations

import io
import struct

import numpy as np
from PIL import Image

from .header import BlpHeader, BlpError, ColorEncoding, PixelFormat


def mip_dimensions(header: BlpHeader, level: int) -> tuple[int, int]:
    """Standard mip-chain halving, per level, floored at 1 pixel."""
    return max(1, header.width >> level), max(1, header.height >> level)


def _slice(data: bytes, offset: int, size: int, what: str) -> bytes:
    """Bounds-checked slice, printing expected-vs-actual on failure (this
    project's foreign-data policy: validate at the boundary, on failure
    print expected and actual)."""
    end = offset + size
    if offset < 0 or end > len(data):
        raise BlpError(
            f"{what} needs {size} bytes at offset {offset} (through {end}), "
            f"but the file is only {len(data)} bytes"
        )
    return data[offset:end]


# --- DXT (via a synthetic minimal DDS container, decoded by Pillow) -------

_DXT_BLOCK_SIZE = {
    PixelFormat.DXT1: 8,
    PixelFormat.DXT3: 16,
    PixelFormat.DXT5: 16,
}
_DXT_FOURCC = {
    PixelFormat.DXT1: b"DXT1",
    PixelFormat.DXT3: b"DXT3",
    PixelFormat.DXT5: b"DXT5",
}


def _dxt_block_count(width: int, height: int) -> int:
    return ((width + 3) // 4) * ((height + 3) // 4)


def _build_minimal_dds(fourcc: bytes, width: int, height: int, block_data: bytes) -> bytes:
    """A DDS file is just `DDS ` + a 124-byte DDS_HEADER + pixel data for a
    single, non-mipmapped, non-cubemap FourCC-compressed 2D texture --
    standard Microsoft DDS layout, not WoW-specific. Only the fields real
    DDS readers actually require are set; the rest (reserved words, caps2-4)
    are zeroed, same as most real-world DDS writers leave them."""
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000  # CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
    header = struct.pack(
        "<7I44x",
        124,  # dwSize
        flags,
        height,
        width,
        len(block_data),  # dwPitchOrLinearSize
        0,  # dwDepth
        0,  # dwMipMapCount
    )
    pixelformat = struct.pack("<2I4s5I", 32, 0x4, fourcc, 0, 0, 0, 0, 0)  # DDPF_FOURCC
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)  # DDSCAPS_TEXTURE
    return b"DDS " + header + pixelformat + caps + block_data


def _dxt_format_or_raise(preferred_format: int) -> PixelFormat:
    try:
        fmt = PixelFormat(preferred_format)
    except ValueError:
        fmt = None
    if fmt not in _DXT_BLOCK_SIZE:
        raise BlpError(
            f"colorEncoding is DXT but preferredFormat {preferred_format} isn't a "
            "supported DXT variant (only DXT1=0, DXT3=1, DXT5=7 are)"
        )
    return fmt


def _decode_dxt(preferred_format: int, block_data: bytes, width: int, height: int) -> Image.Image:
    fmt = _dxt_format_or_raise(preferred_format)
    dds_bytes = _build_minimal_dds(_DXT_FOURCC[fmt], width, height, block_data)
    img = Image.open(io.BytesIO(dds_bytes))
    img.load()
    return img.convert("RGBA")


# --- Palettized -------------------------------------------------------------


def _alpha_byte_size(alpha_bit_depth: int, width: int, height: int) -> int:
    n = width * height
    if alpha_bit_depth == 0:
        return 0
    if alpha_bit_depth == 1:
        return (n + 7) // 8
    if alpha_bit_depth == 8:
        return n
    raise BlpError(
        f"alpha bit depth {alpha_bit_depth} isn't supported -- BLP2's own wiki spec "
        "doesn't clearly document this value's bit layout (only 0, 1, and 8 are handled)"
    )


def _decode_alpha(alpha_bit_depth: int, data: bytes, width: int, height: int) -> np.ndarray:
    n = width * height
    if alpha_bit_depth == 0:
        return np.full((height, width), 255, dtype=np.uint8)
    if alpha_bit_depth == 8:
        return np.frombuffer(data, dtype=np.uint8)[:n].reshape(height, width)
    if alpha_bit_depth == 1:
        bits = np.unpackbits(np.frombuffer(data, dtype=np.uint8), bitorder="little")[:n]
        return (bits * 255).astype(np.uint8).reshape(height, width)
    raise BlpError(f"alpha bit depth {alpha_bit_depth} isn't supported")  # pragma: no cover


def _decode_palette(header: BlpHeader, index_data: bytes, alpha_data: bytes, width: int,
                     height: int) -> Image.Image:
    # header.palette is 256 x 4-byte BGRX entries (blue, green, red, unused).
    pal = np.frombuffer(header.palette, dtype=np.uint8).reshape(256, 4)
    rgb_palette = pal[:, [2, 1, 0]]  # BGRX -> RGB, drop the padding byte

    idx = np.frombuffer(index_data, dtype=np.uint8).reshape(height, width)
    rgb = rgb_palette[idx]  # fancy-index: (H, W, 3)
    alpha = _decode_alpha(header.alpha_bit_depth, alpha_data, width, height)
    rgba = np.dstack([rgb, alpha])
    return Image.fromarray(rgba, "RGBA")


# --- Uncompressed BGRA --------------------------------------------------------


def _decode_bgra(data: bytes, width: int, height: int) -> Image.Image:
    arr = np.frombuffer(data, dtype=np.uint8)[: width * height * 4].reshape(height, width, 4)
    rgba = arr[:, :, [2, 1, 0, 3]]  # BGRA -> RGBA
    return Image.fromarray(rgba, "RGBA")


# --- Entry point --------------------------------------------------------------


def decode_mip_level(header: BlpHeader, file_bytes: bytes, level: int = 0) -> Image.Image:
    """Decodes mip `level` (0 = full resolution) to an RGBA PIL Image.
    Throws BlpError if the level isn't present, its data runs past the end
    of `file_bytes`, or the encoding isn't one of DXT1/DXT3/DXT5/palettized/
    uncompressed-BGRA (JPEG content and the undocumented ARGB8888_DUP
    encoding aren't supported -- see the repo README's format matrix)."""

    if not 0 <= level < len(header.mip_offsets):
        raise BlpError(f"mip level {level} out of range (0-{len(header.mip_offsets) - 1})")

    offset = header.mip_offsets[level]
    if offset == 0:
        raise BlpError(f"mip level {level} isn't present in this file (offset 0)")

    width, height = mip_dimensions(header, level)

    if header.color_encoding == ColorEncoding.DXT:
        # wowdev.wiki explicitly warns mipSizes[] can be wrong for lower
        # mip levels of compressed textures -- the correct size is always
        # recomputable from the block count, so that's what's used here,
        # not the on-disk size field.
        fmt = _dxt_format_or_raise(header.preferred_format)
        size = _dxt_block_count(width, height) * _DXT_BLOCK_SIZE[fmt]
        block_data = _slice(file_bytes, offset, size, f"mip level {level} (DXT)")
        return _decode_dxt(header.preferred_format, block_data, width, height)

    if header.color_encoding == ColorEncoding.PALETTE:
        index_size = width * height
        index_data = _slice(file_bytes, offset, index_size, f"mip level {level} (palette indices)")
        alpha_size = _alpha_byte_size(header.alpha_bit_depth, width, height)
        alpha_data = _slice(file_bytes, offset + index_size, alpha_size,
                             f"mip level {level} (alpha)")
        return _decode_palette(header, index_data, alpha_data, width, height)

    if header.color_encoding == ColorEncoding.BGRA:
        size = width * height * 4
        data = _slice(file_bytes, offset, size, f"mip level {level} (BGRA)")
        return _decode_bgra(data, width, height)

    raise BlpError(
        f"colorEncoding {header.color_encoding} isn't supported yet "
        "(JPEG=0 and ARGB8888_DUP=4 -- see the repo README's format matrix)"
    )
