# EYES_ON_FINDINGS.md

Findings from a real interactive Blender inspection of a `husk export` output
(post-`TRANSFORM_TRIAGE.md` orientation fix). Mesh completeness and
orientation are both confirmed good now. **Fully resolved items are punched
out of this file outright** (git history has the record — same "no `[Fixed]`
noise" discipline `TODO/TODO_correctness.md` uses), not kept as a closed log.
Three items remain, each with real open work.

## 1. Material naming granularity — usability gap, real duplicates confirmed

**Symptom**: dozens of materials named `batch<N>_mat3_tex0_skin` for
varying `N`, hard to tell apart; ~23-25 of them look identical.

**Grounding**: `src/export_materials.cpp:234` creates one `gltf::Material`
per `.skin` batch unconditionally — no merge/dedup step exists anywhere in
`src/`. The name is built from `batch<index>_mat<materialIndex>` (:281),
then `_tex<textureIndex>` (:374), then `_<textureTypeName>` (:388), then an
optional filename/FileDataID suffix. Material identity, as far as husk
models it, is: `mat.blendMode` (:278), `mat.flags` (:279-280,
two-sided/unlit), and the resolved texture index/type (:359-393);
`M2Batch.shader_id` is deliberately not read at all (`src/skin.hpp:60`,
`src/skin.cpp:172`). Rebuilding a real export confirmed 28 batches all
named `..._mat3_tex0_skin`, all referencing the identical
`m2.materials[3]`/`m2.textures[0]` by construction — true duplicates in
every field husk currently models, differing only in which submesh/geoset
batch draws with them (e.g. separately-toggleable skin geosets sharing one
base material, which is exactly the kind of per-geoset selection
`M2_GAPS_TODO.md`'s now-closed `skinSectionId`-extras work was about).

**Verdict**: genuine usability gap, not a correctness bug — every one of
those materials is already correct, just redundant. Two independent
improvements, not mutually exclusive:

- **Dedup**: when two batches resolve to the same
  (`materialIndex`, `textureIndex`, `blendMode`, `flags`, `textureType`)
  tuple, emit one glTF material and have both primitives reference it,
  instead of N structurally-identical materials. Straightforward, and
  shrinks both the material list and the `.glb` JSON.
- **Naming**: even without dedup, `mat<materialIndex>_tex<textureIndex>`
  (dropping the per-batch `batch<N>` prefix, or moving it to `extras`
  rather than the display name) would already make the ~25 duplicates
  visibly identical instead of visibly distinct in Blender's material
  list.

Neither is implemented yet — this is a finding, not a fix.

**Confirmed directly in Blender, a later session, real interactive
inspection**: clicking a face-region primitive in the fixed
`bloodelffemale_hd.m2` full-auto export shows material name
`batch77_mat5_tex2_skin_bloodelffemale_hd_3255415` — the naming itself is
correct (batch/material/texture/type/resolved-filename all line up with
what that mesh part visually is). But **several other primitives, mapped
to visibly different body parts, carry the exact same
`mat5_tex2_skin_bloodelffemale_hd_3255415` suffix** — i.e. this dedup gap
isn't hypothetical or rare, it's real and directly observable by clicking
around a real export. This is the same root cause already described
above (identical `(materialIndex, textureIndex, blendMode, flags,
textureType)` tuples, one `gltf::Material` per batch, no merge step) —
not a new finding, but real confirmation that it manifests exactly as
predicted, plus a concrete real fixture/material name to reproduce it
against directly (`bloodelffemale_hd.m2`, full auto-resolution export,
material name above).

**A second session compounded this, worth understanding before touching
either area**: finding #3 below (`alternate_textures`) makes *every*
genuinely ambiguous hardcoded-slot material pick the same arbitrary
default candidate (deliberately — the whole shared pool is identical
across every ambiguous slot, since husk has no way to tell them apart).
That means batches which already dedup-collide on `(materialIndex,
textureIndex, blendMode, flags, textureType)` **now also collide on the
resolved texture bytes**, for a reason unrelated to the M2's own batch
data — they're not "the same material because the model author made
them so," they're "the same material because husk's ambiguous-texture
fallback can't do better than one shared guess." Untangling which
`mat5_tex2_skin_...` duplicates are *real* (the model genuinely draws
several geosets with one shared material, e.g. mutually-exclusive skin
options) versus *artifacts of the ambiguous-texture fallback* (every
hardcoded `skin`-type slot ends up structurally identical regardless of
which real skin-tone file is actually correct for each) is exactly the
kind of thing dedup work should account for, not paper over — the dedup
key may need to include something about *which* ambiguous-resolution path
was taken (e.g. `textureType` alone vs. `textureType` + "this was an
ambiguous fallback, not a real resolved id"), or ambiguous-slot materials
may need to stay unmerged specifically because merging them would hide
that they're independently unresolved, not confirm they're really
identical.

**For whoever picks this up next**: start from `src/export_materials.cpp`'s
`buildMaterialsAndPrimitives` (the per-batch material-construction loop) —
that's both where the dedup key would need to live and where the
ambiguous-default assignment happens (see finding #3's own "Root cause"
for the exact lines). Real repro fixture: `bloodelffemale_hd.m2` with its
real `wow_export`-style texture directory, default (auto) resolution, no
extra flags — `batch77_mat5_tex2_skin_bloodelffemale_hd_3255415` and its
siblings are the concrete case to check dedup logic against once it
exists.

## 2. `husk info`/`export` given a non-M2 file (e.g. a `.skin` directly) fails with a confusing byte-garbage-looking error, not a clean "wrong file type"

**Symptom**: `husk info bloodelffemale_hd_sdr02.skin` (a real `.skin` file
passed directly, not via `--skin`) fails with:
```
husk: couldn't read '...bloodelffemale_hd_sdr02.skin': chunk '+<0xc8>+<0xc9>'
at offset 22479 declares size 3408644651 but only 310969 bytes remain in
the buffer
```
which reads like memory corruption or a garbled file, not "wrong argument."

**Grounding**: traced byte-for-byte, not guessed at. `src/m2_primitives.cpp`'s
`resolveBlob` checks the first 4 bytes for `MD20` (pre-Legion M2); if that
fails, it unconditionally assumes the file is a Legion+ *chunked M2* and
calls `readChunks` (`src/chunk.cpp`) over the *entire* file looking for an
`MD21` chunk. A `.skin` file's own real magic is `SKIN` (`src/skin.cpp`'s
`parseHeader`, unrelated to M2's chunk system — `.skin` is a flat header,
never chunked), but `SKIN` + the next 4 bytes (`0xc7 0x57 0x00 0x00` =
22471) *happen to look like* a syntactically valid chunk tag+size pair to
`readChunks`, which knows nothing about `.skin`'s real layout. It
"successfully" consumes this fake chunk (8-byte header + 22,471-byte
payload = 22,479 bytes, landing exactly on the reported offset), then tries
to read the *next* chunk header at offset 22479 — which is really the
middle of the `.skin` file's own vertex/index array data, not a chunk at
all. `readChunks` reads 4 arbitrary bytes there as a "size" field (decoding
to the nonsensical 3,408,644,651) and throws. The friendly, already-existing
fallback message ("file doesn't start with MD20 and has no MD21 chunk")
never gets a chance to fire, because `readChunks` throws *while still
enumerating* chunks, before `resolveBlob` can check whether `MD21` was
among them.

**Verdict**: real usability gap, not data corruption or a `.skin`-parsing
bug — the `.skin` file itself is fine (confirmed: its real `SKIN` header
parses correctly via `src/skin.cpp`'s own dedicated, non-chunked parser).
This is `husk info`/`husk export`'s M2-loading path (`resolveBlob`) having
no cheap up-front "does this even look like an M2 file" check before
committing to a full chunk-enumeration pass — any non-M2 file whose first 8
bytes happen to decode as a plausible tag+size pair will produce this same
class of misleading, alarming-looking low-level `ChunkError` instead of a
clear "this isn't an M2 file" message. `husk info`/`export` only ever
documented `.m2` as their direct argument (`.skin`/`.skel` are meant to be
passed via `--skin`/`--skel`, `.bone`/`.phys` are the only sidecars
`dump-chunks` accepts directly) — so this was a real user mistake, but the
tool's failure mode actively obscures that instead of naming it.

**Action** (not yet implemented, no code changed this session): validate
the second 4-byte tag against `MD21` immediately after a failed `MD20`
check, before calling `readChunks` at all — or have `readChunks` bail out
with a distinct, clearly-worded error the first time a chunk's declared
size would run off the end of the buffer *and* look up the file's actual
first-4-bytes magic to report what was really found (e.g. "expected an M2
file (MD20/MD21), got a file starting with 'SKIN' -- did you mean to pass
this via --skin instead?"). Either fix is small; neither is implemented
yet — this is a finding, not a fix.

### DEVELOPER NOTE:

Fix by making it possible read just the skin file, and print the magic bits and all of the data we get access to from just that file, so the info tool can be used as an independent exploration tool, without having to always target the m2 file

## 3. `alternate_textures` (this session's own new feature, item 1 above) caused a real 1.9GB/5m39s export (fixed) — and every ambiguous material sharing one identical default texture (found, not yet fixed — see finding #1)

**Symptom**: `husk export` with full auto-resolution against a real
`bloodelffemale_hd.m2` + real CASC-export texture directory produced a
1.9GB `.glb` and took 5m39s, despite every individual source file being
under 200MB. Confirmed reproducible, not a one-off (same command, same
result). Reported alongside a second observation: many of the embedded
`alternate_textures` images "look like the same image, slightly different
sizes" when browsed in Blender.

**Root cause of the size/runtime blowup**: this real export has **19
separate materials**, each a genuinely ambiguous hardcoded texture slot
sharing the **same 94-file candidate pool** (item 1's own new
`alternate_textures` feature, added earlier this session). The
implementation embedded every candidate's full bytes independently *per
material*, with no cross-material sharing — so the same 94 files got
read, BLP-decoded, and embedded **19 times each** (1,786 total redundant
decode+embed operations for what should have been 94). This is a direct,
self-inflicted regression from that same-session feature, not a
pre-existing bug.

**Fixed two ways, matching the two real costs**:
1. `src/export_materials.cpp`: `buildMaterialsAndPrimitives` now caches
   ambiguous-candidate bytes by path (`ambiguousCandidateCache`), read and
   BLP-decoded once regardless of how many ambiguous materials reference
   the same file — fixes the CPU/runtime cost.
2. `src/gltf_mesh.cpp`: `emitMaterial` now takes a shared
   `alternateTextureCache` (filename → already-created glTF texture
   index), threaded through `emitMeshNode`/`writeGlbMulti`
   (`src/gltf.cpp`) so a candidate that's already been embedded as a real
   glTF image/texture gets *referenced* by every other material that
   shares it, not duplicated — fixes the actual output file size, which
   the read/decode cache alone wouldn't have touched (tinygltf's
   `images`/`textures` arrays would still have held 19 separate copies of
   identical bytes without this).

**Verified on the same real export**: **1.9GB → 104MB** (~18x smaller,
matching the ~19-material duplication factor almost exactly), **5m39s →
5.4s** (~63x faster). Full test suite green (519/519) throughout — no
existing test exercised more than one ambiguous material sharing a pool at
once, so this class of bug had no coverage; worth a real regression test
if this code is touched again (a synthetic fixture with 2+ ambiguous
materials sharing one candidate pool, asserting the `.glb`'s `images`
array has one entry per distinct filename, not one per material).

**The second observation — first pass wrongly cleared it, second pass
(prompted directly) found a real bug.** First check, on the
`alternate_textures` *candidate list itself*:
1. The real source `.blp` files are genuinely distinct — `md5sum` across
   60+ real candidate files in the CASC export directory found zero
   collisions.
2. husk's own embedded C++ decoder output was extracted directly from the
   fixed `.glb` (via a small `tinygltf`-linked scratch tool) and compared
   against the independent Python reference decoder (`blp/`'s `husk-blp`)
   for the same source file — **byte-identical visual result**, confirmed
   by eye. husk's decode is correct, at least for the file checked.
3. Two files sharing the same `skin_color`-prefixed naming convention
   turned out to be genuinely, wildly different images once actually
   viewed — one a small 256×128 ornate belt/tattoo-style detail, the other
   a large multi-thousand-pixel full skin/face/jewelry texture atlas. Real
   content, not a bug; the size difference (~40KB vs. ~550KB) is just
   real resolution difference.

That check was real but answered the wrong question — it verified the
*alternate candidates* are distinct, not what was actually being observed
in Blender. Told directly to look again with a sharper lead ("a plethora
of textures named `Image_<N>`, all the same 256×128 texture, pull a few
from Blender and inspect there") — a headless-Blender probe (pixel-hash,
not just dimensions, on every `bpy.data.images` entry) found the real
thing: **114 images, every one 256×128, every one pixel-identical**
(md5 of the first 4000 pixel components matched across all 114). That
count exactly matches this export's material count (114) — this is every
material's *default* `baseColorTexture`, not the `alternate_textures`
list checked above.

**Actual root cause**: every genuinely ambiguous material shares the
*same* candidate pool (by design — the pool isn't depleted for the
ambiguous case, so every equally-uninformed slot sees every real
candidate). The arbitrary-default logic picks "alphabetically first
candidate in the pool" — and since the pool is identical across every
ambiguous material, **every one of them picks the exact same file**
(confirmed: `bloodelffemale_hd_3255415.blp`, 256×128) as its default. Not
a decode bug, not a caching/aliasing bug — the dedup fix above is working
exactly as designed; the design itself produces one texture "winning" as
the default for every ambiguous slot in the whole export.

**This is the same failure shape this project already found and fixed
once before** (`batch<N>` fuzzy-match's first draft: "68 of 70 materials
all showing the same one texture... worse than embedding nothing since it
looks confidently wrong rather than honestly blank") — reintroduced here
in a new form by this session's own `alternate_textures` feature.

**Deliberately left unfixed this session, per direct instruction** — the
real question turned out to be bigger than "which arbitrary default to
pick": interactive inspection in Blender showed the *same* material
identity (`mat5_tex2_skin_bloodelffemale_hd_3255415`) legitimately
spanning several different mesh parts, which is finding #1's own
already-documented material-dedup gap, now compounded by this ambiguous-
default behavior. See finding #1's own follow-up for the full writeup and
the concrete next step — this note exists so anyone reading #6 in
isolation knows where the actual continuation lives, without re-deriving
the connection.

**Fixed, a later session**: `alternate_textures` used to attach the *entire*
undifferentiated 94-file candidate pool to *every* ambiguous material,
regardless of which hardcoded `textureType` that specific slot actually
is — a `char_eyes` slot's alternate list included `hair_color`/
`jewelry_color`/`face` files that could never be a valid answer for it,
structurally impossible, not just unlikely (concretely: a real face
`.blp` was showing up as a candidate for a shoes-region `skin`-type
batch's material, exactly the case Luna asked to have fixed).

`src/export_materials.cpp` now filters per-slot before a candidate is
ever offered: `classifyCandidateCategory` parses the real community-
listfile category token out of a candidate's own filename (e.g.
`"skin_color"` out of `bloodelffemale_hd_skin_color_3500123.blp` — this
is metadata a human already attached to the file, not husk guessing
anything about M2 internals), and `candidateCategoryTypes` maps that
token to the `M2Texture::type` values it's actually compatible with,
transcribed (not guessed) from `reference/wow.export`'s own character-
customization code (`tab_characters.js`'s legacy `skin`/`face`/
`hair color`/`hair style`/`facial` option-group map, and its explicit
"blindfold = type 9" comment). A candidate whose category names a
*different*, non-overlapping type is excluded from that slot's pool
entirely; an unrecognized token (e.g. a non-character model's plain
alternates) stays a wildcard, unchanged from before.

Types 1 (`skin`) and 8 (`skin_extra`) turned out to be a real, separate
structural fact once traced through `wow.export`'s own
`apply_skinned_model_textures`: the real client binds these two a
*composite* built by blending several layers (base skin tone + face +
others) at runtime, not one raw file — so "which one candidate is
correct" has no answer husk can ever produce without real
ChrModelTextureLayer/DB2 blend-order data it deliberately doesn't have
(`DESIGN.md`'s Non-goals). `"skin_color"`/`"face"`/bare files all stay
valid candidates for these two types (compositing needs all of them,
even though husk can't composite), but a bare or `skin_color` file is
now preferred as the wired default over a narrower overlay like `face`
(`preferBaseLayerCandidate`) — a full-body base skin tone is a much more
plausible stand-in than a small face-only overlay, even though neither
is the real composited answer. Each candidate's parsed category is also
now attached to its `alternate_textures` extras entry
(`AlternateTextureCandidate::category`), so a human or Blender script
can immediately see *what* each unlinked candidate is instead of an
opaque filename.

Verified against the real `bloodelffemale_hd.m2` + its real CASC-export
texture directory: the `skin`-type slot's pool shrank from 94 to 57
candidates (down to only skin/skin_extra-compatible files, default now
the bare base-layer file rather than an arbitrary alphabetical pick),
the `char_eyes` slot's pool shrank to its own 9 `eye_color_*` files
only, `char_jewelry` to its own 19 `jewelry_color`/`body_jewelry`/
`bracelets` files, and the `ui_skin` (blindfold) slot to its own 2
`blindfold_*` files — no cross-category leakage in any of them. Full
test suite green (520/520), including a new synthetic regression test
(`tests/test_cli.cpp`, two hardcoded slots of genuinely different
`M2Texture::type`s sharing one pool) proven to actually fail without the
fix (temporarily disabling the filter reproduces exactly this
cross-contamination, `alt.ArrayLen() == 4` instead of `2` on both
materials) before being confirmed fixed.

**Still open, by design, not by oversight**: within the `skin`/
`skin_extra` compositing types specifically, husk still can't tell
*which* `skin_color` file or *whether* `face` should really be layered
in for a given character's actual customization choices — that
remains genuinely unresolvable without real DB2 data, exactly as
described above. The fix here narrows "which candidates are even
offered" to a structurally-grounded set, same "filtering is safer than
picking" principle the original proposal below already settled on; it
does not and cannot pick the one correct composited look. The shader-
graph side of Luna's original ask (default texture linked, every other
same-slot candidate present as an unlinked node in the material graph)
is a Blender-import-script concern, not something `husk export` itself
builds — the per-candidate `category` field this session added is
exactly the data such a script would need to label those unlinked nodes
meaningfully, but no such script exists in this repo yet.

**Immediate follow-up, same session**: reported directly from Blender
after inspecting this fix's own output — every embedded image (both the
primary `baseColorTexture` and every `alternate_textures` candidate) was
showing up in Blender's image list as an auto-generated `Image_<N>`,
not its real filename, because `gltf_mesh.cpp` never set `tinygltf::
Image::name`/`Texture::name` at any of its three embedding sites. Fixed:
`Material::baseColorImageName` (new field) is populated at every
`export_materials.cpp` resolution site and used to name the primary
image/texture; `alternate_textures` candidates are named from their own
filename, `additionalTextureLayers` from their FileDataID (no filename
tracked there). Verified with a real headless-Blender import of the
fixed `bloodelffemale_hd.m2` export: 99 images, all previously
`Image_0`..`Image_98`, all now real names, 0 left generic.

**Real correction, a later session — the "prefer bare/skin_color as the
base layer" default logic above was wrong, not just imprecise.** Prompted
directly ("we REALLY need to get ridd of the 500 materials produced by
batches... only 1 material per mat<num>_tex<num>_<id> combination", plus
"the texture in those instances is identical" about repeated
`bloodelffemale_hd_body_jewelry_3602029.<N>`-suffixed images in Blender)
and a reference screenshot of what correctly-matched skin/hair/jewelry
should actually look like (tan skin, blue hair, silver jewelry/bracelet).
Two real, separate problems, both fixed this session:

1. **~500 materials from ~500 batches, one gltf::Material per batch with
   no reuse.** `src/export_materials.cpp` now computes a real content
   signature (`materialDedupKey`) for every fully-built `gltf::Material` —
   every field that isn't purely batch-numbering (blend/tint/texture *and*
   per-batch animation curves, since M2Color/M2TextureWeight combo indices
   are batch-level, not material-level, so two batches sharing
   (materialIndex, textureIndex) can still legitimately carry different
   tint/fade animation) — and reuses an existing material via
   `materialByKey` instead of creating a new one whenever a batch's built
   content exactly matches one already emitted. The stored material's name
   also drops the `batch<N>_` prefix once dedup applies, matching what was
   asked for directly (`mat<num>_tex<num>_<id>`). Verified on the real
   `bloodelffemale_hd.m2` export: 114 materials → 10. New regression test
   (`tests/test_cli.cpp`, two batches resolving to the exact same
   material), proven to fail without the fix (114→2 collapsed back to
   "2 == 1" REQUIRE failure when the dedup lookup was temporarily
   hard-disabled) before being confirmed passing.

2. **The repeated `body_jewelry_3602029.<N>`-suffixed images turned out to
   be the *same* root cause as #1**, not a separate texture-embedding bug —
   every one of those was a separate, content-identical `gltf::Material`
   (pre-dedup) independently embedding its own copy of the same image
   bytes; #1's dedup means there's only one material, hence only one
   embedded copy, to begin with. A second, smaller, genuinely separate case
   remained even after dedup: two *different* materials (different
   `textureType`, e.g. `char_hair` and `object_skin`) can still legitimately
   resolve to the exact same source file when both fall back to the same
   unrecognized-category wildcard candidate — `gltf_mesh.cpp`'s primary-
   image embedding now shares the same `alternateTextureCache` (filename →
   texture index) the `alternate_textures` candidates already used, so two
   materials embedding the identical file share one glTF image instead of
   two. Verified: 0 `.NNN`-suffixed duplicate image names left in Blender
   after both fixes, down from 1 residual case with only #1 applied.

3. **The "prefer bare/skin_color, unrecognized-category candidates stay a
   full wildcard" logic itself was wrong, caught by actually looking at
   the images `husk-blp` decodes them to, not just their file sizes/names.**
   `bloodelffemale_hd_3255415.blp` (the bare `<model>_<FileDataID>` file
   that kept winning the `skin`-type slot's default via alphabetical-first,
   then via the session-before-this's explicit "prefer bare over face"
   rule) turned out, viewed directly, to be a tiny, mostly-transparent
   sparkle/glint icon — nothing like a skin texture. The real full-body
   skin atlas (torso/ears/face combined, 1024×512, matching the reference
   screenshot's tan skin tone almost exactly) was sitting the whole time
   under the **recognized** `skin_color` category. Same story for
   `char_hair`: the unrecognized `eyereflect.blp` (a 128×128 pure-white eye-
   reflection sprite) was winning over the real, recognized `hair_color`
   candidates purely because `"eyereflect" < "hair_color"` alphabetically.

   Root cause: `candidateAllowedForType`'s bare-file handling was guessing
   what an unlabeled file *is* (assumed "the base skin/skin_extra layer")
   instead of just correctly excluding what a labeled file *isn't* for a
   given slot — the one guess this project's own established discipline
   (see finding #3's own "filtering is safer than picking" framing) had
   explicitly tried to avoid making, made anyway, and disproven by direct
   evidence. Fixed by removing the guess entirely: `filterCandidatesForType`
   now always prefers whatever's *recognized and compatible* for a slot's
   `textureType`, falling back to unlabeled/unrecognized candidates only
   when nothing recognized exists at all (the one case they're still
   needed — a non-character model with no category vocabulary in its
   texture directory). `orderCandidatesForDefault` (renamed from
   `preferBaseLayerCandidate`) keeps exactly one real, evidence-backed
   preference within the recognized set: `skin_color` (a real full-body
   atlas) ranks above `face` (a real but narrower, darker face-only
   variant) specifically for the two compositing types, both confirmed by
   viewing the actual decoded images side by side, not inferred from
   naming alone.

   Verified on the real `bloodelffemale_hd.m2` export: `mat5_tex2_skin`'s
   embedded default changed from the sparkle icon to
   `bloodelffemale_hd_skin_color_3500114` (average RGB (0.44, 0.27, 0.15),
   a real tan skin tone matching the reference screenshot), `mat0_tex1_
   char_hair`'s default changed from the white eye-reflection sprite to
   `bloodelffemale_hd_hair_color_4556603` (a real hair-strand texture).
   New regression test (`tests/test_cli.cpp`, ambiguous slot with a bare
   candidate *and* a recognized-category one), proven to fail without the
   fix (the bare file leaked back into `alternate_textures` when the
   two-tier recognized/fallback split was temporarily disabled) before
   being confirmed passing. Full suite green throughout, 522/522.

**A second, more specific correction, prompted directly by real Blender
verification written up in `LUNA_FINDINGS.md`** (not `LUNA_NOTES.md` --
misread as the latter at first when asked to "read LUNA_NOTES.md and
start investigating", corrected directly: "You should have asked me when
it didn't have findings, i could have pointed out that i fucked up the
naming, it's LUNA_FINDINGS.md"). That file confirmed the material-dedup
fix above directly (batches that should merge, by exact name) and named
the `char_hair`/`eyereflect` bug above independently too, but added one
more real, specific fact this session hadn't found on its own: for
`bloodelffemale_hd`'s one real `char_jewelry` (type 20) material,
**only** `jewelry_color_3613861`/`_3613862` are correct — not
`body_jewelry_3602029`, which `candidateCategoryTypes` had also mapped to
type 20 on the (English-name) assumption that anything "jewelry" belongs
to the same slot. Viewed directly (`husk-blp`): `jewelry_color_3613861`/
`_3613862` are a matched gold/silver collar-and-gem pair (real color
variants of one design), while `body_jewelry_3602029` is a visually
distinct necklace-chain item and `bracelets_3613863` a wrist band --
neither is a color variant of the jewelry_color design. First fixed by
removing both from `candidateCategoryTypes` entirely (leaving them
unclassified rather than reassigning them without evidence) -- correct as
far as it went (excludes them from `char_jewelry`), but incomplete: told
directly, immediately after, *why* they're different -- `body_jewelry`/
`bracelets` are flat texture overlays meant to be composited onto the
skin texture itself (no UV map of their own), the same family as
`skin_color`/`face`, while `jewelry_color` textures a genuinely separate
3D jewelry mesh with its own UV map. Corrected again: both now map to
types 1/8 (skin/skin_extra) alongside `skin_color`/`face`, not left
unclassified. Verified on the real export: the `char_jewelry` material's
`alternate_textures` lists only the two `jewelry_color` files (matching
`LUNA_FINDINGS.md`'s "the *only* valid options"), while the `skin`-type
material's own candidate count grew back to include `body_jewelry`/
`bracelets` as real compositable overlay candidates instead of losing
them entirely. New regression test (`tests/test_cli.cpp`), proven to
fail without the type-20 exclusion fix (temporarily re-adding
`body_jewelry`/`bracelets` to the type-20 mapping reproduced
`alt.ArrayLen() == 4` instead of `2`) before being confirmed passing.
Full suite green, 523/523.

**A fourth correction, same investigation, prompted by Luna trying to
manually locate `bloodelffemale_hd_skin_color_3500121` in Blender and not
being able to make sense of where it fit**: she described `3500123` as
"the 'base' skin color that gets rendered under armors... the whole
character + face + face jewelry" (matching this session's own earlier
`husk-blp` inspection exactly) and `3500121` as "just the body, with the
underwear... but it has completely different uv layout" -- a real, second
kind of full-body atlas (nude/underwear vs. worn-under-armor), not an
overlay. Investigating turned up a *third* problem, not just an answer to
"where does 3500121 go": `bloodelffemale_hd`'s twelve real
`skin_color_350011X`/`350012X` files split into two starkly different
size classes when actually decoded -- eight of them (`3500114`-`3500121`)
are 256x128 small strap/underwear-detail decals, not full atlases at all
(one directly inspected: a tiny bra-strap graphic on a mostly-transparent
background), while the other four (`3500122`-`3500125`) are the real
1024x512 full-body atlases, matched skin-tone color variants of one
design. `orderCandidatesForDefault`'s "prefer `skin_color`" rule
(previous entry) picked *whichever* `skin_color` file sorted
alphabetically first among all of these -- `3500114`, one of the tiny
decals, not a real atlas at all.

Fixed with a new, more fundamental signal: `pngPixelArea` reads a
candidate's real width×height straight out of its already-decoded PNG's
IHDR chunk (no extra decode pass), and `orderCandidatesForDefault` now
ranks by that first -- largest wins -- falling back to the `skin_color`
category preference only as a tiebreak among same-area candidates (needed
because a same-resolution overlay like `body_jewelry_3602029`, itself a
correct 1024x512 candidate for this slot, would otherwise tie with the
real atlas on size alone). This replaces the purely category-based
ranking the previous entry added -- category alone was never sufficient,
since the very finding that motivated it (a face texture being offered to
a shoes-region skin material) turned out to be one instance of a broader
fact: even one recognized category can contain assets of completely
different kinds and scales. Verified on the real export: the `skin`
material's default changed from the tiny `3500114` decal to
`skin_color_3500122`, a real 1024x512 full-body atlas, matching the class
of asset Luna described as correct. Two new regression tests
(`tests/test_cli.cpp`, using a new `solidColorPng` PNG-of-arbitrary-size
fixture generator, `tests/test_cli_fixtures.hpp`, since every prior
fixture used one fixed 1x1 literal), each proven to fail independently
when its own signal (area, then category-tiebreak) was temporarily
disabled. A real performance regression was caught and fixed in the same
pass before it shipped: the first working version read every candidate's
bytes into a *function-local* cache to check its size, meaning every one
of the ~27 batches sharing this slot re-decoded the same ~60 `.blp`
files from scratch just to sort them -- the same "1786 redundant decodes"
shape this project already found and fixed once before (finding #3) --
fixed by sharing `buildMaterialsAndPrimitives`'s own
`ambiguousCandidateCache` into `orderCandidatesForDefault` instead of a
fresh local one; real export time went from >120s (timed out) back down
to ~4.6s. Full suite green, 524/524.

**Resolved, with a screenshot -- and a real mechanism found, not just an
answer to one file.** The "these are just small decals, not skin
content" read on the small `skin_color` files above was itself wrong,
shown directly: `bloodelffemale_hd_skin_color_3500119`'s content
pixel-matches one specific rectangular region of `_3500123` (the real
base atlas) exactly -- a non-transparent overlay meant to sit precisely
on top of that one sub-region (a chest strap), not an unrelated or junk
asset. A second screenshot confirmed `_3500115` the same way, aligned to
a *different* region of the same base atlas. These are real, deliberately
composited **patches**, each keyed to one specific placement rectangle on
the base atlas -- Luna's own framing: "worth investigating how this is
mapped originally."

Investigated directly in `reference/wow.export`
(`src/js/3D/renderers/CharMaterialRenderer.js:114-118`, and
`src/js/db/caches/DBCharacterCustomization.js:203-215`): the real client
mechanism is exactly this, driven by three real DB2 tables --
`ChrModelMaterial` (the base atlas's own Width/Height), 
`CharComponentTextureSections` (`SectionType`, `X`, `Y`, `Width`,
`Height` -- the literal placement rectangle each patch composites into),
and `ChrModelTextureLayer` (`BlendMode`, which section/target a given
texture-type layer uses). This is real, confirmed, DB2-driven placement
data -- and squarely CASC/DB2 data husk has no access to and never will,
by design (`DESIGN.md`'s Non-goals). Not a dead end from guessing; a
confirmed, named mechanism husk structurally cannot reach.

**What husk can still do, and now does**: `AlternateTextureCandidate`
gained real `width`/`height` fields (`src/gltf_mesh.hpp`, populated by
the new `pngDimensions` in `src/export_materials.cpp`, emitted as
`alternate_textures[].width`/`.height` extras, `src/gltf_mesh.cpp`) --
not the real placement rectangle (husk has no `CharComponentTextureSections`
data to source one from), but real, decoded, load-bearing data
(`orderCandidatesForDefault`'s own ranking already depends on it) that at
least saves a human from decoding each candidate by hand to tell a full
atlas apart from a small region-patch, the exact manual cross-referencing
work that surfaced this whole finding. Verified on the real export: 71
`alternate_textures` entries at 512x512, 20 at 256x256, 13 at 1024x512, 10
at 128x128 -- a real, visible size spread a Blender script or human can
now filter/sort by directly from the glTF extras, without husk-blp.

**Real DB2 access confirmed in scope, not a dead end after all**: asked
directly whether these DB2 tables exist in Luna's own real local
`casc-tool` export (**not** `reference/wow.export`, the untrustworthy
third-party JS tool checked out elsewhere in this repo for source-code
reference only -- corrected directly after conflating the two similarly-
named things), given the actual non-goal is "no CASC *tool* dependency,"
not "no DB2 data ever" -- they do
(`/media/luna/data/wow_export/dbfilesclient/chrmodelmaterial.db2` etc.,
confirmed `WDC5` format by header, plus the full customization-choice
chain needed to resolve *which* file goes in a given slot for a specific
character). This reopens the compositing problem this whole thread has
been working around rather than solving. Full plan, staged, written up in
`TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` rather than here -- goal (Luna,
directly): a real compositing pipeline, plus Blender-side tooling that
lets a human pick from the real, correctly-UV-placed candidate options
per slot. Not started in `src/` yet.

**Genuinely still open, found while investigating, not yet fixed**: several
of the real, deterministically-resolved slots (`M2Texture::type == 0`,
textures 10–12 in `bloodelffemale_hd.m2` — real FileDataIDs `3536810`/
`4530998`/`5210137`) have **no local file at all** in the real
`/media/luna/data/wow_export` export directory under either their exact
`<FileDataID>.blp` name or the model's own basename convention — these
aren't customization slots, they're supposed to be simple, concrete
assets (e.g. a specific cloak/necklace mesh's own texture), so falling
back to the same ambiguous same-basename pool as the hardcoded slots is
questionable: since none of that pool's *recognized* categories are ever
compatible with `textureType == 0` (`candidateCategoryTypes` has no
entry mapping to it), these three slots always land in the unrecognized-
fallback tier and pick the same sparkle-icon default as before this
session's fix, for a different reason this time — genuinely missing local
data, not a resolution bug. Whether these three FileDataIDs are simply
absent from this particular local export, or live under a completely
different (non-basename-matching) filename this project's fuzzy pool was
never designed to search for, is unconfirmed — flagged for whoever picks
this up next rather than guessed at.

**Root cause found, this session — these are eye-glow textures, not
armor/cloak assets, and their absence from the local export is expected,
not a resolution bug.** Investigated by tracing which real `.skin` batches
actually draw with materials 9/10/11 (the three `textureType == 0` slots,
`mat9_tex10_fdid3536810`/`mat10_tex11_fdid4530998`/`mat11_tex12_fdid5210137`
in a real `husk export --textures none` of `bloodelffemale_hd.m2`) and what
geoset IDs those batches' primitives carry in their existing
`skinSectionId` extras (`M2_GAPS_TODO.md`'s now-closed geoset-extras work):
materials 9 and 10 both draw geoset group 17 (`geoset_id` 1701-1705),
material 11 draws geoset group 51 (`geoset_id` 5101-5103). Cross-checked
against `reference/wow.export`'s own tables, not guessed: `GeosetMapper.js`
maps geoset group 1700 to `'Eyeglow'` directly, and
`DBItemGeosets.js` names group 51 `EYE_GLOW_B` (a second glow channel).
Blood elves are the one playable race whose base model always renders a
glowing-eyes effect — this lines up exactly. The three materials' own
blend modes from `husk info` (`material 9: blend_mode=4`,
`material 10: blend_mode=7`, both wowdev.wiki's additive-family blend
modes — `Add`/`BlendAdd` — the conventional choice for a glow effect; only
`material 11: blend_mode=2`, `Alpha`, differs) are consistent with this
too, though husk has no decode table for blend-mode names yet so this part
leans on external wowdev.wiki knowledge, not something verified in-repo.

This also explains the missing-local-file symptom directly: eye-glow
textures are small, race-specific effect assets that real CASC exports
typically don't colocate with the base character model files at all (they
tend to live under a shared effects/spell-texture path, not
`character/bloodelf/female/`) — confirmed at least negatively for this
export: a search of the entire local `/media/luna/data/wow_export` tree
for any of the three FileDataIDs (`3536810`/`4530998`/`5210137`) by
filename found zero matches anywhere, not just under the model's own
directory. So this isn't a case of husk's basename-fuzzy-pool searching in
the wrong place next to the model — the files simply weren't pulled into
this particular local export at all, consistent with a real, separate
CASC extraction gap upstream of husk, not a husk resolution bug.

**Not investigated further, since it's out of scope by this project's own
design**: whether/how to fall back to a plain unlit-emissive placeholder
(Luna's own "transparent shader" suggestion) when a `textureType == 0`
slot's FileDataID has no local file at all — glTF's core material model
has no direct analogue for WoW's additive eye-glow blend mode, and picking
a plausible placeholder color/opacity without the real texture would be
exactly the kind of confident-but-wrong guess `candidateAllowedForType`'s
earlier bare-file mistake, and finding #3's own "filtering is safer than
picking" framing, both already argued against. If this is picked up, the
grounded next step is probably surfacing `blendMode`/`unlit` flags as glTF
material extras for every material (not just these three) so a Blender
script can apply its own emissive/additive shader per-slot, rather than
husk guessing at rendering — but that's a new, separate proposal, not
implemented or scoped further here.
