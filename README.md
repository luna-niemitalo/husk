---
aliases:
  - README
---
# husk

A CLI for converting World of Warcraft's proprietary model/world formats
(M2, M3, WMO) to common ones. This is the very first slice: reading an M2
file's header and printing what's in it. No conversion yet.

Architecture and design rationale live in `DESIGN.md`; real-file
wowdev.wiki corrections/gaps live in `WIKI_FINDINGS.md`; open correctness
work lives in `TODO_correctness.md`; a granular per-feature M2 completion
breakdown (parse depth × consumption depth × glTF ceiling, below the level
of detail the format matrix below can show) lives in `M2_COMPLETENESS.md`;
the equivalent breakdown for WMO + ADT (combined — world placement is the
whole point of both, see that file's own intro), a pre-implementation
scaffold since neither format has any code yet, lives in
`WORLD_COMPLETENESS.md`. This file covers usage, current feature coverage,
and roadmap status.

## Why test-first

WoW's file formats move under this tool constantly — new chunk types, new
fields, occasional outright format changes (M2 → M3). The tests in
`tests/` are written directly from [wowdev.wiki's M2 page](https://wowdev.wiki/M2)
(offsets, chunk semantics, version table — transcribed independently of
`src/`, not copied from it), not from the implementation. When the format
moves and something in `src/` no longer matches, the goal is for a test to
fail loudly and point at exactly which field/offset broke, rather than
`husk` silently misreading a file. See the comments at the top of
`tests/test_m2.cpp` and `tests/test_chunk.cpp` for the exact spec citations.

## Building

From this directory (`$PWD/`), inside its own Nix dev shell:

```
direnv allow          # first time only, or: nix develop ./nix -c bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

The binary lands at `build/husk`.

`blp/` (texture conversion, see Usage below) is a separate Python
subproject with its own setup (`cd blp && uv sync`) -- `uv` is provided by
the same dev shell, but it isn't part of the CMake build above.

## Usage

```
husk info <file.m2>
husk export --input <file.m2> [OPTIONS]   (see below -- all flags order-independent)
husk dump-chunks <file.m2>
husk dump-chunks <file.bone>
husk dump-chunks <file.phys>
```

husk never touches CASC storage itself, and it never resolves a FileDataID to
the *real* WoW filename that would live under it -- no CASC/listfile access,
by design (see `DESIGN.md`'s Non-goals). What it does do is apply one local
filesystem convention consistently: a real extraction drops an `.m2` and
everything it needs (`.skin`, `.skel`, `.anim`, converted textures) into one
directory together, so `export` defaults every optional argument by looking
next to the model instead of requiring every path spelled out (see `export`'s
"Defaults" below). Get a real `.m2` out of a WoW install first with a
separate extraction tool, e.g.
[`casc-tool`](https://github.com/luna-niemitalo/casc-tool) (a standalone CASC
browser/extractor CLI, no relation beyond "also reads WoW files"):

```
casc-tool extract --storage <wow-install> --listfile <listfile.csv> \
  character/bloodelf/female/bloodelffemale.m2 /tmp/bloodelffemale.m2
husk info /tmp/bloodelffemale.m2
```

### `husk info <file.m2>`

Parses the header only and prints:

- pre-Legion (flat `MD20`) vs. Legion+ (chunked, `MD21`-wrapped), the version,
  and a best-guess expansion label -- with a loud warning below Wrath, where
  `parseBones`/`parseSequences`/`parseRibbons`'s fixed record strides are
  unverified (see `DESIGN.md`).
- the model's internal name and record counts: sequences, bones, vertices,
  textures, materials.
- whether the skeleton is inline or external: zero inline bones plus an `SKID`
  chunk is stated explicitly ("skeleton is external, see `.skel`") rather than
  left as a `bones: 0` that reads like "no skeleton".
- every `textures` entry's `type`/`flags`/`filename` (a real filename only for
  `type == 0` -- every other type is a runtime-substituted slot, see
  `src/m2.hpp`'s `Texture` doc comment), cross-referenced against `TXID`
  FileDataIDs when present.
- every `materials` entry's `flags`/`blend_mode`.
- `attachments`/`events`/`lights` records, fully dereferenced (id/bone/
  position, or type/bone/position for lights); `ribbon_emitters`/
  `particle_emitters` get a one-line-per-emitter summary (id/bone/position,
  plus blend/emitter type and tile counts for particles) -- full field/curve
  data lives in `husk dump-chunks`, not here (see the format matrix).
  `particle_emitters` stays counts-only below Cataclysm (version 272, see
  `m2::kMinVerifiedParticleVersion`). `cameras` is still counts-only (not
  dereferenced -- see the format matrix).
- sidecar-reference chunks when present: `SFID` (skin FileDataIDs, used by
  `--skin auto` below), `LDV1` (LOD count), `BFID` (`.bone` FileDataIDs),
  `AFID` (`.anim` FileDataIDs).
- for chunked files, every top-level chunk tag found, flagging any tag outside
  husk's known-M2-chunk-tag list (see `DESIGN.md` for why that matters for
  this format).

### `husk export --input <file.m2> [OPTIONS]`

Resolves the M2's vertices and the `.skin`'s triangle-index lookup into one
glTF primitive per `.skin` batch, converts WoW's Z-up coordinates to glTF's
Y-up, and writes a `.glb`. Only `--input`/`-i` is required; every other flag
is named and order-independent (only `--input`/`-i` and `--output`/`-o` also
work as bare positionals, first and last -- `husk export model.m2 out.glb`
-- the same universal `tool in out` muscle memory as `cp`/`mv`; every other
flag must be spelled out, so adding a tenth flag later can never shift what
a bare word means).

**Defaults.** Every flag left unset resolves from what's already sitting
next to the model, announced on stderr as it's resolved, never silently:

- unset `--output` -> `<model-basename>.glb`.
- unset `--skin` -> the literal `auto` (see below).
- unset `--textures`/`--skin-dir` -> the model's own directory -- a real
  extraction already drops the `.m2` and everything it needs into one place,
  so nothing here requires CASC/listfile access, just the filesystem layout
  you already have.
- unset `--anim` -> the literal `auto` (see below).
- unset `--skel`, only relevant when the model has 0 inline bones -> checks
  for a same-basename `.skel` next to the model; not finding one isn't an
  error, since plenty of 0-bone models genuinely have no skeleton.
- unset `--bones-dir`, only relevant when the model has bones at all ->
  the model's own directory, same as `--textures`/`--skin-dir`.

Each of `--textures`/`--skin-dir`/`--skel`/`--bones-dir` also accepts the
literal `none`, distinct from leaving it unset: `none` means "don't even
try," skipping that resolution deliberately (no image embedded/no SFID
search stage/no same-basename `.skel` lookup/no `.bone` correction data
attached) rather than best-effort deriving it. Every default is overridable
by giving that flag explicitly.

**`--skin`/`-s` argument.** Either a path to the `.skin` matching the LOD
you want, or `auto` (the default), which tries the M2's own `SFID` chunk
first -- resolving `<--skin-dir>/<FileDataID>.skin` for the entry `--lod`
selects (default: entry 0, "the main skin", per wowdev.wiki), the
FileDataID-renamed-directory convention a `--skin-dir` you populated
yourself would use -- and falls back to the lowest-numbered (highest-detail)
`<model-basename><N>.skin` file found in the model's own directory if that
doesn't resolve (`bloodelffemale.m2` finds `bloodelffemale00.skin` next to
it, but not `bloodelffemale_hd00.skin` -- a stricter match than a plain
string prefix, see `src/cmd_export.cpp`'s `findSameBasenameSkins`). `auto`
announces which stage resolved and what it found; an error, naming both
attempts, if neither does. `--skin none` is rejected outright (not a real
state) -- a `.skin` is the sole source of triangle/submesh/batch data, never
optional enrichment. `--skin-dir`/`--lod` only mean anything alongside
`--skin auto` (the default); giving either alongside an explicit `.skin`
path is a usage error.

**A model with real geometry but no resolvable `.skin` at all is a real,
known extraction gap, not a husk bug.** A 130k-file corpus sweep
found ~267 files -- spell-effect models, but also
ordinary item pieces (shoulder armor, collections/recolor variants) -- whose
`SFID` chunk names real FileDataIDs that simply don't exist anywhere in the
extraction, under any name: not next to the model, not FileDataID-renamed in
a `--skin-dir`, and not in the extraction tool's own "files it couldn't
place" bucket either (checked directly). This is the extraction never having
pulled those specific files, not husk failing to find or guess a real path
it has access to -- husk has no CASC/listfile access by design (see
`DESIGN.md`'s Non-goals) and can't re-request anything the extraction
skipped. `auto`'s error message already names exactly what it tried; there's
nothing more to try. Re-running the extraction tool (not husk) is the only
fix, if these specific assets are needed. A 130k-file corpus sweep also
found 334 genuinely 0-byte `.m2` files (same extraction-completeness class,
not even a magic value to read) and, separately, a `.skin` whose own
triangle-index buffer references far more vertices than its `.m2` actually
has -- confirmed on real quest-helm and raid-helm variant files, roughly
double the model's real vertex count, not a small offset -- and batch
records whose `materialIndex`/`textureComboIndex` point one past the end of
the model's own material/texture-combo array, confirmed uniformly across 16
real collections/recolor item variants. Both are real, stale/mismatched
Blizzard source data, not a husk defect: husk already does the right thing
by refusing to fabricate geometry or guess a material it can't verify,
failing loudly with the specific out-of-range index/count instead.

**Materials.** Correct `alphaMode`/`doubleSided` from WoW's blend mode/render
flags, plus a static `baseColorFactor` tint/fade from the batch's `M2Color`/
`M2TextureWeight` references. When that reference is genuinely animated
(a real per-sequence or global-sequence keyframe curve, not a single
constant value -- e.g. an eye-glow or enchant-glow pulse), core glTF still
has no way to *play back* an animated material property, but the real
resolved curve is attached as `tint_animation`/`fade_animation` material
`extras` (see `M2_COMPLETENESS.md`'s "Animated material tint/fade" row) for
a custom renderer or Blender script to apply itself, rather than silently
dropped. A texture whose `M2Texture::type` is nonzero (a hardcoded/
replaceable slot -- character skin, hair, item tint, resolved at runtime
from client-side DB2 data husk doesn't have) is tagged with a `texture_type`
material extras key, so a missing `baseColorImagePng` reads as "husk can't
resolve this slot at all," not "the `--textures` directory just didn't have
the file." Both UV sets are exported (`TEXCOORD_0`/`TEXCOORD_1`);
`baseColorTexture` samples whichever one the batch's `textureCoordComboIndex`
points at (pre-Cataclysm models only -- see `src/cmd_export.cpp`'s
`M2MaterialInputs`).

**Geosets.** Every primitive carries its submesh's real
`M2SkinSection.skinSectionId` as glTF `extras`
(`geoset_id`/`geoset_group`/`geoset_variant`). husk exports every submesh in
the `.skin`, including mutually-exclusive character-customization options --
it has no CASC/DBC access to ground a "correct" selection in (see
`DESIGN.md`) -- and prints every distinct geoset ID present whenever a
`.skin` has more than one, so a downstream renderer or Blender script can
filter using the `extras`.

**Second texture layers.** A batch with `textureCount > 1` (e.g. an
env-mapped "shine" pass) gets a note on export and carries its extra
layer(s) as material `extras` (FileDataID, UV set, embedded image if
`--textures` matches) -- only the first layer is ever wired into the
rendered material, since core glTF has no slot for WoW's fixed-function
combiner math.

**Skeleton + animation.** If the M2 has bones -- inline, or via `--skel`
for models that keep them external instead (see `src/skel.hpp`) -- they
resolve into a bind-pose glTF skin (`JOINTS_0`/`WEIGHTS_0`, inverse bind
matrices, joint-node hierarchy), plus real glTF `animation` clips per
`--anim`/`-a`'s four states:

- `auto` (default): inline sequences (`flags & 0x20`) + global-sequence bone
  tracks (eye-glow pulses, torch flicker, idle sway -- each its own
  `global_seq_<n>` clip, independent of any `M2Sequence`), both resolved
  straight from the model's own data, *plus* best-effort external-directory
  search (default: the model's own directory) for sequences stored in an
  external `.anim` file instead.
- `inline`: inline sequences + global-sequence tracks only; external search
  explicitly skipped.
- `none`: zero `animation` clips at all -- the bind pose itself
  (`JOINTS_0`/`WEIGHTS_0`, inverse bind matrices) is unaffected, only clips
  are suppressed.
- `<dir>`: explicit directory for the external-search stage; inline +
  global-sequence clips still resolve on top of it, same as `auto`.

External `.anim` files resolve either shape husk has seen in real files:
`AFM2` (flat, inline-boned models) or `AFSB` (`.skel`-linked models' real
shape, cracked and resolved -- see `DESIGN.md`/`WIKI_FINDINGS/M2/anim.md`). Only
the `0x40` ("alias") flag case is still skipped, never guessed --
wowdev.wiki: "I have no clue" where that data lives.

The external-directory search tries `<FileDataID>.anim` first (via the
model's/`.skel`'s own `AFID` table), falling back to
`<model-basename><animId>-<subAnimId>.anim` if that isn't found -- a real
`wow.export`-style extraction names its files this second way, not by
FileDataID (same two-stage shape `--skin auto`'s SFID-then-same-basename-scan
already uses).

**`.bone` corrections.** If the model's/`.skel`'s `BFID` array names any
`.bone` sidecar files and `--bones-dir` resolves any of them to a real
`<FileDataID>.bone` on disk, husk attaches every resolved file's real
`(bone_index, correction_matrix)` pairs as `bone_correction_sets` on the
glTF skin's own `extras` -- and stops there. These are never applied to the
bind pose or any animation: which of a model's several `.bone` files is
"correct" for a given character is selected by client-side customization-
choice data (which slider/dropdown value the player picked) husk has no
access to and, like geoset selection above, never will by design (see
`TODO_correctness.md` #6, `WIKI_FINDINGS/BONE.md`). A downstream renderer or
Blender script that does have that mapping has everything it needs to apply
the right slot on top of this data.

**`.phys` physics/collision data.** If `--phys` resolves a real `.phys`
sidecar (see `PFID`'s single-scalar-FileDataID resolution above, mirroring
`--skel`), husk attaches a minimal per-body placement anchor -- id, owning
bone, position, body type -- as a `physics_bodies` key on the glTF skin's
own `extras`, the same "inert placement data only" treatment ribbon/particle
emitters get. The full record set (every body/shape/joint/`PHYV` field,
resolved) is *not* duplicated into the `.glb` -- a single real file can have
40+ bodies, each with several shapes/joints, which would bloat every export
far more than `bone_correction_sets` ever did -- it's available via `husk
dump-chunks <file.phys>` instead (see the Usage section below), which also
accepts a `.phys` file directly, same as `.bone`.

Flags:

| Flag | Short | Meaning | Default |
|---|---|---|---|
| `--input <file.m2>` | `-i` | The model to export (required; 1st bare positional if omitted) | -- |
| `--output <file.glb>` | `-o` | Output path (last bare positional if omitted) | `<model-basename>.glb` |
| `--skin <path>` &#124; `auto` | `-s` | `.skin` path, or `auto` (see above); never `none` | `auto` |
| `--textures <dir>` &#124; `none` | `-t` | Directory of PNGs (from `husk-blp`, see below) for real `baseColorTexture` images, or `none` to never embed one -- matched by real filename first (the M2's own embedded name, or the model's own basename-prefixed convention), `<FileDataID>.png` only as a fallback, see below | model's own directory |
| `--skin-dir <dir>` &#124; `none` | -- | Directory `auto` searches for the SFID-declared `<FileDataID>.skin`, or `none` to skip that stage | model's own directory |
| `--anim <dir>` &#124; `auto`/`inline`/`none` | `-a` | See the four states above | `auto` |
| `--skel <path>` &#124; `none` | -- | External `.skel` path (0-inline-bone models only), or `none` to never look for one | same-basename `.skel` next to the model, if any |
| `--lod <n>` &#124; `all` | -- | With `--skin auto`, pick `SFID` entry `n` instead of 0, or resolve every entry into the same `.glb` as its own node (`lod0`, `lod1`, ...) sharing one skeleton/animation set (`husk info`'s `skin_file_data_ids` shows how many entries exist) | entry `0` |
| `--bones-dir <dir>` &#124; `none` | -- | Directory of `<FileDataID>.bone` files (per the model's/`.skel`'s `BFID` array), attached as inert skin `extras`; or `none` to skip | model's own directory |
| `--phys <path>` &#124; `none` | -- | External `.phys` path, attached as a minimal `physics_bodies` skin `extras` anchor (full records via `dump-chunks`), or `none` to never look for one | same-basename `.phys` next to the model, if any |
| `--collision none` | -- | Omit the collision mesh entirely (it's still tagged `{"collision": true}` in glTF extras when present, but Blender's stock importer has no concept of that tag and renders it like any other mesh -- `none` is for debugging what the render meshes alone look like) | included when the model has one |

Texture resolution deliberately tries real-filename matches before
`<FileDataID>.png`, for every texture slot, not just ones husk can't
resolve a FileDataID for -- a real extraction workflow (`reference/
wow.export`, or a `.blp` converted via `husk-blp` keeping its own real
name) commonly produces descriptively-named files, not FileDataID-named
ones. In order: (1) an exact match on the M2's own embedded filename, when
the file has one (real M2 data, older/classic-era files); (2)
`<FileDataID>.png`, when that specific file is actually present; (3) the
sole real file in `--textures` sharing the model's own basename prefix, if
exactly one remains unclaimed (two or more: reported, never guessed at).
Whichever FileDataID husk resolved for a slot is recorded either way
(material name suffix, and `texture_file_data_id` glTF extras) even when a
differently-named file supplied the actual image. Only (1) and (2) above
are genuinely deterministic; a match via (3) prints a `husk: warning:`
line naming the material and file (plus the resolved FileDataID, if the
slot has one, so it can be checked against a listfile/wow.tools by hand --
husk itself has no CASC/listfile access to verify a FileDataID's real name
against the file it claimed).

If no matching image is found in the resolved `--textures` directory,
materials still carry the correct blend mode, culling, and tint/fade -- they
render as a flat tinted surface instead of the real WoW texture.

Shell completion (bash/zsh) for every subcommand and flag above ships in
`completions/` -- see that directory's own files, generated from husk's real
flag definitions via the hidden `--print-completion=<bash|zsh>` flag rather
than hand-maintained.

Examples:

```
# everything (.skin, .skel, textures) sits next to the model already
husk export bloodelffemale.m2

# same, with paths spelled out explicitly
husk export bloodelffemale.m2 out.glb --skin bloodelffemale00.skin --textures ./png

# FileDataID-renamed-directory workflow, every LOD tier in one .glb
husk export bloodelffemale.m2 out.glb --skin-dir ./skins --lod all

# .skel-sourced skeleton, external animation data
husk export bloodelffemale_hd.m2 out.glb --skin bloodelffemale_hd00.skin \
  --skel bloodelffemale_hd.skel --anim ./anims
```

### `husk dump-chunks <file.m2>` / `husk dump-chunks <file.bone>` / `husk dump-chunks <file.phys>`

Extracts M2 data that doesn't feed `export`'s glTF output at all into
readable JSON on stdout. Two categories: Legion+-only chunk tags (mostly
rendering-effect/gameplay metadata glTF's material model has no equivalent
for -- see [roadmap stage 6's follow-on](#roadmap-modern-m2--blender-via-gltf)
and `DESIGN.md` for which chunks and why), and `ribbon_emitters`/
`particle_emitters` -- core header arrays present in every M2 version, every
field and every resolved animation curve (procedural emitter data, same "no
glTF slot" reasoning as the chunks, broadened to this command once real
particle/ribbon-bearing weapon files made full parsing possible to verify --
see `WIKI_FINDINGS.md`). `husk export` separately attaches a minimal
position/bone placement anchor for each emitter directly to the `.glb`'s skin
`extras`; this command is where the rest of the data lives.

Given a `.bone` file directly (no `.m2`/`.skel` magic to sniff -- husk falls
back to the `.bone` shape), dumps its per-bone correction matrices (`BIDA`/
`BOMT`, see `src/bone.hpp` -- reverse-engineered from real files; wowdev.wiki
documents no `.bone` byte layout at all, only the FileDataID array pointing
at these files).

Given a `.phys` file directly (sniffed by its own leading `PHYS` chunk tag,
byte-reversed on disk -- see `src/phys.hpp`), dumps every body/shape/joint/
`PHYV` record, each shape/joint resolved to its real type-specific data
inline -- unlike `.bone`, `.phys`'s byte layout *is* documented on
wowdev.wiki (`documentation/wowdev-wiki/md/PHYS.md`), verified against 103
real files (`WIKI_FINDINGS/PHYS.md`). `husk export --phys` separately attaches
a minimal per-body placement anchor to the `.glb`'s skin `extras`; this
command is where the full record set lives.

### Texture conversion (`husk-blp`)

Textures are a separate tool, not a `husk` subcommand -- `blp/` is a small
uv-managed Python package (see [roadmap stage
4](#roadmap-modern-m2--blender-via-gltf) below for why):

```
cd blp && uv sync
uv run husk-blp <file.blp> <output.png> [--mip N]
```

Converts a BLP2 texture to PNG (mip level 0, full resolution, by default).
Supports palettized, DXT1, DXT3, DXT5, and uncompressed-BGRA content —
DXT3 turned out to already be wired through the same generic DXT decode
path DXT1/DXT5 use (`_DXT_BLOCK_SIZE`/`_DXT_FOURCC` both already listed
it), just never exercised by a real test or verified against a real file
until a 779,056-file local-corpus scan found 6,759 real DXT3 BLP2 files
(character hair/skin textures among them) and one, decoded, turned out to
be an obviously-correct troll hair texture. JPEG content is genuinely
absent from that same corpus (zero real hits) and remains unimplemented —
see the format matrix. husk reads a
material's texture FileDataID off the M2's own `TXID` chunk; pass
`--textures <dir>` to `export` pointing at a directory of `husk-blp`-converted
PNGs named `<FileDataID>.png` to have them embedded -- husk doesn't go
looking for the matching file on its own (same non-goal as `.skin`/`.skel`
resolution above).

Every claim above is backed by a real-data run against
`character/bloodelf/female/bloodelffemale.m2` and its HD variant --
`tests/` and git history carry the verification detail (byte-exact counts,
the bugs it caught, the regression tests it left behind).

## Format support matrix (M2 / M3 / WMO / ADT / BLP)

The single source of truth for "does husk handle X yet." Rows are grouped
by feature area, not by chunk tag — WMO alone has ~70 documented chunk
tags across its root and group files, most of them small variants of a
handful of real features, so tracking by chunk would be noise. Every chunk
tag this matrix is built from is still named in the table/footnotes so
nothing is silently dropped, just grouped sensibly.

Built directly from wowdev.wiki (M2, M3, WMO, BLP pages, fetched
2026-07-24). **ADT column added 2026-07-31** — a scope declaration, not a
researched breakdown: husk's own domain was always meant to be "unmangle
WoW's proprietary static files into industry-standard/engine-agnostic
ones," and terrain (crucial for any actual rendered world, not just
props/buildings) was simply missed when this project was first scoped, not
deliberately excluded. Grounded in one read of wowdev.wiki's `ADT/v18`
page (already mirrored locally) and its real chunk-tag list, not yet
cross-checked against real files or read in the depth M2/WMO's existing
columns were — several cells below are marked ❔ for exactly that reason,
and even the ⬜ cells should be read as "real chunk identified, not yet
verified" rather than "confirmed correct."

**Legend:**

| Symbol | Meaning |
|---|---|
| ✅ | Read + write implemented |
| 📖 | Read implemented, write not started |
| 🚧 | Partially read (some fields/counts only, not full contents) — current MVP state |
| ⬜ | Not started |
| ⬛ | N/A — this concept doesn't exist in this format |
| ❔ | Blocked — not fully documented upstream as of the 2026-07-24 fetch, not just untackled by us |

| Feature | M2 | M3 | WMO | ADT | BLP |
|---|---|---|---|---|---|
| Chunk container / magic detection | 📖 `MD20`/`MD21` detected, generic (tag-agnostic) chunk reader in `src/chunk.*`; every top-level chunk tag found is tracked (`Header::chunkTags`) and cross-checked against a wiki-sourced known-tag list (`husk info`'s `documentedM2ChunkTags`), flagging anything husk has never seen — see `DESIGN.md` for why this format needs that. `WFV1`/`WFV2`/`DPIV`/`AFRA` (no wowdev.wiki struct at all) are now byte-decoded and structurally parsed by `husk dump-chunks` too, not left as a raw hex dump — see `WIKI_FINDINGS.md` | ⬜ (chunked like WMO, 16-byte chunk header + property fields) | ⬜ (`MOMO` wrapper, Legion+) | ⬜ `MVER`+`MHDR` wrapped, chunked like WMO/M3 — but also split across a root file plus `_obj0`/`_obj1`/`_tex0`/`_tex1`/`_lod` sidecar files since Cataclysm, a bigger structural difference than one extra chunk (not researched in depth this pass) | ⬛ flat header, not chunked |
| Header / global metadata | 📖 `husk info` reads magic/version/name/bounding boxes/array counts, `global_flags` decoded into its wiki-named bits (`m2::globalFlagNames`, alongside the raw hex value), and `textureCombinerCombos` (the header struct's own last field, only present when `flag_use_texture_combiner_combos` is set) — every field the wowdev.wiki header struct documents, not just counts, though a real 130,576-file local-corpus scan found zero files with that one flag actually set | ⬜ `M3DT` (376 bytes: flags, 2 bounding boxes, particle count) | ⬜ `MOHD` (counts, ambient color, WMOID, bounding box, flags) | ⬜ `MHDR` (offsets to every sub-chunk region, flags) | ⬜ 148-byte header + 1024-byte palette/JPEG region |
| Skeleton / bone hierarchy | 📖 `bones` resolved to `key_bone_id`/`flags`/`parent_bone`/`pivot` (`src/m2.cpp`'s `parseBones`) whether inline in the M2 or, via `SKID` → `.skel` → `SKB1` (`src/skel.cpp`, `husk export`'s optional 4th argument), in an external file — both feed the same bind-pose glTF joint hierarchy, and (see the next row) the same animation resolution. `SKB1`'s own `key_bone_lookup` field, and `.skel`'s `SKL1`/`SKA1`/`SKPD` chunks, are still unread — none needed for a bind-pose skeleton or its animation. `husk info` flags when a model needs this (0 inline bones + an `SKID` chunk present) instead of silently reading as bone-less. A real M2 bone array is often a forest, not a tree — WoW never required a single root (35% of a real 130k-file corpus has more than one root bone, `tools/find_multiroot_skeletons.py`); `husk export` represents this faithfully by synthesizing one plain, non-joint glTF node as the parent of every real root joint (never a fake extra joint, never a dropped bone) so glTF tooling built around a single common root, `gltf_validator` included, doesn't misbehave — see `DESIGN.md`'s Key design decisions. | ❔ no joint-hierarchy chunk documented — only per-vertex weights/bind-poses exist (`VWTS`/`VIBP`) | ⬛ no skeleton | ⬛ no skeleton | ⬛ |
| Vertex skinning (bone weights/indices) | 📖 read as part of `M2Vertex` (`bone_weights[4]`/`bone_indices[4]`), wired into a glTF skin (`JOINTS_0`/`WEIGHTS_0`) via `husk export` | ⬜ `VWTS` (weights), `VIBP` (inverse bind poses) | ⬛ | ⬛ | ⬛ |
| Mesh geometry (positions, indices) | 📖 `vertices` array resolved to real `M2Vertex` records (`src/m2.cpp`); triangle indices resolved via one explicitly-given `.skin` file (`src/skin.cpp`); exported to glTF via `husk export` | ⬜ `VPOS`/`VINX`/`VGEO`+`Geoset`/`LODS`/`RBAT` | ⬜ `MOVT`/`MOVI`/`MOVX`, `MOBA` batches, `MORI`/`MORB` triangle-strip variants | ⬜ `MCVT` (per-`MCNK` 9×9+8×8 heightmap grid, one of 256 `MCNK`s per tile) — indices implicit from the fixed grid topology, not stored | ⬛ |
| Normals | 📖 part of `M2Vertex`, resolved | ⬜ `VNML` | ⬜ `MONR` | ⬜ `MCNR` | ⬛ |
| UV / texture coordinates | 📖 both `tex_coords[2]` sets resolved and both exported to glTF (`TEXCOORD_0`/`TEXCOORD_1`); a material's `baseColorTexture` samples whichever set the batch's `textureCoordComboIndex` selects (pre-Cataclysm models only — see `src/cmd_export.cpp`) | ⬜ `VUV0`–`VUV5` (up to 6 sets) | ⬜ `MOTV` | ⬛ not stored as an explicit array — terrain texture mapping is reportedly computed from world position + each `MCLY` layer's own scale (inferred from general ADT structure, not confirmed against wiki text this session) | ⬛ |
| Tangents | ❔ not in the documented base header — appears to be runtime-computed, not stored | ⬜ `VTAN` | ⬜ `MOTA` (often auto-generated client-side for shaders 10/14) | ❔ not checked | ⬛ |
| Per-vertex colors | 📖 not truly per-vertex (M2's `colors`/`textureWeights` are per-*batch* material tint/fade, not per-vertex mesh color — see `src/m2.cpp`'s `Color`/`TextureWeight`); resolved into glTF `baseColorFactor` by `husk export` as a *static* approximation (only when the underlying `M2Track` is unambiguously constant — real keyframe animation is stage 6, see `DESIGN.md`) | ⬜ `VCL0`/`VCL1` | ⬜ `MOCV`/`MOC2` | ⬜ `MCCV` — genuinely per-vertex here, unlike M2's per-batch tint | ⬛ |
| LOD / mesh views | 🚧 each LOD tier's `.skin` file's `vertices`/`indices` lookup tables, plus `submeshes`/`batches` (material/texture linkage per submesh, `src/skin.cpp`'s `Submesh`/`Batch`) read directly; `.skin` filename can be given explicitly, or auto-selected via the M2's own `SFID` chunk + `husk export --skin-dir <dir>` (`--skin` defaults to `auto`; defaults to the highest-detail LOD; `--lod <n>` picks a specific `SFID` entry, `--lod all` exports every entry as its own named node in one `.glb`, roadmap stage 8, see Usage) — `LDV1` `lod_count` (`husk info`) is now a real `--lod <n>` range-check consumer, not just display; `Submesh.skinSectionId` (the "geoset ID") is read and carried into every exported primitive as glTF `extras`, but not filtered on (no CASC/DBC data to ground a selection in, see `DESIGN.md`) — every submesh, including mutually-exclusive character-customization options, is always exported; still 🚧 because `Submesh`/`Batch`'s other non-material fields (culling/sort/hardware-bone-limit metadata `src/skin.hpp` documents but doesn't read -- husk exports full per-vertex global joint indices instead of the engine's per-drawcall bone-limit mechanism these exist for, see `src/cmd_export.cpp`'s `buildSkinning`) still aren't | ⬛ `LODS` folds LOD into the one file, no sidecar | ⬛ (`GFID`'s `Flag_Lod` is a different, coarser concept — tracked under World/group structure) | ⬜ real and reportedly substantial (see `ADTLodImplementation.md`, already mirrored locally in `wow_modding`) — not read this session | ⬜ mip pyramid — tracked under Texture pixel data below, not here |
| Collision / physics | 📖 `bounding_box`/`collision_box`/`collision_sphere_radius` scalars printed by `husk info`; the low-poly hit-test mesh's own content is real, dereferenced data (`m2::CollisionMesh`/`parseCollisionMesh`, `src/m2.cpp`) — `husk export` writes it as a plain indexed triangle mesh in the `.glb` (one more `gltf::NamedMesh`, unskinned even when the render mesh shares a skeleton, per-vertex normals averaged from the M2's own per-face `collision_face_normals`), tagged `{"collision": true}` in glTF node `extras` so a renderer/Blender script can filter it out — not applied to any render/physics behavior itself, this tool has no runtime. The `.phys` sidecar's own *content* (rigid bodies, collision shapes, joints for Blizzard's "Domino" physics engine) is now fully parsed too (`src/phys.hpp`/`phys.cpp`, documented on wowdev.wiki, verified against 103 real files — `WIKI_FINDINGS/PHYS.md`): `husk export --phys` attaches a minimal per-body placement anchor (`physics_bodies` skin extras, same id/bone/position pattern as ribbon/particle emitters below), full body/shape/joint/`PHYV` records via `husk dump-chunks <file.phys>` (see the Usage section's "`.phys` physics/collision data" paragraph). `PCOL` (player-housing collision, War Within 11.1.7+) is also fully parsed, diagnostic-only via `husk dump-chunks` — four independent `(count, offset)` regions (`vertexPositions`/`faceNormals`/`indices`/`flags`), verified against all 2,354 real `PCOL`-bearing files in the local corpus (`WIKI_FINDINGS/M2.md`); no glTF slot, same class as `EXP2`/`PFDC`/`DETL` | ⬜ `M3CL` collision mesh (`CPOS`/`CNML`/`CINX`) | ⬜ `MOBN`/`MOBR` BSP tree, `MCVP` convex volumes, `MOPL` terrain-cutting planes | ⬛ no separate chunk found — terrain collision is presumably the render mesh itself (inferred, not confirmed) | ⬛ |
| Materials | 📖 `materials` array (`flags`/`blending_mode`, `src/m2.cpp`'s `parseMaterials`) resolved per-batch and translated to glTF `alphaMode`/`doubleSided`, plus a static color tint/alpha-fade into `baseColorFactor` (see Per-vertex colors above), by `husk export` (`src/cmd_export.cpp`) — write-back to M2 not applicable (glTF-only tool); a batch's additional texture layers (`M2Batch.textureCount > 1` — a real second env-mapped/blended layer, wowdev.wiki M2/.skin#Texture_units) are resolved and surfaced as glTF `extras` (FileDataID/UV set, plus a real embedded-but-unused image if `--textures` has a match) but not rendered — core glTF has no slot for WoW's fixed-function combiner math (`Mod2x`/`Add`/env-map blending) to translate into; a batch's UV scroll/rotate/scale animation (`M2TextureTransform`, `Header::textureTransforms` + `.skin`'s `Batch.textureTransformComboIndex` — flowing lava/water, some portal/aura effects) is likewise resolved (`m2::parseTextureTransforms`) and surfaced as `extras` (`gltf::Material::textureTransform`) rather than a real `KHR_texture_transform`, for the same "no verified-safe translation" reason: core glTF's own extension has no animation-channel target either (so the animated case, almost certainly the common one for a real scrolling-UV model, has no representation regardless), and correctly folding WoW's texture-center rotation pivot into the extension's own origin-based one hasn't been checked against a real animated file yet — see `src/m2.hpp`'s `TextureTransform` doc comment | ⬜ `M3SI` Instances → external `MaterialLibrary` (`.mtl3lib`) | ⬜ `MOMT`, `MOM3` (v3 override), `MOUV` (UV anim), per-face `MOPY`/`MPY2`/`MOBS` | ⬜ `MCLY` (per-layer blend/material flags) + `MTXP`/`MTXF` (texture params/flags) | ⬛ |
| Texture references (names/FileDataIDs) | 📖 `textures` array (`type`/`flags`/`filename`) + `textureCombos` lookup table resolved (`src/m2.cpp`); Legion+ `TXID` chunk FileDataIDs surfaced (`Header::textureFileDataIds`) — same non-resolved-to-a-path treatment as `SKID`, see the Sidecar row below | ⬜ indirect, via `MaterialLibrary` → compiled shader files (`GFAT`/`BLS`) — separate formats, not yet even scoped | ⬜ `MOTX` | ⬜ `MTEX` (texture filename table) | ⬛ BLP is the referenced asset, not a referencer |
| Texture pixel data | ⬛ | ⬛ | ⬛ | ⬛ BLP is the referenced asset, not a referencer (same as M2/WMO) | 📖 `blp/` (Python, `husk-blp` CLI) — header + mip table resolved, palette/DXT1/DXT3/DXT5/BGRA decode to PNG done (DXT3 was already generically wired through the same DXT decode path as DXT1/DXT5, just never verified — a 779,056-file local-corpus scan found 6,759 real DXT3 files, confirmed correct against a real one); JPEG content unimplemented and confirmed genuinely absent from that same corpus (zero real hits, not just unseen in this repo's own small test set) |
| Animation sequences / tracks | 📖 `sequences` resolved to real `M2Sequence` records (`id`/`variationIndex`/`duration`/`flags`) whether inline (`src/m2.cpp`'s `parseSequences`) or `.skel`-sourced (`SKS1`, `src/skel.cpp`'s `parseSequences`); each bone's `translation`/`rotation`/`scale` `M2Track` resolved per-sequence (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`) into real glTF `animation` clips by `husk export`, for sequences whose data lives inline (`flags & 0x20`) *or* an external `.anim` file resolved via `--anim` + the model's own `AFID` chunk (or the `.skel`'s own separate `AFID` table, for a `.skel`-sourced skeleton), falling back to a same-basename filename convention when no `AFID`-mapped file resolves — verified against real files both ways (see the Usage section and roadmap stage 6), `AFM2`- and `AFSB`-shaped external files alike (`AFSB` is the real shape for `.skel`-linked models; its undocumented byte layout was cracked — `SKB1`'s own per-bone/per-sequence descriptors point directly into it, no new parser needed, see `WIKI_FINDINGS/M2/anim.md`'s follow-up). A bone track whose `global_sequence` field is set (continuous, `M2Sequence`-independent looping — glow pulses, idle sway) also resolves to its own real glTF clip (`resolveVec3GlobalSequenceTrack`/`resolveQuatGlobalSequenceTrack`, one clip per distinct global-sequence index actually used), for inline and `.skel`-sourced bones alike. Still unresolved: sequences with `flags & 0x40` ("alias", wowdev.wiki: "I have no clue" where that data lives) | ❔ no sequence/track chunk documented in the fetched spec at all | ⬛ (`MOUV` texture-translation anim is the closest thing; counted under Materials) | ⬛ no animation in core terrain data | ⬛ |
| Interaction points (attachments, cameras, events) | 🚧 `attachments`/`events` resolved to real records (`id`/`bone`/`position` for attachments, `identifier`/`data`/`bone`/`position` for events — both static-fields-only, their own `M2Track` sub-fields skipped, `src/m2.cpp`'s `parseAttachments`/`parseEvents`, surfaced via `husk info`); `cameras` still count/offset-only — `M2Camera` is almost entirely `M2SplineKey`-animated data with a version-ambiguous field layout, not attempted yet | ❔ not present in the fetched chunk list | ⬛ | ⬛ | ⬛ |
| Lights | 🚧 `lights` resolved to `type`/`bone`/`position` (`src/m2.cpp`'s `parseLights`, static fields only — ambient/diffuse color+intensity and attenuation are all `M2Track`-animated and skipped), surfaced via `husk info` | ❔ | ⬜ `MOLT` + `MOLR`/`MOLS`/`MOLP` + Shadowlands lightset system (`MLSS`/`MLSP`/`MLSO`/`MLSK`), `MNLD` dynamic lights, legacy v14 `MOLM`/`MOLD` lightmaps | ❔ not checked — outdoor lighting may be zone/`Light.dbc`-driven rather than stored per-ADT, unconfirmed | ⬛ |
| Particles / ribbons (effects) | 📖 both `m2::Ribbon`/`parseRibbons` and `m2::ParticleEmitter`/`parseParticles` (`src/m2.cpp`) fully dereference every field, including the M2Track/FBlock-based animation curves — Ribbon's 6 tracks and Particle's ~10 `M2Track<float>` simulation parameters resolved per-sequence (or via the global-sequence resolver, `resolveFloatTrackSequence`/`resolveVec3TrackSequence`/`resolveRawIntTrackSequence`), Particle's Wrath+ `FBlock`-based color/alpha/scale/UV curves resolved directly (no per-sequence indirection, `resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16`) — verified against real weapon particle data (`mace_2h_bolvar_d_01.m2`: decoded colors form a real fire/ember gradient, alpha/scale curves are clean envelopes, the `MultiTexture` flag bit correlates exactly with non-zero `multiTexScale` — see `WIKI_FINDINGS.md`). `M2Particle` is gated to `kMinVerifiedParticleVersion` (272, Cataclysm — the shape genuinely changed there, unlike Ribbon/Bone/Sequence's Wrath floor); below that, count-only with a loud warning, same policy as `kMinVerifiedRecordStrideVersion`. Neither type has a native glTF representation (procedural emitters, not geometry): the **full** record (every field, every resolved curve) is `husk dump-chunks`'s JSON output (`ribbon_emitters`/`particle_emitters` keys, broadened from that command's original Legion+-chunk-only scope — see `src/cmd_dump.cpp`'s doc comment); `husk export` additionally attaches a **minimal** placement anchor (id/bone/position only) to the `.glb` skin's `extras`, so a Blender script can place a marker without needing the JSON at all. `TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1` (small per-particle side-chunks, a separate concern from the M2Particle record itself) remain `dump-chunks`-only, not integrated into glTF | ❔ `M3PT` chunk family declared but wiki notes "not yet seen in files" | ⬜ `MPVD` particulate volumes, `MAVG`/`MAVD`/`MBVD` ambient/box volumes + their `*VR` reference lists | ⬛ `MCSE` (sound-emitter placement) exists but is a distinct concept from particle emitters, not conflated here | ⬛ |
| Fog / environment volumes | ⬛ | ⬛ | ⬜ `MFOG` + `MFED` extra data + `MFOB` fog objects | ❔ not checked | ⬛ |
| Liquid / water | ⬛ | ⬛ | ⬜ `MLIQ` | ⬜ `MH2O` (Cata+) / `MCLQ` (legacy, pre-Cata, superseded by `MH2O`) | ⬛ |
| Portals / visibility culling | ⬛ | ⬛ | ⬜ `MOPV`/`MOPT`/`MOPR`/`MOPE`, `MOVV`/`MOVB` visible-block lists | ⬛ not an ADT concept (WMO-specific) | ⬛ |
| Doodad / object placement (scene composition) | ⬛ | ⬛ | ⬜ `MODS`/`MODN`/`MODI`/`MODD`/`MODR` + `MDDI`/`MDDL` additional info | ⬜ `MDDF` (M2 doodad placement) + `MODF` (WMO placement), backed by `MMDX`/`MMID` and `MWMO`/`MWID` filename tables — arguably the single most important ADT row: this is what actually places M2/WMO instances into the rendered world at all | ⬛ |
| World/group structure (root+group files, skybox) | ⬛ | ⬛ single-file, no group split | ⬜ `MOGN`/`MOGI`/`MOGP`/`MOGX`/`GFID` + `MOSB`/`MOSI` skybox + `MGI2` group-info-v2 | ⬜ no group-file split the way WMO has, but Cata+ splits one logical tile across root/`_obj0`/`_obj1`/`_tex0`/`_tex1`/`_lod` files — see row 1's caveat | ⬛ |
| Sidecar FileDataID resolution | 📖 `SFID`/`AFID`/`BFID`/`PFID`/`SKID`/`TXID` → `.skin`/`.anim`/`.bone`/`.phys`/`.skel`/BLP textures — none of these FileDataIDs are resolved to a *WoW/CASC* path (no CASC/listfile access, by design, see the README's Usage section) — that CASC-resolution half is a deliberate non-goal, not a deferred read (`DESIGN.md`'s Non-goals), so *local-file* resolution (all six IDs, below) is the full scope of "read" this row measures, and it's fully implemented; `SKID` is surfaced as a raw ID only (`husk info`; `Header::skeletonFileId`), and `SFID`/`TXID`/`AFID`/`BFID` get a *local-directory* resolution convention -- `husk export`'s `--skin-dir <dir>`/`--textures <dir>`/`--anim <dir>`/`--bones-dir <dir>` look for `<dir>/<FileDataID>.skin`/`.png`/`.anim`/`.bone`, a directory the user populates themselves (e.g. via `husk-blp`), never CASC. `PFID` gets a *same-basename-file* resolution convention instead, mirroring `SKID`/`.skel` (`PFID`, like `SKID`, is a single scalar FileDataID, not an array) -- `husk export --phys <path|none>` (unset: auto-detects a same-basename `.phys` next to the model). `.skin`/`.skel`/`.phys` paths can also still be given explicitly instead, and so can a `.bone` or `.phys` file directly to `dump-chunks` (see the Usage section) -- `.bone` *content* itself is parsed (`src/bone.hpp`, reverse engineered, no wowdev.wiki byte layout exists for it) and, via `--bones-dir`, attached to the exported glTF skin as inert `bone_correction_sets` extras (never applied to the render -- see the Usage section's "`.bone` corrections" paragraph, `TODO_correctness.md` #6). `.phys` *content* is parsed too (`src/phys.hpp`, documented on wowdev.wiki, verified against real files -- `WIKI_FINDINGS/PHYS.md`) and, via `--phys`, attached as inert `physics_bodies` extras (never applied to the render) | ⬛ self-contained, no sidecars per spec | ⬜ `GFID` → group files | ⬜ Legion+ ADTs reference sidecar/FileDataID-bearing chunks too (`MHID`/`MDID`/`MWDR`/`MWDS` seen in the raw chunk-tag list) — roles not yet confirmed | ⬛ |

**Not individually rowed above** (still real, just low-priority/niche —
tracked here so nothing's silently dropped): WMO's `MOQG`/`MOGX` per-face
`groundType` gameplay metadata; `MDAL`/`MOPB` material/prepass-batch
overrides; `MOMX` (structure entirely unknown, "just a guess" per the
wiki itself); `MPB*` (present in exactly one known alpha file, never read
by any shipped client — not planned). v14-alpha-only WMO chunks (`MOLV`,
`MOIN`, pre-1.0 `MOLM`/`MOLD`) are real but scoped out until there's an
actual reason to read a 2004-era WMO.

**Related sidecar/dependency formats** (their own files, not covered by
the matrix above, will need their own row set once work starts on them):
M2's `.skin` (mesh/LOD views), `.anim` (offloaded animation data),
`.bone`, `.phys`, `.skel`; M3's `.mtl3lib` (`MaterialLibrary`) and the
`GFAT`/`BLS` compiled-shader files it points to; ADT's own `.wdt` (per-map
index of which ADT tiles exist, plus the global WMO placement for maps
that are a single WMO — an instance dungeon, say — instead of open
terrain) and `.wdl` (a coarse, whole-continent low-resolution heightmap,
used for distant/minimap-scale terrain — the wiki page is already mirrored
locally as a directory, `wowdev-wiki/md/WDL/`). All: not started.

## Roadmap: modern M2 → Blender, via glTF

The eventual goal is a real Blender import path — mesh, skeleton, textures,
materials, and animation, for a *modern* (Legion+ chunked) M2. husk doesn't
write a Blender addon itself; the target is a sensible glTF 2.0 export
(binary `.glb`, core PBR metallic-roughness material model) that Blender's
own built-in glTF importer can open unmodified. That keeps husk's job
scoped to "read WoW formats, write correct glTF" — Blender-side concerns
(addon UI, live reimport, etc.) are explicitly out of scope unless the
glTF path turns out to be insufficient.

The order below is a dependency chain, not a wishlist — each stage only
makes sense once the one before it works, and each is meant to be a
demoable milestone (something you can actually open in Blender and look
at), not an invisible internal refactor. Current status per piece is
tracked precisely in the [format support matrix](#format-support-matrix-m2--m3--wmo--blp)
above; this section is about *sequencing* that work, not duplicating it.

1. **Static mesh, no material. Done** — `husk export <file.m2> <file.skin>
   <out.glb>` (see [Usage](#usage) above). Resolves the `vertices` array's
   actual contents (`M2Vertex`: `pos`, `bone_weights[4]`, `bone_indices[4]`,
   `normal`, `tex_coords[2]`), not just the array's `(count, offset)` pair.
   M2 itself has no triangle indices; those come from a `.skin` file
   (LOD/submesh views) — its filename is given explicitly on the command
   line for now, not yet resolved automatically via the Legion+ `SFID`
   chunk (that's still open, see the format matrix's "Sidecar FileDataID
   resolution" row). Positions/normals/UVs/indices get written into a
   minimal glTF (no material, no image — Blender will render it flat gray).
   The Z-up (WoW) → Y-up (glTF) coordinate flip is applied in
   `src/gltf.cpp`'s `zUpToYUp` — **`(X, Y, Z)` → `(X, Z, -Y)`**, not the
   `(X, -Z, Y)` wowdev.wiki's own note literally states: that literal
   formula, composed with Blender's own glTF-import axis convention,
   produced a real, measurably upside-down import (found much later, once
   Blender actually entered the picture — see `TRANSFORM_TRIAGE.md` for the
   full investigation and fix). Verified against `bloodelffemale.m2` +
   `bloodelffemale00.skin` (8061 vertices, 10,458 triangles, glTF binary
   framing round-trips through tinygltf's own loader intact); the
   Blender-specific verification this stage's own text originally flagged
   as missing is now real, automated, headless coverage (see "Testing"
   below and `TRANSFORM_TRIAGE.md`) — a real animated pose visually
   confirmed in Blender's own GUI is still the one open item.
2. **Skeleton + skinning, still untextured. Done** — `husk export` now
   resolves the `bones` array (`M2CompBone`: `parent_bone`, `pivot`,
   `flags`, `key_bone_id`; the embedded `M2Track` animation blocks are
   skipped, not parsed — that's stage 6 below) and wires `M2Vertex`'s
   `bone_weights`/`bone_indices` into a glTF skin (`JOINTS_0`/`WEIGHTS_0`
   accessors, inverse bind matrices, a joint-node hierarchy from the bone
   parent chain, see `src/gltf.hpp`'s `Skeleton`). `M2Vertex.bone_indices`
   are direct indices into the M2's own `bones` array — confirmed against
   [pywowlib](https://github.com/wowdev/pywowlib)'s M2 writer, since the
   `.skin` file's own, *differently*-indirected `bones` lookup table (see
   its wiki page's debated remarks) is a separate, unrelated field this
   parser doesn't touch. Since M2's bind pose has no baked rotation/scale,
   each joint's inverse bind matrix is a plain translation by its negated
   absolute pivot — no matrix-chain composition needed. Success at this
   stage is "imports as an armature-bound mesh in the correct bind pose" —
   no animation playback yet. Verified against `bloodelffemale.m2` (119
   bones): exports cleanly, round-trips through tinygltf's own loader with
   a populated skin/joint hierarchy; **not yet verified** in Blender itself
   (same caveat as stage 1). Models with a genuinely empty inline `bones`
   array correctly fall back to an unskinned mesh, same as stage 1's
   output — but as stage 3 below explains, "inline `bones` array is empty"
   and "this model has no skeleton" turned out to be different things, so
   that fallback currently fires for some models that do have one.
3. **External skeleton sidecar (`.skel` via `SKID`). Done** — belongs here,
   not down in the animation stage, because what it resolves is structural
   (bind-pose bones), the same category of thing as stage 2 above — it
   just lives in a different file for some models. Legion+ (7.3+) can
   move a model's `bones` array out of the M2 entirely: an `SKID` chunk
   (`uint32_t SKeletonfileID`) points at a `.skel` file, itself chunked
   (`SKL1` header, `SKA1` attachments, `SKS1` sequences, optional `SKPD`
   for parent-skeleton dedup — see [wowdev.wiki
   M2/.skel](https://wowdev.wiki/M2/.skel)), whose `SKB1` chunk holds
   `M2Array<M2CompBone> bones` — the *identical* struct `m2::parseBones`
   already read for stage 2, just relocated (`src/skel.cpp` reuses it
   directly rather than re-parsing bones a second way). So this stage
   turned out to be exactly "find and read one more chunk + one more
   file," not a new struct to reverse-engineer. `husk export` takes the
   `.skel` path as an optional 4th argument, used only when the M2's own
   `bones` array is empty (a model with inline bones takes priority; a
   redundant `.skel` argument is noted on stderr and ignored, not an
   error); `husk info` now also flags the `SKID` file data ID when it's
   present and inline bones are empty, so the "0 bones" gap this stage
   closes doesn't have to be rediscovered by hand again. Deliberately
   **not** in scope for this stage: the `BFID` chunk (`.skel`'s or M2's)
   pointing at numbered `${basename}_${i}.bone` files — per wowdev.wiki,
   those replaced *per-bone animation track* data (the same category as
   `.anim`/`AFID`), not bind-pose structure, so they're a stage-6 concern,
   not this one. Verified against `bloodelffemale_hd.m2` +
   `bloodelffemale_hd.skel` in this repo's test data: resolves all 245
   bones from a 23.4 MB `.skel` file (dominated by embedded `M2Track`
   keyframe data -- stage 3 itself only reads the bind pose, this stage's
   job; stage 6 below is what later put that keyframe data to use) in well
   under a second, and produces a glTF skin that round-trips through
   tinygltf's own loader the same as stage 2's inline-bones case.
4. **Textures: BLP → PNG. Done** — a hard prerequisite for materials to
   show anything other than gray, and genuinely separate work from M2
   parsing, so genuinely separate it's not even C++: `blp/` is a small
   Python package (`husk-blp <file.blp> <out.png> [--mip N]`, uv-managed,
   `nix develop` provides `uv` — see the flake and `blp/pyproject.toml`),
   not part of the `husk` binary. Split of responsibility inside it: the
   BLP2 container format itself (148-byte header + 1024-byte
   palette/JPEG-header region, mip offset/size tables) is hand-rolled and
   independently spec-transcribed, same rigor as husk's C++ modules — it's
   plain structured data, nothing fuzzy about it. Actual DXT1/DXT3/DXT5
   block decoding is *not* hand-rolled: `husk_blp/decode.py` wraps the raw
   compressed bytes from a mip level in a minimal synthetic DDS container
   (bit-for-bit the same block layout) and hands that to Pillow's own,
   battle-tested DDS reader, rather than reimplementing color/alpha
   interpolation math — confirmed pixel-correct against hand-built
   single-block fixtures first (`blp/tests/test_decode.py`), not assumed.
   Scoped to the three encodings this repo's real test data (1021 `.blp`
   files under `test_data/character/bloodelf/female/`) actually contains
   — Palettized (alpha depth 0), DXT1 (opaque), DXT5 (interpolated alpha)
   — checked empirically before writing any decode code, not assumed;
   uncompressed BGRA is also implemented (trivial, no library needed) even
   though this test data doesn't happen to use it. Explicitly deferred:
   DXT3 (unseen in this repo's test data, so not a confirmed-needed gap
   yet) and JPEG content (wiki: rare in BLP2). Palette alpha depth 4 is
   also deliberately unimplemented and raises a clear error rather than
   guessing — the wiki's own spec table doesn't clearly document that
   value's bit layout, unlike the other three depths. Verified against
   three real files, one per implemented DXT/palette encoding, output
   inspected directly (not just "didn't crash"): a DXT1 face texture, a
   DXT5 particle-effects atlas with working transparency, and a palettized
   face-detail texture — all visually correct, unmistakably the right
   image content, not garbage that merely happened to be the right
   dimensions.
5. **Materials. Done** — resolves the `.skin` file's `batches` array
   (`M2Batch`, wowdev.wiki M2/.skin#Texture_units, `src/skin.cpp`'s
   `Batch`) into one glTF material + primitive per batch: `batch ->
   submesh` (a slice of the already-resolved triangle-index buffer, via
   `Submesh.indexStart`/`indexCount`) `-> materialIndex` (M2's own
   `materials` array, `m2::Material`'s `flags`/`blendMode`) `->
   textureComboIndex` (through the header's `textureCombos` lookup table,
   wowdev.wiki's "Texture lookup table") `-> textures` array entry. WoW's
   `M2BLEND_*` blend mode collapses to glTF's three-way `alphaMode`
   (`0`→`OPAQUE`, `1`→`MASK`, everything else — real alpha blend plus the
   additive/multiply modes glTF's core material model has no equivalent
   for — `BLEND` as the closest approximation) and the material's
   "two-sided" render flag (`0x04`) becomes glTF `doubleSided`
   (`src/cmd_export.cpp`'s `alphaModeForBlend`). Deliberately **not**
   attempting real PBR authoring (roughness/metalness/normal maps) — WoW's
   own shader model doesn't map cleanly onto metallic-roughness, and
   faking plausible-looking values is a separate, later problem from
   "does the right blend mode show up on the right part of the mesh."
   One glTF material per *batch* rather than per M2 material index is
   deliberate: a batch is the thing that actually pins down both blend
   mode *and* texture together, and nothing in the format guarantees two
   batches sharing a `materialIndex` also share a texture (this repo's
   real test data happens to keep that 1:1, but the spec doesn't promise
   it) — some duplicate-looking glTF materials is a fair trade for never
   guessing.
   `baseColorTexture` embedding is real, not just metadata: husk still
   doesn't resolve a texture's FileDataID to a WoW/CASC path itself (same
   non-goal as `.skin`/`.skel`, see Usage below and the format matrix's
   Sidecar row) — but `husk export --textures <dir>` embeds an actual PNG
   when one already exists at `<dir>/<FileDataID>.png` (produced by
   `blp/`'s `husk-blp`, roadmap stage 4), closing the loop between the two
   tools without husk ever touching CASC or a listfile. Without
   `--textures`, materials still get the right `alphaMode`/`doubleSided`,
   just no image — both paths verified against real data (see Usage).
   Four follow-on pieces extend this stage rather than opening a new one:
   **vertex-color/transparency** (`M2Color`/`M2TextureWeight`, resolved
   into `baseColorFactor` as a *static* value only when the underlying
   `M2Track` is unambiguously constant — see `DESIGN.md` for
   why that safeguard exists, and `src/m2.cpp`'s `Color`/`TextureWeight`);
   **the second UV set** (`TEXCOORD_1`, plus per-material `texCoord`
   selection via `textureCoordCombos`, pre-Cataclysm-only); **LOD
   auto-selection** (`SFID` + `husk export --skin-dir <dir>` -- `--skin` defaults to `auto`,
   `LDV1`'s `lodCount` surfaced for information); and **`BFID`/`AFID`
   surfaced as raw FileDataIDs** (`husk info`), the same
   surface-now-resolve-later treatment `SKID` got before `.skel` support
   existed — real `.anim` *content* parsing (and `.bone`'s, though not
   integrated into glTF export) is stage 6 below.
6. **Animation. Done, for inline bones and `.skel`-sourced bones alike,
   verified against real data both ways.** `husk export` resolves
   `sequences` into real `M2Sequence` records -- inline (`src/m2.cpp`'s
   `parseSequences`) or, for a `.skel`-sourced skeleton, that `.skel`'s own
   `SKS1` chunk (`src/skel.hpp`'s `parseSequences`, same struct/stride) --
   and each bone's `translation`/`rotation`/`scale` `M2Track`s into
   per-sequence keyframes (`resolveVec3TrackSequence`/
   `resolveQuatTrackSequence`) -- one glTF `animation` clip per sequence
   with real inline data (`M2Sequence.flags & 0x20`, wowdev.wiki: "the
   animation data is in the .m2 file"), covering every bone that actually
   has keyframes for it. Translation keyframes are the bind-pose local
   offset (`Skeleton::Joint::localTranslation`) plus the animated delta --
   glTF's animated translation *replaces* a node's translation at sampled
   times rather than adding to it, so the bind offset has to be baked into
   every keyframe. Rotation/scale keyframes are converted from WoW's Z-up
   space the same way positions are (see `src/cmd_export.cpp`'s
   `toGltf(m2::Quat)`/`toGltfScale`) -- derived from general
   change-of-basis principles and checked numerically against several test
   rotations rather than taken from an explicit wowdev.wiki formula (none
   is documented for this specific step). Verified against
   `bloodelffemale.m2`: 258 animation clips (256 per-`M2Sequence` + 2
   `global_seq_<n>`, see below), 73,465 rotation keyframes, all finite and
   unit-norm; and against the `.skel`-sourced `bloodelffemale_hd.m2`: 334
   clips, 84,486 sampler channels, zero non-finite/non-monotonic keyframes
   (see the Usage section's verified-numbers paragraphs) -- **not yet
   verified** in Blender itself (does a clip actually play back looking
   right, not just structurally valid data). A bone track whose
   `global_sequence` field is set instead of belonging to any `M2Sequence`
   (a continuously-looping animation -- eye-glow pulses, torch flicker,
   idle sway, wowdev.wiki "Global Sequences": "always loops") resolves the
   same way, just against its own single outer sub-array instead of a
   per-`M2Sequence` one (`resolveVec3GlobalSequenceTrack`/
   `resolveQuatGlobalSequenceTrack`) -- one further glTF clip per distinct
   global-sequence index a model's bones actually use, alongside (not
   instead of) the per-sequence clips above. `bloodelffemale_hd.m2`'s own
   334-clip count above predates this specific addition and wasn't
   re-verified against it (the file isn't in this repo's committed test
   data) -- expect it to gain further clips the same way `bloodelffemale.m2`
   did (256 → 258) if re-checked.
   **External `.anim`-file sequences: also done and now verified against
   real files, for both inline and `.skel`-sourced models.** `flags`
   without `0x20` (and without `0x40`, "alias" -- wowdev.wiki: "stored...
   somewhere. I have no clue," skipped entirely rather than guessed at)
   means a sequence's data lives in a `.anim` file instead, resolved via
   an `AFID` chunk (`animId`/`subAnimId` -> FileDataID -- the model's own
   for inline bones, or the `.skel`'s own separate `AFID` table for a
   `.skel`-sourced skeleton, `skel::findAnimFileIds`) and
   `export --anim <dir>` (same local-directory-by-FileDataID
   convention as `--skin-dir`/`--textures`). The owning descriptor's own
   per-bone `M2Track` fields are still what's read
   (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`'s
   `externalDataBlob` parameter) -- per wowdev.wiki, "these files are just
   a blob of data... pointed to by the first array_ref layer," meaning the
   per-sequence inner `M2Array`'s `offset` field is real, just relative to
   the `.anim` file's blob instead of the M2's/`.skel`'s
   (`m2::extractAnimBlob` resolves that file's own optionally-chunked
   shape, keyed off the model's `global_flags & 0x200000`). For an inline
   model (`bloodelffemale.m2`), this held up byte-for-byte against real
   data on the first try: all 50 of the model's own real `.anim` files
   (renamed to `<FileDataID>.anim` per its `AFID` table) resolved cleanly,
   306 total clips, zero malformed reads. For a `.skel`-sourced model
   (`bloodelffemale_hd.m2`), it did *not* -- see the next paragraph.
   **`.skel`-sourced external `.anim` files: real per-bone data lives in an
   `AFSB` chunk almost universally, not `AFM2` -- and `AFSB`'s undocumented
   byte layout has since been cracked and resolved, not just detected.**
   All 54 of `bloodelffemale_hd.m2`'s own real `.anim` files (resolved
   through the `.skel`'s own `AFID` table) turned out to carry an `AFSB`
   chunk -- either alone (13 files) or alongside a small (16-1344 bytes
   across the full corpus, always a multiple of 16, and near-zero) `AFM2`
   "stub" that is *not* real track data (confirmed by trying to resolve
   against it anyway and getting a real "claims more keyframes than this
   blob holds" bounds error, not silently wrong output). The crack: `AFSB`
   isn't a new format at all -- it's the same per-bone `M2Track` data
   `.skel`'s `SKB1` chunk already describes, just stored in the external
   `.anim` file's `AFSB` payload instead of `SKB1` itself. `SKB1`'s own
   per-bone, per-sequence `(count, offset)` descriptors (the exact ones
   `resolveVec3TrackSequence`/`resolveQuatTrackSequence` already resolve
   for the `AFM2`-external case above) turn out to be real and non-zero far
   more often than assumed -- the `offset` just needed to be read against
   `AFSB`'s payload, the same `externalDataBlob` mechanism already used one
   paragraph up, not a new parser. `husk export`'s external-file loading
   now extracts `AFSB`'s own chunk payload directly as that blob whenever
   present (taking priority over the `AFM2` stub, same "husk doesn't have
   this one" skip only for a truly unrecognized future shape -- neither
   chunk present at all). Verified against the entire real 104-file
   `bloodelffemale_hd_*.anim` corpus (every bone/sequence combination's
   timestamps monotonic and in-bounds, every decoded value finite, rotation
   quaternions unit-length) and, end to end, three independent ways: husk
   itself now reports 336 real animation clips for this model (up from the
   inline/global-sequence-only clips available before), the Khronos
   `gltf_validator` reports zero new errors, and Blender's own glTF
   importer, run headlessly, independently counts 336 actions -- see
   `WIKI_FINDINGS/M2/anim.md`'s follow-up for the full byte-level writeup.
   **`.bone` (`BFID`) content: done, structure reverse engineered from real
   files (no wowdev.wiki documentation exists for it at all), not
   integrated into glTF export.** A `.bone` file (`src/bone.hpp`) is a
   4-byte version field followed by two chunks in the same generic
   container M2/`.skel` use: `BIDA`, a flat `uint16_t` bone-index array, and
   `BOMT`, a flat array of 4x4 row-major float matrices in lockstep with
   `BIDA` (`husk dump-chunks <file.bone>` dumps both as `(bone_index,
   matrix)` pairs). Confirmed against 6 real `bloodelffemale_hd_*.bone`
   files: every matrix's last column reads exactly `(0,0,0,1)` and the
   upper-left 3x3 stays near-identity with small (millimeter-to-centimeter)
   translation-sized deltas -- the signature of a small corrective delta
   transform per listed bone, not arbitrary data or a full replacement
   pose. What context each of a model's several `.bone` files (per its
   `BFID` array) applies to isn't documented or inferred here -- husk
   surfaces the raw pairs and stops there. LOD is now ruled out as that
   selector by real data (all 20 of a real model's `.bone` files don't fit
   its 7-tier LOD count, collapse into only 5 distinct bone-index sets with
   heavy exact duplication, and share corrections that are pure magnitude
   scales along one of two fixed directions -- the signature of a
   customization-choice lookup, not a detail-reduction ladder; see
   `WIKI_FINDINGS/BONE.md` and `TODO_correctness.md` #6). Wiring a bone
   correction into the exported skeleton would need that external
   (client-side DB2, out of husk's reach by design) lookup answered first.
   **Particles/ribbons: fully parsed, split across a `.glb` anchor and
   `dump-chunks` JSON.** `M2Ribbon`/`M2Particle` (`m2::parseRibbons`/
   `parseParticles`, `src/m2.cpp`) are now fully dereferenced -- every
   static field, plus every M2Track/FBlock animation curve, resolved and
   real-data-verified against actual weapon particle effects (fire/ember
   color gradients, alpha fade envelopes, growing scale curves -- see
   `WIKI_FINDINGS.md`). Like `.bone` corrections, neither type has a
   native glTF representation (procedural emitters, not geometry), but
   unlike the small, bounded `.bone`-correction/geoset/texture-transform
   `extras`, this data can be large (dozens of emitters, each with several
   curves) -- so it's deliberately split: `husk export` attaches only a
   minimal placement anchor (id/bone/position) to the `.glb`'s skin
   `extras`, and the full record lives in `husk dump-chunks`'s JSON output
   instead (see below). `M2Particle`'s record shape is version-gated
   (Cataclysm+, `m2::kMinVerifiedParticleVersion`) the same way Bone/
   Sequence/Ribbon already gate on Wrath.
   **Simple, unintegrated M2 chunks: extracted to JSON, not glTF.**
   `husk dump-chunks <file.m2>` (`src/cmd_dump.cpp`) pulls `TXAC`/`EXPT`/
   `PABC`/`PADC`/`PSBC`/`PEDC`/`RPID`/`GPID`/`PGD1`/`WFV3`/`NERF`/`EDGF`/
   `DBOC`/`TEXL` -- all reasonably well documented, but genuinely unrelated to
   glTF's own material/animation model (parent-sequence overrides,
   PBR-ish waterfall shader constants, per-model alpha-distance falloff,
   edge fade, ...) -- into readable JSON, a new intermediary format rather
   than folding into `export`'s output. Chunks with no documented byte
   layout, or a wiki-acknowledged-uncertain one (`WFV1`/`WFV2`/`DPIV`/
   `AFRA`), are still included as a raw hex dump plus a note explaining
   why, rather than silently dropped -- see `DESIGN.md` for the per-chunk
   reasoning. `DETL`/`PFDC`/`PCOL`/`EXP2` all had their own byte layout
   corrected and confirmed against real files since this fallback list was
   first written, and are now fully parsed into structured JSON, not
   fallback hex. This same command also now
   carries the full `ribbon_emitters`/`particle_emitters` records (see
   above) -- a deliberate broadening from its original Legion+-chunk-only
   scope, since the "no glTF slot" rationale applies equally to both.
7. **Output hardening. Done** — every real-data export
   (`tests/test_conformance.cpp`) now runs through two independent, real
   downstream consumers, not just tinygltf's own (fairly permissive)
   reader: the Khronos glTF-Validator CLI (`gltf_validator`, packaged in
   `nix/flake.nix` from the official precompiled release -- see that
   file's own comment for a real packaging gotcha this surfaced: the
   binary is Dart-AOT, and both `autoPatchelfHook`'s ELF rewrite *and*
   stdenv's default strip step independently corrupt its embedded VM
   snapshot; fixed with `dontPatchELF`/`dontStrip` plus a `steam-run-free`
   wrapper instead of patching the file at all), asserting zero validator
   errors; and Blender itself, run headlessly (`blender --background
   --factory-startup`) via `tests/blender_import_check.py`, cross-checked
   against tinygltf's own reading of the same file (bone count, animation
   clip count) so the test can't pass by both readers sharing the same
   blind spot. Verified against real data: `bloodelffemale.m2`'s export
   passes the validator with 0 errors/0 warnings, and Blender's importer
   agrees with tinygltf on both bone count (119) and animation clip count
   (258). ~~Decide the LOD/skin-profile policy~~ Done, then extended --
   see stage 8 below: the default stayed "always emit the highest-detail
   skin profile," but it's no longer the only option.
8. **Multi-LOD export (`--lod`). Done** — `husk export`'s default (`--skin auto`)
   (SFID entry 0, stage 7's policy) can now be overridden: `--lod <n>`
   picks a specific SFID entry instead (`husk info`'s `skin_file_data_ids`
   shows how many a model has), and `--lod all` resolves every entry and
   exports all of them into one `.glb`, each as its own named glTF node
   (`gltf::writeGlbMulti`, `src/gltf.cpp`) with its own primitives/
   materials -- sharing one skeleton and one set of animation clips, since
   every LOD tier of one M2 draws from the same `bones` array (only the
   triangle/vertex-index *subset* a `.skin` selects differs per LOD, see
   `src/skin.hpp`). `LDV1`'s `lod_count` (surfaced by `husk info` since
   before this stage, but previously never consumed by anything -- see the
   format matrix) now has a real consumer: an out-of-range `--lod <n>`
   reports it in the error message.

Explicitly not in this chain yet: WMO, M3, and anything in the
"write"/round-trip direction (a real Blender import *addon* rather than a
glTF file Blender happens to be able to open).

## Testing

Same two-tier split as `casc-tool`:

- **Pure-logic** (`tests/test_chunk.cpp`, `test_m2.cpp`, `test_skin.cpp`,
  `test_skel.cpp`) — synthetic buffers built field-by-field from the wiki
  spec, every offset cross-checked with a distinct sentinel value so a
  field landing at the wrong byte shows up as a specific failing `CHECK`,
  not a coincidental pass. `tests/test_gltf.cpp` takes a related but
  different approach: since `src/gltf.cpp` delegates the actual glTF
  binary framing to tinygltf rather than hand-rolling it, its tests
  round-trip `writeGlb()`'s output back through tinygltf's own loader and
  check the mesh data survived, rather than re-deriving byte offsets by
  hand. No real files needed for any of the above, always run.
- **Command-layer** (`tests/test_cli.cpp`, `test_dump.cpp`) — a third tier,
  between the two below: spawns the real compiled `husk` binary (like
  Integration) but against small synthetic fixtures (like Pure-logic), so
  it needs no real game files yet still exercises argv parsing and
  `cmd_*.cpp`'s own exception handling, not just the underlying parser
  functions. Exists because of a real, confirmed gap:
  `cmd_info.cpp`/`cmd_export.cpp` used to have zero committed coverage that
  didn't require a personal WoW install. `test_dump.cpp` covers `dump-chunks`
  specifically -- its per-chunk JSON logic lives entirely inside
  `cmd_dump.cpp`'s own translation unit, so this is the only way to reach
  it at all.
- **Conformance** (`tests/test_conformance.cpp`) — a fourth tier, answering
  a different question than Integration's "did husk itself run correctly":
  does a real downstream *consumer* accept the output? Runs the same real
  export through the Khronos glTF-Validator CLI (`gltf_validator`) and
  through Blender's own importer, headlessly (`blender --background
  --factory-startup --python tests/blender_import_check.py`), cross-
  checking Blender's reported bone/animation counts against tinygltf's own
  reading of the same file rather than hardcoded fixture numbers, plus a
  *third* leg: the M2 source file's own header counts —
  `header.vertices.count`/`header.bones.count` against
  Blender's/tinygltf's readback exactly, and the exported bind-pose mesh's
  computed accessor bounds against the header's own declared bounding box
  (containment, not tight equality; see the test's own comment for
  why a real cross-check found the header box isn't a tight fit around the
  bind pose, and `WIKI_FINDINGS/M2.md`). `blender_import_check.py` clears
  Blender's own default startup scene (Cube/Camera/Light survive
  `--factory-startup`) and passes `disable_bone_shape=True` to the import
  operator before counting anything -- both are real Blender-importer-side
  contamination sources found while making the vertex/mesh-object counts
  above exact rather than the `> 0` they used to be (the latter creates a
  real, otherwise-invisible 42-vertex Icosphere object per armature import,
  see `io_scene_gltf2`'s own `armature_display()`). Both tools are optional
  in the nix flake devShell (`blender`, `gltf-validator` in `nix/flake.nix`)
  -- skipped, not failed, if either isn't on `PATH`, on top of the same
  `HUSK_TEST_M2`/`HUSK_TEST_SKIN` gating as Integration. Also includes a
  real orientation-correctness tier (`TRANSFORM_TRIAGE.md`): a synthetic,
  asset-agnostic coordinate-frame probe (a fabricated skeleton, not tied to
  any real M2 file or body plan) asserts local X/Y/Z offsets survive a real
  husk-export → Blender-import round trip as the identical coordinate --
  the property a real husk transform bug once violated undetected, since
  every check before this one was either purely structural (counts) or
  compared two values that both went through the same conversion (blind to
  a consistent sign error). A second, explicitly non-load-bearing check
  confirms a real humanoid landmark bone (`_Name`) lands above the armature
  origin on real data, and `HUSK_TEST_QUADRUPED_M2`/`_SKIN` (a real
  `wolf.m2`, defaulting to `test_data/creature/wolf/`) exercises the same
  conformance checks against a body-plan/bone-hierarchy shape
  `bloodelffemale.m2` doesn't represent.
- **Integration** (`tests/test_integration.cpp`) — runs the compiled
  `husk` binary against a real, game-extracted `.m2` (+ matching `.skin`,
  for `export`) as a subprocess. Deliberately asserts only on shape (exit
  code, "did it find some vertices", "is the output a plausibly-sized
  well-formed `.glb`"), not on any one model's specific field values —
  those belong in the synthetic tests. Also covers the failure paths: a
  `.skin` that doesn't belong to the given M2 must fail loudly, not
  silently misread; a model with an external `.skel` skeleton must produce
  a real skinned glTF skin, not a silent unskinned fallback. Each fixture
  resolves via `tests/test_data_paths.hpp`: an explicit `HUSK_TEST_*` env
  var always overrides, but absent that, it falls back to a matching file
  already sitting in this repo's own (gitignored) `test_data/` directory
  -- `HUSK_TEST_M2`/`HUSK_TEST_SKIN`/`HUSK_TEST_MISMATCHED_SKIN`/
  `HUSK_TEST_SKEL_M2`/`HUSK_TEST_SKEL_SKIN`/`HUSK_TEST_SKEL`/
  `HUSK_TEST_ANIM_DIR` all default this way (the last one just resolves to
  the same directory the `.skel`-sourced fixtures already live in, since
  that's where the real, `AFSB`-shaped `.anim` files sit too), and
  `HUSK_TEST_SKIN_DIR`/`HUSK_TEST_BONES_DIR` are *built* on the fly
  (`autoSkinDir`/`autoBonesDir`) by reading the real SFID entry 0 / `BFID`
  array out of the resolved M2's/`.skel`'s own header and copying the
  resolved sidecar(s) there under the right FileDataID(s) -- the same
  fixture a hand-populated directory would need, constructed automatically
  instead of requiring one. Only `HUSK_TEST_TEXTURES_DIR` has no default
  (no `husk-blp`-converted PNGs are committed to `test_data/`) and still
  needs to be set by hand, pointing at a directory of `<FileDataID>.png`
  files. A fixture that doesn't resolve marks its `TEST_CASE` with
  `* doctest::skip(...)` rather than a runtime `MESSAGE` + early `return`
  -- it shows up as a distinct, non-zero "skipped" count in doctest's own
  summary instead of being folded silently into "passed" (this was a real
  gap: `./build/husk-tests` used to report "0 skipped" even when 12 of the
  260 cases never ran a single assertion). `tests/test_main.cpp`'s startup
  banner prints exactly what each fixture resolved to, or why it didn't,
  every run -- "why did N tests just skip" is a read, not a rerun.
  `--anim`'s synthetic-fixture coverage in `tests/test_cli.cpp` (both the
  inline-M2 and `.skel`-sourced cases, including the `AFSB`-vs-`AFM2`
  shape) is complemented by a real, `HUSK_TEST_ANIM_DIR`-gated
  `test_integration.cpp` case that resolves the entire real
  `bloodelffemale_hd_*.anim` corpus (336 clips) and checks every decoded
  rotation/translation keyframe -- the ad hoc, one-off real-file
  verification a previous version of this section used to describe is now
  a repeatable, committed test case, not a gap.
  `test_data/` (gitignored) is a convenient local spot for real,
  copyrighted game data extracted from your own install, never meant to be
  committed -- everything above degrades gracefully (skips, visibly) when
  it isn't there.

```
cmake --build build -j$(nproc)
./build/husk-tests                                    # pure-logic only
HUSK_TEST_M2=test_data/bloodelffemale.m2 \
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin \
  ./build/husk-tests                                  # + integration
```

`blp/` (Python) has its own, separate test suite -- same two-tier shape
(synthetic `test_header.py`/`test_decode.py`, always run; real-file
`test_integration.py`, skipped unless `HUSK_TEST_BLP_DXT1`/
`HUSK_TEST_BLP_DXT5`/`HUSK_TEST_BLP_PALETTE` point at real files):

```
cd blp
uv sync
uv run pytest                                          # pure-logic only
HUSK_TEST_BLP_DXT1=../test_data/character/bloodelf/female/bloodelffemale_hd_face_3500074.blp \
HUSK_TEST_BLP_DXT5=../test_data/character/bloodelf/female/bloodelffemale_hd_4530998.blp \
HUSK_TEST_BLP_PALETTE=../test_data/character/bloodelf/female/bloodelffemalefacelower10_00.blp \
  uv run pytest                                        # + integration
```

## Design notes

Architecture and design rationale — why the code is shaped the way it is,
not just what it currently does — live in `DESIGN.md`, not here, so a
structural change has one canonical place to be checked against. That
includes: the chunk-tag-tracking/diagnostic mechanism, the `.skel`/`.bone`
sidecar reuse pattern, the `M2Track` constant-value safeguard and the real
bug that motivated it, external `.anim` blob splicing and the `AFSB`
crack, the `M2Sequence`-is-64-bytes finding, the bone-track Z-up→Y-up
quaternion/scale conversion, why glTF output goes through tinygltf, why
`dump-chunks` is a separate JSON format, why LOD tiers share one skeleton,
and why `blp/` stays a separate Python process. Real-file
reverse-engineering findings specifically (the wowdev.wiki gaps/errors
found along the way) are their own document, `WIKI_FINDINGS.md`, cited
from `DESIGN.md` and from the Usage/roadmap sections above where relevant.

## Disclaimer

This tool is co-coded by AI, verified by a massively autistic developer —
every field-offset claim here was checked against the real spec and a real
game file, not taken on faith.
