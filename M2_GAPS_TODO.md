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

1. **Item 1 — `M2Sequence`'s missing fields.** Highest value-per-effort:
   pure struct-field additions to an already-fully-parsed record, no new
   chunk-walking, no new glTF construct needed (extras only) — and it's the
   one piece of this whole investigation that affects animation *feel*
   (blending, movement-speed sync), not just data completeness.
2. **Item 5 — `Texture.type` export.** Trivial (one field already parsed,
   just not threaded through to material extras), meaningfully closes a
   "why is this material's texture just missing" confusion for any
   downstream consumer.
3. **Item 2 — `PFDC`.** husk already has the full `.phys` parser
   (`src/phys.hpp`/`phys.cpp`) — this is "point it at a different chunk,"
   not new reverse-engineering.
5. **Item 3 — `EXP2`.** Needs one real-file check before implementing (see
   below) but the struct shape is simple.
6. **Item 7 — animated material tint/fade extras dump.** Lowest urgency —
   diagnostic value only, no glTF slot exists for the animated case anyway
   (`M2_COMPLETENESS.md` already logs this ceiling as `native-possible, not
   done`, meaning "worth doing eventually," not "blocks anything").
7. **Item 4 — `PCOL`.** Blocked on finding a real file with this chunk at
   all (War Within 11.1.7+, player-housing furniture) — do the corpus
   search first; if nothing turns up, this one just waits.
8. **Item 8 — `DETL`.** New (from `M2_UNKNOWNS_EXPLORATION.md`'s
   investigation, now closed): real byte layout fully confirmed against
   1,043 real files, zero ambiguity left. Similar effort/shape to Item 5 —
   one small struct, straightforward `dumpDetl`-style addition, no glTF
   translation needed (diagnostic-only, same class as `WFV3`).

---

## Item 1: `M2Sequence`'s unparsed fields (`movespeed`, `frequency`, `replay`, `blendTimeIn`/`blendTimeOut`, `bounds`, `variationNext`, `aliasNext`)

### Current state

`src/m2.hpp`'s `Sequence` struct (line ~323) has exactly four fields:
`id`, `variationIndex`, `duration`, `flags`. The wowdev.wiki struct
(`documentation/wowdev-wiki/md/M2.md`, "Animation sequences" section) has
eight more, every one of them a fixed-offset scalar with a fully
documented byte layout for the ≥ Wrath shape this project already targets
(`kMinVerifiedRecordStrideVersion`-style version gating already exists
elsewhere in this file for exactly this "the struct changed shape at a
known version" situation — reuse that pattern, don't invent a new one):

```
/*0x08*/  float movespeed;
/*0x0c*/  uint32_t flags;                 // already parsed
/*0x10*/  int16_t frequency;
/*0x12*/  uint16_t _padding;
/*0x14*/  M2Range replay;                 // { uint32_t minimum, maximum; }
/*0x1c*/  uint16_t blendTimeIn;
/*0x1e*/  uint16_t blendTimeOut;
          M2Bounds bounds;                // { CAaBox extent; float radius; } -- reuse husk's existing M2Bounds-shaped type if one exists, else add it
/*0x20*/  int16_t variationNext;
/*0x22*/  uint16_t aliasNext;
```

(Offsets above are for the ≥ Wrath layout, i.e. every version this project
already targets — see `M2.md`'s own `#if ≤ BC-Logo` branch for the older
`start_timestamp`/`end_timestamp` shape, out of scope per this project's
pre-Cata non-goal.)

### What's genuinely known vs. not

- `movespeed`, `frequency`, `replay`, `blendTimeIn`, `blendTimeOut`,
  `bounds`, `variationNext` — byte offsets and semantics both documented
  cleanly on the wiki, no ambiguity. Parse and expose these with full
  confidence.
- `aliasNext` — **now fully resolved**, not just parseable
  (`WIKI_FINDINGS.md` §12, superseding `TODO_correctness.md`'s former item
  4 and `M2_UNKNOWNS_EXPLORATION.md` target 6, both since removed/resolved).
  Two things were wrong in the original open question, both now fixed: (1)
  the wiki's literal `/*0x22*/` offset is pre-`M2Bounds`-correction — at
  the real 64-byte stride (§1), `aliasNext` is at **offset 0x3E** (right
  after `variationNext` at 0x3C), not 0x22; (2) reading at the corrected
  offset shows `aliasNext` is a **local array index into this same file's
  own `sequences` array** (`sequences[aliasNext]`), not an
  `AnimationData.dbc` id and not anything cross-file — confirmed on 157
  real alias sequences across 4 real character-model files, 100% valid
  in-range indices, chain-following (`flags & 0x40` → jump to
  `sequences[aliasNext]` → repeat) terminates cleanly with zero cycles in
  every case. **This unblocks a real feature, not just a data field**:
  `buildAnimations` (`cmd_export.cpp`) currently skips any sequence with
  `flags & 0x40` outright, citing the wiki's now-superseded "I have no
  clue" — it could instead resolve the alias chain to its terminal
  non-alias sequence and reuse *that* sequence's animation data (inline or
  external, whichever the terminal sequence itself uses), producing a real
  clip instead of nothing. Both the raw-field parse (trivial, same as the
  other seven fields) and the chain-resolution behavior change in
  `buildAnimations` belong in this item's implementation — the latter is a
  bigger, real behavioral addition (more real animation clips export than
  today), not just another struct field, so budget it as such rather than
  folding it into the "parse eight scalars" estimate above.

### Implementation plan

1. Extend `m2::Sequence` (`src/m2.hpp`) with the seven new fields above.
   `M2Bounds`/`M2Range`-shaped helper types: check whether an equivalent
   already exists (`CAaBox`-adjacent types are already used for the
   header's own `bounding_box`/`collision_box` — reuse rather than
   duplicate; `M2Range` is a plain `{min, max}` `uint32_t` pair, add if
   nothing matches).
2. `src/m2.cpp`'s sequence-parsing function: read the extra fields at
   their fixed offsets, bounds-checked the same way every other field in
   this parser already is (foreign-data discipline — nothing here changes
   that).
3. **Export path**: these become **per-sequence extras** on each
   animation clip, not core glTF animation properties — core glTF
   `animation` objects have no field for playback speed scaling, blend
   time, or replay count (same ceiling class as `M2_COMPLETENESS.md`'s
   other `extras`-only rows). Check `src/gltf.hpp`'s existing animation-
   clip construction (`buildAnimations` in `cmd_export.cpp` /
   whatever glTF `Animation` struct wraps it) for where a per-clip
   `extras` object would attach — if none exists yet, this is the first
   thing that needs one; if clip-level `extras` isn't natively supported
   by the JSON layer this project already writes glTF with,
   check whether `tinygltf::Animation` has an `extras` field before
   assuming a workaround is needed (it should — `tinygltf` mirrors the
   glTF 2.0 spec, and `extras` is universal on every top-level object).
4. `aliasNext`: expose the raw field, **and** implement chain resolution
   (`WIKI_FINDINGS.md` §12 — offset 0x3E at the real 64-byte stride, a
   local `sequences` array index, chain-walk while `flags & 0x40` until a
   non-alias record) so `buildAnimations` can produce a real clip for
   currently-skipped alias sequences by reusing the terminal sequence's own
   animation data. Since the terminal sequence may itself be inline or
   external (`.anim`), reuse whatever branch `buildAnimations` already uses
   to decide that for a normal sequence — an alias sequence's own clip
   should come out identical to building the terminal sequence's clip
   directly, just registered under the alias's own `id`/index too.

### Test plan

- `tests/test_m2.cpp`: extend the existing `Sequence`-parsing test(s) with
  the new fields — happy path (real or synthetic values at the right
  offsets), a bounds-check throw case if the chunk/blob is too short to
  contain them. New cases for chain resolution: a simple 2-hop alias, a
  multi-hop chain, and (since real data proves it's never seen but the
  code must still not hang on foreign/corrupted input) a synthetic
  self-referencing or cyclic `aliasNext` — should throw or otherwise fail
  loudly, not loop forever, matching this project's foreign-data
  discipline even though 0 real files exhibit this.
- `tests/test_gltf.cpp`: per-clip extras round-trip (present values, and
  confirm nothing breaks for the existing fixtures that don't exercise
  this path — regression, not just new coverage).
- `tests/test_integration.cpp`: real-fixture check — `bloodelffemale_hd`'s
  own sequences have real `movespeed`/`blendTimeIn`/`blendTimeOut` values;
  spot-check a couple of known ones (e.g. a locomotion sequence should have
  nonzero `movespeed`, a "Stand" sequence should have zero) the same way
  other real-data tests in this file check specific, named values rather
  than just "some value present." Also: `bloodelffemale_hd.skel` has 38
  real alias sequences (per `WIKI_FINDINGS.md` §12) that currently produce
  no clip at all — after this item, the exported animation count should
  grow by exactly that many (or fewer, if some terminal sequences are
  themselves unresolvable for an unrelated reason, e.g. a missing external
  `.anim` file — don't assume every alias necessarily gains a clip without
  checking).

### Docs to update

- `M2_COMPLETENESS.md`'s Animation section — new row, or extend the
  existing "Animation sequences + per-bone tracks" row's Note.
- `DESIGN.md` — if a new per-clip `extras` convention is introduced,
  document it as a Key design decision (same pattern every other extras
  shape got); also document the alias chain-resolution behavior itself as
  a Key design decision (real animation clips now come from
  `flags & 0x40` sequences, not just `flags & 0x20`/external ones).
- `TODO_correctness.md` #4 — already removed once `aliasNext`'s resolution
  mechanism was confirmed (`WIKI_FINDINGS.md` §12); no further doc-sync
  needed there.

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
