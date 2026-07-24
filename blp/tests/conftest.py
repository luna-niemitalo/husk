# Shared test fixture builder. Offset correctness itself is
# test_header.py's job (independently transcribed there, per the wiki spec
# comment at the top of that file) -- this is just a convenience for
# building well-formed header bytes so other test modules (test_decode.py)
# can focus on pixel/decode correctness, not re-litigate offsets.

import struct

from husk_blp.header import HEADER_SIZE


def build_header(
    color_encoding=2,
    alpha_bit_depth=8,
    preferred_format=7,
    has_mipmaps=1,
    width=64,
    height=32,
    mip_offsets=None,
    mip_sizes=None,
    palette=None,
    magic=b"BLP2",
    version=1,
):
    mip_offsets = mip_offsets or [0] * 16
    mip_sizes = mip_sizes or [0] * 16
    palette = palette or bytes(1024)
    assert len(mip_offsets) == 16 and len(mip_sizes) == 16
    assert len(palette) == 1024

    buf = bytearray(HEADER_SIZE)
    buf[0x00:0x04] = magic
    struct.pack_into("<I", buf, 0x04, version)
    buf[0x08] = color_encoding
    buf[0x09] = alpha_bit_depth
    buf[0x0A] = preferred_format
    buf[0x0B] = has_mipmaps
    struct.pack_into("<I", buf, 0x0C, width)
    struct.pack_into("<I", buf, 0x10, height)
    for i, off in enumerate(mip_offsets):
        struct.pack_into("<I", buf, 0x14 + 4 * i, off)
    for i, sz in enumerate(mip_sizes):
        struct.pack_into("<I", buf, 0x54 + 4 * i, sz)
    buf[0x94 : 0x94 + 1024] = palette
    return bytes(buf)
