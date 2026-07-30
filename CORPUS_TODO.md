# CORPUS_TODO — grounded findings from the 130k-file corpus sweep

This is `HUSK_CORPUS_FINDINGS.md` re-checked against the actual code
(`src/`) and real corpus files at `/media/luna/data/wow_export`, byte by
byte where it mattered. Several of the original doc's hypotheses turned out
right; a couple turned out **wrong once grounded**, and one investigation
uncovered a real bug the original doc didn't have enough information to
find. Nothing in `src/` has been changed yet — this is the plan the next
session should work from.

Numbered by priority (impact × fixability), not by the original doc's
`FAIL-00NN` order (that numbering isn't stable across runs anyway).

---

## 1. FAIL-0001 — 3,807 files — empty-primitive crash — CONFIRMED, fixable, highest leverage

**Original hypothesis:** legitimate zero-triangle submesh convention that
`writeGlbMulti` treats as fatal.

**Grounded:** correct, and root-caused precisely. Checked
`particles/lootglow_boss.m2` (one real FAIL-0001 file) byte-for-byte —
its resolved `.skin` (`lootglow_boss00.skin`) has **zero vertices, zero
indices, zero submeshes, zero batches**. It's a pure particle-effect model
(no ribbons here, but the same shape as the particle/ribbon-only VFX
models this corpus has plenty of) with no renderable mesh at all.

`buildMaterialsAndPrimitives` (`src/cmd_export.cpp:637-649`):
```cpp
if (batches.empty()) {
    gltf::Primitive prim;
    prim.indices = triangleIndices;   // empty, when the .skin is itself empty
    result.primitives.push_back(std::move(prim));
    return result;
}
```
unconditionally emits one primitive even when `triangleIndices` is empty.
That primitive then hits `writeGlbMulti`'s hard check
(`src/gltf.cpp:167-170`) and the whole export aborts.

The function's own doc comment already anticipated "a genuinely
material-less model" but not a genuinely **geometry-less** one — which is
the common case here (spell-effect/particle .m2 files with only
particle/ribbon emitters and no mesh).

**Fix direction:** when `triangleIndices` is empty, don't manufacture a
primitive at all — the model should still export (skeleton + ribbon/
particle emitter anchors, already unconditional per `DESIGN.md`), just
without a render mesh. That likely means `cmd_export.cpp` needs to skip
adding a `NamedMesh` for a LOD tier whose resolved skin has no real
geometry, not just special-case it inside `buildMaterialsAndPrimitives`.
Needs a design decision: does a mesh-less model get zero `NamedMesh`
entries (skip mesh output entirely, keep skeleton/skin extras), or an
empty-but-valid one? glTF itself requires every `mesh.primitives[]`
entry to have non-empty `indices` when present at all, so "zero
primitives" isn't valid either — the real fix is likely "zero meshes,"
not "one mesh with zero primitives."

**Action:** confirm this shape (empty `.skin`) is actually the dominant
case across a larger sample of the 3,807 (not just one file) before
committing to the fix — pull ~20 more `FAIL-0001` paths from
`failures.txt` and check submesh/batch counts the same way. If a
minority instead have non-empty submeshes with `indexCount == 0` on
*individual* submeshes (mixed real+empty geosets in one file), the fix
needs to handle that at the per-primitive level too (skip that one
primitive, keep the others), not just the whole-file empty case.

### DEVELOPER NOTES

Go with 'zero meshes' approach for these

---

## 2. `.skin`-not-found "buffer truncation" — DISPROVED, real bug is elsewhere (in the test harness, not husk)

**Original hypothesis:** a fixed-size buffer / unchecked `snprintf`/
`strncpy` in husk's error-formatting path, worth ~14 collapsed
`FAIL-00NN` codes.

**Grounded: this is wrong.** `src/cmd_export.cpp:1071-1073` builds this
message with plain `std::string` concatenation:
```cpp
throw std::runtime_error("'auto' couldn't resolve a .skin file for '" + modelPath + "': " + reason +
                          ", and no same-named '<model-basename><N>.skin' file exists next to it "
                          "either -- pass an explicit .skin path instead of 'auto'");
```
No `snprintf`, no fixed buffer, nothing that could truncate. husk itself
always emits the full message.

**Real cause:** the corpus test harness. `tools/corpus_checks.py:156-162`:
```python
def _last_meaningful_line(out: str) -> str:
    for line in reversed(out.splitlines()):
        if line.strip():
            return line.strip()[:400]
    return ""
```
truncates every captured stderr line to 400 characters before it's
recorded in `*.status.json`. Since `modelPath` (a long, variable-length
absolute path) is interpolated *before* the fixed tail
(`"...pass an explicit .skin path instead of 'auto'"`), a longer path
pushes more of the fixed tail text past the 400-char cutoff — which is
exactly why different files produce different truncation points and
collapse into ~14 different `FAIL-00NN` signatures for what's genuinely
one bug.

**This is not a husk bug at all**, and fixing it doesn't belong in
`src/`. Two options for the test tooling itself (not requested yet, flag
for a separate pass): raise `_last_meaningful_line`'s limit, or normalize
path-length-dependent truncation before hashing into a failure signature
(e.g. truncate the *detail* after substituting `<path>`, not before).
Either fixes the code-count inflation without touching husk.

**Real underlying bug size, once you look past the truncation:** ~267
files across the corpus (`FAIL-0003` through `FAIL-0016`, `0023`, `0024`,
plus 2 in `failures_unique.txt`) hit "SFID-declared FileDataID's .skin
wasn't found, and no same-named fallback either." Spot-checked one
(`spells/fx_barrierblossom_areatrigger.m2`): the model has 10,410
vertices and a real 8-particle-emitter array (genuine renderable mesh,
not VFX-only) and declares 4 real skin FileDataIDs
(`5104074`–`5104077`), but **no `.skin` file of any name exists next to
it in this corpus dump at all**. This is a corpus-extraction
completeness gap (wow_export didn't pull skins for these spell-effect
models), not a husk defect — husk is correctly and loudly refusing to
guess. Not fixable in husk; not worth chasing further unless the corpus
extraction itself gets redone.

### DEVELOPER NOTES

> _last_meaningful_line

I will manually fix this

> wow_export didn't pull skins for these spell-effect models

Explore if possible to fix, aka pulling skin files for spell effect models, or if they are somehow misplaced, or referenced differently, if there genuinely is no way to find the correct one, or if the model doesn't need one, add explanatin note in README



---

## 3. Vertex-index mismatches — split into two *different* real bugs, neither matches the original doc's framing

The original doc split this into "exact off-by-one" (blamed on husk's
`>=` vs `>` bounds check) and "larger gaps" (blamed on bad source
pairing). Both framings turned out incomplete once checked against real
bytes.

### 3a. The bounds check itself is correct — not a husk bug

`src/cmd_export.cpp:1509-1516`:
```cpp
for (uint32_t idx : triangleIndices) {
    if (idx >= vertices.size()) {
        throw std::runtime_error("'" + path + "' references M2 vertex " + ...);
    }
}
```
`idx >= vertices.size()` is the only correct bounds check (valid indices
are `0..count-1`). There's no `>=`-vs-`>` bug to fix here — checked, it's
already right.

### 3b. Real bug found: `findSameBasenameSkins` can silently grab a *different model's* `.skin` file when one model's basename is a literal string-prefix of another's in the same directory

This is the actual finding this investigation turned up, and it explains
both of the original doc's vertex-mismatch buckets simultaneously.

Byte-verified on `world/expansion04/doodads/mogu/mogu_library_crate_10.m2`
(`FAIL-0021`-adjacent in `failures_unique.txt`, listed as "gap" case):
the directory also contains `mogu_library_crate_1.m2` (a **different**
model). `findSameBasenameSkins` (`src/cmd_export.cpp` ~line 990-1015)
matches any file starting with the model's own stem followed by *one or
more* digits then `.skin` — no fixed suffix width:
```cpp
while (pos < name.size() && std::isdigit(...)) ++pos;
if (pos == digitsStart) continue;  // no digit right after the basename
```
For `mogu_library_crate_10.m2` (stem `mogu_library_crate_10`), the file
`mogu_library_crate_100.skin` matches — stem + digit `"0"` — even though
that file is actually the real LOD00 skin for the *shorter* sibling model
`mogu_library_crate_1.m2` (stem `mogu_library_crate_1` + two-digit LOD
suffix `"00"`). Confirmed with real data: `mogu_library_crate_1.m2` has
exactly **578 vertices**, and the wrongly-grabbed skin's own vertex
lookup table has exactly 578 entries (max index 577) — an exact match.
`mogu_library_crate_10.m2` itself only has 68 vertices; the real skin
that belongs to it is `mogu_library_crate_1000.skin`
(`mogu_library_crate_10` + `"00"`), which also matches the same
prefix-scan and sorts after `...100.skin` isn't picked because both tie
at parsed LOD `0` and `std::sort` + `.front()` breaks the tie
lexicographically — the wrong one wins.

Confirmed the exact same pattern on `world/nodxt/detail/vebgrs10.m2` /
`vebgrs1.m2` (and `vebbsh10.m2` / `vebbsh1.m2`, same directory) —
`FAIL-0027`, filed in the original doc as an unrelated 2-file oddity.

Wrote a byte-level checker
(scratch script, not committed) that resolves a `.skin`'s full triangle-
index buffer and reports every M2-vertex index actually referenced that's
out of range for the model's own vertex count — not just the first one
`cmd_export.cpp`'s error message reports. Every "vertex mismatch" file
checked this way (`mogu_library_crate_10`, both `raidroguenerubian`
variants, `vebgrs10`) turned out to reference **hundreds of out-of-range
indices**, not one — the error message only names the *first* bad index
it hits while iterating `triangleIndices` in order, which is why some of
these looked like a clean "off by exactly 1" to the original doc (the
first bad index happened to equal the vertex count) and others looked
like "a gap of 4-32" (the first bad index happened to land a bit further
in) — **both presentations are artifacts of "report the first offender,"
not two different underlying bugs.**

**Fix direction (real, in husk):** `findSameBasenameSkins` should not
accept an arbitrary-length digit suffix when the model's own basename is
itself a digit-string-prefix of another model's basename in the same
directory. WoW's own convention is a fixed 2-digit LOD suffix
(`00`-`0N`) — enforcing exactly 2 digits (reject 1-digit or 3+-digit
suffixes as non-canonical rather than silently accepting them) would
have resolved every case checked here correctly: `mogu_library_crate_10`
+ `"00"` = `1000.skin` (correct), and `mogu_library_crate_1` + `"00"` =
`100.skin` (correct, for the *other* model) — no longer ambiguous.
Worth double-checking against a few real 3+-digit LOD files before
committing to "exactly 2" as a hard rule (some very high-LOD-count
models might use 3 digits) — but "reject single-digit suffixes when a
2-digit interpretation is available" is a safe, minimal first fix that
directly addresses every case found here.

**Also worth doing regardless of the fix above:** make the error message
report *how many* indices are out of range and the *max* offender, not
just the first — "references M2 vertex 596 (and 1403 more, up to 1129)
but only has 596 vertices" would have made this whole investigation
faster and would immediately flag "this isn't a one-off" to any future
reader.

### DEVELOPER NOTES

Aproved both fix ideas, add improvement to both husk, and to the test suite so we get comprehensive visibility

### 3c. What's left after 3b is subtracted: genuinely bad source pairing, not fixable in husk

Checked `helm_cloth_questbloodelf_b_01_*` (17 files, all in the
"exact off-by-one" bucket) and `helm_leather_raidroguenerubian_d_02_*`
(19 files, all in the "larger gap" bucket) for the same prefix-collision
shape as 3b — **no match**: these models' basenames end in letter-coded
race/gender suffixes (`_kt_m`, `_hu_m`, ...), not digits, so there's no
sibling model whose name is a numeric-suffix prefix collision. Each of
these has exactly one plausible same-basename `.skin` candidate, and it
genuinely references vertex indices the model doesn't have (checked
`helm_cloth_questbloodelf_b_01_kt_m`: skin references up to index 1129,
model has only 596 vertices — roughly double, not a small offset).

This is real mismatched/stale Blizzard source data (Class B per the
original doc's own severity framework) — every race/gender variant of
this one quest helm, and every variant of that one raid helm, ships a
`.skin` that doesn't match its `.m2`'s current vertex count. Not fixable
in husk; husk is correctly refusing to fabricate geometry it can't
verify. **~36 files total, no further action** beyond what husk already
does (fail loudly with a clear message).

---

## 4. Duplicate-timestamp animation keyframes — CONFIRMED real, tolerance fix needed (small, 5 files)

**Original hypothesis:** real shipped Blizzard animation data (a
duplicate/near-duplicate keyframe at a loop seam), husk's tolerance
should be relaxed rather than treating it as corruption.

**Grounded:** the check itself
(`src/cmd_export.cpp:104-121`, `checkKeyframesWellFormed`) is exactly
what its own doc comment says: `i > 0 && keyframes[i].first <=
keyframes[i-1].first` throws, because glTF genuinely requires strictly
increasing sampler input times (`gltf.cpp`'s `addChannel` takes
`times.front()`/`times.back()` as the accessor min/max, which is only
correct if sorted-ascending holds). This is a real glTF-spec constraint,
not an arbitrary husk choice — the fix has to be a repair (collapse or
nudge), not just loosening the check, or the exported animation would be
spec-non-compliant.

Hits both world bosses (`yoggsaronbrain`, `maldraxxusskeleton`) and base
character rigs (`mechagnomemale`, `gnomemale_hd`) at unrelated bone
indices/timestamps, plus one world doodad
(`twilightshammer_largedevice_sand01`) — consistent with a genuine,
occasionally-occurring "hard cut" pose authored as two keyframes at the
same timestamp, not corruption. 5 files total, low priority by file
count but easy, well-understood fix: when
`keyframes[i].first == keyframes[i-1].first`, either drop keyframe `i`
(the two encode the same instant; the later one wins as "the real
value there") or nudge `keyframes[i].first` by 1ms. Confirm against
another known M2 reader's handling before picking one (per the original
doc's own suggestion) — not done yet.

### DEVELOPER NOTES

Approved

---

## 5. `materialIndex`/`textureComboIndex` out of range — genuinely bad data, not fixable (confirmed on one example)

**Grounded:** spot-checked
`item/objectcomponents/collections/armor_flameskull_d_01_green_wo_m.m2`
(`FAIL-0011`, "batch 3's materialIndex (3) out of range for 3
materials"). No sibling-basename collision here (unlike bucket 3b) — the
model has exactly one matching `.skin` candidate, correctly resolved.
The `.skin`'s own batch data references a 4th material that this
specific color/variant `.m2` doesn't have (only 3 materials defined).
Plausible real-world cause: collections/recolor items often share
`.skin` batch data across variants that don't all have the same material
count; this one variant's `.m2` just has fewer materials than the batch
data expects. Real, independent per-file data issue — not a shared root
cause, not fixable in husk beyond what it already does (fail loudly,
name the exact batch/index/count). ~23 files total (`FAIL-0011`,
`0021`, `0022`, plus ~7 in `failures_unique.txt`). Low priority,
matches the original doc's assessment here — no correction needed.

### DEVELOPER NOTES

Check other examples if existing before declaring unfixable

---

## 6. `dump-chunks` WFV3 chunk-size failure — CONFIRMED, real narrow bug, fixable (9 files)

Not analyzed at all in the original doc beyond "one dump-chunks failure,
9 files." Root-caused: `dumpWfv3` (`src/cmd_dump.cpp:314-360`) assumes
every `WFV3` chunk is a fixed 80-byte struct (fields through offset
`0x4C` + 4 bytes = 0x50 = 80), but all 9 real failing files
(`world/expansion08/doodads/maw/*.m2` — Shadowlands "Maw" zone
waterfall doodads) carry a **64-byte** `WFV3` chunk — i.e., missing the
trailing 4 `unk1`-`unk4` floats (16 bytes) that the 80-byte assumption
expects. This reads as a real, version/zone-conditional shorter `WFV3`
variant, not corruption (all 9 hit the identical shape, all in the same
zone/doodad family).

**Fix direction:** make `unk1`-`unk4` conditionally read only when
`c.size >= 80`, falling back to omitting those 4 keys (or `null`) when
the chunk is the shorter 64-byte variant — same "don't guess, dump what
overwrite exists" discipline this codebase already applies elsewhere.
Small, contained, single-file fix (`src/cmd_dump.cpp`).

### DEVELOPER NOTES

Approved

---

## 7. 0-byte `.m2` files — CONFIRMED, not fixable, not a husk bug (334 files)

`FAIL-0002`. Verified directly: `find /media/luna/data/wow_export -name
"*.m2" -size 0` returns exactly 334 files, matching `FAIL-0002`'s count
exactly. These are genuinely empty files in the corpus extraction (0
bytes, can't contain even a magic value) — a corpus-extraction gap, not
a husk defect. husk's failure message is already correct and precise.
No action needed in husk.

### DEVELOPER NOTES

Noted

---

## Priority order for actually fixing things

1. **#1 (empty-primitive crash, 3,807 files)** — biggest win by far.
   Needs a design decision (skip the mesh entirely for geometry-less
   models vs. some other representation) before touching code — worth a
   short plan-mode pass given it changes `cmd_export.cpp`'s per-LOD
   mesh-building flow.
2. **#3b (`findSameBasenameSkins` prefix-collision bug)** — smaller file
   count in this specific 130k-file corpus (confirmed on at least 4
   files: `mogu_library_crate_10`, `vebgrs10`, `vebbsh10`, likely more
   uncounted among the "larger gap" cluster not individually checked),
   but a **real correctness bug** that will keep silently mispairing
   `.skin` files in any future corpus with numbered model-variant
   families — much higher real-world severity than its current file
   count suggests, since a wrong-but-*successful* pairing (not always
   caught by the downstream vertex-bounds check) would silently export
   corrupt geometry instead of failing loudly. Worth checking whether
   any *currently-passing* exports in the 126,098 successes are actually
   silently affected by this (same collision, but the wrong skin
   happens to have equal-or-fewer vertices than the target model, so no
   out-of-range index ever gets triggered) — that's a real risk this
   investigation didn't have time to rule out.
3. **#6 (WFV3 short-chunk variant, 9 files)** — small, contained,
   well-understood, easy fix.
4. **#4 (duplicate-timestamp keyframes, 5 files)** — small, needs a
   short cross-check against another M2 reader before picking
   collapse-vs-nudge, per the original doc's own suggestion.
5. **#5 (materialIndex/textureCombo out of range, ~23 files)** and
   **#3c (genuinely bad `.skin`/`.m2` pairing, ~36 files)** — both
   confirmed real, independent, source-data issues. Not fixable in
   husk. No further action beyond what husk already does.
7. **#2's truncation artifact** — not a husk fix at all; flag for
   whoever owns `tools/corpus_checks.py` (`_last_meaningful_line`'s
   400-char cutoff, or normalize before hashing into a failure
   signature) if reducing the `FAIL-00NN` code count for future sweeps
   is worth doing. Out of `src/`'s scope.

### DEVELOPER NOTES

... we own the tools corpus check, it's our testing framework for husk :D
