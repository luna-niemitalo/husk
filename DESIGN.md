---
aliases:
  - README
---
# DESIGN.md — husk

Why husk is shaped the way it is: goal, scope, invariants, and the
decisions behind the code, each with what was rejected and why. A
structural change is checked against this file.

Where things live:

| Question | Document |
|---|---|
| What exists, per feature, and what is open | `README.md` "Implementation" (the hub) |
| The target architecture (`parse → resolve → canonical → write`) and invariants I1–I8 | `REFACTOR/README.md` |
| `canon::` types and the writers over them | `REFACTOR/CANON_DESIGN.md` |
| The bundle format | `REFACTOR/BUNDLE_FORMAT.md`, `schema/`, `CANONICAL_FORMAT.md` |
| Real-file corrections to wowdev.wiki | `WIKI_FINDINGS.md` |
| Per-M2-feature depth; WMO/ADT depth | `M2_COMPLETENESS.md`, `WORLD_COMPLETENESS.md` |
| Tests | `TEST_DESIGN.md` |
| Python tooling under `tools/` | `TOOLS.md` |
| Open work | `TODO/README.md` |
| Session narrative | `CLAUDE_HISTORY.md` |

## Goal

Read World of Warcraft's game files from a local extraction and write them
as open data. The **canonical husk bundle** is the export target;
MantleCore is its consumer. glTF and Blender are tertiary (Luna,
2026-10-09): the only glTF husk writes is `export --glb`'s lean projection
of a model bundle.

### Non-goals

- **No live CASC, ever, and no CASC-tool dependency.** husk never links or
  shells out to a CASC/DB2 client and never resolves a FileDataID against a
  live install. A separate tool (e.g. `casc-tool`) extracts files first;
  husk reads what is on disk. Sidecar directories (`--skin-dir`,
  `--textures`, `--anim`, `--bones-dir`) are local, user-populated,
  FileDataID-named.
- **Local DB2 files are in scope.** Luna, 2026-08-08: "the only hard
  boundary is not loading casc tool as a dependency... all data in
  wow_export is free for all, to be used." Parsing WDC5 locally
  (`src/db2.*`), naming columns from a local WoWDBDefs checkout
  (`src/dbd.*`), and typed per-table readers (`*_db2.*`) are the same tier
  as any sidecar. An out-of-band tool that learns something from live
  CASC at build time and hands husk a local file is also fine. Older
  wording ("husk has no DB2 access") is superseded by this.
- **A local listfile is in scope**, same tier: optional, user-supplied,
  never fetched (`--listfile`, `src/listfile.*`). A 130,576-file render
  pass (2026-08-09) found 99.9% of "missing" FileDataID textures were
  present under their real names (`casc-tool`'s `FAILURES.md` item 13).
  Unset, behaviour is identical to having no listfile.
- **No write-back** to WoW's native formats.
- **M3: not started.** A full-storage `M3DT` scan (build 68887,
  1,891,552 files) found 8 `.m3` files (unnamed,
  `models\unknown\unk_exp*\<fdid>.m3`).
- **WMO, ADT, WDT are implemented, as bundles only (never glTF).** ADT
  tiles via `export-terrain`/`export-world`; WMOs via `export-wmo`, as
  object bundles with their doodad sets as placement sets
  (`REFACTOR/PLACEMENT_SETS.md`); maps via `export-map`. Per-chunk state:
  `README.md` format matrix, `WORLD_COMPLETENESS.md`. Terrain was missed
  when the project was first scoped, not excluded. `.wdl` is not read.
- **PM4/PD4 (server-side pathing meshes): in scope, not started**
  (2026-07-31, "we want pathing"). Real geometry, hidden by default, like
  the M2 collision mesh. Never shipped to the client (zero files in a full
  live-storage sweep), so fixtures need another source
  (`TODO/WORLD/PM4_PD4_TODO.md`).
- **Shader bytecode (`BLS`/`GFAT`, `.dxbc`/`.spv`): deprioritized, not
  ruled out.** Decoding is possible (Luna found stripped dumps); it would
  answer questions nothing else can (e.g. LOD dithering) but the
  effort-to-fidelity ratio is poor while M2/WMO/ADT coverage is the
  bottleneck.
- **Retired non-goal: "No Blender addon. A `.glb` file Blender's stock
  importer can open is the entire deliverable."** Retired 2026-08-28:
  `tools/` had grown ~4,150 lines of Blender Python (an addon in all but
  packaging). Target now: a packaged addon reading the bundle
  (`REFACTOR/BLENDER_ADDON.md`); the world-authoring addon exists
  (`blender/addons/husk_addon`).

### World export is composed bottom-up

Confirmed by Luna, 2026-08-01: never a monolithic "export the whole game."
Small, individually testable exporters first (one ADT tile, one WMO, a
placement resolver), then thin composition on top (`export-world`,
`export-corpus`) — the way the M2 pipeline grew one sidecar at a time. A
world is a scene descriptor referencing separately exported bundles (the
world document and placement sets), not one baked file.

## Module map

```
file bytes
  ├─ src/chunk.*            generic chunk reader (tag-agnostic)
  ├─ src/m2*.*              MD20/MD21 header, arrays, M2Track/FBlock resolution;
  │                         m2::Model (m2_model.hpp) parses eagerly
  ├─ src/skin.* skel.* bone.* phys.*   M2 sidecars
  ├─ src/adt.* wmo.* wdt_volumes.*     world formats
  ├─ src/blp.*              BLP2 decode, DDS rehousing (in-process)
  ├─ src/db2.* dbd.* db2table.* *_db2.*   WDC5, WoWDBDefs naming, typed readers
  └─ src/listfile*.*        listfile parse, cache, mmap index
        │
  src/sources/              resolution: where the bytes live (catalog, texture
        │                   and skin resolution tiers, directory listing)
  character_texture_context.*  the catalog's DB2 character-texture tier input
        │
  *_canon_input.*, m2_*_input.*   input modules: format → canon:: values
  src/canon_*.*             canon:: model (REFACTOR/CANON_DESIGN.md)
        │
  src/writers/              bundle (primary), lean glTF, materials, textures,
                            physics; glTF axis math (gltf_math.*)
  src/bundle_schema.*       validate-bundle; schema/ embedded

commands:          cmd_*.cpp, main.cpp (export: cmd_export.cpp flags and
                   sidecars → cmd_export_canon.cpp canon assembly and write)
blp/               Python reference decoder (husk-blp), test oracle only
```

Flat `MD20` and chunked `MD21` funnel into one parser at the earliest
point: both resolve to "one MD20-shaped blob, offsets relative to its
start" before `m2::parseHeader` runs. The branch is where the blob starts,
not how it is read.

## Data flow (`husk export`)

1. Read the model; flat vs. chunked → MD20 blob.
2. Parse header (fixed offsets) and arrays: vertices, bones, sequences,
   materials, textures, combos, attachments/events/lights, ribbons,
   particles, sidecar FileDataID chunks (`SFID`/`AFID`/`BFID`/`PFID`/
   `SKID`/`TXID`).
3. Triangles and submesh/batch structure from every resolved `.skin`
   (one per `SFID` LOD entry).
4. If inline `bones` is empty and an `SKID`/`.skel` is available, bones and
   sequences come from the `.skel` (same structs, other blob).
5. Textures through the catalog's resolution tiers ("Resolution" below),
   plus the DB2 character tier when `--db2-dir` is given.
6. Per sequence: per-bone tracks, inline (`flags & 0x20`) or external
   `.anim` (via the model's or `.skel`'s `AFID`); alias (`0x40`) sequences
   resolve through their `aliasNext` chain.
7. `.phys`, `.bone` correction sets, DB2 customization/creature options,
   `--appearance` gear (each item model exported as its own bundle).
8. `m2input::buildCanonModel` assembles `canon::Model`;
   `writers::writeBundle` writes it; `--glb` also runs
   `writers::writeLeanGlb` over the same model.

## Boundaries (where foreign data enters)

The list of boundaries is `CLAUDE.md` "Boundaries". The invariants:

- **Every foreign byte is read through bounds-checked parsing at named
  offsets**, failing with expected vs. actual (`ParseError`/
  `std::runtime_error`), never a silent misread.
- **Byte-layout ambiguities are settled against real decoded files**, not
  prose (`WIKI_FINDINGS.md` holds the receipts).
- **Local files only**; no network anywhere.
- **What husk can't resolve is reported, not fabricated.**
- Sidecar conventions: directories (`--textures`, `--skin-dir`, `--anim`,
  `--bones-dir`) are FileDataID-named; `--skel`/`--phys` are single files
  (one scalar FileDataID, `SKID`/`PFID`), found by same basename.

## Key design decisions

### Parsing

**Chunk tags are read literally in M2, reversed elsewhere.** M2 is the odd
one out: `MD21` on disk matches `"MD21"`. WMO, ADT, WDT and `.phys` store
tags byte-reversed; `readChunks`/`findChunk` are reused with the reversed
literal. Getting this backwards is a classic WMO/ADT-experience trap.

**Fields are read by explicit, bounds-checked `memcpy` at named offsets,
never a packed struct.** The header's tail is version/flag-conditional;
compiler layout would be silently wrong across versions.

**Every top-level M2 chunk tag is diffed against a known list.**
`readChunks` skips unknown tags by construction; `Header::chunkTags` +
`husk info`'s `documentedM2ChunkTags` turn an unknown one into a `note:`,
and `tests/test_integration.cpp` is a live canary against real data.

**Sidecars carrying M2-shaped structs reuse the M2 parser, never a copy.**
`.skel`'s `SKB1`/`SKS1` are the same structs at offsets into another blob
(verified: a real `.skel`'s bone-track outer count equals its `SKS1`
count). A new sidecar with an M2-shaped struct is wired in via a different
blob/offset base.

**Constant `M2Track` resolution requires exactly one sub-array with exactly
one keyframe** (`constantTrackValueOffset`), never `[0][0]`
unconditionally. Reading `[0][0]` (the wiki's worked example) once took a
per-sequence alpha track's sequence-0 key (`0`) as a static default — an
invisible model. The regression test in `tests/test_m2.cpp` is named after
it.

**External `.anim` data is spliced across two blobs.** The M2's (or
`.skel`'s) per-sequence `M2Array` descriptor stays real; only its offset
points into the `.anim` blob. `resolveVec3TrackSequence`/
`resolveQuatTrackSequence` take descriptor from the owning blob and payload
from `externalDataBlob`.

**`AFSB` wins over `AFM2`.** `.skel`-linked models keep real per-bone data
in `AFSB`; a near-empty `AFM2` stub beside it must not be read as tracks
(it bounds-errors). No spec exists, and none was needed: `SKB1`'s
`(count, offset)` descriptors point straight into `AFSB`'s payload
(`WIKI_FINDINGS/M2/anim.md`). Verified on all 104
`bloodelffemale_hd_*.anim` files (no bounds/monotonicity/finiteness
errors), and end to end: 336 clips from husk, validator clean, 336 Blender
actions.

**`.anim` file resolution tries `<FileDataID>.anim`, then
`<model-basename><animId>-<subId>.anim`** (`findAnimFileByBasename`), the
`wow.export`-style name a real extraction uses — also when there is no
`AFID` entry.

**Spec ambiguities are settled by decoding real records at every plausible
stride.** `M2Sequence` is 64 bytes, not 36: the wiki's offset comment reads
as if `M2Bounds` weren't there (`WIKI_FINDINGS/M2.md`).

**Version-conditional record shapes are parsed only from a verified
floor.** `kMinVerifiedRecordStrideVersion` (Bone/Sequence/Ribbon) and
`kMinVerifiedParticleVersion` (Cata+ 492-byte `M2Particle`) warn below
the floor; older `M2ParticleOld` shapes are real but unimplemented (no real
file to verify against). `flag_new_particle_record` is an *alternative*
signal to `version > 271` (the wiki's OR), not a second gate.

**`skin::Submesh::indexStart` is a 32-bit value: `(level << 16) |
rawIndexStart`.** `M2SkinSection::Level` (0x02) is address-extension bits,
not LOD metadata (found via `reference/wow.export`'s `Skin.js`;
`triangleStart += level << 16`). `bloodelffemale_hd00.skin` has 136,254
indices and 77/114 submeshes need it. Without it husk silently sliced the
wrong region — invisible to every validator/importer check, since none
compares *which* triangles were drawn. Callers never see `Level`
(`CLAUDE_HISTORY.md`).

**`PCOL` regions are read by their own `(count, offset)`, never
sequentially**: the wiki's "extra bytes between the data" is real (an
8-byte gap seen). `flagsCount` is read independently of `faceNormCount`
(3/2,354 files differ). Verified on all 2,354 `PCOL` files; `flags` is a
per-triangle bitmask, bit meanings unknown (`WIKI_FINDINGS/M2.md`).
Diagnostic only (`dump-chunks`).

**Chunks with no wiki struct are byte-decoded, flagged tentative where
thin**: `AFRA`/`WFV1` one 16-byte struct (a float + 12 zero bytes), `DPIV`
`size/32` records of 8 floats, `WFV2` 16 floats. `WFV1`/`WFV2` rest on 2
byte-identical files; fields that may not be floats are exposed as floats,
not reinterpreted.

**`global_flags` are decoded into wiki-named bits; `textureCombinerCombos`
is read when its bit is set** (offset 0x130). Zero of 130,576 local files
set it, so its "use instead of index+1" role in multitexture blending is
not wired (no key documented, nothing to verify against); `husk info`
only.

**Two parse-failure contracts.** `m2::loadModel` isolates each array's
`ParseError` in `Model::parseFailures` (field left empty).
`info`/`info --json`/`dump-chunks` are `Tolerant`: they show failures as
data and keep going. `export` is `Strict`: an empty array would produce a
*wrong* export (fewer anchors, bones silently from `.skel`), so
`canon::enforcePartialFailurePolicy(model, field, policy)`
(`canon_policy.hpp`) re-raises at the point each field is read — one call
per field in read order, since read order differs from parse order and
decides which error wins. Declared once:
`commands::kExportPartialFailurePolicy`.

**A duplicate animation keyframe timestamp is repaired, not rejected.** Exact
duplicates are real data (a "hard cut" authored as two keys at one
instant, 5 corpus files). The later duplicate moves +1 ms (cascading),
classified by *original* timestamps; a genuinely decreasing timestamp
still throws (repairing it would mean guessing).

**Deliberately unparsed fields — a scope ledger, not scattered inline
claims.** "Not parsed" is a decision; it lives here, where it is reread,
not in header comments where it rots (`~/nix/claude-rules/
CODE_COMMENTS.md`). An early "first pass" cut once hid `shader_id` for
months this way; it is now `skin::Batch::shaderId`/
`m2::resolveShaderNames`.

- `M2SkinSection`: `centerPosition`/`sortCenterPosition`/`sortRadius`/
  `boneCount`/`boneComboIndex`/`boneInfluences`/`centerBoneIndex` —
  hardware bone-limit/culling data; husk uses full per-vertex global joint
  indices, so nothing consumes them (`M2_COMPLETENESS.md`).
- `M2Batch`: `priorityPlane`/`geosetIndex`/`materialLayer` — draw-order and
  legacy fields with no role in one-primitive-per-batch.
- `.skel`: `SKB1`'s `key_bone_lookup`, `SKA1`'s `attachment_lookup`,
  `SKL1`/`SKPD`, `SKS1`'s `global_loops`/`sequence_lookups` — no consumer
  yet. (`SKA1` attachments are read, for the bundle.)
- `M2Event.enabled` — when during playback an event fires; no static-scene
  consumer. Identifier/data/bone/position are parsed and exported.
- Pre-Cata `M2ParticleOld` shapes — see the version floor above.

### M2 → model bundle

**One material per `.skin` batch, not per M2 material.** A batch pins blend
mode *and* texture; two batches sharing a `materialIndex` needn't share a
texture. No PBR authoring: WoW's shading doesn't map onto
metallic-roughness and invented values would be guesses.

**Everything the source carries is written as data, never applied.**
Geosets all export (selection is the consumer's, from options and
presets); extra texture layers, tint/alpha/weight/UV-transform tracks,
billboard flags, `.bone` correction sets and DB2-resolved choices are
fields in the bundle (`REFACTOR/BUNDLE_FORMAT.md`). `.bone` application
(multiply order, space) is unverified against the client
(`TODO/BONE_CORRECTION_APPLICATION_TODO.md`); LOD- and weapon-type
hypotheses for slot selection were ruled out (`WIKI_FINDINGS/BONE.md`).
Animated `M2Color`/`M2TextureWeight` are `fixed16`, resolved with
`resolveRawIntTrackSequence`, not the float resolver.

**Global-sequence tracks keep their own loop.** Such a track loops on its
own timeline against `Header::globalLoops`, whatever sequence plays, so
folding it into per-sequence clips would misstate when it plays. Per the
wiki such tracks have exactly one sub-array;
`resolveVec3GlobalSequenceTrack`/`resolveQuatGlobalSequenceTrack` read it.

**Alias sequences resolve through `aliasNext`, inline data wins.**
`aliasNext` is a local `sequences` index (`WIKI_FINDINGS/M2.md`).
`resolveAliasChain` (bounded to `sequences.size()` hops, throws on a cycle)
finds the terminal non-alias sequence, whose data is registered under the
alias's own id. Only a *pure* alias (`0x40` without `0x20`) is resolved:
31 of `bloodelffemale_hd.skel`'s 38 aliases also carry `0x20` and have
their own data. The other `M2Sequence` fields are each clip's own facts
(`REFACTOR/BUNDLE_FORMAT.md` "Clips"). The `AnimationData`
name is attached when the DB2 has a `Name` column; current clients don't
(`LAYOUT BBF66A3C`, absent since 7.3.5).

**Volume decides the destination: emitters get a placeholder in the
bundle, the full record in `dump-chunks`.** A model can have dozens of
emitters with ~15 resolved curves each. `M2Ribbon`/`M2Particle` are
bone-anchored placeholder effects (`REFACTOR/EFFECTS.md`); full records
with every curve are in `dump-chunks` (FBlock curves are flat; `M2Track`
curves are per sequence or global). `.phys` is the bundle's
`resources.physics`; `dump-chunks` also takes a `.phys` directly and
prints its `PHYV` records. `.phys` is documented on the wiki and
implemented from it, verified on 103 files (`WIKI_FINDINGS/PHYS.md`);
chunk types never seen in the corpus are parsed but unverified
(`phys.hpp`).

**`husk dump-chunks` emits every chunk, never drops one.** Parent-sequence
overrides, waterfall constants, edge fade and side data are printed as
records; undocumented chunks are decoded or printed as hex with a reason.

**The collision mesh is always in the bundle**
(`resources.geometry.collision`, `m2::parseCollisionMesh` →
`buildCollision`): its own positions, indices and the source's
per-triangle normals as they are. Indices out of range, a non-multiple of
3, or a normal count other than one per triangle fail the export.

**A geometry-less model exports with no triangles, keeping its skeleton.**
3,807 of 130k files (pure VFX, e.g. `particles/lootglow_boss.m2`) have no
vertices; such a model is its skeleton and effects. A model with neither
vertices nor bones is an error.

### Lean glTF projection (`--glb`)

`writers::writeLeanGlb` projects a `canon::Model` to `.glb`: mesh, skin,
animation, base-colour materials. What has no core glTF slot is left out,
never smuggled into `extras` (options, presets, effects, physics,
correction sets).

**glTF output is never hand-rolled.** tinygltf builds and frames it. Never
define `TINYGLTF_IMPLEMENTATION` (it is in `libtinygltf.a`). husk pads its
own buffer (`gltf::padTo4`, `writers/gltf_buffer_utils.hpp`) after every
append, because accessors need component-aligned offsets and embedded PNG
lengths are rarely multiples of 4 (a real misalignment bug, fixed).

**Axis conversion is one matrix.** `kWowToGltf` (`writers/gltf_math.*`), `(x, y,
z) → (x, z, -y)`; positions/normals apply it, rotations conjugate by it
(quat → matrix → quat), scales take its permutation without signs. A
`static_assert` on the determinant forbids a reflection. Rejected: three
hand-typed formulas — the old `(x, -z, y)` and a rotation rule derived on
paper from it exported every model upside down undetected.
`tests/test_conformance.cpp` checks it asset-agnostically (unit offsets
along local X/Y/Z must land at identical coordinates after a real Blender
import; proven to fail on the old matrix). Corroborated by
`reference/wow.export`'s independent conversion code (not treated as
ground truth). Bones are assumed plain parent-relative TRS around
`M2CompBone.pivot`; Luna confirmed animation in Blender's GUI
(2026-08-08). History: `CLAUDE_HISTORY.md`.

**Blend mode → `alphaMode`**: the material's framebuffer blend when the
source has one (opaque → `OPAQUE`, alpha key → `MASK`, else `BLEND`),
otherwise from its last layer's combine op.

**A multi-root bone forest gets one synthesized non-joint parent node.**
35% of 130k files (45,804) have several roots — intentional data. One
node named `skeleton`, outside `skin.joints` (no IBM), parents every
root and is `skin.skeleton` — the shape glTF tooling expects for multi-rooted
skeletons (KhronosGroup/glTF issue 1270). Joint indices stay raw M2 bone
indices. Rejected: an extra identity joint (grows `skin.joints` past the
bone count for nothing) and dropping non-hierarchical bones
(`wow.export`'s apparent approach; loses real bones). The validator's
`SKIN_NO_COMMON_ROOT` flags only some multi-root files; why is open
(`TODO/INVESTIGATIONS_TODO.md` #16).

**Names**: the mesh node is the model's name; joints take the skeleton's
structural label, else `joint_<index>`; clips are
`sequence_<index>`/`global_sequence_<index>`.

### Resolution

**Texture resolution tiers, in order**: the M2's embedded filename,
`<FileDataID>.png|.blp` (only if present), the `--listfile` path under
`--listfile-root`, then the one unclaimed same-basename file (ambiguity
reported). Deterministic tiers run before the shared fuzzy pool: a fuzzy
claim once stole a file belonging to a later FileDataID-resolvable slot
(now a `tests/test_cli.cpp` case). Owner: `sources/texture_resolution.*` /
`sources::Catalog`. With `--db2-dir`, the catalog also has a DB2 character
tier for runtime-replaceable slots (`character_texture_context.*`).

**A reimplementation of a resolution chain must mirror every tier.** A
Python scan (`unfillable_texture_task.py`) dropped the listfile tier; after
an 18,742-file re-extraction placed textures under listfile names, its
counts didn't move — it never checked the tier
that changed. Counterpart: `CLAUDE.md` Hazards.

**`.skin` resolution**: `SFID`-declared FileDataID first (the model's own
self-description), same-basename numbered scan second. The scan prefers
exactly 2-digit suffixes when any exists (`mogu_library_crate_1` + `00`
otherwise matched `mogu_library_crate_10`); other lengths stay a fallback.
A miss names the exact candidate path checked. The other resolvers skip
per item by design and have no failure message.

**Listfile names can belong to another FileDataID**; the mount's
`_by_fdid/<fdid>` cannot. Order per lookup kind: `CLAUDE.md` Hazards.

### BLP

**BLP2 decodes in-process** (`src/blp.cpp`): DXT1/3/5 block decode is
small public maths, PNG encoding comes from `stbi_write_png_to_mem` inside
the already-linked tinygltf. Reversed from "permanently a separate Python
process": running `husk-blp` first broke husk being one standalone tool.
Nothing is written unless `--textures-out` is given. `blp/` stays as an
independent second implementation the C++ tests check against. A
779,056-file scan found 6,759 DXT3 files (already supported, just
unverified) and zero JPEG (recorded negative, not implemented).

## World export

**`export-terrain`** takes the root `<map>_<x>_<y>.adt`; `_obj0`/`_tex0`/
`_lod`/`_obj1` and `<map>.wdt` are found beside it (each flag overrides,
`none` skips; the WDT has no `none`), each choice announced as a note. A
sibling that exists but doesn't parse, or a missing WDT, is a warning
(expected vs. actual) and only that part is left out; an explicit path that
doesn't parse fails the export. File-name coordinates are checked against
the chunk positions. Name-table paths resolve through `--listfile`; an
unknown path keeps its raw name (`name_source: adt_embedded`).

**`export-world`** runs four parallel passes: collect references, export
each referenced M2/WMO once (in-process), export each tile, write each
map's world document. A failed tile, model or WDT goes to `errors.log` and
is skipped; an unreadable `_obj0`/`_tex0` is logged and the tile exports
without it; an unreadable WDT leaves its tiles untextured. A rerun skips
outputs whose `manifest.json` exists (written last) and retries failures.
Tree: `REFACTOR/CORPUS_EXPORT.md`.

## CLI

### CLI argument grammar for `export`

Built on [CLI11](https://github.com/CLIUtils/CLI11) (`commands.hpp`'s
`addExportOptions`). The flag list is generated: `README.md` "Flag
reference". Grounded in `~/docs/CLI.md`.

- **Named flags, order-independent.** Replaced a positional grammar where a
  word's meaning depended on how many came before it (§1/§2.2) and
  `--help` needed a pre-parse hack.
- **Only `-i`/`-o` keep a positional fallback**: `tool in out` is universal
  muscle memory; `.skin`/`.skel`/directories have no such convention.
  `--output`'s default (`<model-basename>.bundle` beside the model) is a
  computed literal, not an `auto` state.
- **Shorthands for per-invocation flags** (§2.8): `-s`/`-t`/`-a`.
  `--skin-dir` has none because `-s` is `--skin` (a different axis, not a
  rarer spelling); `--skel`/`--glb` are occasional (§2.6).
- **`--skin none` is rejected at parse time.** A `.skin` is the sole source
  of triangles, submeshes and batches; a skin-less export would be new
  scope with its own design, not a quiet `none`.
- **CLI11 over a hand-rolled parser or a framework**: header-only, tight
  fit (`help`, order independence, positional fallback for free).
  Rejected: a Python wrapper — blurs the single-binary boundary for nothing
  CLI11 doesn't give in C++.
- **Hazard**: `App::parse(std::vector<std::string>&)` consumes from the
  *back*. `cli_args.cpp` reverses `args` first; forward order silently binds
  flags to the wrong tokens.
- **Flag tables and completions are generated, never hand-written**
  (`--print-flag-docs`, `--print-completion`); `tests/
  test_cli_flag_docs.cpp` fails on drift.

### Three-state resolution, not two

(`~/docs/CLI.md` §2.11.) For a flag naming something the model itself
points at (`--skin`, `--textures`, `--skin-dir`, `--skel`, `--bones-dir`,
`--phys`), unset and "don't" are different states:

- **Unset → `auto`**: derive from the input's self-description,
  best-effort. *What* to look for comes from the model's bytes (`SFID`
  entries); *where* to search is a directory whose default is
  the model's own. That default is a value, not the definition of `auto`,
  so a second "where" signal wouldn't change the flag's meaning.
- **Explicit value → override.**
- **`none` → skip deliberately**, no attempt, no warning.

Per flag:

- `--textures none`: no textures from a directory; blend mode/tint are
  still written.
- `--skel none`: never look for a same-basename `.skel`; unskinned mesh.
- `--skin-dir none`: skip the `SFID` stage, go straight to the basename
  scan.
- `--bones-dir none`: `.bone` files only through the listfile and store.
- `--phys none`: no `.phys`, no `resources.physics`.

**`--db2-dir`/`--dbd-dir` are required together.** `--char-layout-id` is
derived from the resolved `--chr-model-id` (`ChrModel.
CharComponentTextureLayoutID`, same row), with the same `auto`/`none`/
`<id>` control. `--db2-dir` without `--dbd-dir` prints a diagnostic and
skips; the rest of the export proceeds.

**`--anim` has four states**, because "any animation?" and "external
`.anim` files too?" are independent (inline and global-sequence tracks need
no filesystem):

- `auto` (default): inline + global sequences + external search (default:
  the model's directory).
- `inline`: inline + global sequences, no external search.
- `none`: no clips at all; the skin and bind pose are unaffected.
- `<dir>`: external search there, inline + global on top.

### `--db2-dir`/`--dbd-dir`/`--listfile`/`--listfile-root`: no `auto`, and why

**`auto` is honest only when the input says where the thing is.**
`SFID`/`SKID`/`AFID`/`BFID` name what to look for; nothing in an `.m2`
names a WoWDBDefs clone, a DB2 directory or a listfile. `auto` here would
be guessing someone's filesystem layout and being confidently wrong. These
are **unset or an explicit path**.

**They do have `none`**: it overrides a config-file value for one
invocation, as if unset (CLI11's precedence: explicit beats config; verified
with a real config). Before it, tests had to blank the whole config
(`HUSK_CONFIG=/dev/null`) to disable one flag.

- `--db2-dir none`/`--dbd-dir none`: no DB2-driven data (character
  customization and textures, creature options and presets,
  `--appearance`'s gear).
- `--listfile none`: no listfile tier. `--listfile-root none`: clears a
  configured root.

Where `none` exists: all four on `export` and `export-terrain`;
`--listfile`/`--listfile-root` on `resolve`; `--dbd-dir` on `db2-export`;
`--db2-dir`/`--dbd-dir` on `appearance-string` and `export-world` (whose
listfile flags are required). Not on `db2-build` (all required) or
`db2-info` (no config). A directory literally named `none` is not
special-cased.

### Does `inline` generalize past `--anim`?

A flag earns an `inline` state only when (1) the model's own blob yields a
meaningful result with no filesystem access, **and** (2) inline and
external results combine rather than exclude each other. `--anim` meets
both. The rest, checked:

- **`--skel` fails (2)**: inline bones and a `.skel` are mutually exclusive
  in every file examined (`SKID` appears only when inline bones are empty).
  `inline` would equal `auto` or `none` in every case. Reopen only on a
  real counterexample.
- **`--textures` fails (1)**: M2 never embeds pixels; `inline` would be
  `none`.
- **Data read unconditionally** (geosets, extra layers, transforms,
  emitters, collision, every LOD level) has no flag at all.

### Config-file defaults for `export`

Per-machine constants (`dbd-dir`, `db2-dir`, `listfile`, `listfile-root`,
`textures`, `store`, ...) come from TOML.

- **CLI11's `App::set_config`, not a config parser**: TOML keys map onto
  the same option registrations, so config values get the same `->check()`
  validators (a configured `skin = "none"` is rejected like a typed one)
  and CLI11's precedence is the wanted one (flag > config > default). No
  key→flag table to keep in sync.
- **Lookup**: `--config <path>` > `$HUSK_CONFIG` > `husk::
  defaultConfigPath()` (`$XDG_CONFIG_HOME/husk/config.toml`, else
  `~/.config/husk/config.toml`). No file is not an error. The path helper
  is shared by every command.
- **Not filtered to a "safe" subset**: every flag is technically settable;
  a filter would fight the mechanism chosen to avoid code. The README's
  example shows only per-machine flags.

### Shell completion generation

`completions/husk.{bash,zsh}` are captured output of the hidden
`--print-completion=<bash|zsh>`, which builds `CLI::App` trees from the real
option registrations and walks them with CLI11's introspection
(`get_options()`, `get_subcommands()`, `get_lnames()`/`get_snames()`/
`get_expected_max()`), never `.parse()`. No C++ parser generates
completions (CLI11's docs checked; unlike `clap_complete`, Cobra, Click);
generating from live options keeps them from drifting. The flag is absent
from `--help` (no human reader). Regenerate, never hand-edit.

## DB2 → SQLite: `db2-info`, `db2-export`, `db2-build`

- **WDC5 only** — every file under `dbfilesclient/` is WDC5. `db2-info`:
  header, per-section layout, field storage, `--rows N` (default 5, `all`)
  of the first readable section, fields by index. Offset-map sections embed
  strings inline, so `db2::decodeOffsetMapRecord` walks each record with a
  bit cursor.
- **Readability is `db2::Section::recordsAvailable()`**, never
  `tactKeyHash != 0` (extractions are usually already decrypted). husk has
  no TACT key store and never will. Counterpart: `CLAUDE.md` Hazards.
- **Offset-map compression other than None** is implemented but unverified
  (none of the 5 local offset-map files use it).
- **`db2-export`**: one table per file, one row per record, every readable
  section. Encrypted sections are skipped with a count; in `--dir` mode an
  unparseable file is skipped with a diagnostic.
- **Real column names only when the shape matches.** `--dbd-dir` is matched
  by table/layout hash, then each field's `<Size>`/`[Length]` against the
  file's `field_storage_info` (`dbd::resolveFieldNames`). Otherwise
  `field_<N>` — never real names on the wrong bytes.
- **Relations**: `<Table::Col>` becomes a `FOREIGN KEY` when the target is
  in the same batch, else a plain column. Non-inline-ID tables get an ID
  column (`$noninline,id$` name, else `id`); `$noninline,relation$`
  (`relationship_mapping`) becomes a named column; arrays become
  `<name>_<i>`.
- **Verified**: `chrmodelmaterial` (336 rows), `namesreserved` (UTF-8),
  `chrmodeltexturelayer` → `charcomponenttexturelayouts` (all 922 rows
  resolve), `scenescripttext` (36,498 rows), `conversationline` (69,312;
  negatives never read as text).
- **`db2-build`** writes the knowledge base: `ModelFileData`/
  `ItemDisplayInfo`/`TextureFileData`/`ItemDisplayInfoModelMatRes`/`Item`/
  `ItemAppearance`/`ItemModifiedAppearance` + listfile into `models`,
  `textures` and `model_object_skin_texture`, filtered by an
  `InventoryType`-vs-path slot check; `_meta` stamps each source's size and
  mtime so `export --knowledge-db` detects staleness. Same-slot collisions
  are not filtered — the subsystem is disabled for real output
  (`TODO/TEXTURE_POOL_RECALL_TODO.md`, `STYLE_CLEANUP.md` item 2).

## Listfile index

`--listfile` is a ~148 MB, 2.2M-row CSV. **The cost was building the map,
not parsing it.** Measured (`husk resolve`, simple item model, n=15):

| | per invocation |
|---|---|
| `--listfile none` (floor) | 333 ms |
| CSV parse, no cache | 1000 ms |
| binary cache of the CSV text | 980 ms |

The text blob reads in 18 ms page-cached; ~97 % was allocating 2.2M strings
and hash-inserting them. **So the map is never materialized.**
`ListfileIndex` (`listfile_index.hpp`: `lookup(fdid) ->
optional<string_view>`, `forEach`) is the interface; the mmap backends
(`listfile_mmap_index.hpp`) answer from a prebuilt, pointer-free image —
zero per-process construction. Offsets, not pointers, so concurrent workers
share one copy through the page cache.

**Sorted vs. hash** (`HUSK_LISTFILE_BACKEND=sorted|hash`, n=30):

| backend | per invocation | on disk |
|---|---|---|
| sorted (binary search) | ~340 ms | 154 MB |
| hash (open addressing) | ~344 ms | 261 MB |
| floor | ~347 ms | — |

Both at the floor, within noise. **Sorted is the default: 41 % smaller and
a simpler failure mode.** Hash stays behind the env var so the comparison
remains reproducible. Extrapolated (arithmetic, not a measured run): ~23 h
saved over a 130,576-file corpus.

**Path → FileDataID** (`fileDataIdsForPaths`, format version 2): the sorted
image also stores its rows in path order (equal paths keep FileDataID
order, so the lowest id wins, as a full pass in FileDataID order would), and
answers each path by binary search; the map and hash backends keep the
single full pass. Measured 2026-11-09, `export-terrain` of forever
`azeroth_32_49` (two path batches per tile): 141 ms sorted vs 688 ms hash,
n=3 warm (the hash pass walks every slot, so the old sorted pass was
somewhat cheaper than 688 ms). The image grew 159 → 168 MB.
`tools/listfile_index.py` reads the same image from Python (FileDataID →
path only), so scan workers share it instead of parsing the CSV each.

### Listfile cache

- **Location**: `$HUSK_CACHE_DIR`, else `$XDG_CACHE_HOME/husk/`, else
  `$HOME/.cache/husk/` (then a temp dir) — the sibling `tact-fetch`
  project's convention. Tests set `$HUSK_CACHE_DIR` (`tests/run_husk.hpp`).
  Files: `listfile.bin`, `listfile.tag`.
- **Freshness: a tag file's mtime**, refreshed on use; older than 10
  minutes is stale ("when we are hammering it, it stays cached," Luna).
  Not atime: the pool is `atime=off`/`noatime`, so atime never updates.
- **Source identity**: the header's recorded path + size + mtime must match
  the current `--listfile`, else rebuild — never a warning.
- **Atomic writes**: a per-process temp file in the same directory, then
  `rename()`. Concurrent rebuilds are accepted (same source, equivalent
  result); a torn read is impossible.
