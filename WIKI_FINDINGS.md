---
aliases:
  - WIKI_FINDINGS
---
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

`AFSB`'s own internal byte layout was **not** reverse-engineered in this
first pass — husk detected its presence (`readChunks` + `findChunk(...,
"AFSB")`, `src/cmd_export.cpp`) and skipped that sequence rather than
guessing. A very shallow peek at one 135,024-byte `AFSB` payload showed a
small header (looked like a count) followed by a run of
monotonically-increasing 4-byte values ~33–34 apart — consistent with a
per-bone offset table into a packed keyframe region — but this was not
pursued far enough to write up as even a hypothesis-confidence structure
at the time. **See the follow-up below: this was cracked in a later
session.**

Correction to the "91 have `[('AFM2', 64), ...]`" claim above: a full
104-file re-survey found `AFM2`'s size actually varies (16, 32, 48, 64, 80,
160, 240, or 288 bytes across the plain `_hd` set; up to 1344 bytes for the
`_hd_sdr` variant) — always a multiple of 16, never a fixed 64. The
original investigation happened to sample files where it came out to 64;
the "small, near-zero stub, not real track data" conclusion still holds
(see the follow-up's explanation of what `AFM2`'s bytes actually are).

### Follow-up: `AFSB`'s byte layout, cracked

**Confidence: verified** — against the entire real 104-file
`bloodelffemale_hd_*.anim` corpus, cross-checked two independent ways
(husk's own tinygltf-based output check, and a completely separate
implementation: Blender's own glTF importer, run headlessly).

**The core insight: `AFSB` isn't a new format to reverse-engineer at
all — it's the exact same per-bone `M2Track` data `.skel`'s `SKB1` chunk
already describes, just stored in a different blob than the one husk was
looking in.** `src/m2.cpp`'s `trackSequenceInnerArrays` already reads,
for any bone and any sequence index, a `(timestamp_count, timestamp_offset,
value_count, value_offset)` tuple straight out of `SKB1` — this is the
exact mechanism `skel.hpp`'s own doc comment describes for *inline*
`.skel` sequences. What turned out to be wrong was the assumption (stated
in `src/m2.hpp`'s `Bone` doc comment, "every M2Track they point at is
expected to be genuinely empty") that this tuple is *always* zero for
`.skel`-sourced bones. Decoding it for real, non-inline (no `0x20` flag)
sequences of `bloodelffemale_hd.skel` shows **211 of 245 bones have real,
non-zero entries** — the tuple was never empty; husk had simply never
tried resolving it against anything other than `SKB1`'s own (very large,
23 MB) blob, where those particular byte offsets don't mean anything.

The fix is exactly what the tuple's `offset` field was pointing at all
along: **the owning sequence's own external `.anim` file's `AFSB`
payload**, used as `resolveVec3TrackSequence`/`resolveQuatTrackSequence`'s
existing `externalDataBlob` parameter — the identical mechanism already
used for `AFM2`-shaped external files, just fed a different blob. No new
parsing code was needed at all; `AFSB` support turned out to be a
one-`if`-branch change (see "Where these live in husk" below).

**How this was found**, briefly (full byte-level walk not reproduced
here — see git history's scratch analysis if ever needed):

1. A full 104-file chunk survey (this session) found `AFM2`'s payload size
   is always a multiple of 16 and often all-zero — never large enough to
   hold real keyframe data, confirming (with a full corpus rather than one
   file) the earlier "stub, not real content" conclusion.
2. Dumping several real `AFSB` payloads as raw `uint32` arrays showed a
   clean, unambiguous pattern in the cleanest samples (an `AFM2`-all-zero
   file): the payload's *first* bytes are a monotonically increasing run of
   millisecond values from `0` up to exactly the owning sequence's
   `duration` (e.g. 31 values, `0, 33, 66, 100, ..., 1000`, for a
   1000 ms/30 fps sequence) — real keyframe timestamps, not a bone-offset
   table as the original shallow peek guessed.
3. Cross-referencing `bloodelffemale_hd.skel`'s own `SKB1` bone records
   against `SKS1`'s sequence array (mapping each real `.anim` filename's
   `<animId>-<subId>` to its `SKS1` position) showed those "expected
   empty" per-sequence `(count, offset)` tuples are frequently *not* zero —
   and for a bone/sequence pair with a real non-zero tuple, the `offset`
   value **exactly matches** where that sequence's own `.anim` file's
   `AFSB` payload has a clean timestamp run starting at that exact byte
   position.
4. Decoding the value region immediately after each timestamp run (with
   its own byte length padded up to the next multiple of 16, confirmed by
   the *next* track's timestamp offset always starting exactly there,
   checked across multiple bones/sequences) as a raw 12-byte `C3Vector`
   (translation) or an 8-byte `M2CompQuat` (rotation, `src/m2.cpp`'s
   already-existing `readCompQuat` — the exact same decode function
   `.skel`'s own inline case already uses) produces real data: every
   decoded rotation quaternion comes out unit-length (checked to 4 decimal
   places across every sample), and every translation curve is smooth and
   finite across consecutive keyframes — not noise.
5. A full self-consistency sweep across all 54 non-`_sdr` `bloodelffemale_hd*.anim`
   files (every bone × every track × the file's own matching sequence)
   found **zero** problems: every timestamp run in-bounds and monotonic,
   every decoded value finite, across the entire corpus.
6. Implemented in `src/cmd_export.cpp`'s `buildAnimations` (see below) and
   verified three independent ways against the real
   `bloodelffemale_hd.m2`/`.skel` + all 104 real `.anim` files: husk itself
   reports **336 real animation clips** (up from ~0 external ones
   possible before); the Khronos `gltf_validator` reports zero new errors
   (the model's own pre-existing, unrelated `JOINTS_0` duplicate-value
   issue is identical with or without this change); and Blender's own
   independent glTF importer, run headlessly, reports **336 actions** —
   matching exactly, from a completely separate glTF implementation.

Prior art: no published byte layout for `AFSB` was found anywhere reachable
(wowdev.wiki's own summary confirms only the *semantic* split — `AFSA` for
attachment animation, `AFSB` for bone animation — not a byte structure; the
wiki itself blocks direct automated fetches, `WIKI_FINDINGS.md`'s own
"fetched ... via a local proxy" note already flagged this; no open-source
WoW tooling found any mention of `AFSB` either).

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

### Follow-up: the LOD/render-distance hypothesis is ruled out

**Confidence: verified** (LOD is not the selector) for the negative claim;
**inferred, plausible** for the positive one (character customization).

`TODO_correctness.md` #6 asked "which `.bone` file (of a model's several)
applies to which LOD/context." All 20 real `bloodelffemale_hd_00.bone`
through `_19.bone` files (plus their 20 `_sdr_00`–`_sdr_19` siblings — same
count, same container shape) were decoded and compared directly against
each other, not just individually:

- **The count itself doesn't fit LOD.** `bloodelffemale_hd.skel`'s `BFID`
  chunk holds exactly 20 FileDataIDs (`80 bytes / 4`), but the same model's
  `LDV1`/`SFID` data gives `lod_count: 7`. 20 doesn't divide or multiply
  cleanly into 7, and no other per-model count in this codebase (texture
  count 13, material count 12, geoset/skin-section counts) is 20 either —
  there's no LOD-shaped quantity for 20 slots to map onto 1:1.
- **The 20 files collapse into only 5 distinct bone-index sets, heavily
  duplicated** (e.g. the 33-bone set `{52,53,54,58,59,60,64-67,71,72,
  81-84,86-94,96,128,130-135}` is shared *verbatim* by 10 of the 20 files:
  `_01/_03/_05/_08/_09/_11/_13/_15/_18/_19`). A LOD ladder produces a
  handful of tiers with *decreasing* bone/vertex involvement as detail
  drops (matching `lod_count: 7`, not repeating the same set 10 times
  across 20 slots).
- **Where a bone is corrected in multiple files, the correction itself is
  a pure magnitude scale along one of exactly two fixed 3D directions**,
  not 20 independent hand-authored deltas. Checking bone 64's translation
  (`BOMT` row 3) across all 20 `_hd_*.bone` files: 14 of them share one
  direction (`tx:ty` ratio ≈ `1.394` in every single one, only the
  magnitude changes: `0.0001708 → 0.0003746 → 0.0005784 → 0.0006467 →
  0.0009188 → 0.0009871 → 0.0011921 → 0.0015325 → 0.0023489`, nine distinct
  magnitudes across those 14 slots, several repeated exactly), and the
  other 6 share a second, different fixed direction (pure `+tz`, magnitude
  `0.0010628 → 0.0018992 → 0.0024151`, three distinct magnitudes across
  those 6 slots). That's the signature of a small number of underlying
  *shape variants* scaled/reused across many selectable slots — e.g. a
  slider or a set of dropdown choices where several choices are texture/
  color-only and intentionally reuse the same bone-shape data — not a
  detail-reduction ladder, which would not produce exact-duplicate deltas
  reused non-adjacently across the file's own numbering.
- The specific slot→context mapping (which in-game customization choice
  picks `BFID[7]` versus `BFID[13]`) isn't recoverable from the `.bone`
  files or the `.skel`/`.m2` themselves — that lookup lives in client-side
  DB2 data (something like `ChrCustomizationBoneSet`/`ChrCustomizationElement`
  by name, going from memory, not verified against a real DB2 dump this
  session) that husk has no access to and, per `DESIGN.md`'s non-goals,
  never will (no CASC/DBC access, same reason geoset default-selection is
  still unresolved). **This part of #6 stays open** — but it's now known
  *what kind* of open question it is (a customization-choice lookup table
  husk can't reach), not an undetermined LOD question husk could plausibly
  answer with more file-reading alone.

Scratch analysis (chunk-offset dump of `bloodelffemale_hd.skel`'s `BFID`,
and a full 20-file `BIDA`/`BOMT` decode/compare) was done ad hoc for this
finding and isn't checked into the repo — the numbers above are the
receipts.

### Follow-up: weapon-type/armor-type is also ruled out — the corrected bones are facial

**Confidence: verified** (structural fact about the skeleton). A natural
second hypothesis for the 20-slot count is WoW's classic weapon-subclass
enum (`ITEM_SUBCLASS_WEAPON`), which also has exactly 20 values — but the
real bone hierarchy rules this out just as cleanly as LOD was ruled out
above. Parsing `bloodelffemale_hd.skel`'s own `SKB1` bone records (parent
index, `keyBoneId`, bind-pose pivot — the same struct `husk::m2::Bone`
already exposes) for every bone index any of the 20 `.bone` files
corrects:

- All 26 bones in the largest cluster (indices 52–54, 58–60, 64–67, 71–72,
  81–94, 96) share the exact same parent: bone 42, which is this
  skeleton's own Head bone (`keyBoneId=6`, pivot z≈1.749 — head height for
  this character). Every one of them has a pivot within a few centimeters
  of the head (z≈1.75–1.86).
- The second, smaller cluster (128, 130–135) is parented to bone 95, the
  Jaw (`keyBoneId=7`, itself a child of Head), pivot z≈1.71–1.74.
- For comparison, this same skeleton's real finger bones (`keyBoneId` 8–17,
  `IndexFingerR`/`L` etc.) live at index 102–116, pivot z≈1.0–1.06 — the
  hand, nowhere near any bone a `.bone` file actually corrects.

A weapon-grip or armor-fitting correction would need to touch wrist/hand
(or waist/shoulder) bones, not a ~30-bone cluster bolted onto the skull.
This is the anatomical signature of facial detail bones (brow/cheek/ear/
chin) instead — consistent with the customization-choice-slider read
above, and with Blood Elves specifically having received an extensive
facial-customization pass in Blizzard's "Character Customization 2.0"
work. Reinforces, rather than changes, the open/closed split above: still a
customization-choice lookup husk can't reach, now known to plausibly be a
*facial* one specifically, not a body-type/equipment one.

---

## 5. `M2` — `bounding_box` is not a tight fit around the bind-pose mesh, on any axis, in either real file checked

**Confidence: hypothesis** for *why* (evidence is real and reproducible;
the explanation is the most consistent one found, not confirmed against
an authoritative source). wowdev.wiki's M2 page documents `bounding_box`/
`bounding_sphere_radius` only as "used as a fast, rough check if the whole
model is in the view frustum" — it doesn't say what pose or range that box
is computed relative to. The natural assumption (and this project's own,
implicitly, until this session actually checked it) is "the bind-pose
render mesh's own tight AABB." Real data says otherwise.

**Evidence.** Computed the raw M2 vertex array's own min/max directly
(independent of husk's own parsing code — a from-scratch byte-level
Python read against `M2Header::vertices`' array descriptor) and compared
against the same file's `bounding_box` field, both in native M2 (Z-up)
space, no axis remap involved yet:

| File | Axis | Bind-pose vertex range | Header `bounding_box` |
|---|---|---|---|
| `bloodelffemale.m2` | x | -0.572 .. 0.175 | -1.141 .. 1.093 |
| `bloodelffemale.m2` | y | -0.329 .. 0.329 | -1.111 .. 1.054 |
| `bloodelffemale.m2` | z | -0.011 .. 1.992 | -0.096 .. 2.308 |
| `bloodelffemale_hd.m2` | x | -0.581 .. 0.188 | -8.990 .. 9.163 |
| `bloodelffemale_hd.m2` | y | -0.521 .. 0.521 | -9.655 .. 9.553 |
| `bloodelffemale_hd.m2` | z | -0.017 .. 2.084 | -0.921 .. 8.710 |

`bloodelffemale.m2`'s header box runs roughly 2x the bind-pose mesh's own
extent per axis; `bloodelffemale_hd.m2`'s runs roughly 4x on x/y and over
4x on z. Both real character models, both non-trivially larger than a
tight fit, both larger by an amount that scales with the *character's own
proportions* rather than a fixed padding constant (x/y width tracks
roughly the same ratio in both files despite `_hd`'s otherwise-similar
bind pose). The pattern is consistent with `bounding_box` accounting for
the model's full *animated* range — arm/weapon swings, spread limbs —
not just its rest pose, though this hasn't been confirmed against an
authoritative source (no wowdev.wiki text says so, and this session
didn't attempt to trace it against a specific animation's own keyframe
extremes to prove it directly).

**What does hold, exactly, on both files**: the bind-pose mesh's own AABB
is fully *contained* inside `bounding_box`, per axis, with no exceptions
found. That's the real, tolerance-free invariant `tests/test_conformance.cpp`
now checks (VERIFICATION_IDEAS.md case 3) — containment, not a tight-fit
equality this session initially assumed and then had to correct once the
numbers above came back.

---

## Where these live in husk

| Finding | Code | Tests |
|---|---|---|
| §1 `M2Sequence` = 0x40 bytes | `src/m2.hpp`/`m2.cpp` (`Sequence`, `parseSequences`) | `tests/test_m2.cpp` |
| §2 `AFSB` real resolution | `src/cmd_export.cpp` (`buildAnimations`'s external-file branch), `src/m2.cpp` (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`/`trackSequenceInnerArrays`, unchanged — just fed the `AFSB` payload as `externalDataBlob`) | `tests/test_cli.cpp`, `tests/test_integration.cpp` (real 336-clip corpus check) |
| §3 `.skel` `SKS1` indexing + own `AFID` | `src/skel.hpp`/`skel.cpp` (`parseSequences`, `boneTrackBlob`, `findAnimFileIds`) | `tests/test_skel.cpp`, `tests/test_cli.cpp` |
| §4 `.bone` format | `src/bone.hpp`/`bone.cpp` | `tests/test_bone.cpp` |
| §5 `bounding_box` containment, not tight fit | `src/cmd_export.cpp` (unaffected — no code depends on `bounding_box` being tight), `tests/test_conformance.cpp` (`transformedM2BoundingBox`) | `tests/test_conformance.cpp` |
