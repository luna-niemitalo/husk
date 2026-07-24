"""husk-blp <file.blp> <output.png> [--mip N] -- see husk_blp/__init__.py."""

from __future__ import annotations

import argparse
import sys

from .decode import decode_mip_level, mip_dimensions
from .header import BlpError, ColorEncoding, parse_header


def _encoding_label(header) -> str:
    try:
        enc = ColorEncoding(header.color_encoding)
    except ValueError:
        return f"colorEncoding {header.color_encoding}"
    if enc == ColorEncoding.DXT:
        return f"DXT (preferredFormat {header.preferred_format})"
    return enc.name


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="husk-blp",
        description="Convert a BLP2 texture to PNG (husk roadmap stage 4).",
    )
    parser.add_argument("input", help="path to a .blp file")
    parser.add_argument("output", help="path to write the .png to")
    parser.add_argument(
        "--mip", type=int, default=0, metavar="N",
        help="mip level to decode, 0 = full resolution (default: 0)",
    )
    args = parser.parse_args(argv)

    try:
        with open(args.input, "rb") as f:
            file_bytes = f.read()
    except OSError as e:
        print(f"husk-blp: couldn't open '{args.input}' for reading: {e}", file=sys.stderr)
        return 1

    try:
        header = parse_header(file_bytes)
        image = decode_mip_level(header, file_bytes, args.mip)
        image.save(args.output, "PNG")
    except BlpError as e:
        print(f"husk-blp: {e}", file=sys.stderr)
        return 1
    except OSError as e:
        print(f"husk-blp: couldn't write '{args.output}': {e}", file=sys.stderr)
        return 1

    width, height = mip_dimensions(header, args.mip)
    print(f"{args.output}: {width}x{height}, {_encoding_label(header)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
