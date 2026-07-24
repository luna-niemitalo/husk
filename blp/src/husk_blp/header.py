"""BLP2 header parsing, per https://wowdev.wiki/BLP (fetched 2026-07-25).

Mirrors husk's C++ modules (src/m2.py, src/skin.py, ...) in spirit: every
field is read at an explicit, bounds-checked, named offset -- no packed
struct/ctypes overlay -- and the offsets here are typed out fresh from the
wiki page, not copied from anywhere else, so a transcription mistake shows
up as a test failure rather than being rubber-stamped.

Scope: BLP2 only (the format every Cataclysm+ M2 -- i.e. every version
husk's M2/.skin/.skel readers target -- actually uses). BLP0/BLP1 have a
different header layout entirely and are out of scope for now.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

MAGIC = b"BLP2"
VERSION = 1
MIPMAP_COUNT = 16
PALETTE_SIZE = 256

# Fixed 148-byte header, offsets from the wowdev.wiki BLP2 Header table:
#   0x00 magic (char[4])            0x04 version (u32)
#   0x08 colorEncoding (u8)         0x09 alphaBitDepth (u8)
#   0x0A preferredFormat (u8)       0x0B hasMipmaps (u8)
#   0x0C width (u32)                0x10 height (u32)
#   0x14 mipOffsets (u32[16])       0x54 mipSizes (u32[16])
# Followed by a 1024-byte extended region at 0x94: a 256-entry BGRX palette
# for palettized/BGRA/DXT content, or a JPEG shared header for JPEG
# content. Total header size 0x494 (1172) bytes.
_OFF_MAGIC = 0x00
_OFF_VERSION = 0x04
_OFF_COLOR_ENCODING = 0x08
_OFF_ALPHA_BIT_DEPTH = 0x09
_OFF_PREFERRED_FORMAT = 0x0A
_OFF_HAS_MIPMAPS = 0x0B
_OFF_WIDTH = 0x0C
_OFF_HEIGHT = 0x10
_OFF_MIP_OFFSETS = 0x14
_OFF_MIP_SIZES = 0x54
_OFF_PALETTE = 0x94
_PALETTE_REGION_SIZE = 1024
HEADER_SIZE = _OFF_PALETTE + _PALETTE_REGION_SIZE  # 0x494 = 1172


class ColorEncoding(IntEnum):
    JPEG = 0
    PALETTE = 1
    DXT = 2
    BGRA = 3
    ARGB8888_DUP = 4  # "same decompression, likely other PIXEL_FORMAT" per the wiki


class PixelFormat(IntEnum):
    """`preferredFormat` -- only meaningful (selects the DXT variant) when
    colorEncoding == DXT. wowdev.wiki's struct definition names this
    BLPPixelFormat; the header-table section calls the same byte
    "AlphaType" -- same field, two names across the page."""

    DXT1 = 0
    DXT3 = 1
    ARGB8888 = 2
    ARGB1555 = 3
    ARGB4444 = 4
    RGB565 = 5
    A8 = 6
    DXT5 = 7
    UNSPECIFIED = 8
    ARGB2565 = 9
    BC5 = 11


class BlpError(Exception):
    """Anything structurally wrong with a .blp file: bad magic/version, a
    header shorter than the fixed portion this parser reads, or (raised by
    husk_blp.decode) foreign data that doesn't add up -- a mip level
    claiming more bytes than the file has, an unsupported encoding, etc."""


@dataclass
class BlpHeader:
    color_encoding: int
    alpha_bit_depth: int
    preferred_format: int
    has_mipmaps: int
    width: int
    height: int
    mip_offsets: tuple[int, ...]  # 16 entries; 0 = level not present
    mip_sizes: tuple[int, ...]  # 16 entries -- see husk_blp.decode for why
    # these are NOT trusted for compressed formats
    palette: bytes  # raw 1024-byte region at 0x94, meaning depends on color_encoding


def _u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def parse_header(data: bytes) -> BlpHeader:
    """Parses the fixed BLP2 header out of `data` (a full .blp file, or at
    least its first HEADER_SIZE bytes). Throws BlpError if `data` is too
    short, or the magic/version don't match BLP2."""

    if len(data) < HEADER_SIZE:
        raise BlpError(
            f"BLP header is {len(data)} bytes, need at least {HEADER_SIZE} "
            "for the fixed header + palette/JPEG-header region"
        )

    magic = data[_OFF_MAGIC : _OFF_MAGIC + 4]
    if magic != MAGIC:
        raise BlpError(f"expected {MAGIC!r} magic, got {magic!r} -- not a BLP2 file "
                        "(BLP0/BLP1 use a different header layout, not supported here)")

    version = _u32(data, _OFF_VERSION)
    if version != VERSION:
        raise BlpError(f"expected BLP2 version {VERSION}, got {version}")

    mip_offsets = tuple(_u32(data, _OFF_MIP_OFFSETS + 4 * i) for i in range(MIPMAP_COUNT))
    mip_sizes = tuple(_u32(data, _OFF_MIP_SIZES + 4 * i) for i in range(MIPMAP_COUNT))

    return BlpHeader(
        color_encoding=data[_OFF_COLOR_ENCODING],
        alpha_bit_depth=data[_OFF_ALPHA_BIT_DEPTH],
        preferred_format=data[_OFF_PREFERRED_FORMAT],
        has_mipmaps=data[_OFF_HAS_MIPMAPS],
        width=_u32(data, _OFF_WIDTH),
        height=_u32(data, _OFF_HEIGHT),
        mip_offsets=mip_offsets,
        mip_sizes=mip_sizes,
        palette=bytes(data[_OFF_PALETTE : _OFF_PALETTE + _PALETTE_REGION_SIZE]),
    )
