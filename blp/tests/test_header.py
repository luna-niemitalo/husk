# Spec source: https://wowdev.wiki/BLP "BLP2 Header" section (fetched
# 2026-07-25). Offsets below are typed out fresh from that page, not
# copied from husk_blp/header.py -- same independent-transcription
# rationale as husk's C++ tests (see the repo root README's "Why
# test-first" section).
#
# BLP2 fixed header (148 bytes) + 1024-byte palette/JPEG-header region:
#   0x00 magic (char[4])            0x04 version (u32)
#   0x08 colorEncoding (u8)         0x09 alphaBitDepth (u8)
#   0x0A preferredFormat (u8)       0x0B hasMipmaps (u8)
#   0x0C width (u32)                0x10 height (u32)
#   0x14 mipOffsets (u32[16])       0x54 mipSizes (u32[16])
#   0x94 palette (1024 bytes: 256 x BGRX, or JPEG shared header)
# -> 0x494 = 1172 bytes total.

import pytest

from husk_blp.header import HEADER_SIZE, BlpError, parse_header

from conftest import build_header


def test_parse_header_reads_every_field_at_the_right_offset():
    mip_offsets = list(range(1000, 1016))
    mip_sizes = list(range(2000, 2016))
    palette = bytes((i * 7) % 256 for i in range(1024))

    data = build_header(
        color_encoding=2,
        alpha_bit_depth=8,
        preferred_format=7,
        has_mipmaps=1,
        width=512,
        height=256,
        mip_offsets=mip_offsets,
        mip_sizes=mip_sizes,
        palette=palette,
    )

    h = parse_header(data)
    assert h.color_encoding == 2
    assert h.alpha_bit_depth == 8
    assert h.preferred_format == 7
    assert h.has_mipmaps == 1
    assert h.width == 512
    assert h.height == 256
    assert list(h.mip_offsets) == mip_offsets
    assert list(h.mip_sizes) == mip_sizes
    assert h.palette == palette


def test_parse_header_wrong_magic_throws():
    data = build_header(magic=b"XXXX")
    with pytest.raises(BlpError, match="XXXX"):
        parse_header(data)


def test_parse_header_wrong_version_throws():
    data = build_header(version=2)
    with pytest.raises(BlpError, match="version"):
        parse_header(data)


def test_parse_header_too_short_throws():
    data = build_header()[: HEADER_SIZE - 1]
    with pytest.raises(BlpError, match="1172"):
        parse_header(data)


def test_parse_header_extra_trailing_bytes_are_fine():
    # Real files have mip pixel data after the header -- parse_header only
    # looks at the fixed-size prefix.
    data = build_header() + b"\xAB" * 500
    h = parse_header(data)
    assert h.width == 64
