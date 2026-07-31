# M2_GAPS_TODO — implementation plan for documented-but-unbuilt M2 coverage

**Status: ready to implement, item by item.** Unlike `PHYS_TODO.md`'s
situation (one cohesive feature, one investigation, one plan), this file
bundles several independent, small-to-medium gaps a coverage review this
session found — each one has a fully documented byte layout (wowdev.wiki,
or husk's own already-verified parsers for a sibling format) and no
external-data blocker. That's the dividing line that put something here
rather than in `TODO_correctness.md` (genuinely low-priority-by-design, or
blocked on client-side DB2 data) or the new `M2_UNKNOWNS_EXPLORATION.md`
(byte layout not actually known yet, anywhere). Each item below is
independently implementable — do them in any order, or split across
sessions, without the cross-item coupling `PHYS_TODO.md`'s single feature
had.

**Convention note, carried over from `PHYS_TODO.md`/`ANIM_TODO.md`**: once
an item below is implemented and tested, remove it from this file outright
(don't mark it `[Done]` and leave it) — fold the permanent record into
`M2_COMPLETENESS.md`/`DESIGN.md`/`README.md` as usual, same as every prior
TODO file's lifecycle (see `CLAUDE.md`'s Resume log for the pattern). When
every item is gone, delete this file the same way `PHYS_TODO.md`/
`ANIM_TODO.md`/`MULTIROOT_SKELETON_TODO.md`/`CORPUS_TODO.md` were.

---

## Priority order (bottom-line-up-front, per this project's own convention)

(Former Item 1 — `M2Sequence`'s missing fields, including `aliasNext` chain
resolution — is implemented; see `DESIGN.md`'s Key design decisions and
`M2_COMPLETENESS.md`'s Animation section. Remaining items keep their
original numbers below, not renumbered, since those numbers are live
cross-references elsewhere in this file and in `src`/`tests`.)

1. **Item 5 — `Texture.type` export.** Trivial (one field already parsed,
   just not threaded through to material extras), meaningfully closes a
   "why is this material's texture just missing" confusion for any
   downstream consumer.
2. **Item 2 — `PFDC`.** husk already has the full `.phys` parser
   (`src/phys.hpp`/`phys.cpp`) — this is "point it at a different chunk,"
   not new reverse-engineering.
3. **Item 6 — Attachments/Events/Lights as real glTF nodes.** Well
   understood, no ambiguity, moderate effort (new node-emission code, not
   just extras).
4. **Item 3 — `EXP2`.** Needs one real-file check before implementing (see
   below) but the struct shape is simple.
5. **Item 7 — animated material tint/fade extras dump.** Lowest urgency —
   diagnostic value only, no glTF slot exists for the animated case anyway
   (`M2_COMPLETENESS.md` already logs this ceiling as `native-possible, not
   done`, meaning "worth doing eventually," not "blocks anything").
6. **Item 4 — `PCOL`.** Blocked on finding a real file with this chunk at
   all (War Within 11.1.7+, player-housing furniture) — do the corpus
   search first; if nothing turns up, this one just waits.
7. **Item 8 — `DETL`.** New (from `M2_UNKNOWNS_EXPLORATION.md`'s
   investigation, now closed): real byte layout fully confirmed against
   1,043 real files, zero ambiguity left. Similar effort/shape to Item 5 —
   one small struct, straightforward `dumpDetl`-style addition, no glTF
   translation needed (diagnostic-only, same class as `WFV3`).

---

## Item 2: `PFDC` — reuse husk's own `.phys` parser

### Current state

`cmd_dump.cpp`'s `kFallback` table dumps `PFDC` as an opaque raw blob with
the note: "embeds a `.phys`-shaped `PHYS` sub-structure that itself has no
documented byte layout on wowdev.wiki." That note undersells husk's own
position — wowdev.wiki's `PFDC` entry says outright: "Contains inline
physics information in the same structure as the `.phys` files," and husk
independently reverse-engineered and verified that exact structure against
103 real files this session (`src/phys.hpp`/`phys.cpp`, `WIKI_FINDINGS.md`
§9). The blocker was never "the structure is unknown" — it's "nobody's
pointed the existing parser at this chunk yet."

### Implementation plan

1. Check `PFDC`'s payload shape against `phys::parse`'s expectations: the
   wiki notes `PFDC` = `PHYS physics; char PADDING[6];` — i.e. the chunk's
   payload *is* a `.phys` file's own byte layout (chunk container and all,
   per `PHYS.md`), plus up to 6 trailing padding bytes. `phys::parse`
   currently takes a `std::vector<uint8_t>` (a whole file) — check whether
   it assumes the input starts exactly at a `.phys`-specific leading
   structure (`PHYS.md`'s own header) that would also be present verbatim
   inside `PFDC`'s payload, or whether any assumption needs relaxing to
   accept "here's the same bytes, just embedded in another chunk instead
   of being the whole file." The 6 trailing padding bytes need to be
   tolerated (not consumed as if they were another chunk) — `phys::parse`
   already reads chunks by walking to the end of the buffer or by chunk
   count; confirm it stops cleanly rather than choking on trailing
   padding, or trim the last 6 bytes defensively before handing the slice
   to `phys::parse` if not.
2. `cmd_dump.cpp`: move `PFDC` out of `kFallback` into a new dedicated
   entry that slices the chunk's payload and calls `phys::parse` +
   `writePhysFile` (the same function `dump-chunks <file.phys>`'s direct
   path already uses) instead of `dumpRawFallback`.
3. Real-file check before calling this done: find a real M2 with a `PFDC`
   chunk (Shadowlands 9.0.1.33978+) — likely candidates are creature/
   world-object models with inline physics rather than an external
   `.phys` sidecar; a quick corpus grep for the `PFDC` tag (reversed or
   not — confirm which, `.m2` chunk tags are **not** byte-reversed per
   `M2.md`'s own note, unlike `.phys` itself) across
   `/media/luna/data/wow_export` will find one if it exists in the corpus.
   If none exists in the real corpus, this item still ships (structure is
   known either way) but stays flagged "unverified against real data,"
   same "verified floor" honesty this project already applies elsewhere.

### Test plan

- `tests/test_dump.cpp`: a synthetic `PFDC` chunk (a real `.phys` fixture's
  bytes, embedded inside a hand-built `PFDC` chunk wrapper + trailing
  padding) round-tripping through the new dump path — reuse an existing
  `.phys` test fixture's bytes rather than authoring new ones from
  scratch.
- If a real `PFDC`-bearing file turns up: a real-data
  `tests/test_integration.cpp` case, gated the same `HUSK_TEST_*` way
  every other real-fixture check in this project is.

### Docs to update

- `cmd_dump.cpp`'s own `kFallback`-table comment (remove the `PFDC` entry
  and its now-inaccurate note).
- `M2_COMPLETENESS.md`'s "Particle/ribbon side-chunks" row or a new row —
  `PFDC` isn't a particle/ribbon thing, it may deserve its own row under
  Collision & physics instead, next to the `.phys` sidecar row.
- `README.md`'s `dump-chunks` section, if it enumerates specific chunk
  names anywhere.

---

## Item 3: `EXP2` — extended-particle curve data

### Current state

`cmd_dump.cpp`'s `kFallback` table cites "contains a nested
`M2PartTrack<fixed16>` whose own byte layout isn't given on wowdev.wiki"
as the blocker. That's not quite right — `M2PartTrack` **is** defined, in
`M2.md`'s `Types` section:

```
template<typename T>
struct M2PartTrack {
  M2Array<fixed16> times;
  M2Array<T> values;
};
```

This is the flat `{times, values}` pair — a simpler shape than the
per-sequence-indirected `M2Track<T>` husk already resolves elsewhere
(`resolveFloatTrackSequence` and friends, `src/m2.cpp`), not the same
`FBlock` shape `M2Particle`'s Cata+ curves use either. `EXP2`'s full
struct:

```cpp
struct M2ExtendedParticle {
  float zSource;
  float colorMult;
  float alphaMult;
  M2PartTrack<fixed16> alphaCutoff;
};
struct M2InitExtendedParticleArray {
  M2Array<M2ExtendedParticle> content;  // same length as particle_emitters
};
```

### What needs checking before implementing

1. Confirm `fixed16`'s exact interpretation (husk likely already has this
   type from `M2Particle`'s own Cata+ curve work — reuse, don't
   re-derive).
2. Find a real file with an `EXP2` chunk (7.3+) and confirm: `content`'s
   length equals `particle_emitters.count` (per the wiki's own claim —
   verify, don't assume, same discipline every other real-data check in
   this project uses), and that `alphaCutoff.times`/`values` decode to
   plausible monotonic-timestamp / bounded-alpha curves the way `M2Particle`'s
   own FBlock curves did when that was verified (`WIKI_FINDINGS.md` §6).

### Implementation plan

1. `src/m2.hpp`/`m2.cpp`: new `ExtendedParticle` struct + parser, using
   the existing `M2PartTrack`-style flat-array resolver if one already
   exists from another chunk, else add one (this is the simplest track
   shape in the whole format — should be a small addition).
2. `cmd_dump.cpp`: move `EXP2` from `kFallback` to a dedicated `dumpExp2`
   entry in `kDocumented`, indexed by particle-emitter position (same
   convention `TXAC`'s per-material/per-particle indexing already uses,
   if applicable).

### Test plan

Real-fixture-gated case in `tests/test_dump.cpp` (or synthetic if no real
`EXP2`-bearing file exists in the corpus — flag which, honestly, the same
way `PHYS_TODO.md` flagged its own real-data gaps up front rather than
discovering them mid-implementation).

### Docs to update

`M2_COMPLETENESS.md`'s "Particle/ribbon side-chunks" row (currently lists
`TXAC`/`EXPT`/`RPID`/`GPID`/`PGD1` — add `EXP2`, and update `EXPT`'s own
note if `EXP2` is confirmed to supersede it for files that have both, per
the wiki's own "probably outdated... client tries to reconstruct EXPT from
EXP2" text).

---

## Item 4: `PCOL` — player-housing collision (War Within 11.1.7+)

### Current state

Held in `kFallback` because the wiki flags its own struct "Preliminary
structure as per Zee's research" — but a full byte-accountable struct
*is* given (counts + offsets for vertex positions, face normals, indices,
flags, each a self-describing region the same way `PLYT`'s polytope shapes
already are in `.phys`).

### Blocker

**Real test data, not structural uncertainty.** This chunk only appears on
11.1.7+ player-housing furniture models — check whether the corpus at
`/media/luna/data/wow_export` has any real coverage of that expansion
range at all before investing implementation time. If it doesn't, this
item just waits for newer extraction data; don't invent synthetic-only
coverage for a "preliminary" wiki struct with zero real-byte
cross-checking, same caution this project already applies elsewhere
(`kMinVerifiedParticleVersion`'s whole reason for existing).

### If real data is found

Follow the same "self-describing offset region, full byte-accounting
cross-check" discipline `PLYT`'s implementation already used (compute
expected total size from the struct's own count fields, assert it equals
the chunk's real size before trusting the offsets) — `PCOL`'s own struct
has exactly this shape (`vertexPosCount`/`vertexPosOffset` pairs, etc.),
and the wiki explicitly warns "there can be extra bytes between the data,
use the offsets" — i.e. don't assume the regions are contiguous, this is
not a `PLYT`-style single dense blob.

### Docs to update

`M2_COMPLETENESS.md`'s Collision & physics section — new row, likely
`n/a` glTF-ceiling initially (extras, no native slot, same class as
`.phys`) unless a real file surfaces and a translation to husk's existing
`collision` extras-tagged mesh makes sense.

---

## Item 5: `Texture.type` — export the hardcoded/replaceable-slot marker

### Current state

`m2::Texture::type` (`src/m2.hpp`) is parsed and printed by `husk info`
(`cmd_info.cpp:138`), but **`husk export` never reads it** — confirmed by
grep, `type` appears nowhere in `cmd_export.cpp`'s material-building code.
A texture with `type != 0` (hardcoded slot — character skin, hair, item
tint, etc.) exports with no texture and no signal to a downstream consumer
about *why* — indistinguishable from "the `--textures` directory just
didn't have this file."

### Implementation plan

1. `src/cmd_export.cpp`'s material-building path (`buildMaterialsAndPrimitives`
   or wherever `Texture` records currently get turned into glTF material
   inputs): thread `type` through to the constructed material's `extras`,
   alongside `additional_textures`/`texture_transform`'s existing pattern
   (`gltf.cpp:558`/`581`).
2. `src/gltf.hpp`/`gltf.cpp`: new field on whatever struct feeds
   `materialExtras` (mirror the existing `additionalTextureLayers`/
   `textureTransform` fields' shape) — a plain integer, `texture_type` or
   similar key, written unconditionally (even `type == 0`, so a consumer
   can distinguish "confirmed filename-based" from "field absent because
   this predates the change") or only when nonzero (cheaper, matches
   `additional_textures`' "only present when relevant" convention) — pick
   whichever matches this project's existing extras-emission convention
   more closely (check a couple of existing extras fields for precedent
   before deciding).

### Test plan

- `tests/test_gltf.cpp`: material extras round-trip for `texture_type`.
- `tests/test_cli.cpp` or `test_integration.cpp`: a real fixture with a
  known hardcoded texture slot (character models are the obvious source —
  `bloodelffemale`'s own textures likely include hardcoded skin/hair
  slots; confirm via `husk info`'s existing `type=` printout on that
  fixture before writing the assertion).

### Docs to update

`M2_COMPLETENESS.md`'s Materials & textures section — likely a new row
("Hardcoded/replaceable texture slot marker") rather than folding into the
existing "Texture references" row, since it's a genuinely distinct piece
of information (marks *why* a texture is absent, doesn't add a texture).
`README.md`'s Materials paragraph, if it currently implies every texture
slot either resolves or is silently missing.

---

## Item 6: Attachments / Events / Lights — real glTF nodes, not diagnostic-only

### Current state

All three are parsed (static fields: `bone`, `position`, plus `id`/
`identifier`/`data`/`type` per-struct) and visible only via `husk info` —
`M2_COMPLETENESS.md` calls this "node-possible, unclaimed": nothing about
glTF or Blender blocks a plain empty-transform child node per entry, it's
just not been built.

### Implementation plan

1. `src/gltf.hpp`/`gltf.cpp`: extend `Skeleton` (or wherever
   `EmitterAnchor`/`PhysicsBody`-style lists already live) with
   `Attachment`/`Event`/`Light` anchor lists — same shape as
   `EmitterAnchor` (`id`/`joint`/`position`), since all three are
   structurally identical to what ribbon/particle anchors already are:
   `bone`-relative position markers.
2. Decide: real glTF child **nodes** (parented under the relevant joint,
   zero-length/identity local transform, named descriptively) vs. more
   `extras` entries on the skin (like `ribbon_emitters`/`particle_emitters`
   currently are). `M2_COMPLETENESS.md`'s own "node-possible, unclaimed"
   ceiling value implies real nodes are the intended target, not more
   extras — that's a meaningfully different code path (new
   `tinygltf::Node` entries, not just JSON `extras` values) and a bigger
   change than items 1/2/5 above. Worth a quick plan-mode check before
   committing to nodes-vs-extras, per this project's own established
   practice for anything touching the glTF-schema surface.
3. If nodes: each needs a name (`attachment_<id>`/`event_<identifier>`/
   `light_<index>`, or similar), correct parenting under the bone's joint
   node (reuse whatever joint-node-index bookkeeping `writeGlbMulti`
   already does for `EmitterAnchor`'s `joint` field validation), and
   should **not** be added to `skin.joints` (same "never pollute the real
   joint list" invariant `DESIGN.md`/`src/gltf.hpp` already document for
   the multi-root synthesized parent node).
4. Lights specifically: the 5 animated fields (color/intensity/
   attenuation/visibility) stay out of scope for this item — that's a
   separate, larger animated-material-track-style problem (see Item 7's
   sibling scope), don't scope-creep this item into resolving those too.

### Test plan

Mirror `EmitterAnchor`'s existing test shape (`tests/test_gltf.cpp`:
present/absent/out-of-range-joint-throws/coexists-with-other-extras-or-
nodes) for each of the three, plus a real-fixture check
(`tests/test_integration.cpp`) confirming node count matches
`header.attachments.count`/`events.count`/`lights.count` exactly.

### Docs to update

`M2_COMPLETENESS.md`'s Interaction points & effects section — all three
rows move from `node-possible, unclaimed` to `native — 100%` (if real
nodes) or stay `extras` with an updated Consumption value (if the
plan-mode check above lands on extras instead). `DESIGN.md` Key design
decisions, if a new node-naming/parenting convention is introduced.

---

## Item 7: Animated material tint/fade — extras curve dump

### Current state

`m2::Color`'s doc comment (`src/m2.hpp:359`) already explains this
clearly: a constant-track value is surfaced; a genuinely-animated track is
detected (`colorAnimated`/`alphaAnimated` flags) but the actual keyframe
data is never resolved or exposed anywhere, not even diagnostically.
`cmd_export.cpp`'s `animatedTintOrFadeBatchCount` note only counts how
many batches hit this case, doesn't dump the curve.

### Implementation plan

1. `src/m2.cpp`: for a batch whose `Color`/`TextureWeight` reference is
   genuinely animated, resolve the full `M2Track<Vec3>`/`M2Track<float>`
   curve — reuse `resolveVec3TrackSequence`/`resolveFloatTrackSequence`,
   the same functions bone translation/scale tracks and `M2Particle`'s
   simulation-parameter curves already use. No new resolver needed, this
   is the same track shape.
2. **Export target**: core glTF has no `baseColorFactor` animation-channel
   target (per `M2_COMPLETENESS.md`'s own honest note) — this stays
   `extras`-only regardless of parse depth. Attach as material `extras`
   (a new key, e.g. `tint_animation`/`fade_animation`, holding the
   resolved keyframe arrays), same shape `additional_textures`/
   `texture_transform` already use for "real data, no native slot."

### Test plan

`tests/test_m2.cpp` (curve resolution — likely near-zero new test
surface if it's truly reusing existing resolvers verbatim), a real-fixture
case if `bloodelffemale`/an existing weapon fixture has a genuinely
animated tint/fade batch (check via `animatedTintOrFadeBatchCount`'s
existing stderr note on a known fixture before assuming one needs to be
found fresh).

### Docs to update

`M2_COMPLETENESS.md`'s "Animated material tint/fade" row: Parse depth
`deref` → `full`, Consumption `diagnostic` → `extras`.

---

## Item 8: `DETL` — per-light shadow-RT/diffuse-multiplier data (≥ 9.0.1.34365)

### Current state

`cmd_dump.cpp`'s `kFallback` table dumps `DETL` as an opaque raw blob,
citing a 6-byte discrepancy between wowdev.wiki's stated field list (sums
to 0x0c) and its own end-offset comment (`/*0x0a*/`) as the reason it isn't
parsed structurally. `M2_UNKNOWNS_EXPLORATION.md`'s investigation (now
closed, folded into `WIKI_FINDINGS.md` §11) resolved this completely
against all 1,043 real `DETL`-bearing files in the corpus:

- **Real per-record stride is 0x0c (12 bytes)** — the wiki's own field
  list, not its `/*0x0a*/` comment (which is simply wrong).
- **The whole chunk is zero-padded up to the next 16-byte alignment
  boundary** — real chunk size is `((12 × lights.count + 15) / 16) × 16`,
  not `12 × lights.count`. This part isn't on the wiki at all.
- One real record per light (`lights.count` from the header, no off-by-one
  once the alignment padding above is accounted for).
- Field values, decoded at the confirmed stride, are sane across all 1,386
  real records sampled: `flags` takes only two values (`0x0000`/`0x0008`),
  `scale` (half-float) is a constant 0.013885498046875, `diffuseColorMultiplier`
  (half-float) is a constant 1.0, `unk0`/`unk1` are always zero.

No ambiguity or real-file gap remains — this is purely "write the parser,"
same shape as `WFV3`'s own already-implemented short-variant handling.

### Implementation plan

1. `src/cmd_dump.cpp`: new `dumpDetl` (move `DETL` from `kFallback` to
   `kDocumented`), reading records at stride 12 for `min(lights.count,
   chunk.size / 12)` entries (defensive floor in case a foreign/corrupted
   file's declared `lights.count` doesn't match its own `DETL` chunk size —
   this project's usual "don't trust foreign data's own claims" discipline,
   even though 1,043/1,043 real files agree cleanly). Decode `scale`/
   `diffuseColorMultiplier` as half-floats — husk already has a half-float
   decoder from `M2Particle`'s `FBlock` work (`resolveFBlockVec3`/`Vec2`/
   friends, `src/m2.cpp`); reuse rather than re-derive.
2. No glTF translation — `DETL` is diagnostic-only (`dump-chunks`), same
   class as `WFV3`/`TEXL`, no core glTF slot for per-light shadow-matrix
   scale or diffuse multiplier data.
3. The 16-byte alignment padding at the end of the chunk (when present)
   should simply be ignored/unread — it's not additional record data
   (verified: reading the padding bytes as if they were a partial record
   would either decode nothing meaningful or run past the chunk, and no
   real file's `lights.count` implies a partial trailing record).

### Test plan

- `tests/test_dump.cpp`: a synthetic `DETL` chunk (a few records, at least
  one file with alignment padding present — e.g. 1 light → 16-byte chunk
  with 4 padding bytes, to exercise the padding-ignored path directly) plus
  a real-fixture case if a committed test fixture happens to carry a real
  `DETL` chunk (check first — none of this project's current character/
  weapon fixtures are player-housing doodads, the dominant `DETL`-bearing
  category found in the corpus scan, so a synthetic fixture built from real
  observed byte values is the likely path, same as `WFV3`'s own test
  fixture approach).

### Docs to update

- `cmd_dump.cpp`'s own `kFallback`-table comment (remove the `DETL` entry
  and its now-resolved note).
- `M2_COMPLETENESS.md` — likely a new row under Lighting, or extend
  whichever row already covers `M2Light` itself.
- `WIKI_FINDINGS.md`'s "Where these live in husk" table (§11's row,
  currently pointing at "not yet implemented — see `M2_GAPS_TODO.md`")
  once this ships.
