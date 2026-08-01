# TODO: PM4/PD4 (server-side navigation/pathing mesh) support

**Status: an open punch list, not a historical record.** Fixed/resolved
items get removed outright once closed (see `TODO_correctness.md`'s own
convention) — git history is the record of what was fixed and when, not
this file.

## Why this file exists now

`DESIGN.md`'s Non-goals section already declared PM4/PD4 in scope
(2026-07-31): "PM4/PD4 (server-side navigation/pathing mesh) declared in
scope... explicitly for pathing use cases ('we want pathing')... not yet
researched at all." `WORLD_COMPLETENESS.md` (the WMO/ADT scaffold a
parallel session is fleshing out right now) took the opposite framing in
its own "explicitly out of scope for this file" section: PM4/PD4 is "never
touched by the client renderer, so it doesn't belong in a 'what does the
rendered world look like' completeness file."

**Luna corrected that framing directly**, in `LUNA_NOTES.md` (repo root,
quoting the `WORLD_COMPLETENESS.md` text above): "This should be part of
the world mesh, as even if it's not rendered directly, for example a debug
rendering might want to render it, so it should be hidden by default but
100% included." So the target isn't "skip it, it's server-only" — it's
"parse and export it for real, same as husk's own M2 collision mesh, just
defaulting to invisible in an ordinary render." This file is the actual
implementation plan for that, not another survey — struct-per-chunk,
grounded against real bytes wherever real bytes could be had this session
(see "Real-data verification" below for the one place that genuinely
couldn't happen), a C++ data-model sketch mirroring `src/phys.hpp`'s own
idiom, and a concrete test plan.

This file does **not** touch `WORLD_COMPLETENESS.md`, `DESIGN.md`,
`README.md`, or any sibling `*_TODO.md` file four other agents are writing
in parallel right now for WMO/ADT/WDT/liquid/lighting/fog/collision — a
later consolidated pass wires the cross-document pointers once all of
those land.

## What PM4/PD4 are

Per wowdev.wiki (`documentation/wowdev-wiki/md/PM4.md`/`PD4.md`, both
already mirrored locally): **PM4** is a server-side supplementary file, one
per root ADT tile, sitting alongside the client's own `.adt`. **PD4** is
the same concept scoped to WMOs, one per root `.wmo`. Both hold navigation/
pathing geometry (walkable surfaces, boundary/link data, coarse waypoint
references) the game server needs for creature/NPC movement and
line-of-sight — never downloaded to a retail client, never touched by the
client-side renderer at all. PM4 additionally carries destructible-building
bookkeeping (which surface belongs to which building, and that building's
current destruction state) — an ADT-tile concept with no WMO-scoped
counterpart, since a WMO is already one building.

Both formats use WoW's usual chunked container
(`husk::readChunks`/`findChunk`, `src/chunk.hpp`), and — like every format
in the WMO/ADT/`.phys` family, unlike M2 itself — their FourCC chunk tags
are almost certainly **byte-reversed on disk** (`chunk.hpp`'s own doc
comment: "unlike WMO/ADT, M2 chunk tags are NOT byte-reversed";
`src/phys.hpp`'s doc comment confirms this for `.phys` specifically). PM4/
PD4 share the exact chunk-name vocabulary WMO/ADT use (`MVER`, `MSHD`,
`MS*` prefixes matching WMO's own `MO*`/ADT's own `MC*` naming
convention) and wowdev.wiki explicitly describes them as ADT's/WMO's own
server-side companions — strong family-convention grounds to expect the
same reversal, but **this is not confirmed against a real PM4/PD4 file
this session** (see below for why no real file was obtainable at all) —
flagged here explicitly rather than silently assumed, and the first thing
to confirm the moment a real file exists.

## Reference-material findings

- **`documentation/wowdev-wiki/md/PM4.md`/`PD4.md`**: read in full (both
  short — PD4 is the canonical definition, PM4's own page mostly reads
  "See PD4#X" for every chunk it shares). Struct completeness varies wildly
  by chunk — `MVER`/`MSPV`/`MSPI` are simple and unambiguous; `MSCN`/
  `MSLK`/`MPRL`/`MPRR` have most of their own fields marked
  `wowdev-unverified` *by the wiki itself* ("flags?", "an index somewhere",
  "n ≠ normals"). This is a genuinely thinner spec than `.phys` had going
  in (PHYS.md gave real offsets for nearly every field with only a handful
  of genuinely unverified corners) — closer to `.bone`'s starting position
  (no wiki page at all) than to `.phys`'s.
- **`documentation/wowdev-wiki/HUSK_AMENDMENTS.md`**: checked, zero PM4/PD4
  mentions — no prior husk-side correction exists for either page.
- **`reference/wow.export/` (design/architecture reference only, per this
  project's own rule — never copy code from it)**: grepped
  `src/js/3D/loaders/` and `src/js/3D/exporters/` case-insensitively for
  `pm4`/`pd4`. **Zero PM4 references anywhere** in the tool. PD4 appears
  in exactly two files, and only as a FileDataID pass-through, never
  parsed: `WDTLoader.js`'s `MPHD` chunk handler reads a `pd4FileDataID`
  field (confirmed against `documentation/wowdev-wiki/md/WDT.md` line 24 —
  a real, wiki-documented `MPHD` field, `uint32_t pd4FileDataID`, one
  sibling of `lgtFileDataID`/`occFileDataID`/`wdlFileDataID`/etc. — this is
  a **map-level** PD4 reference, presumably for the rare case where a
  whole map is itself one root WMO, distinct from the per-WMO-file
  resolution the wiki's prose describes for the ordinary case);
  `ADTExporter.js` only ever does `casc.getFile(wdt.pd4FileDataID)` then
  `.writeToFile(...pd4)` — a raw byte copy to disk, no decode logic at
  all. **So: husk attempting a real PM4/PD4 parser is genuinely novel** —
  no established client-tool prior art to lean on architecturally, exactly
  the situation this session was told to expect and confirm. The one
  useful design takeaway: PD4 has *two* real resolution paths worth
  mirroring in husk's own CLI eventually — same-basename-next-to-the-`.wmo`
  (the common case) and a WDT `MPHD.pd4FileDataID` scalar (the map-is-a-
  WMO case) — the same "FileDataID field, resolved via a user-populated
  local directory" shape every other husk sidecar (`SFID`/`AFID`/`BFID`/
  `PFID`/`SKID`/`TXID`) already uses, not a new resolution idiom.
- **husk's own `src/`**: `src/phys.hpp`/`phys.cpp` is the direct structural
  model — standalone chunked-container sidecar, chunk-tag-dispatched
  record arrays, bounds-checked cross-references, a single `ParseError`
  type, a `parse(bytes) -> File` entry point. `src/skel.cpp`'s "no parallel
  struct implementation" precedent (external bones/sequences are hand — ed
  straight to `m2::parseBones`/`parseSequences` rather than re-implemented)
  is the second precedent this plan leans on, since PM4 and PD4 share
  essentially all of their geometry chunks verbatim (see the data-model
  sketch below). `src/m2.hpp`'s `parseVec3Array`/`parseCollisionMesh` plus
  `src/cmd_export.cpp`'s collision-mesh block (lines ~2109-2196) are the
  direct precedent for "real, non-render-primary geometry becomes a real
  tagged `gltf::NamedMesh`, unskinned, `{"collision": true}` in node
  `extras`" — the exact shape this file recommends reusing for PM4/PD4
  below, and the exact shape whose limits motivate this file's own
  "hidden by default" open question.

## Real-data verification — the hardest negative result this project has hit

Every prior husk format investigation that came up empty (`WFV1`/`WFV2`/
`blp` DXT3, etc.) was "zero hits in a corpus that could in principle
contain one." **This is a harder negative**: PM4/PD4 are structurally
absent from any retail CASC storage, by Blizzard's own design, not by
extraction bad luck.

- Confirmed upfront (before this session): `/media/luna/data/wow_export`
  (husk's usual 84k-WMO/55k-ADT pre-extracted corpus) has zero `.pm4`/
  `.pd4` files.
- The community listfile
  (`/media/luna/work/tinker/dev/wow_modding/m2mod/mappings/listfile.csv`,
  2,206,299 entries) has **26,412** real known PM4/PD4 paths — confirmed
  directly (`grep -ci` for `\.pm4$`/`\.pd4$` combined) — so real paths
  genuinely exist and are enumerable, e.g. `world/wmo/cameron.pd4`,
  `world/maps/azeroth/azeroth_33_60.pm4`.
- Tried three real, well-chosen candidates via `casc-tool extract`/`info`
  against live storage `/media/luna/games/World of Warcraft`
  (`--listfile` pointed at the same community listfile): a PD4 whose
  sibling `.wmo`/`.wmo` LODs are already present in the local corpus
  (`world/wmo/cameron.pd4`, next to `cameron.wmo`/`cameron_000.wmo`), a
  second PD4 (`world/wmo/azeroth/buildings/altarofstorms/
  altarofstorms.pd4`), and a PM4 tile (`world/maps/azeroth/
  azeroth_33_60.pm4`). **All three failed identically**: `error: no file
  named '...' found (not in the listfile -- check the path is right, or
  that --listfile is up to date)` — i.e. absent from this build's own root
  manifest, not a listfile mismatch (the exact same listfile resolved
  `cameron.wmo` fine moments earlier in the local corpus check).
- Escalated to a full sweep rather than trust three misses: `casc-tool
  list --storage ... --listfile ...` against the entire live storage
  (`product=wow build=68887`, 3,190,909 total files) piped through `grep
  -ci "\.pm4\|\.pd4"` — **zero matches, anywhere in the storage.** This
  is the structural confirmation: PM4/PD4 categorically never enter a
  retail client's CDN root manifest at all (matches both wiki pages' own
  text verbatim — "not supposed to be shipped to the client"). No future
  `casc-tool` sweep against any live retail install, however broad, will
  ever find one — this is a permanent ceiling on husk's usual
  "verify-against-real-bytes-via-casc-tool" playbook for this one format,
  not a temporary extraction gap.
- Searched every locally accessible directory that might already hold a
  stray sample (`/media/luna/work/tinker/dev/wow_modding/`, husk's own
  `reference/` clone) — nothing.
- Bounded web research (five searches total, matching this project's own
  "don't over-spend" precedent from the multi-root-skeleton investigation):
  no downloadable community PM4/PD4 sample fixture found; no open-source
  parser with committed real-byte test fixtures found either.
  `namreeb/namigator` (the closest "WoW private-server pathfinding"
  project) turned out not to be a source at all once actually checked —
  it *generates its own* navmesh from ADT/WMO terrain geometry directly
  (via Recast), it does not consume Blizzard's own PM4/PD4 files, so it's
  not even a design cross-check, let alone a byte source. The listfile's
  26,412 known paths most likely come from a non-retail source (a leaked
  internal/alpha build is the explanation commonly repeated in WoW-modding
  circles) — **this is an unconfirmed community claim, not something this
  session's own searches verified**, noted honestly rather than asserted
  as fact.

**Disposition: zero real PM4/PD4 bytes were obtained or inspected this
session.** Every struct below is a wiki transcription only. Per this
project's own methodology (verify against real bytes before shipping a
parser as anything but explicitly preliminary — `WIKI_FINDINGS.md`
throughout), every field is flagged `unverified` below, the same "wiki
text only, not independently confirmed" flag `.phys`'s own `BOXS`/`SPHJ`/
etc. already carry for their own unobserved variants — just applied to
essentially the whole format at once, since there is no observed variant
to fall back on here. This does not mean "don't implement" (see the
Parse/Consumption recommendation below) — it means implement openly as
preliminary, exactly as PCOL's wiki struct was flagged before its own
corpus check existed, and revisit hard the moment a real file turns up.

## Per-chunk-tag findings

Every offset/size below is a literal transcription of `PM4.md`/`PD4.md`;
none of it has been cross-checked against real bytes. "Shared" means the
PM4 page itself says "See PD4#X" — i.e. PD4 is the canonical definition.

### Shared (both PM4 and PD4)

- **`MVER`** — `uint32_t version`. PD4 documents `version_48 = 48` (seen
  in two named WoD builds). PM4 doesn't give its own enum at all — assume
  the same scalar shape, version meaning unconfirmed for PM4 specifically.
- **`MSHD`** (PD4-defined, PM4 shares it) — a 0x20 (32-byte) header:
  three `uint32_t` fields (`_0x00`/`_0x04`/`_0x08`) plus a trailing
  `uint32_t _0x0c[5]`, wiki-flagged "Always 0 in version_48, likely
  placeholders." Verification-signal value only, same class as M2's own
  collision-box/sphere-radius scalars (`M2_COMPLETENESS.md`'s Collision
  row) — not an export target.
- **`MSPV`** — `C3Vector msp_vertices[]` (12 bytes/record, flat array). A
  vertex pool referenced by `MSPI`.
- **`MSPI`** — `uint32_t msp_indices[]`, each an index into `MSPV`. Flat
  index array, no documented grouping of its own (grouping comes from
  `MSLK`'s `MSPI_first_index`/`MSPI_index_count` pair, below) — likely
  boundary/portal/link-line geometry, not the walkable surface itself
  (that's `MSVT`/`MSVI`/`MSUR`, below).
- **`MSCN`** — `C3Vector mscn[]`. Wiki's own text: "Not related to MSPV
  and MSLK... n ≠ normals" (unverified) — genuinely unclear semantics,
  a second, independent vector pool with no known cross-reference into it
  from anything else in the file.
- **`MSLK`** — 0x14 (20-byte) records: `uint8_t _0x00` (flags, wiki-listed
  observed bits `&1;&2;&4;&8;&16`, meanings unnamed), `uint8_t _0x01`
  ("0…11-ish; position in some sequence?"), `uint16_t _0x02` (padding,
  "Always 0 in version_48"), `uint32_t _0x04` ("an index somewhere"),
  `int24_t MSPI_first_index` (-1 if `_0x0b` — i.e. `MSPI_index_count` — is
  0), `uint8_t MSPI_index_count`, `uint32_t _0x0c` ("Always 0xffffffff in
  version_48"), `uint16_t msur_index` (a real, named cross-reference into
  `MSUR` below), `uint16_t _0x12` ("Always 0x8000 in version_48"). The one
  clean, actionable piece: `MSPI_first_index`/`MSPI_index_count` is a
  bounded-region-into-`MSPI` reference, structurally identical to how
  `.phys`'s `Body.shapeBase`/`shapeCount` indexes into `File.shapes` —
  `phys.cpp`'s `validateReferences` idiom applies directly once real bytes
  exist to confirm bounds against.
- **`MSVT`** — `C3Vector msvt[]`, the walkable-surface vertex pool. **Two
  real, concrete facts, not just an unverified struct**: (1) on-disk field
  order is Y, X, Z (wiki: "the values are ordered YXZ"), not the usual
  X/Y/Z; (2) a documented coordinate-conversion formula to get in-game
  world coordinates: `worldPos.y = 17066.666 - position.y`,
  `worldPos.x = 17066.666 - position.x`, `worldPos.z = position.z / 36.0`
  (the /36 wiki-flagged as "used to convert the internal inch height to
  yards", unverified). This is a **new coordinate convention husk has
  never needed before** — every M2/`.phys`/collision-mesh position husk
  currently exports goes through `toGltf()`'s Z-up→Y-up swap alone;
  `MSVT` needs this world-space unswizzle applied *first*, then still
  needs the same Z-up→Y-up remap the rest of husk's Y-up glTF output
  uses (WoW's own world space is Z-up like M2's model space) — a genuinely
  new function, not a reuse of `toGltf()`. **Cross-format dependency
  worth flagging, not resolving here**: PM4 in particular yields
  already-world-space (not model-space) positions per ADT tile — the
  parallel ADT/WMO effort currently being scoped elsewhere will need to
  settle its own world-space→glTF convention regardless; PM4's export
  should reuse whatever that lands on rather than invent an independent
  one, so implementation here should happen after (or in close
  coordination with) that work, not fully standalone.
- **`MSVI`** — `uint32_t msv_indices[]`, indices into `MSVT`. Wiki's own
  text: "Likely not triangles but quads, or an n-gon described somewhere,
  possibly MSUR where `_0x01` is count and `_0x14` is offset" (unverified)
  — i.e. even whether this is a triangle list is not confirmed. A real
  glTF triangle-mesh export needs a triangulation step (fan-triangulate
  each `MSUR`-described run, assuming convexity) that has no precedent in
  husk's existing collision-mesh code (which already assumes clean
  triangle triples) — flagged as a real implementation risk, not routine
  plumbing.
- **`MSUR`** — 0x20 (32-byte) surface-descriptor records: `uint8_t _0x00`
  ("earlier documentation has this as bitmask32 flags", unverified),
  `uint8_t _0x01` ("count of indices in MSVI", unverified but the most
  load-bearing field here), `uint8_t _0x02`, `uint8_t _0x03` (padding),
  four `float`s (`_0x04`/`_0x08`/`_0x0c`/`_0x10` — undocumented, plausibly
  a plane equation/normal+distance given the 4-float shape, not
  confirmed), `uint32_t MSVI_first_index` (a real, named offset into
  `MSVI`), `uint32_t _0x18`, `uint32_t _0x1c`. Same "self-describing
  offset region" idiom `PLYT` already uses in `phys.cpp`, but simpler — a
  fixed-stride record referencing a variable-length run elsewhere, not
  `PLYT`'s own header-and-data-interleaved-in-one-chunk shape.

### PD4-only

- **`MCRC`** — a single `uint32_t _0x00`, wiki-flagged "Always 0 in
  version_48" (unverified). Trivial scalar, diagnostic-only, no glTF
  relevance — same class as `MSHD`.

### PM4-only

- **`MPRL`** — 0x18 (24-byte) records: `uint16_t _0x00` ("Always 0",
  unverified), `int16_t _0x02` ("Always -1", unverified), `uint16_t
  _0x04`, `uint16_t _0x06`, `C3Vector position`, `int16_t _0x14`,
  `uint16_t _0x16`. Name ("m_position_reference_list"-shaped, by wowdev's
  own naming convention elsewhere) plus the real `position` field suggests
  a coarse waypoint/reference-point table, not walkable-surface geometry
  itself — genuinely unclear whether it's consumed by anything else in
  the file (no cross-reference field name points at it from `MSLK`/
  `MSUR`).
- **`MPRR`** — 4-byte records: two `uint16_t` fields, no names, no
  documented meaning at all. Plausibly index pairs into `MPRL` (a
  reference-graph edge list) given the adjacent chunk ordering, but this
  is inference, not wiki text — flagged explicitly as a guess.
- **`MDBH`/`MDBI`/`MDBF`** — a genuinely different shape from every other
  chunk here: `MDBH`'s own payload is just `uint32_t
  m_destructible_building_count`, and the wiki states it "is followed by
  `count` MDBI and 3x MDBF each" — i.e. `MDBI` (a plain `uint32_t
  m_destructible_building_index`) and three `MDBF` records (each a bare
  null-terminated `char[]` filename, no length prefix) appear as their own
  **repeated top-level chunk records** immediately after `MDBH` in the
  chunk stream, not as an array packed inside `MDBH`'s own payload. This
  is new for husk: every existing sidecar parser (`.phys` included) uses
  `findChunk` to grab the *first* chunk matching a given tag — `MDBH`'s
  own count only makes sense read together with a **positional** walk of
  the chunk list (find `MDBH`, then consume the next `count` groups of
  one `MDBI` chunk + three `MDBF` chunks in file order), which
  `chunk.hpp`'s current `findChunk(chunks, tag)` (single-match) doesn't
  support at all — a real, new small addition needed (an
  index-aware/positional overload, or the `pm4.cpp` parser just walking
  `chunks` directly instead of going through `findChunk` for this one
  case). The "3x MDBF each" detail itself is unexplained by the wiki —
  plausibly three named model-state variants per destructible building
  (intact/damaged/destroyed, a common WoW destructible-building shape) but
  this is inference, not a documented fact.
- **`MDOS`** — 8-byte records: `uint32_t m_destructible_building_index`,
  `uint32_t destruction_state`. A flat state table, one entry per
  destructible building (or per state transition — unclear which from the
  wiki text alone).
- **`MDSF`** — 8-byte records: `uint32_t msur_index`, `uint32_t
  mdos_index`. Links a surface (`MSUR`) to a destruction state (`MDOS`) —
  the clearest cross-reference chunk in the whole format, structurally
  identical to how an M2 batch references a material index.

## C++ data-model sketch

Mirrors `src/phys.hpp`'s idiom (standalone header, chunk-tag-dispatched
record parsing, one `ParseError`, one `parse(bytes) -> File` entry point)
and `src/skel.cpp`'s "share the parser, don't reimplement" precedent (PD4
defines the geometry chunks; PM4 reuses them verbatim per the wiki's own
"See PD4#X" cross-references, so PM4 should call into PD4's parser for
those chunks, not duplicate them):

```
src/pathmesh.hpp / pathmesh.cpp   -- shared geometry chunks: MVER, MSHD,
                                      MSPV, MSPI, MSCN, MSLK, MSVT, MSVI,
                                      MSUR. One SurfaceGeometry struct +
                                      one parseSurfaceGeometry(chunks)
                                      free function, called by both
                                      pd4::parse and pm4::parse -- same
                                      role m2::parseBones/parseSequences
                                      play for skel.cpp today.

src/pd4.hpp / pd4.cpp             -- pd4::File { version, crc (MCRC,
                                      optional), geometry
                                      (pathmesh::SurfaceGeometry) },
                                      pd4::parse(bytes) -> File.

src/pm4.hpp / pm4.cpp             -- pm4::File { version, geometry
                                      (pathmesh::SurfaceGeometry),
                                      positionReferences (MPRL),
                                      positionReferenceLinks (MPRR),
                                      destructibleBuildings (MDBH count +
                                      the MDBI/3xMDBF positional walk,
                                      folded into one
                                      DestructibleBuilding{index,
                                      filenames[3]} vector),
                                      destructionStates (MDOS),
                                      surfaceDestructionLinks (MDSF) },
                                      pm4::parse(bytes) -> File.
```

Every struct field ships flagged `unverified` in its own doc comment
(mirroring `.phys`'s own per-variant unverified flags), and every
cross-reference (`MSLK.msur_index`, `MDSF.msur_index`/`mdos_index`,
`MSUR.MSVI_first_index`, `MSLK.MSPI_first_index`/`_count`) gets the same
`validateReferences`-style bounds check `phys.cpp` already runs — a real
one triggering here would be corruption or a parser bug, not data to
silently accept, per this project's Foreign Data policy.

## Parse / Consumption / glTF-ceiling recommendation

Using `WORLD_COMPLETENESS.md`'s own three-axis vocabulary (Parse depth:
`none`/`descriptor`/`deref`/`full`; Consumption: `none`/`diagnostic`/
`extras`/`native`; glTF ceiling: as defined there):

| Chunk(s) | Parse | Consumption | glTF ceiling | Note |
|---|---|---|---|---|
| `MVER`/`MSHD`/`MCRC` | full | diagnostic | n/a — verification-signal only | mirrors M2's own collision-box/sphere-radius row |
| `MSPV`/`MSPI`/`MSVT`/`MSVI`/`MSUR` | full | **native**, tagged | native — 100% once verified | direct M2-collision-mesh precedent: one more `gltf::NamedMesh`, unskinned (PM4/PD4 tiles have no skeleton at all — there's no armature to share, unlike M2's collision mesh which at least *can* share a skin), `{"pathing": true}` (or similar) in node `extras`. Needs `MSVI`→triangle fan-triangulation (unverified whether n-gons are convex) and the new `MSVT` world-space unswizzle (see above) — genuinely more assembly work than M2's collision mesh needed |
| `MSLK` | full | extras (raw fields) or a second tagged line/edge mesh | native-possible, not done | too semantically unclear today (most fields wiki-flagged unverified) to commit to a specific glTF shape beyond "surface it raw" |
| `MPRL`/`MPRR` (PM4 only) | full | extras or diagnostic | extras-capped, permanent (tentative) | a coarse reference-point/graph table, not renderable surface geometry — no owning bone/joint concept exists here (unlike M2 Attachments, which parent to a joint), so even a "plain node per point" translation has no natural parent; revisit once real bytes clarify what these actually reference |
| `MDBH`/`MDBI`/`MDBF`/`MDOS`/`MDSF` (PM4 only) | full | diagnostic (`dump-chunks`-equivalent JSON) | n/a, by design | game-logic bookkeeping (which surface belongs to which building, and its state) — same "real data, no glTF slot" treatment `EXP2`/`PFDC`/`PCOL`/`DETL` already got in `M2_COMPLETENESS.md` |

**Overall recommendation**: implement the parser now (the format is small
enough, and even wiki-flagged-unverified fields are worth reading raw —
same call `.phys`'s preliminary chunks and M2's `PCOL` made before their
own corpus checks existed), but ship it explicitly labeled preliminary end
to end — `husk info`/`dump-chunks` output, doc comments, and this file
itself should all say "wiki-transcribed, unverified against real bytes"
until a real file exists to check it against, per this project's own
standing discipline. Do **not** treat "the parser compiles and round-trips
a synthetic fixture" as equivalent to the verified confidence every other
husk format currently carries.

## Open design question: does "hidden by default" actually mean anything here?

Luna's own framing — "100% included but hidden by default" — needs an
honest look at what husk can actually promise, not a silent assumption
either way.

**The only precedent husk has** is the M2 collision mesh: a real
`gltf::NamedMesh`, unskinned, tagged `{"collision": true}` in that node's
`extras`. It is **present, not hidden** — `writeGlbMulti`'s own doc comment
says so directly ("doesn't skip rendering it... only marks it for a custom
renderer or Blender script to filter out"). Applying that same pattern to
PM4/PD4 (`{"pathing": true}`) would satisfy "100% included" cleanly, but
it would **not** satisfy "hidden by default" in any renderer that hasn't
been specifically taught to look for the tag — Blender's own stock
importer, husk's explicitly stated sole target (`DESIGN.md`'s Goal: "a
`.glb` file Blender's stock importer can open is the entire deliverable"),
included. Every geoset/skinSectionId/billboardMode `extras` tag already in
husk works exactly this way, and this project's own precedent has always
been fine with that — but none of those were ever framed as needing to be
*hidden*, only *identifiable*.

**A real glTF mechanism for this exists**: `KHR_node_visibility` (a
Khronos-ratified glTF 2.0 extension — confirmed via
`github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/
KHR_node_visibility` this session, not assumed) puts a plain `visible:
false` boolean directly on a node, recursively hiding its whole subtree —
exactly "hidden by default," and already implemented by Godot, Unity,
Babylon.js, and glTF-Transform. **But Blender's stock glTF importer does
not implement it at all** (confirmed via `github.com/KhronosGroup/
glTF-Blender-IO` issue #2597, open as of this session: "such models can't
be imported into Blender, which does not yet support this extension").
Worse, whether that failure mode is "silently ignores the extension and
imports the node visible anyway" (harmless but defeats the whole point) or
"refuses to open the file at all" depends on whether the extension is
declared in glTF's `extensionsUsed` (optional, should degrade gracefully)
or `extensionsRequired` (mandatory — a spec-compliant importer that
doesn't support it **must** refuse the whole file, which is exactly the
failure Godot's own users are hitting against Blender per that same
issue thread). Declaring it `extensionsRequired` risks breaking husk's own
"Blender must be able to open every `.glb` this tool produces" bar
outright — a strictly worse outcome than the collision-mesh precedent's
"present, not actually hidden."

**Left as an open question, not silently resolved either way.** Three
real options, none of them free:

1. **Reuse the collision-mesh precedent as-is** (`{"pathing": true}`
   extras tag). Guaranteed to open cleanly in Blender, matches every
   existing husk `extras` convention, but is honestly "identifiable," not
   "hidden," in any renderer that isn't specifically told to look.
2. **Adopt `KHR_node_visibility`, declared in `extensionsUsed` only (never
   `extensionsRequired`)**. The spec-correct mechanism for real hidden-by-
   default behavior in every importer that *does* support it (Godot,
   Unity, Babylon.js today) — but a `.glb` opened in Blender's own stock
   importer would, at best, silently ignore the flag and render the mesh
   visible anyway (no worse than option 1 there), and this needs to be
   confirmed empirically (not assumed) the moment this is implemented,
   since option 1's "at best" hasn't actually been tested against a real
   Blender import yet either.
3. **A separate, opt-in Blender-side script** (not an addon — `DESIGN.md`'s
   own non-goal: "No Blender addon... the entire deliverable") that reads
   the `extras` tag from option 1 and hides the object post-import. Fully
   satisfies "hidden by default" for anyone who runs it, but is new
   tooling husk doesn't have today, and "the user has to remember to run a
   script" is a materially weaker promise than "hidden by default" as
   Luna wrote it.

**Decision, confirmed by Luna 2026-08-01: option 1, reuse the
collision-mesh precedent as-is** (`{"pathing": true}` extras tag,
guaranteed to open cleanly in Blender, matching every existing husk
`extras` convention) — accepted explicitly as "identifiable, not
literally hidden," the same honest tradeoff the collision mesh itself
already lives with. `KHR_node_visibility` (option 2) and a separate
opt-in Blender script (option 3) are not adopted, but are recorded above
as real, considered alternatives should a future session want genuine
hidden-by-default behavior for a specific downstream consumer that
supports the extension — this decision covers husk's own default export
path only, not a hard rule against ever revisiting it.

## Test plan

**No real fixture exists to test against** (see Real-data verification
above) — this is the one part of this plan that has to stay honest about
that constraint rather than pretend otherwise.

1. **Synthetic fixtures only, for now**, mirroring `.phys`'s own synthetic-
   fixture-plus-real-file split (`tests/test_phys.cpp`) but without the
   real-file half yet: hand-built byte buffers for `MVER`/`MSHD`/`MSPV`/
   `MSPI`/`MSVT`/`MSVI`/`MSUR` covering the happy path, an out-of-range
   `MSLK.msur_index`/`MDSF.msur_index` throw, a malformed-stride throw,
   and — specifically for `MDBH`/`MDBI`/`MDBF` — a synthetic chunk stream
   proving the positional (not `findChunk`-single-match) walk logic
   actually consumes the right number of `MDBI`/`MDBF` groups in order.
2. **A real end-to-end round-trip is still worth building now**, same
   shape as every other husk export path: a synthetic PM4/PD4 → `pm4::
   parse`/`pd4::parse` → the recommended tagged `gltf::NamedMesh` → real
   `gltf_validator` (0 errors) → headless Blender import (mesh imports,
   whatever the "hidden by default" decision above lands on, actually
   behaves as decided) — proves the pipeline shape is right even though
   the byte-level struct itself is still preliminary.
3. **Flag this file's own priority-order note** (below) as the trigger for
   revisiting: the moment a real `.pm4`/`.pd4` file becomes available from
   any source (a future community listfile update against a non-retail
   build, a leak, a shared sample from someone with archive access), redo
   the verification pass this session couldn't — decode it independently
   (a from-scratch script, not husk's own parser, same discipline every
   other `WIKI_FINDINGS.md` entry uses), check every cross-reference
   in-bounds, and only then promote the parser's own doc comments from
   "preliminary" to a real `WIKI_FINDINGS.md` section.

## Priority: PM4 over PD4

**PM4 (ADT-tile-scoped) is the higher-value target, PD4 (WMO-scoped)
second**, for three concrete reasons, not just "ADT work is already in
scope elsewhere":

1. **Coverage**: PM4 covers open-world terrain pathing (every ADT tile);
   PD4 only covers the subset of buildings that are WMOs with their own
   PD4 companion. A pathing use case ("we want pathing," `DESIGN.md`'s own
   framing) cares about the ground far more than any one building.
2. **Real-file availability, if it ever changes, likely favors PM4 too**:
   the listfile split is lopsided in exactly this direction already —
   20,857 of the 26,412 known PM4/PD4 paths are `.pm4` (confirmed via
   `grep -ci` against the same community listfile), only 5,555 `.pd4`,
   consistent with "one per ADT tile" simply outnumbering "one per WMO
   with a companion file" at the source.
3. **PM4 shares essentially all of PD4's own chunk vocabulary** (per the
   data-model sketch's `pathmesh.hpp` split) — implementing PM4 first
   means PD4's own geometry-chunk parsing arrives almost for free,
   needing only its own thin wrapper (`MCRC` plus the shared geometry)
   rather than a second independent implementation effort.

The one reason to *not* start with PM4 alone: its `MSVT` positions are
already real-world-space (not model-local), which — per the cross-format
dependency flagged above — wants the parallel ADT/WMO effort's own
world-space→glTF convention to exist first, so PM4's own export doesn't
invent an axis convention independently that then has to be reconciled
later. PD4's positions, by contrast, are presumably WMO-group-local (same
open question the wiki text doesn't resolve, flagged above) and might be
implementable slightly more independently of that dependency — worth a
second look once the ADT/WMO world-space convention actually lands,
in case it changes this ordering.

## References

- `documentation/wowdev-wiki/md/PM4.md` (wiki_revision 36770)
- `documentation/wowdev-wiki/md/PD4.md` (wiki_revision 36772)
- `documentation/wowdev-wiki/md/WDT.md` (line 24, `MPHD.pd4FileDataID`)
- `documentation/wowdev-wiki/HUSK_AMENDMENTS.md` (checked, no PM4/PD4
  amendments exist)
- `reference/wow.export/src/js/3D/loaders/WDTLoader.js` (line 50),
  `reference/wow.export/src/js/3D/exporters/ADTExporter.js` (lines
  444-446) — the only two PD4-touching lines in the whole tool, a raw
  FileDataID→disk copy, never parsed; zero PM4 references anywhere
- `src/phys.hpp`/`src/phys.cpp` — direct structural model (chunk-tag
  dispatch, bounds-checked cross-references, `ParseError`/`File`/`parse`
  shape)
- `src/skel.cpp` — the "share the parser, don't reimplement" precedent
  this file's `pathmesh.hpp` split leans on
- `src/m2.hpp` (`parseVec3Array`/`parseCollisionMesh`), `src/cmd_export.cpp`
  (collision-mesh block, ~lines 2109-2196), `src/gltf.hpp`
  (`NamedMesh::isCollision` doc comment) — the direct precedent for "real,
  non-render-primary geometry as a tagged glTF mesh," and the source of
  this file's own "hidden by default" open question
- `src/chunk.hpp` — chunk-tag reversal convention (M2 vs. WMO/ADT-family),
  `findChunk`'s single-match limitation relevant to `MDBH`/`MDBI`/`MDBF`
- `DESIGN.md`'s Non-goals section (PM4/PD4 in-scope declaration,
  2026-07-31) and Goal section ("No Blender addon" non-goal, relevant to
  the hidden-by-default question)
- `WORLD_COMPLETENESS.md`'s "The three axes" section (Parse/Consumption/
  glTF-ceiling vocabulary reused directly above) and its own now-superseded
  "explicitly out of scope for this file" PM4/PD4 paragraph
- `LUNA_NOTES.md` — the direct correction that prompted this file
- `github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/
  KHR_node_visibility` and `github.com/KhronosGroup/glTF-Blender-IO`
  issue #2597 — real, checked this session (not assumed), backing the
  hidden-by-default open question
