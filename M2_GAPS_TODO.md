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
2. **Item 4 — `PCOL`.** Blocked on finding a real file with this chunk at
   all (War Within 11.1.7+, player-housing furniture) — a corpus search
   found zero real files carrying it (0/130,576, see the item's own section
   below); it just waits for newer extraction data.

(Items 2 (`PFDC`), 3 (`EXP2`), 5 (`Texture.type` export), 6 (Attachments/
Events/Lights as real glTF nodes), 7 (animated material tint/fade extras
dump), and 8 (`DETL`) are all implemented, tested, and documented; their
sections have been removed from this file. See `M2_COMPLETENESS.md`,
`WIKI_FINDINGS.md` §11, `README.md`'s Materials paragraph, and
`src/cmd_dump.cpp`'s `dumpPfdc`/`dumpExp2`/`dumpDetl` for the permanent
record.)

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

**Checked**: a top-level-chunk-tag scan across the full real corpus
(`/media/luna/data/wow_export`, all 130,576 `.m2` files, same fast scan
`tools/find_m2_unknown_chunks.py` already established) found **zero** real
files carrying a `PCOL` chunk. This item stays parked exactly as scoped
above — waiting for newer extraction data, not implemented against a
"preliminary" wiki struct with no real bytes to ground it.

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
