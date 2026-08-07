# wowdev.wiki findings — `.bone` (`BONE` page, referenced from `M2`/`M2/.skel`'s `BFID`)

Current, correct facts only. Full evidence trail: `../WIKI_FINDINGS_HISTORY.md`
§4.

**Note on this investigation's own history**: an earlier pass claimed
wowdev.wiki has no page for `.bone`'s content at all. That was wrong — the
wiki page is titled `BONE` (not `.bone`), and it already documents the
container shape; the investigation had searched for the wrong page title.
The reverse-engineered semantic findings below (correction-matrix meaning,
LOD hypothesis ruled out, facial-bone identification) are unaffected — see
`documentation/wowdev-wiki/HUSK_AMENDMENTS.md` for where this correction is
recorded against the actual wiki mirror.

---

## File format — verified (container/fields), inferred (semantic label) — history §4

```
struct {
  uint32_t version;   // 1 in every real file sampled
  // generic chunked container, M2/.skel-style (4-byte tag, uint32 LE
  // size, payload, tags NOT byte-reversed) -- exactly two chunks,
  // always in this order:
} bone_header;

// BIDA: flat array, no M2Array<T> descriptor -- count = chunk_size / 2.
uint16_t bone_indices[];  // indices into the owning model's `bones` array

// BOMT: flat array, count = chunk_size / 64, same entry count as BIDA,
// BOMT[i] corresponds to BIDA[i].
float correction_matrix[16][BOMT_count];  // row-major 4x4
```

Every sampled `BOMT` entry's last column is exactly `(0, 0, 0, 1)`, upper-left
3×3 at or very near identity, small translation-row values — the signature
of a small **corrective delta transform** per listed bone, not a full
bind-pose replacement. Implemented in `src/bone.hpp`/`bone.cpp`
(`husk::bone::parse`), exposed via `husk dump-chunks <file.bone>` and
`husk export --bones-dir` (attached as inert `bone_correction_sets` glTF
skin `extras`, never applied to the render).

## Which `.bone` slot applies — partially resolved — history §4 (two Follow-up sections)

**LOD/render-distance is ruled out** (verified): a model's ~20 `.bone`
files don't fit its own LOD tier count (20 vs. `lod_count: 7`, no clean
relationship), collapse into only 5 distinct bone-index sets heavily
duplicated across slots, and where corrections repeat they're a pure
magnitude scale along one of exactly two fixed 3D directions — the
signature of a small set of underlying shape variants reused across many
*selectable* slots (a customization slider/dropdown), not a detail-reduction
ladder.

**Weapon-type/armor-type is also ruled out** (verified): the corrected
bones cluster on the real skeleton's **Head**/**Jaw** bones (26 bones
parented to Head, 6 to Jaw, pivots at head/jaw height) — nowhere near the
wrist/hand/waist/shoulder bones a weapon-grip or armor-fitting correction
would need to touch. This is the anatomical signature of **facial** detail
bones (brow/cheek/ear/chin), not equipment.

**Still open**: the actual slot→context mapping (which in-game
customization choice picks `BFID[7]` vs. `BFID[13]`) lives in client-side
DB2 data husk has no access to and, by design, never will (no CASC/DBC
access — see `DESIGN.md`'s non-goals). Now known to be a customization-
choice lookup, plausibly facial specifically — not an unresolved LOD or
equipment question.
