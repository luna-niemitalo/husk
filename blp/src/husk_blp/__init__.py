"""husk-blp: BLP2 -> PNG texture conversion (husk roadmap stage 4).

See the repo root README.md for how this fits into husk overall. Public
API re-exported here for `import husk_blp`; `main()` is the `husk-blp` CLI
entry point (see pyproject.toml's [project.scripts]).
"""

from .cli import main
from .decode import BlpError, decode_mip_level, mip_dimensions
from .header import BlpHeader, ColorEncoding, PixelFormat, parse_header

__all__ = [
    "main",
    "BlpError",
    "decode_mip_level",
    "mip_dimensions",
    "BlpHeader",
    "ColorEncoding",
    "PixelFormat",
    "parse_header",
]
