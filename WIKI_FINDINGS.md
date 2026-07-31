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

**Correction to point 6, above**: "husk itself reports 336 real animation
clips" verified the `AFSB`/`AFM2` *decode* logic — that count came from a
separate script reading all 104 real `.anim` files directly, not from
`husk export --anim`'s own file-resolution actually finding them. That
resolution had its own, separate gap: `--anim` only ever looked for
`<FileDataID>.anim`, but real `wow.export`-style extractions (this fixture
set included) name external `.anim` files
`<model-basename><animId>-<subId>.anim` instead — of the 336 real clips,
only the 335 inline + 2 global-sequence-driven ones were ever reachable
through husk's actual CLI before this was fixed (`findAnimFileByBasename`,
`cmd_export.cpp`); none of the genuinely-external clips were, including the
2 present in this repo's own pruned fixture set
(`bloodelffemale_hd0069-00.anim`/`-01.anim`). `tests/test_integration.cpp`'s
`AFSB`-follow-up case now asserts those two clips by exact name
(`anim_69_0`/`anim_69_1`) to prove the CLI path itself resolves them, not
just that a from-scratch decode of the raw bytes works.

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
now checks — containment, not a tight-fit equality this session initially
assumed and then had to correct once the numbers above came back.

---

## 6. `M2` — `M2Particle`'s Cata+ byte offsets, and `FBlock` timestamps being `uint16_t` (not the `uint32_t` a real `M2Track` uses), both confirmed against real weapon particle data

**Confidence: confirmed**, both against a genuine real-file corpus (Luna's
own weapon-model extraction, `test_data/item/objectcomponents/weapon/`, a
4112-file scan that found 1270 files with real ribbon/particle data — the
first time this project has had any) and, for the offset table itself,
independently cross-checked against wowdev.wiki's own stated total-record-
size claim.

**`M2Particle`'s post-`childEmittersModelFilename` offsets aren't given as
explicit hex on the wiki** — only sequential struct-declaration order,
version-conditional branches included (late-BC `blendingType`/`emitterType`
width, Cata's `multiTexScale` replacing `particleType`/`headOrTail`, Wrath's
`lifespanVariation`/`emissionRateVariation`/`baseSpin*`/`spinSpeed*` and its
`FBlock`-based color/alpha/scale/UV curves replacing the pre-Wrath fixed
arrays). Hand-deriving the Cata+ shape field-by-field and summing it landed
on exactly 0x1DC (476) bytes for the "default" `M2ParticleOld`, then exactly
0x1EC (492) once the Cata+ `multiTexScrollMid`/`multiTexScrollRange`
wrapper is added — matching the wiki's own independent claim ("if 0x200 is
set or if version is bigger than 271, length of `M2ParticleOld` is 492")
precisely, without that total being fudged to fit. Every real weapon
fixture sampled is version 272 or 274 (Cataclysm+), so the "version bigger
than 271" branch always applies uniformly — no per-record length variation
to handle within the verified range.

**Real-data sanity checks, not just size arithmetic**, against
`mace_2h_bolvar_d_01.m2` (64 real particle emitters):

- `particleId` is `0xFFFFFFFF` (-1) for every one of the 64 entries,
  matching the wiki's "Always (as I have seen): -1" note exactly.
- `boneId` runs in small, mostly-sequential integers (12, 13, 14, ...) —
  consistent with a run of glow-point attachment bones down the mace head,
  not garbage.
- The `MultiTexture` flag bit (`0x10000000`) is set if and only if that
  entry's `multiTexScale` decodes to a non-zero value — an independent
  cross-check between two unrelated fields (a flags bitmask and a
  `fixed_point<int8_t,2,5>` pair) that only lines up if both offsets are
  correct.
- Decoded `colorTrack` keyframes (the Wrath+ `FBlock<C3Vector>`) form a
  genuine fire/ember gradient: `(255, 119, 0) → (151, 11, 11)` for one
  emitter, `(245, 238, 217) → (255, 158, 2) → (180, 75, 0) → (100, 16, 0) →
  (25, 12, 5) → (0, 0, 0)` (white-hot to ember to black) for another.
- Decoded `alphaTrack`/`scaleTrack` keyframes are clean fade-in/fade-out
  envelopes and smoothly-growing scale curves respectively — not the
  NaN/garbage a wrong offset produces (an early hand-written verification
  script *did* produce exactly that kind of garbage once, from a real bug:
  see below).

**`FBlock` timestamps are `uint16_t`, confirmed by both the wiki text and
real bytes.** The wiki's own "Fake-AnimationBlock" section says "the
timestamps are shorts" — read literally at first and then re-verified
directly: decoding a real `colorTrack`'s timestamp array as `uint16_t`
produces a clean monotonic run from 0 up to exactly `0x7FFF` (32767);
decoding the same bytes as `uint32_t` produces enormous, non-monotonic
garbage. The `0..0x7FFF` range strongly suggests a normalized fraction of
the particle's own lifetime (0 = spawn, 0x7FFF = death) rather than an
absolute millisecond count — consistent with these curves having no
`M2Sequence`/global-sequence to be absolute *against* in the first place
(the wiki: "they're unable to change between different animations, so they
directly point to the data") — but that specific interpretation is a
hypothesis, not confirmed against an authoritative source, so husk exposes
the raw `uint16_t` value rather than rescaling it (see
`m2::FBlockMeta`'s doc comment).

**A real bug this verification process caught, not just confirmed against:**
an early ad hoc Python script decoding the *real* per-sequence `M2Track`
curves (e.g. a ribbon's `alphaTrack`) read the outer `M2Array<M2Array<T>>`
descriptor's bytes directly as if they were already the final keyframe
values — skipping the inner-array indirection `trackSequenceInnerArrays`
(the actual, already-existing, already-tested resolution mechanism) performs
correctly. That shortcut produced a plausible-looking but wrong alpha value
(a raw `int16` of `1`, i.e. ~0.00003 — implausibly near-invisible for a
supposedly-visible ribbon effect); the real resolver, on the same bytes,
correctly resolves to `0.8`. Caught by not trusting a quick script's output
over the codebase's own established, tested decode path — the same
discipline this file's other findings were built on.

---

## 7. `M2/.skin` — multi-texture-layer arithmetic confirmed exact against real data; `textureCoordCombos` found real but not matching its documented value range

`cmd_export.cpp`'s handling of a `.skin` batch's `textureCount > 1` case
(`TODO_correctness.md`'s former #3) was implemented straight from wowdev.wiki
prose — "if the textureCount is e.g. 3 and the texunit's uv anim lookup is
2, then the 3 uv animation lookups are 2, 3, and 4" — but had never been
cross-checked against a real multi-layer file. Luna's full extraction of
every currently-accessible game file (`/media/luna/data/wow_export`, ~1.9M
files total) finally made that possible.

**Finding the real files.** A from-scratch Python scan of every `.skin`
file in the extraction (~287k files, reading just the batch array header +
records, no husk code involved) for a batch with `textureCount > 1` found
this to be *extremely common* — 226,294 of 287,005 files (~79%) have at
least one such batch. A separate full scan of every `.m2` file (~130,576)
for a **nonzero `textureCoordCombos` array** — the other half of the same
arithmetic, documented as "still present but unused in Cataclysm" — found
the opposite: only **3** files, all Warlords of Draenor doodad/creature
models (`6ih_ironhorde_siegeweapon03.m2`, two `felsiege0{3,4}` creature
files), all with `textureCoordCombos.count == 2`.

**`textureComboIndex + layer`: confirmed exact.** Picked the smallest, cleanest
real hit — `pennant_guild_alliance_a_01.m2` (`world/
replaceabletextureprops/guild/`), a guild-pennant doodad with a single
batch at `textureCount=6`. A from-scratch independent Python parser (reading
the M2's raw `textureCombos` array and the `.skin`'s raw batch record
directly, sharing no code with husk) resolved the batch's 6 layers to
FileDataIDs `457665, 0, 458521, 0, 0, 0` (the `0`s are legitimate —
those texture slots are client-side "replaceable" types 15/16/17/18, not
real files, confirmed via the model's own `TXID` chunk). husk's actual
`husk export` output matches exactly: base material `..._tex2_fdid457665`,
`additional_textures = [{0}, {458521}, {0}, {0}, {0}]`. Byte-for-byte match.

**`textureCoordComboIndex + layer`: confirmed safe, semantics of the table
itself now an open question.** Hand-verified `6ih_ironhorde_siegeweapon03.m2`
the same independent way: its real `textureCoordCombos` array holds
`[33, 34]` — not the wiki's documented `-1 (0xFFFF)/0/1` range at all.
Every batch's `textureCoordComboIndex` is 0, so every batch resolves to 33
(layer 0) / 34 (layer 1), never `1` — `cmd_export.cpp`'s `mapping == 1`
special case therefore never fires, and the real export correctly falls
back to UV set 0 everywhere, confirmed directly in the exported `.glb`'s
material JSON (`baseColorTexCoord`/`additional_textures[].tex_coord` all
absent/0). So: no bug, the existing safe-fallback code handles this
correctly — but the wiki's stated value range for this table is now known
to be an oversimplification, at least for these 3 files. Best read as
genuinely-present-but-vestigial data (consistent with the wiki's own
"present but unused" phrasing) rather than a still-meaningful selector;
not confirmed against an authoritative source, since none exists for this
field beyond that same wiki prose.

**Verification discipline.** Both cross-checks used a parser written fresh
for this investigation (raw `struct.unpack` over the file bytes), not
`cmd_export.cpp`'s own resolution code or even husk's own `m2`/`skin`
library functions — a genuinely independent computation, then compared
against `husk export`'s real output. The permanent regression test
(`tests/test_integration.cpp`'s `checkMultiTextureLayerArithmetic`) instead
cross-checks against husk's own already-unit-tested `m2::parseHeader`/
`m2::extractBlob`/`m2::parseUint16Array`/`skin::parseHeader`/
`skin::parseBatches` (not a third independent parser, since a test that
ships in the repo should reuse trusted, tested primitives) — still a real
regression guard against `cmd_export.cpp`'s own arithmetic drifting from
those primitives, and deliberately parameterized so it stays meaningful if
`HUSK_TEST_MULTITEX_*`/`HUSK_TEST_COORDCOMBO_*` ever point at a different
real file.

---

## 8. `M2` — `WFV3` has a real, undocumented 64-byte short variant, missing exactly the trailing `unk1`-`unk4` floats

**Confidence: confirmed**, against all 9 real files in a 130k-file corpus
sweep (`/media/luna/data/wow_export`) that carry a
`WFV3` chunk at all: 1 Shadowlands "Maw" zone waterfall doodad
(`world/expansion08/doodads/maw/9maw_torghast_clouds_01.m2`) and 8
Nazjatar zone waterfall/water-effect doodads
(`world/expansion07/doodads/nazjatar/8nzj_water*`/`8nzj_titan_water_bubble_01.m2`).

**Current wiki text** (`documentation/wowdev-wiki/wikitext/M2.wiki`,
`===WFV3===`) gives one fixed-size `WaterFallDataV3` struct, unconditionally
80 bytes (20 `float`/`uint16_t`-pair fields through `unk4` at the very end),
with no mention of a shorter or version-conditional variant.

**Evidence.** husk's `dumpWfv3` (`src/cmd_dump.cpp`) originally read every
field at its documented fixed offset, including `unk1`-`unk4` at
`0x40`-`0x4C`, unconditionally. Run against real files, every one of the 9
above threw the same shape of error: `"unk1": husk: dump-chunks failed:
chunk field at offset 64 needs 4 bytes but the chunk is only 64 bytes"` --
not a corrupted read, a genuinely and consistently 64-byte chunk (`c.size ==
0x40`) across all 9, every time. All fields *before* `unk1` (`bumpScale`
through `values4_y`, ending at byte offset 0x3C+4=0x40) decode cleanly on
every file; the chunk simply ends exactly where the last of those fields
does, 16 bytes (4 floats) short of the wiki's documented 80.

**Proposed addition to the wiki's `WFV3` section**: a
`{{Template:SectionBox/VersionRange}}` note (matching the page's own
convention for `WFV2`'s "unknown" placeholder just above it) that a 64-byte
variant exists, omitting `unk1`-`unk4`, seen on Battle for Azeroth
(Nazjatar, 8.2) and Shadowlands (the Maw, 9.0) zone water-effect doodads --
not confirmed which build first introduced the 80-byte fields, only that
both shapes are real and the shorter one isn't a version husk's corpus
happens to lack coverage for elsewhere (all 9 hit files are the *entire*
set of `WFV3`-bearing files in this ~130k-file corpus, not a sample).

**Fix**: `dumpWfv3` now reads `unk1`-`unk4` conditionally on `c.size >=
0x50`, emitting `null` for the shorter variant instead of throwing -- same
"genuinely absent, not a parse failure" treatment `dumpTextureWeights`'s
optional `weight`/`alpha` fields already use elsewhere in the same file.

---

## 9. `.phys` — wiki's struct listing verified correct against 103 real files, with one real transcription bug found and fixed

**Confidence: verified**, against 103 real `.phys` files (7 in-repo
fixtures under `test_data/item/objectcomponents/weapon/`, plus 96 real
corpus files — world doodads, item components, creatures, spell-effect
arena flags — sampled from `/media/luna/data/wow_export`, paths recorded
in `phys_files_for_exploration.txt`). Unlike every other sidecar husk has
investigated so far, `.phys` is not undocumented —
`documentation/wowdev-wiki/md/PHYS.md` (wiki_revision 30458) already gives
byte offsets for nearly every field, so this entry verifies/extends an
existing page rather than reverse-engineering one from nothing. Now fully
implemented (`src/phys.hpp`/`phys.cpp`, `husk export --phys`, `husk
dump-chunks <file.phys>` — see "Where these live in husk" below and
`DESIGN.md`'s Key design decisions for the anchor/dump-chunks split); the
verified-vs-unverified-per-chunk-type coverage table this investigation
produced now lives in `src/phys.hpp`'s own doc comment.

### Current text (PHYS#PLYT)

The `PLYT` chunk's self-describing `header[count]` struct listing goes:

```
/*0x30*/  uint64_t RUNTIME_30_ptr_data_3;  // = &data[i].unk_3
/*0x38*/  float unk_38[6];                 // not sure if floats: has e-08 values
} header[count];
```

`unk_38[6]` (6 floats, spanning 0x38–0x50) genuinely is the struct's last
field — but it's easy to read the whole struct as ending at 0x38 (the
last field before it, and the offset most transcriptions would naturally
stop at) rather than continuing 24 more bytes to 0x50. The exact same
shape of misreading as §1 (`M2Sequence`, this page, 0x40 not ~0x24) — a
struct listing that's textually complete but easy to under-count.

### Proposed addition

> `PLYT`'s `header[]` array entries are **0x50 (80) bytes each**, not
> 0x38 — the true stride includes the trailing `unk_38[6]` float tail.
> Decoding at 0x38 produces a plausible-looking first entry (chunk-base
> offset is the same either way) and garbage for every entry after it.

### Evidence

`world/expansion07/doodads/8xp_heartofazeroth_prop_floatychain.phys`'s
`PLYT` chunk (`count=4`, 1500 bytes). At stride 0x38: `header[0]` decodes
sanely (`vertexCount=8 count_10=6 nodeCount=24`, matching the wiki's own
"mostly 8"/"mostly 6"/"mostly 24" comments exactly), but `header[1]`
decodes to `vertexCount=2994733056 count_10=1048605387 nodeCount=6` —
garbage, and the resulting expected-data-size calculation overflows to
~54 billion bytes against a 1500-byte chunk. At stride 0x50: all 4 header
entries decode identically clean, and the variable-length data region that
follows consumes **exactly** the chunk's remaining bytes: `4 + 4×0x50 =
324` bytes of header, `1500 − 324 = 1176` bytes of data, and
`4 × (8×12 + 6×16 + 6×1 + 24×4) = 4 × 294 = 1176` — an exact match. Holds
across every one of the 55 sampled files that carry a `PLYT` chunk, zero
exceptions.

### Follow-up: full verification sweep across every other documented chunk type

**Confidence: verified**, same 103-file sample, via an independent
from-scratch Python decoder (not committed, no dependency on husk's own —
not yet written — `.phys` parser).

- **Chunk tags are byte-reversed on disk** (WMO/ADT convention), the
  *opposite* of M2's own inline chunks (`src/chunk.hpp`'s doc comment: M2
  tags are not reversed). Confirmed via hex dump
  (`test_data/.../mace_1h_warfrontsforsaken_d_01.phys`: `53 59 48 50` =
  `"SYHP"` = `"PHYS"` reversed) and holds on all 103 files — every reversed
  tag resolved to a name PHYS.md documents, never an unrecognized one.
  `husk::readChunks`/`findChunk` (`src/chunk.hpp`), as used verbatim by
  `bone.cpp`/`skel.cpp`, assume the M2 (non-reversed) convention and can't
  be reused as-is for `.phys`.
- **`BODY`/`BDY3`/`BDY4`'s "only one body should be of type 0 (root)"
  claim is contradicted by most real files.** 78 of 98 sampled files with
  a body chunk have *more than one* type-0 body (up to 27 of 44 in
  `creature/gallywix/gallywix.phys`). Cross-tabulated against `BDY3`'s
  documented `unk1`-as-weight field (same page, "if version >= 3 and
  unk1 == 0 the body will be non kinematic even if the flag is set"): 96%
  of 1256 sampled bodies (1205/1256) cleanly split type-0↔`unk1==0`,
  type-1↔`unk1≠0` — consistent with type-0 meaning "kinematic, driven by
  its bone, not simulated" as a real per-body classification, not a
  single distinguished root. The wiki's own worked example,
  `offhand_1h_artifactskulloferedar_d_06` ("all the bodies have the
  kinematic flag"), has 4–5 of 16 bodies as type-0 — many, not one.
- **`PHYV`'s worked example and mutual-exclusivity claim both confirmed**
  on the exact file the wiki names (`7vs_detail_nightmareplant01_phys.phys`)
  and its sibling `..._02`: both files' entire chunk set is
  `{PHYS, PHYT, PHYV}`, 54 bytes total, no slack. Both `PHYV` payloads
  genuinely *differ* from the wiki's own listed default six floats (which
  the wiki itself calls uncertain, "some kind of tuning?") — real per-file
  override data, not a fixed constant the worked example happened to guess
  right.
- **Version ↔ chunk-name-variant pairing holds with zero exceptions**:
  v0/1 → `BODY`+`SHAP`+`WELJ` (no `PHYT` at v0); v3 → `BDY3`+`SHP2`; v4/5
  → `BDY4`+`SHP2`. `SHOJ`'s documented version-2 stride ambiguity (0x6c
  vs. 0x74, same chunk name either way) never actually produced an
  ambiguous case: every one of 86 real `SHOJ` chunks divides evenly by
  exactly one of the two strides, never both. No version 2 or 6 file seen
  in the sample (6 remains exactly as unverified as PHYS.md's own `ᵘ` flag
  states); `BOXS`/`SPHJ`/`PRSJ`/`PRS2`/`DSTJ`/`SHJ2` never appeared either
  — real chunk types, just not exercised by this particular sample.
- **Every cross-chunk index reference resolves in range, in all 103
  files, zero exceptions** — `BODY`/`BDY3`/`BDY4`'s shape range vs. `SHAP`/
  `SHP2`'s count, `SHAP`/`SHP2`'s `shapeIndex` vs. the target shape
  chunk's count, `JOIN`'s `bodyAIdx`/`bodyBIdx` vs. body count, and
  `JOIN`'s `jointId` vs. the matching joint-type chunk's count (including
  the `SHOJ` stride disambiguation above). A genuinely wrong stride
  anywhere in this chain would very likely have produced *some*
  out-of-range value across 103 files and 1256 body records — a clean
  zero is strong independent confirmation the transcribed layouts are
  right.
- **`BDY3`/`BDY4`'s `boneIndex` decodes to plausible M2 bone indices**:
  `mace_1h_warfrontsforsaken_d_01.m2` (`husk info`: `bones: 17`) pairs with
  a `.phys` whose `BDY4` chunk uses `boneIndex` values `{0..9}`, each
  exactly once, all within `[0, 17)`.

Not resolved by this investigation: what several of the wiki's own
`unk`-tagged fields actually *mean* (`SHOJ`'s `motorMode`, `PLYT`'s
per-node tree-structure fields beyond "connects the vertices together")
— these decode to sane numbers but confirming semantics, not just
byte-layout, needs simulation-behavior testing this investigation didn't
attempt.

---

## 10. `M2` — `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL`: the original "confirmed absent" result was a scanner bug, not a real finding — all five are real and present

**Confidence: corrected, verified positive result.** The original version of
this section (below, for the record) reported all four original targets as a
verified zero-hit result across the full 130,576-file corpus. That was
**wrong** — a real bug in `tools/find_m2_unknown_chunks.py`, not a real
absence. Caught by `casc-tool`'s independently-built `scan-chunks` command (a
structural, dual-orientation chunk walker with no Python and no shared code
with this script), which found real hits for all five tags — `PCOL`
(`M2_GAPS_TODO.md` Item 4) folded in here too, since it's the same class of
check and was blocked on the same "zero real files" belief.

### The bug

`find_m2_unknown_chunks.py`'s `hits` dict is keyed by `str`
(`tag.decode()`), but the per-chunk membership check compared the raw
`bytes` tag against it: `if tag in hits`. `bytes` and `str` never compare
equal in Python 3 (different type, different hash) — that check was **always
`False`**, for every chunk, in every file, regardless of what the real data
contained. The scan never had a chance to record a hit; the "0/130,576"
result it reported was a property of the bug, not of the corpus. Fixed by
comparing against `TARGET_TAGS` (still raw `bytes`) directly instead of the
`str`-keyed `hits` dict.

### Corrected real counts (same corpus, `/media/luna/data/wow_export`, 130,576 real `.m2` files, 334 unreadable/skipped — unchanged from the original scan)

| Tag | Hits | Notes |
|---|---|---|
| `WFV1` | 2 | Both `world/expansion07/doodads/nazjatar/` (BfA 8.2 Nazjatar zone) |
| `WFV2` | 2 | Both `world/expansion07/doodads/nazjatar/`, same zone as `WFV1` |
| `AFRA` | 32 | Spell-effect/void-themed assets (`spells/fx_*_aura.m2`, `world/expansion11/doodads/void/...`, several `models/*/unk_exp11_*` unnamed models) |
| `DPIV` | 2,632 | Always exactly 32 bytes, matching the wiki's own "mostly empty" description |
| `PCOL` | 2,354 | Variable size (2,016–12,608+ bytes seen), consistent with the wiki's self-describing count+offset struct |

Independently re-confirmed against a second, differently-sourced corpus: a
live CASC install scanned directly via `casc-tool scan-chunks --mask '*.m2'
--watch WFV1,WFV2,DPIV,AFRA,PCOL` (product `wow`, build 68887) found the
*exact same* `WFV1`/`WFV2`/`AFRA` counts and paths, and `DPIV`/`PCOL` counts
within ~1% (2,610/2,333 vs. 2,632/2,354 — the small gap is consistent with
this corpus's own known 334-file extraction-completeness gap, not a
disagreement about real content). Two independently-written tools, in two
different languages, against two different extractions of the game data,
converging on the same result — about as strong a confirmation as this
project's own discipline asks for.

**The single strongest individual data point**: one of the two `WFV1` hits
is FileDataID **2445860** — `world/expansion07/doodads/nazjatar/
8nzj_waterfall_test_01.m2` — the exact file wowdev.wiki names as its own
concrete "first tested" example for `WFV1`. An independent scanner landing
exactly on the wiki's own named example is not a coincidence a bug could
produce.

### Raw content observed (not yet a confirmed semantic reading — flagged as a hypothesis, not fact, per this project's own "don't guess at semantics" discipline)

- `AFRA` (16 bytes, all 32 real hits): first 4 bytes decode as a real
  little-endian `float32` in a plausible 0.0–1.0 range across every hit
  (`0000003f`=0.5, `9a99193f`≈0.6, `48e17a3f`≈0.979, `cdcc4c3e`≈0.2,
  `9a99993e`≈0.3), remaining 12 bytes zero. Consistent with a single
  fade/radius parameter given the tag's own "AFRA" naming and the
  spell-effect/aura context every real hit's path shares, but not confirmed
  against any authoritative source.
- `DPIV` (32 bytes, every one of 2,632 hits): mostly zero-filled, matching
  the wiki's "mostly empty" text exactly; a minority of real files
  (e.g. `creature/pa_kite_lamp/pa_kite_lamp_creature.m2`) have non-zero
  leading bytes that decode as plausible real float values, not garbage.
- `PCOL`: leading bytes decode as a run of small, plausible array-count
  integers (e.g. `40, 32, 74, ...`) exactly matching the wiki's own
  documented count+offset struct shape — consistent with real, well-formed
  collision data, not yet implemented/cross-checked field-by-field.

### Original text (for the record — this is what was wrong, not what should be trusted)

> As of this corpus's extraction (2026, retail client — the corpus has
> real coverage of both 8.2.0-era and War Within-era content, confirmed
> via other version-gated chunks like `DETL`/`WFV3` and player-housing
> `DETL`-bearing doodads elsewhere in this corpus), **zero real files carry
> a `WFV1`, `WFV2`, `DPIV`, or `AFRA` top-level chunk** — not "rare," not
> "sampled and missed," a complete zero-hit result across all 130,576 real
> `.m2` files.

### What went wrong the first time

The scanner's own sanity check (running it against `test_data/
bloodelffemale.m2`'s known-good chunk sequence first) didn't catch the bug
because that fixture doesn't carry any of the four original target tags —
the sanity check confirmed `iter_top_level_chunks` walks correctly, but
never exercised the broken `if tag in hits` line at all, since it only
checked that the *walk* was clean, not that a *positive* case would actually
be recorded. A single synthetic positive-case unit test (a fake buffer
containing a real `DPIV` tag, asserting the scanner records it) would have
caught this immediately — worth keeping in mind for any future from-scratch
corpus scanner in this project: prove the detector fires on a case
constructed to be positive, not just that it walks real negative data
cleanly.

### Still not resolved by this investigation

Whether `WFV1`/`WFV2` exist anywhere outside the single `expansion07/
doodads/nazjatar/` zone both real hits share — genuinely rare either way (2
files out of 130,576), unlike `DPIV`/`PCOL`/`AFRA` which are common enough
that "rare/newer than this corpus's coverage" is no longer a live
hypothesis. Implementation (per `M2_GAPS_TODO.md`'s former Item 4 discipline
for `PCOL`) still needs the same self-describing offset-region byte
accounting `PLYT` already established, not yet attempted for any of these
five.

---

## 11. `M2` — `DETL`'s real stride is 12 bytes, matching the wiki's own field sum; the stated `/*0x0a*/` end-offset is the actual error, and chunks are zero-padded to a 16-byte boundary (undocumented)

**Confidence: verified**, against all 1,043 real files carrying a `DETL`
chunk in `/media/luna/data/wow_export` (`tools/check_detl_stride.py`, full
corpus sweep) — every one of the wiki's two candidate sizes (0x0a stated,
0x0c computed from the field list) was tested against real bytes, plus a
third possibility neither candidate anticipated.

### Current text (M2#DETL)

```
struct
{
/*0x00*/  uint16_t flags;
/*0x02*/  float16 scale;
/*0x04*/  float16 diffuseColorMultiplier;
/*0x06*/  uint16_t unk0;
/*0x08*/  uint32_t unk1;
/*0x0a*/
} DETL_recs[m2data.header.lights.count];
```

Fields sum to 12 bytes (0x0c), but the trailing offset comment says 0x0a —
a 6-byte discrepancy that made `husk::cmd_dump.cpp` treat this chunk as
"wiki's own math doesn't check out" and fall back to a raw hex dump rather
than parse it structurally.

### Proposed addition

> **The record stride really is 0x0c (12 bytes), matching the field list,
> not the stated `/*0x0a*/` comment** — the same "trust the field list over
> one inline offset annotation" resolution as §1 (`M2Sequence`). The
> `/*0x0a*/` annotation is simply wrong (most likely intended as `0x0c` and
> mistyped, or a leftover from an earlier, shorter draft of the struct).
>
> **New finding, not on the wiki at all**: the `DETL` chunk's total byte
> size is **zero-padded up to the next 16-byte alignment boundary** when
> `12 × lights.count` isn't already a multiple of 16 — i.e. real chunk size
> is `((12 × lights.count + 15) / 16) × 16`, not simply `12 ×
> lights.count`. `lights.count` itself is otherwise accurate (one real
> record per light, no off-by-one).

### Evidence

Every one of 1,043 real `DETL`-bearing files was checked three ways:

1. **Naive `size / lights.count` division**, testing whether the result is
   a clean integer at all: 1,012 files (97%) divide evenly at 16 bytes,
   18 at 12 bytes, 13 at neither — an apparent three-way split that looked,
   at first, like *two* real struct variants (12-byte and 16-byte) plus a
   handful of broken files.
2. **Direct byte decode at both candidate strides**, on a real multi-light
   file (`creature/goblinspidertank/goblinspidertank.m2`, 4 lights, 48-byte
   `DETL` chunk — one of the "divides evenly at 16" bucket from step 1).
   Decoding at stride 16 produces garbage after the first record (`unk1`
   values in the hundreds of millions, nonsense `flags`); decoding at
   stride 12 produces **four identical, clean records** (`flags=0`,
   `scale`=half-float `0x231c` = 0.013885498046875, `diffuseColorMultiplier`
   = half-float `0x3c00` = exactly 1.0, `unk0`/`unk1` both 0) — the
   "divides evenly at 16" result for this file was a coincidence of
   `48 = 12×4 = 16×3` both being true, not evidence for a 16-byte struct.
3. **The alignment-padding hypothesis, tested against all 1,043 files at
   once**: `predicted_size = ((12 × lights.count + 15) // 16) × 16` matches
   the real `DETL` chunk size for **1,043 / 1,043 files (100%)** — stride
   10 matches only 998/1043, stride 14 only 1,012/1043, neither anywhere
   near a clean sweep. Decoding every real record in the corpus at stride
   12 (1,386 total records across all files) finds exactly two distinct
   `flags` values (`0x0000`, 1,101 records; `0x0008`, 285 records — a
   small, sane bitfield), a **single constant `scale`** value
   (0.013885498046875) across every real record with no exceptions, a
   **single constant `diffuseColorMultiplier`** value (exactly 1.0) across
   every real record, and `unk0`/`unk1` always zero — about as clean a
   confirmation as real data gets, though this corpus's own sample may
   simply not include files that vary these fields (not proof they never
   do).

### Not resolved by this investigation

Why `scale`/`diffuseColorMultiplier`/`unk0`/`unk1` never vary across 1,386
real records in this corpus — either these fields are genuinely
near-constant defaults in practice (plausible: "scale for shadow RT
matrix" and a 1.0 "no-op" multiplier both sound like sensible defaults an
artist would rarely touch), or this corpus's own `DETL`-bearing files
(mostly player-housing lighting fixtures) happen to share one lighting
preset. `flags`' two observed values (0/0x8) aren't matched against any
documented bit semantics — the wiki gives none.

---

## 12. `M2` — `M2Sequence.aliasNext` is a local array index into the same file's own `sequences` array, not an `AnimationData.dbc` id or anything cross-file

**Confidence: verified**, against 157 real alias sequences (`flags & 0x40`)
across 4 real files from one character-model family (`character/bloodelf/
female/bloodelffemale.m2`, `.../female/bloodelffemale_hd.skel`, `.../male/
bloodelfmale.m2`, `.../male/bloodelfmale_hd.skel` — `tools/
check_alias_next.py`). Directly resolves `M2_UNKNOWNS_EXPLORATION.md`
target 6 / `TODO_correctness.md` former item 4's open question — see "What
went wrong the first time" below for why an earlier pass on this exact
question concluded the opposite.

### Current text (M2#Animation_sequences)

`aliasNext` (`uint16_t`, "id in the list of animations. Used to find actual
animation if this sequence is an alias") sits at `/*0x22*/` in the wiki's
own field-offset annotations, and the Flags table adds a real mechanism:
"the client skips these by following `aliasNext` until an animation without
`0x40` is found." A separate, older bullet elsewhere on the same page says
flatly "I have no clue" where this resolves.

### Proposed addition

> **`aliasNext` is a plain index into this same file's own `sequences`
> array** (`sequences[aliasNext]`), not an `AnimationData.dbc` id and not
> anything resolved cross-file. Despite the field's own doc-comment saying
> "id," it behaves exactly like an index: every real alias sequence
> checked resolves to an in-range local array position, and following the
> documented chain-walk (repeatedly jumping to `sequences[aliasNext]` until
> reaching a record without `flags & 0x40`) always terminates cleanly, with
> no cycles, at a real non-alias sequence. The wiki's own "I have no clue"
> bullet is simply stale/wrong for this field, superseded by its own more
> specific struct annotation and Flags-table text elsewhere on the page.
>
> **Byte-offset correction this depends on**: the wiki's literal `/*0x22*/`
> annotation is pre-§1-correction — it doesn't account for the real
> 28-byte `M2Bounds bounds` field §1 already established. At the real,
> `M2Bounds`-corrected 64-byte stride, `aliasNext` sits at **offset 0x3E**
> (not 0x22), immediately after `variationNext` at 0x3C. Reading at the
> literal, uncorrected 0x22 offset (inside what is actually the middle of
> `M2Range replay`/`blendTimeIn` at the real stride) is exactly what
> produces the nonsensical 48,861–48,983-range values an earlier pass on
> this file reported — see below.

### Evidence

`tools/check_alias_next.py` decodes `id`/`flags`/`variationNext`/
`aliasNext` at the corrected offsets (`0x00`/`0x0C`/`0x3C`/`0x3E` within the
64-byte record) for all four real family files (339/396/343/405 sequences
respectively). Across all **157** real alias sequences (`flags & 0x40`)
found:

- **157/157 (100%)** have an `aliasNext` value that is a valid index into
  their own file's `sequences` array (`aliasNext < sequences.size()`).
- Following the documented chain (`cur = aliasNext` repeatedly while
  `sequences[cur].flags & 0x40`) terminates at a non-alias sequence for
  **all 157**, with **zero cycles and zero runaway chains** (bounded by
  `len(sequences)` steps as a safety cap, never hit).
- The terminal (non-alias) sequence's own `id` is **never** the same as the
  alias's own `id` (0/81 for `bloodelffemale_hd.skel`, 0/86 for
  `bloodelfmale_hd.skel`) — consistent with aliasing being a genuine
  cross-animation-id redirect ("play a *different* animation's data for
  this id"), not a same-id variant selector.
- 10 of the 157 alias records also have a real (non-`-1`) `variationNext`
  set simultaneously — the two fields aren't mutually exclusive, though
  most records (147/157) leave `variationNext` at `-1` while aliasing.
- A cross-file `id`-matching check (does `aliasNext`, read as if it were
  meant as an `AnimationData.dbc`-scale id rather than an index, match any
  sequence's `id` in a sibling race/gender file?) found matches for 101/157
  records — but this is very likely **coincidental**, not the real
  mechanism: small integers like local sequence indices (which is what
  `aliasNext` actually is, per the local-index result above) collide
  constantly with small real `id` values purely by chance in a
  several-hundred-entry `id` space, and the match rate tracks exactly the
  set of low-numbered `aliasNext` values already explained by the local-
  index finding, not an independent signal.

### What went wrong the first time

The 7/396-alias pre-existing finding (`TODO_correctness.md`'s former item
4, `bloodelffemale_hd.skel`) reported `aliasNext` values in the
48,861–48,983 range and concluded they resolve neither as a local index nor
a same-file `id` match. That check read `aliasNext` at the wiki's literal,
uncorrected `/*0x22*/` offset — which, at the real 64-byte stride established
by §1, lands inside `M2Range replay`'s second `uint32_t` field, not anywhere
near the real `aliasNext`. The huge, nonsensical values it found are exactly
what reading a `replay` bound as a `uint16_t` alias index would produce —
not evidence of an external/unresolvable mechanism, just a stale-offset bug
carried forward from before §1's own correction was made. Once read at the
corrected offset 0x3E, the same file's real `aliasNext` values are small,
sane, in-range local indices (e.g. sequence #222 → 221, #294 → 293) — no
external data needed at all.

### Not resolved by this investigation

Whether `aliasNext`'s "id" wording in its own doc comment reflects some
historical meaning (perhaps an older client version really did use a
global id here, later changed to a local index without the wiki text being
updated) — speculative, not checked.

### Follow-up: implemented

`buildAnimations` (`cmd_export.cpp`) now resolves the chain (bounded to
`sequences.size()` hops, throwing rather than looping forever on a cycle
real data has never shown) and reuses the terminal sequence's own animation
data for a "pure" alias sequence (`flags & 0x40` set, `flags & 0x20` not) —
one real subtlety this needed a committed real fixture to surface: 31 of
`bloodelffemale_hd.skel`'s 38 real alias sequences also carry `flags & 0x20`
("stored inline"), so they already have real data of their own and must keep
using it, unaffected by alias resolution, exactly as before it existed (see
`DESIGN.md`'s Key design decisions for the full writeup). Measured against
this exact fixture, the net effect on `bloodelffemale_hd.m2`'s own clip count
is zero: all 7 "pure" alias sequences resolve (in the full real corpus) to a
terminal sequence needing one of 6 distinct external `.anim` files, none of
which happen to be among the ~104 already committed to this repo's own
`test_data/` — a real, checked answer, not an assumption.

---

## 13. `M2` — `EXP2`/`PFDC` real files exist (a local-extraction gap, not a real absence); one true anomaly (`BLP2`) resolved as a listfile mismatch, not an M2 chunk

**Confidence: verified, both corrections and the resolved anomaly.** A
separate casc-tool thread ran two independent full-corpus scans directly
against a live CASC install (product `wow`, build 68887): a top-level
chunk-tag census across all 130,576 real `.m2` files (31 distinct tags, not
just the 5 §10 targeted), and a full-storage (1,891,552-file, not
`.m2`-scoped) byte-signature scan.

### `EXP2`/`PFDC`: "zero real files" was a local-extraction gap

`M2_COMPLETENESS.md` and `src/m2.hpp`/`cmd_dump.cpp` previously stated
husk's own corpus (`/media/luna/data/wow_export`, the extraction this
project's other WIKI_FINDINGS entries verify against) has **zero** real
`EXP2`- or `PFDC`-bearing files — both were implemented from the wiki's
struct listing alone, unverified. The live-CASC chunk census found
**17,065** real `EXP2` files and **2,430** real `PFDC` files — a much
bigger gap than the ~1% extraction slack §10 already knew about for
`PCOL`/`DPIV`, consistent with these two tags' files being systematically
absent from the local extraction rather than just randomly missed.

Pulled two real files directly via `casc-tool extract` (storage
`/media/luna/games/World of Warcraft`) and ran them through husk unmodified:

- FileDataID 126382 (`EXP2` only): `husk info`/`dump-chunks` parse cleanly,
  9 particle emitters, `EXP2` records all sane defaults
  (`zSource`=0/`colorMult`=1/`alphaMult`=1), `alphaCutoffOffset` correctly
  resolves to an empty curve where the source has none.
- FileDataID 1003471 (`EXP2` **and** `PFDC` together): parses cleanly, 4
  particle emitters; `EXP2` shows a real monotonic 3-keyframe
  `alphaCutoff` curve (life_fraction 0 → 0.500015 → 1); `PFDC` decodes a
  real version-6/`phyt`-3 physics body record, same shape the 103-file
  `.phys` sweep (§9) already established for standalone `.phys` files.

No code changes were needed — both parsers already handle real data
correctly. Only the "unverified"/"zero real files" claims were stale.

**Follow-up (`M2_GAPS_TODO.md` item 9, now closed)**: the two real fixtures
above (`test_data/verification/exp2_126382.m2`/`pfdc_1003471.m2`) are now
permanent, gitignored test fixtures with real `doctest::skip()`-gated
regression coverage in `tests/test_dump.cpp` — the hand-verification above
is checked by `ctest` on every run now, not just this session's prose.

### `BLP2`: a genuine anomaly, resolved — not an M2 chunk at all

The chunk census also reported one real `.m2`-masked file (FileDataID
7507381) with a 1-byte top-level `BLP2` chunk — `BLP2` is a texture
container magic, not any documented M2 chunk tag, and husk's own parser
outright refused to open the file (`chunk tag` error, not silently
misread). Pulling the file directly and hex-dumping its first 32 bytes
found the real explanation: the file's actual content **is** a genuine
BLP2 texture (`42 4c 50 32` = "BLP2", followed by a real
compression-type/width-height/mipmap-offset-table header, decoding to a
plausible 512×256 texture) — not an M2 file at all. FileDataID 7507381 is
absent from this project's own listfile snapshot, consistent with the
chunk-census tool's `*.m2`-masked enumeration trusting a (stale or
mis-guessed) extension in whatever listfile *it* used, rather than
sniffing real content — a texture registered under an `.m2`-shaped name
slipped into the `.m2` scan and got its first 4 bytes read as if they were
a chunk tag. Not a husk bug, not a real M2 anomaly — a listfile/FileDataID
labeling mismatch upstream of both tools.

**Follow-up (`M2_GAPS_TODO.md` item 10, now closed)**: the same real file
(`test_data/verification/blp2_7507381.m2`) now has permanent regression
coverage in `tests/test_integration.cpp` locking in husk's correct
throw-not-misread behavior across `info`/`export`/`dump-chunks`, so a
future chunk-walker refactor can't silently regress it.

### `M3`: 8 real files exist, out of scope

The full-storage `M3DT`-magic scan found 8 real `.m3` files (unresolved
listfile names, `models\unknown\unk_exp*\<fdid>.m3`) — an entirely
different, undocumented model format. Recorded in `DESIGN.md`'s Non-goals
for the record; no investigation started, consistent with WMO/M3 being an
explicit, by-design non-goal for this project.

---

## Where these live in husk

| Finding | Code | Tests |
|---|---|---|
| §1 `M2Sequence` = 0x40 bytes | `src/m2.hpp`/`m2.cpp` (`Sequence`, `parseSequences`) | `tests/test_m2.cpp` |
| §2 `AFSB` real resolution | `src/cmd_export.cpp` (`buildAnimations`'s external-file branch), `src/m2.cpp` (`resolveVec3TrackSequence`/`resolveQuatTrackSequence`/`trackSequenceInnerArrays`, unchanged — just fed the `AFSB` payload as `externalDataBlob`) | `tests/test_cli.cpp`, `tests/test_integration.cpp` (real 336-clip corpus check) |
| §3 `.skel` `SKS1` indexing + own `AFID` | `src/skel.hpp`/`skel.cpp` (`parseSequences`, `boneTrackBlob`, `findAnimFileIds`) | `tests/test_skel.cpp`, `tests/test_cli.cpp` |
| §4 `.bone` format | `src/bone.hpp`/`bone.cpp` | `tests/test_bone.cpp` |
| §5 `bounding_box` containment, not tight fit | `src/cmd_export.cpp` (unaffected — no code depends on `bounding_box` being tight), `tests/test_conformance.cpp` (`transformedM2BoundingBox`) | `tests/test_conformance.cpp` |
| §6 `M2Particle` offsets + `FBlock` `uint16_t` timestamps | `src/m2.hpp`/`m2.cpp` (`ParticleEmitter`, `parseParticles`, `resolveFloatTrackSequence`/`resolveRawIntTrackSequence`/`resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16`), `src/cmd_dump.cpp` (full-record JSON), `src/cmd_export.cpp`/`gltf.hpp`/`gltf.cpp` (`EmitterAnchor` extras) | `tests/test_m2.cpp`, `tests/test_dump.cpp`, `tests/test_gltf.cpp`, `tests/test_integration.cpp` (real weapon corpus) |
| §7 multi-texture-layer arithmetic confirmed; `textureCoordCombos` value range | `src/cmd_export.cpp` (`buildMaterialsAndPrimitives`'s additional-layer loop, unchanged) | `tests/test_integration.cpp` (`checkMultiTextureLayerArithmetic`, real pennant/ironhorde fixtures) |
| §8 `WFV3`'s real 64-byte short variant | `src/cmd_dump.cpp` (`dumpWfv3`) | `tests/test_dump.cpp` |
| §9 `.phys` format verified (`PLYT` stride fix + full sweep) | `src/phys.hpp`/`phys.cpp` (full parser), `src/gltf.hpp`/`gltf.cpp` (`PhysicsBody` extras), `src/cmd_export.cpp` (`--phys`), `src/cmd_dump.cpp` (full body/shape/joint/`PHYV` JSON, `.phys` file accepted directly) | `tests/test_phys.cpp`, `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_dump.cpp`, `tests/test_integration.cpp`/`test_conformance.cpp` (real weapon fixture) |
| §10 `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL` real, present (corrected from a scanner bug's false "absent") | `src/cmd_dump.cpp` (`kFallback` notes, unchanged — none of the five implemented yet) | investigation-only, `tools/find_m2_unknown_chunks.py` (bug fixed), cross-checked via `casc-tool scan-chunks` |
| §11 `DETL` real stride (0x0c) + 16-byte alignment padding | `src/cmd_dump.cpp` (`dumpDetl`, `readHalfFloat`) | `tools/check_detl_stride.py` (investigation), `tests/test_dump.cpp` (implementation) |
| §12 `aliasNext` = local `sequences` array index, chain-resolved into real clips | `src/m2.hpp`/`m2.cpp` (`Sequence`'s 7 new fields, `parseSequences`), `src/cmd_export.cpp` (`resolveAliasChain`, `buildAnimations`), `src/gltf.hpp`/`gltf.cpp` (`Animation::SequenceMetadata` extras) | `tests/test_m2.cpp`, `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_integration.cpp`, `tools/check_alias_next.py` |
| §13 `EXP2`/`PFDC` real files exist (local-extraction gap corrected); `BLP2` anomaly resolved as listfile mismatch; `M3` noted, out of scope | `src/m2.hpp` (`ExtendedParticle` comment), `src/cmd_dump.cpp` (`physPayloadRealLength` comment), `DESIGN.md` Non-goals — no parser changes needed | `tests/test_dump.cpp` (real `EXP2`-only and `EXP2`+`PFDC` fixtures, exact values), `tests/test_integration.cpp` (`BLP2`-anomaly throws-cleanly across `info`/`export`/`dump-chunks`) — `test_data/verification/` |
