# TODO: native `husk blp-export` CLI

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

Prompted by comparing husk's own CLI (`cmd_info.cpp`/`cmd_export.cpp`/
`cmd_dump.cpp`/`cmd_db2.cpp`, CLI11-based, named flags, generated bash/zsh
completions — see `README.md`'s format-support matrix) against the separate
Python `blp/` tool's CLI: `husk-blp <one.blp> <one.png>` and nothing else —
no directory/batch mode, no `--help`-documented flag conventions matching
the rest of this project. Direct assessment: "mechanically unusable" next
to husk's own tooling coverage. Real, concrete case this surfaced it:
populating `example_exports/creature/tripod2/*.png` (11 real `.blp` files,
used to investigate the glow/base texture-resolution bug fixed this same
session — see git history, `export_materials.cpp`'s `orderCandidatesForDefault`
now takes the batch's own `blendMode` into account) took a manual shell
loop, one `husk-blp` invocation per file, instead of one command.

**husk-blp's own *decode* logic is not the problem** — it's already
real and robust (verified against the full corpus: palettized/DXT1/
DXT3/DXT5/BGRA all handled). The actual fix isn't a `--dir` flag bolted
onto the separate Python tool, though — husk itself **already has** a
full, real BLP2 decoder and PNG encoder in C++ (`src/blp.hpp`/`.cpp`,
`blp::decode`/`blp::encodePng`), used internally by
`export_materials.cpp` (`blp::encodePng(blp::decode(raw))`) to embed
real textures into every `.glb` export. There is no new decode work
needed here at all — just a thin CLI subcommand reusing that existing,
already-corpus-verified pipeline directly, the same way `husk db2-export`
wraps `db2.hpp`/`dbd.hpp` instead of shelling out to a separate tool.

## The actual task (not started)

A new `husk blp-export <in.blp> <out.png>` subcommand (`src/cmd_blp.cpp`,
new — naming/flag conventions mirroring `cmd_db2.cpp`'s `db2-export`),
plus a `--dir <in-dir> <out-dir>` batch mode (walks every `.blp` in
`in-dir`, writes a same-basename `.png` per file into `out-dir`, skipping
and warning on any single bad/corrupt file rather than aborting the whole
batch — same "one bad file doesn't kill the run" discipline
`husk db2-export --dir` already has for `.db2` files). Once this lands,
the separate Python `blp/`/`husk-blp` tool's role shrinks to whatever (if
anything) still needs Pillow/numpy that the C++ path doesn't cover —
check `blp/`'s own test suite and README before assuming it can be
deleted outright.
