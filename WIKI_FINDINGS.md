# wowdev.wiki findings

Things husk's development turned up that go beyond, or correct, what
[wowdev.wiki](https://wowdev.wiki) currently documents (pages fetched
2026-07-24/25 via a local proxy — see `src/skel.hpp`'s doc comment for
dates). Written as if they were wiki edits — "current text" / "proposed
addition" / "evidence" — because that's the clearest way to show *exactly*
what's new versus what was already known. Nobody's submitting these; they're
just a record of what got reverse-engineered here, with the receipts, kept
next to the code that depends on them.

Confidence is called out per finding, same convention the README uses:
**verified** (checked against real game files, numbers included),
**inferred** (structurally justified but not cross-checked against a second
independent source), or **hypothesis** (plausible, not confirmed).

---

## 1. `M2` — `M2Sequence` is 0x40 (64) bytes, not the ~0x24–0x36 a literal reading of the struct listing implies

**Confidence: verified**, against `bloodelffemale.m2`'s real 339 sequences.

### Current text (M2#Animation_sequences)

The struct listing shows `id`, `variationIndex`, `duration`, `moveSpeed`,
`flags`, `frequency`, `padding`, `replay`, `blendTimeIn`/`blendTimeOut` (or
`blendTime`, version-dependent), then:

```
M2Bounds bounds;
/*0x20*/ int16_t variationNext;
/*0x22*/ uint16_t aliasNext;
```

`M2Bounds bounds;` has **no offset comment** — every other field on the page
does. Immediately after it, `variationNext` is annotated `/*0x20*/`. Read at
a glance, this looks like a documentation leftover (an offset comment the
editor forgot to update after some earlier restructuring), and `bounds`
looks like it might be vestigial or wrong. Taking the annotated offsets at
face value gives a struct that ends around 0x24, or — if `M2Bounds` is
counted at its 28-byte size but assumed to land right before
`variationNext` — a total nowhere near a clean value.

### Proposed addition

> **Note:** `M2Bounds bounds` is a real, live 28-byte field (`CAaBox`
> min/max + `float radius`, the same shape as the header's own
> `bounding_box` + `bounding_sphere_radius` pair) — not a stale annotation.
> Total record size is **0x40 (64) bytes**. The `/*0x20*/` comment on
> `variationNext` is simply wrong/unrenumbered for versions that carry
> `bounds`; treat the field list order as authoritative over that one
> inline offset comment.

### Evidence

Decoded all 339 real `M2Sequence` records from `bloodelffemale.m2` in
Python at every plausible stride. A 36-byte stride (the "ignore `bounds`,
trust `/*0x20*/`" reading) decodes `id`/`variationIndex` into nonsense —
`variationIndex` in the tens of thousands, multi-gigasecond `duration`
values — for roughly half the records. A 64-byte stride decodes all 339
cleanly: small, sane `id`s, `variationIndex` values in the single digits,
millisecond-scale durations. Husk's `m2::Sequence`/`parseSequences`
(`src/m2.cpp`) uses the 64-byte stride; `tests/test_m2.cpp` transcribes the
offset table independently (not copy-pasted from `m2.cpp`) as its own
cross-check.

---

## 2. `M2/.anim` — the page has no content; here's what real Legion+ `.skel`-linked character files' `.anim` files actually contain

**Confidence: verified** for the chunk shapes and their consequences;
**hypothesis** for what's actually *inside* `AFSB` (not attempted — see the
end of this section).

### Current text

The dedicated `M2/.anim` wiki page returns "There is currently no text in
this page." The only description of the mechanism at all lives in the main
`M2` page's `AFID`/global-flags prose:

> "these files are just a blob of data which may as well be in the main
> model file, that is pointed to by the first array_ref layer"

> `flag_unk_0x200000` — "apparently: use 24500 upgraded model format:
> chunked .anim files"

That's the entire spec: *if* a model uses chunked `.anim` files, *an* `AFM2`
chunk exists, and its payload is "identical to the flat format." Nothing
says whether other chunks can appear alongside it, or what happens once a
model's bones move to a `.skel` file.

### Proposed addition

> For a model whose bones live in an external `.skel` file (see `M2/.skel`)
> rather than inline, a chunked `.anim` file's real per-bone keyframe data
> does **not** live in `AFM2` — it lives in an `AFSB` chunk instead, a
> format with no documented byte layout as of this writing. Two shapes were
> observed in real Legion+ character `.anim` files:
>
> - `AFSB` alone (no `AFM2` at all) — 13 of 104 sample files.
> - A small (64 bytes, every sample observed) `AFM2` chunk *followed by* a
>   much larger `AFSB` chunk — 91 of 104 sample files. The `AFM2` payload
>   in this shape is **not** real flat-format keyframe data — every field
>   in it reads as zero or near-zero in the samples checked, and treating
>   it as the flat-format content (per the "identical to the flat format"
>   description, which evidently only holds for *inline-boned* models) and
>   resolving a real bone track against it throws a bounds error
>   ("claims more keyframes than this blob holds") rather than producing
>   plausible-looking data. It's best understood as a small stub/compat
>   chunk, not a second copy of the real content.
>
> Practically: for a `.skel`-linked model, *any* chunked `.anim` file
> carrying an `AFSB` chunk should be treated as unparseable by tools that
> don't implement `AFSB`, regardless of whether `AFM2` is also present.

### Evidence

`bloodelffemale_hd.m2` (a real Legion+ character file with 0 inline bones,
an `SKID`→`bloodelffemale_hd.skel` skeleton, and `global_flags &
0x200000` set) plus its own 104 real `.anim` files. Every one of them was
chunk-dumped (tag + size, walking the file) in Python: 91 have the shape
`[('AFM2', 64), ('AFSB', N)]`, 13 have `[('AFSB', N)]` alone, **zero** have
`AFM2` alone. Contrast: `bloodelffemale.m2` (inline bones, no `.skel`, same
`global_flags` bit set) has 50 real `.anim` files, and every single one is
`[('AFM2', N)]` with a real-sized payload (tens of KB, not 64 bytes) — no
`AFSB` at all, and all 50 resolve correctly through husk's existing
(spec-only, AFM2-as-flat-format) implementation with zero errors. So the
`AFM2`-is-sufficient description is confirmed correct for inline-boned
models and confirmed *insufficient* for `.skel`-sourced ones, in the same
real file corpus.

`AFSB`'s own internal byte layout was **not** reverse-engineered — husk
detects its presence (`readChunks` + `findChunk(..., "AFSB")`,
`src/cmd_export.cpp`) and skips that sequence rather than guessing. A very
shallow peek at one 135,024-byte `AFSB` payload showed a small header
(looked like a count) followed by a run of monotonically-increasing
4-byte values ~33–34 apart — consistent with a per-bone offset table into a
packed keyframe region — but this was not pursued far enough to write up
as even a hypothesis-confidence structure.

---

## 3. `M2/.skel` — `SKS1`'s sequence-array position is the same per-sequence index `M2Track`'s outer arrays use, confirmed across the `.skel`/`SKB1` boundary

**Confidence: verified**, against `bloodelffemale_hd.skel`.

### Current text

`M2/.skel`'s `SKS1` section documents the struct shape correctly (already
matches husk's implementation exactly):

```
struct {
  M2Array<M2Loop> global_loops;
  M2Array<M2Sequence> sequences;
  M2Array<uint16_t> sequence_lookups;
  uint8_t _0x18[8];
} skeleton_sequence_header;
```

What the page doesn't say anywhere: whether a bone's `M2Track`'s outer
per-sequence array (in `SKB1`) is indexed by this `sequences` array's
*position*, the same convention the main `M2` page documents for inline
models ("[the outer array is] indexed by animation" — referring there to
the M2's own `sequences` array). It's a reasonable assumption by analogy,
but `.skel` moves sequences into a completely different chunk in a
completely different file from the bones that reference them, so it isn't
automatically obvious the same positional indexing convention still holds
across that split.

### Proposed addition

> A `.skel`'s `SKB1` bone `M2Track` outer-array indexing uses the *same*
> convention as an inline M2's: index *i* corresponds to the *i*-th record
> in this same `.skel` file's own `SKS1.sequences` array (not the owning
> M2's `sequences`, if it even has one — many `.skel`-linked models have
> zero inline sequences).

### Evidence

`bloodelffemale_hd.skel`'s `SKS1` has exactly 396 sequences. Every `SKB1`
bone whose translation-track outer array had a nonzero count was checked:
every single one reported outer-array `count = 396` — an exact match, not a
coincidence of a round number. Separately, one specific sequence
(`SKS1` index 172, `M2Sequence.flags` with both `0x20` and `0x40` set) was
picked because its flags mark it as carrying real inline data; probing that
exact index across all 245 bones' translation-track inner arrays found 112
bones with real, nonzero keyframe counts (61, 1, 5, 61, ... — plausible
per-bone animation density, not garbage), while an index picked from a
sequence with *neither* flag bit set (an external-`.anim` sequence) read
`(count=0, offset=0)` for the same bone — the empty/no-inline-data case,
exactly as expected. Both checks land on the same conclusion from
independent angles: the outer array really is indexed by `SKS1` position.

Also confirmed as a quieter finding in the same investigation: a `.skel`'s
own `AFID` chunk is a **separate table** from the owning M2's `AFID` — the
same `(animId, subAnimId)` pair resolves to a completely different
FileDataID in each (e.g. `(60, 0)` → `469818` in `bloodelffemale.m2`'s own
`AFID`, but → `1100263` in `bloodelffemale_hd.skel`'s). Not surprising once
stated, but nothing on the wiki says it explicitly, and getting it backwards
(looking up a `.skel`-sourced sequence's file in the *M2's* `AFID` table)
would silently resolve to the wrong file for any FileDataID that happens to
also be present in both tables.

---

## 4. `.bone` (referenced from `M2`/`M2/.skel`'s `BFID` chunk) — complete file format, undocumented anywhere on the wiki

**Confidence: verified** for the container shape and field sizes/roles;
**inferred** for the semantic label "correction matrix" (structurally very
strong evidence, not a documented name).

### Current text

The wiki documents `BFID` itself (on both `M2` and `M2/.skel`) as nothing
more than a `FileDataID` array, one entry per `.bone` file a model/skeleton
references:

> "Same structure and semantics as in M2.BFID."

There is no wiki page for `.bone`'s own content at all — not empty like
`M2/.anim`, just entirely absent. Nothing describes the file's magic (it
has none), its container shape, or what any of its data means.

### Proposed addition

> **`.bone` file format** (reverse engineered from real Legion+ character
> files, no confirmed name for the format — "bone" chosen for the
> extension only):
>
> ```
> struct {
>   uint32_t version;        // 1 in every real file sampled
>   // followed immediately by the same generic chunked container
>   // M2/.skel use (4-byte tag, uint32 LE size, payload, tags NOT
>   // byte-reversed) -- exactly two chunks observed, always in this order:
> } bone_header;
>
> // BIDA chunk payload: flat array, no M2Array<T> descriptor of its own --
> // count = chunk_size / 2.
> uint16_t bone_indices[];   // indices into the owning model's `bones` array
>
> // BOMT chunk payload: flat array, same convention -- count = chunk_size / 64.
> // Always the same entry count as BIDA, entries in lockstep (BOMT[i]
> // corresponds to BIDA[i]).
> float correction_matrix[16][BOMT_count]; // row-major 4x4, per bone_indices[i]
> ```
>
> Every sampled `BOMT` entry's last column reads exactly `(0, 0, 0, 1)`
> (standard affine-matrix convention), and the upper-left 3×3 stays at or
> very near identity, with small (millimeter-to-low-centimeter scale)
> values in row 3 (the translation row) and occasionally small off-diagonal
> terms elsewhere. This is the signature of a small **corrective delta
> transform** applied to each listed bone — not a full replacement
> bind-pose matrix (those would show real rotation/scale, not
> near-identity) and not arbitrary/unstructured data. What triggers which
> of a model's several `.bone` files (its `BFID` array typically lists
> ~20) to apply — LOD level, render distance, something else — is **not**
> determined by this investigation.

### Evidence

Six real `bloodelffemale_hd_*.bone` files (`_00.bone` through `_05.bone`)
were byte-dumped and cross-checked: leading `uint32` is `1` in all six;
`BIDA` chunk count (22–33 across the six files) always exactly equals
`BOMT` chunk count; every `BOMT` entry's bytes decode as 16 finite
little-endian floats forming a plausible 4×4 matrix (not garbage/non-finite
values); the last-column-is-`(0,0,0,1)` and near-identity-upper-left-3×3
pattern held for *every* entry checked across all six files, including
deliberately checking the *last* entry of each file (not just the first, to
rule out "only the first record happens to look clean"). Implemented and
tested in `src/bone.hpp`/`src/bone.cpp` (`husk::bone::parse`), exposed via
`husk dump-chunks <file.bone>`; `tests/test_bone.cpp` has the synthetic
fixtures.

---

## Where these live in husk

| Finding | Code | Tests |
|---|---|---|
| §1 `M2Sequence` = 0x40 bytes | `src/m2.hpp`/`m2.cpp` (`Sequence`, `parseSequences`) | `tests/test_m2.cpp` |
| §2 `AFSB` detection/skip | `src/cmd_export.cpp` (`buildAnimations`'s external-file branch) | `tests/test_cli.cpp` |
| §3 `.skel` `SKS1` indexing + own `AFID` | `src/skel.hpp`/`skel.cpp` (`parseSequences`, `boneTrackBlob`, `findAnimFileIds`) | `tests/test_skel.cpp`, `tests/test_cli.cpp` |
| §4 `.bone` format | `src/bone.hpp`/`bone.cpp` | `tests/test_bone.cpp` |
