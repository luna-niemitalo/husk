# Real-file smoke tests, same role as husk's C++ tests/test_integration.cpp:
# decode an actual game-extracted .blp end to end and check shape (it
# decoded, dimensions are sane, alpha survived where expected) rather than
# exact pixel values -- those belong in test_decode.py's synthetic
# fixtures. Skipped (not failed) unless the relevant env var points at a
# real file. All three color encodings husk_blp currently supports are
# covered separately since they're genuinely different code paths:
#   HUSK_TEST_BLP_DXT1     e.g. a bloodelf *_hd_face_*.blp
#   HUSK_TEST_BLP_DXT5     e.g. a bloodelf *_hd_*.blp with alpha (hair, etc.)
#   HUSK_TEST_BLP_PALETTE  e.g. a bloodelf *facelower*.blp
# test_data/ (gitignored, repo root) is a convenient local spot for these.

import os

import pytest

from husk_blp.decode import ColorEncoding, decode_mip_level
from husk_blp.header import parse_header


def _decode_env_file(env_var: str):
    path = os.environ.get(env_var)
    if not path:
        pytest.skip(f"set {env_var} to a real .blp file to run this")
    with open(path, "rb") as f:
        file_bytes = f.read()
    header = parse_header(file_bytes)
    image = decode_mip_level(header, file_bytes, 0)
    return header, image


def test_real_dxt1_blp_decodes_to_a_plausible_opaque_image():
    header, image = _decode_env_file("HUSK_TEST_BLP_DXT1")
    assert header.color_encoding == ColorEncoding.DXT
    assert image.size == (header.width, header.height)
    assert header.width > 1 and header.height > 1
    # A real DXT1 face/skin texture is not going to be uniformly
    # transparent -- confirms this isn't accidentally reading garbage.
    assert image.convert("RGBA").getextrema()[3][1] > 0


def test_real_dxt5_blp_decodes_with_alpha():
    header, image = _decode_env_file("HUSK_TEST_BLP_DXT5")
    assert header.color_encoding == ColorEncoding.DXT
    assert image.size == (header.width, header.height)
    alpha_min, alpha_max = image.convert("RGBA").getextrema()[3]
    # DXT5 was specifically picked (over DXT1) by the real exporter for its
    # interpolated alpha -- a real such texture should have some variation
    # in it, not be uniformly 0 or uniformly 255.
    assert alpha_min != alpha_max


def test_real_palette_blp_decodes_to_a_plausible_image():
    header, image = _decode_env_file("HUSK_TEST_BLP_PALETTE")
    assert header.color_encoding == ColorEncoding.PALETTE
    assert image.size == (header.width, header.height)
    assert header.width > 1 and header.height > 1
    assert image.convert("RGBA").getextrema()[3][1] > 0
