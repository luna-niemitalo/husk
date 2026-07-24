# DXT1/DXT3/DXT5 block bytes below are constructed directly from the S3TC
# bit layout (color0/color1 as little-endian RGB565, a 2-bit index per
# pixel for DXT1's color block; DXT5's alpha block is alpha0/alpha1 bytes
# plus a 3-bit index per pixel) -- this is the standard, publicly
# documented DXT/BC1-3 layout, not WoW-specific, and is exactly what
# husk_blp.decode's synthetic-DDS wrapper hands to Pillow's own decoder
# unmodified. These tests exist to prove *husk_blp's* wiring (byte
# slicing, DDS header construction, RGBA channel order) is correct, not to
# re-verify Pillow's block math itself.

import struct

import pytest
from PIL import Image

from husk_blp.decode import decode_mip_level, mip_dimensions
from husk_blp.header import BlpError, parse_header

from conftest import build_header


def _rgb565(r5, g6, b5):
    return struct.pack("<H", (r5 << 11) | (g6 << 5) | b5)


def _dxt1_solid_block(r5, g6, b5):
    """One 4x4 block, opaque, every pixel the same RGB565 color: color0 ==
    color1 (both = the target color) so decoders don't have to care which
    of DXT1's two interpolation modes they're in -- index 0 always resolves
    to color0 in both. Indices all zero (2 bits/pixel * 16 pixels = 4
    bytes)."""
    c = _rgb565(r5, g6, b5)
    return c + c + b"\x00\x00\x00\x00"


def _dxt5_solid_block(r5, g6, b5, alpha):
    """DXT5 = one alpha block (8 bytes) + one DXT1-shaped color block (8
    bytes). alpha0 = target alpha, alpha1 = 0, all indices 0 -> alpha0
    everywhere (DXT5's 8-value interpolation table's index 0 is always
    alpha0)."""
    alpha_block = bytes([alpha, 0]) + b"\x00" * 6
    return alpha_block + _dxt1_solid_block(r5, g6, b5)


def test_mip_dimensions_halves_per_level_floored_at_1():
    h = parse_header(build_header(width=64, height=17))
    assert mip_dimensions(h, 0) == (64, 17)
    assert mip_dimensions(h, 1) == (32, 8)
    assert mip_dimensions(h, 2) == (16, 4)
    assert mip_dimensions(h, 6) == (1, 1)  # floors at 1, never 0


def test_decode_dxt1_solid_red_block():
    block = _dxt1_solid_block(31, 0, 0)  # pure red
    data = build_header(color_encoding=2, preferred_format=0, width=4, height=4,
                         mip_offsets=[len(build_header())] + [0] * 15)
    file_bytes = data + block
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    assert img.size == (4, 4)
    assert img.getpixel((0, 0)) == (255, 0, 0, 255)
    assert img.getpixel((3, 3)) == (255, 0, 0, 255)


def test_decode_dxt5_solid_blue_half_alpha_block():
    block = _dxt5_solid_block(0, 0, 31, 128)  # blue, alpha ~128
    header_bytes = build_header(color_encoding=2, preferred_format=7, width=4, height=4,
                                 mip_offsets=[len(build_header())] + [0] * 15)
    file_bytes = header_bytes + block
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    r, g, b, a = img.getpixel((0, 0))
    assert (r, g, b) == (0, 0, 255)
    assert a == 128


def test_decode_dxt_unsupported_preferred_format_throws():
    header_bytes = build_header(color_encoding=2, preferred_format=5,  # RGB565, not a DXT variant
                                 width=4, height=4, mip_offsets=[len(build_header())] + [0] * 15)
    file_bytes = header_bytes + b"\x00" * 8
    h = parse_header(file_bytes)
    with pytest.raises(BlpError, match="DXT variant"):
        decode_mip_level(h, file_bytes, 0)


def test_decode_palette_alpha_depth_0_is_fully_opaque():
    palette = bytearray(1024)
    # Palette entry 5 = BGRX(0, 255, 0) -> green.
    palette[5 * 4 : 5 * 4 + 4] = bytes([0, 255, 0, 0])
    header_bytes = build_header(color_encoding=1, alpha_bit_depth=0, width=2, height=2,
                                 mip_offsets=[len(build_header())] + [0] * 15, palette=bytes(palette))
    indices = bytes([5, 5, 5, 5])  # all pixels -> palette entry 5 (green)
    file_bytes = header_bytes + indices
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    assert img.getpixel((0, 0)) == (0, 255, 0, 255)


def test_decode_palette_alpha_depth_8_reads_explicit_per_pixel_alpha():
    palette = bytearray(1024)
    palette[9 * 4 : 9 * 4 + 4] = bytes([0, 0, 255, 0])  # entry 9 = BGRX(B=0,G=0,R=255) -> RGB red
    header_bytes = build_header(color_encoding=1, alpha_bit_depth=8, width=2, height=1,
                                 mip_offsets=[len(build_header())] + [0] * 15, palette=bytes(palette))
    indices = bytes([9, 9])
    alpha = bytes([10, 200])
    file_bytes = header_bytes + indices + alpha
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    assert img.getpixel((0, 0))[3] == 10
    assert img.getpixel((1, 0))[3] == 200
    # palette[9] BGRX(0,0,255,0) -> B=0,G=0,R=255 -> RGB=(255,0,0)
    assert img.getpixel((0, 0))[:3] == (255, 0, 0)


def test_decode_palette_alpha_depth_1_unpacks_bits_lsb_first():
    palette = bytearray(1024)
    palette[0:4] = bytes([255, 255, 255, 0])  # entry 0 = white
    header_bytes = build_header(color_encoding=1, alpha_bit_depth=1, width=8, height=1,
                                 mip_offsets=[len(build_header())] + [0] * 15, palette=bytes(palette))
    indices = bytes([0] * 8)
    # LSB-first: bit0->pixel0=1(opaque), bit1->pixel1=0(transparent), rest 0.
    alpha_byte = 0b00000001
    file_bytes = header_bytes + indices + bytes([alpha_byte])
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    assert img.getpixel((0, 0))[3] == 255
    assert img.getpixel((1, 0))[3] == 0


def test_decode_bgra_channel_order():
    header_bytes = build_header(color_encoding=3, width=1, height=1,
                                 mip_offsets=[len(build_header())] + [0] * 15)
    pixel = bytes([10, 20, 30, 40])  # B, G, R, A
    file_bytes = header_bytes + pixel
    h = parse_header(file_bytes)
    img = decode_mip_level(h, file_bytes, 0)
    assert img.getpixel((0, 0)) == (30, 20, 10, 40)


def test_decode_mip_level_not_present_throws():
    header_bytes = build_header(width=4, height=4, mip_offsets=[0] * 16)
    h = parse_header(header_bytes)
    with pytest.raises(BlpError, match="isn't present"):
        decode_mip_level(h, header_bytes, 0)


def test_decode_data_running_past_end_of_file_throws():
    header_bytes = build_header(color_encoding=2, preferred_format=0, width=4, height=4,
                                 mip_offsets=[len(build_header())] + [0] * 15)
    file_bytes = header_bytes + b"\x00" * 4  # DXT1 needs 8 bytes, only 4 present
    h = parse_header(file_bytes)
    with pytest.raises(BlpError, match="needs 8 bytes"):
        decode_mip_level(h, file_bytes, 0)


def test_decode_unsupported_color_encoding_throws():
    header_bytes = build_header(color_encoding=0, width=4, height=4,  # JPEG
                                 mip_offsets=[len(build_header())] + [0] * 15)
    file_bytes = header_bytes + b"\x00" * 100
    h = parse_header(file_bytes)
    with pytest.raises(BlpError, match="colorEncoding 0"):
        decode_mip_level(h, file_bytes, 0)
