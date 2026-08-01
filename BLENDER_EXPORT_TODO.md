# TODO: real-eyes-on Blender import findings (2026-08-01)

Source: Luna's own interactive Blender pass over real `husk export` output
(`bloodelffemale.m2` and `bloodelffemale_hd.m2`), notes dropped in
`LUNA_NOTES.md`'s "New notes: 01.08.2026" section. This is exactly the gap
the previous session's own closing note flagged and deferred: every
Blender-side check this project has ever run (`test_conformance.cpp`'s
headless-Blender tier) verifies structure/counts, never what an export
*looks like* to a human in Blender's own GUI — this was the first real
interactive pass, and it found several real things headless checks
structurally cannot catch.

This file turns those raw notes into grounded, prioritized investigations.
Every item below was checked against real code and/or a real re-export this
session (not just restated from the notes) — see each item's own Grounding
paragraph.

**Update, same session, after Luna went to sleep**: she asked directly for
every item that didn't need her input to be implemented while she was away
("Implement items that do not need my input... have fun tinkering"), plus
one concrete new idea (material naming should attempt string-matching
against real texture filenames, not just FileDataIDs, since real texture
directories are sometimes named descriptively rather than by FileDataID).
§3 (`--collision none`), §5 (texture-type-based material naming), §6
(bone/joint node naming via `keyBoneId`), and §4 (a real filename-matching
fallback for hardcoded texture slots, per her own idea) are now
**implemented, tested, and verified against real exports** — see each
section's own "Implemented" subsection. §1 was investigated and closed as
**not a bug** — a real, decisive finding, but the opposite of what this
document originally hypothesized. §7/§8 were investigated as far as
possible without a human actually looking at Blender's viewport, and §8
turned into **the single most important finding of this whole session** —
see its own section before doing anything else with this file.

The older top-of-file notes in `LUNA_NOTES.md` (PM4/PD4 "100% included but
hidden" framing, `MCSE`/`MCSH`) are **not** covered here — both are already
fully absorbed into `PM4_PD4_TODO.md` and `WORLD_MISC_METADATA_TODO.md`
respectively (checked this session; `PM4_PD4_TODO.md` already has its own
"Open design question: does 'hidden by default' actually mean anything
here?" section discussing `KHR_node_visibility`, and `WORLD_MISC_METADATA_TODO.md`
already promoted both `MCSE`/`MCSH` to real `extras` rows). Those notes
predate — and are superseded by — that later work.

## Priority order (updated after this session's implementation pass)

0. **§8 (model/skeleton "upside down") — read this first.** Empirically
   confirmed real via headless Blender, not a Blender-import-path artifact
   as originally guessed. A verified fix exists (tested, then reverted —
   **deliberately not shipped**, see the section for why) but it touches
   every position husk exports and likely needs the rotation/scale
   conversion re-derived alongside it, not just a one-line sign flip. Needs
   Luna's own review before anything ships.
1. ~~Vertex→bone assignment (§1)~~ — investigated, **closed as not a bug**:
   real data confirms husk's current approach is already correct. See §1's
   own "Real-data investigation" subsection.
2. Missing base-body mesh (§2) — §1 (the original leading hypothesis) is
   closed; §8 is now the more likely explanation, but unconfirmed until
   §8 itself is resolved and someone looks again.
3. ~~Collision mesh blocking visibility (§3)~~ — **implemented**: `--collision
   none`.
4. ~~No embedded textures (§4)~~ — **implemented**: a real basename-matching
   fallback (Luna's own idea) for hardcoded slots, plus an exact-match path
   for old-format embedded filenames.
5. ~~Material naming (§5)~~ — **implemented**: `m2::textureTypeName` +
   real-filename-based names.
6. ~~Node naming (§6)~~ — **implemented**: `m2::keyBoneName` + `bone_<index>`
   fallback.
7. Bone tail direction (§7) — investigated as far as possible headlessly;
   very likely a genuine Blender-importer limitation (see its own
   section's real evidence), not something husk's export can fix, but not
   100% certain without a human actually looking at the viewport.
8. Blender import-path guidance (§9) — still pure documentation, blocked on
   §8 having a final disposition first (its own recommended path may
   change once §8 is resolved).

---

## 1. Vertex→bone assignment may be reading the wrong indirection entirely

**Symptom** (Luna's notes): "some vertices are parented to wrong bones, ex
a vertex in boot is parented to other leg's bone."

**Grounding**: `documentation/wowdev-wiki/md/M2/.skin.md`'s own "Bones"
section (the `.skin` file's own `M2Array<ubyte4> bones` array, header
offset — `src/skin.cpp`'s `bones = 0x14`) is **three-way disputed by its
own wiki editors**, quoted here in full since this is the crux of the
whole investigation:

> *It seems to be an index into actual bones struct, not the lookup table
> -- Skarn*
>
> *An index into the bone list would make more sense, than into the bone
> lookup table (boneCombos). Vertex bone weights point to that list too.
> -- Nieriel*
>
> *Seems like an index here points to bone_lookup_table with offset =
> Submesh.boneComboIndex. I.e.
> bone_lookup_table\[submesh.boneComboIndex + Bones\[i\]\[j\]\] ==
> M2Vertices\[SkinVertices\[i\]\].bone_indices\[j\] -- Vovangrat*

Vovangrat's remark is the most specific (an exact formula, not a guess),
and is backed by a real SGSLib code excerpt on the same page:

```
override_vertices[vertIndex].bone_indices[boneInd] =
  m_data->bone_lookup_table.data[subMesh->StartBones + skinFile->properties.data[4*vertIndex + boneInd];
...
m_data->bone_lookup_table.data[i] = i;   // default: identity
```

That last line matters: when `bone_lookup_table` is the identity mapping
(seemingly the common/default case), reading `M2Vertex.bone_indices`
directly is equivalent to going through the indirection — which would
explain why husk's current approach "mostly works" and isn't an obvious,
always-reproducing crash. It would only diverge on files/submeshes where
the lookup table is *not* identity, which is exactly a "some vertices,
some files" bug shape, not "everything is wrong."

**Current husk behavior, confirmed by reading the code directly this
session**: `src/cmd_export.cpp:688` reads `v.boneIndices[j]` (an
`m2::Vertex`, the *global* M2 vertex — `src/m2.hpp`) completely raw and
uses it straight as a glTF joint index, after only a bounds check
(`< boneCount`). Three things that would be needed for Vovangrat's
indirection are parsed but **never dereferenced anywhere**:

- `Header::boneCombos` (`src/m2.hpp:113`, the M2-level "bone lookup table")
  — parsed as an `Array` descriptor (`src/m2.cpp:186`) and never read
  again. `grep -rn boneCombos src/` turns up exactly those two lines, plus
  the struct field itself.
- `.skin`'s own `submesh.boneComboIndex`/`boneCount` fields (`M2SkinSection`,
  per `.skin.md`) — need to check whether `src/skin.hpp`/`skin.cpp` even
  parse these two fields today (not confirmed this session; check before
  starting).
- `.skin`'s own separate `bones` (`ubyte4`) array — `src/skin.cpp`'s
  `bones = 0x14` is an offset constant only, never used to read the array's
  actual contents.

**Why this would also explain the boot/leg symptom specifically**: a
wrong-by-a-fixed-offset bone index (raw index used where
`boneCombos[submesh.boneComboIndex + raw]` was needed) doesn't produce
garbage — it produces a *plausible, in-range, wrong* bone, exactly the
"attached to the other leg" shape rather than a crash or an obviously
nonsensical result.

**Investigation plan** (do not implement blind — this needs real-file
verification, same discipline every other husk finding in this repo uses):

1. Confirm what `src/skin.hpp`/`skin.cpp` currently parse from
   `M2SkinSection` — specifically whether `boneComboIndex`/`boneCount` are
   already available in the parsed struct or need adding.
2. Write an independent scratch decoder (Python, not reusing husk's own
   parser — same precedent as every prior from-scratch verification in
   this repo) that resolves vertex bone indices *both* ways —
   raw-M2Vertex.bone_indices, and
   `boneCombos[submesh.boneComboIndex + skin.bones[i][j]]` — for the real
   `bloodelffemale00.skin`/`.m2` pair already in `test_data/`.
3. Diff the two resolutions per vertex. If identical everywhere, the
   lookup table on this fixture happens to be identity and this file is a
   real negative result (document and close, same as any other "checked,
   turned out fine" finding this project has). If they diverge, spot-check
   a handful of divergent vertices against real bone *positions* (does the
   Vovangrat-resolved bone sit anatomically closer to that vertex than the
   raw one? — e.g., is a boot-region vertex's resolved bone actually a
   leg/foot bone under one interpretation and something implausible like a
   spine or opposite-side bone under the other?).
4. Only implement the fix once step 3 gives a real, file-grounded answer —
   this is exactly the kind of "wiki editors disagree, don't guess" case
   `WIKI_FINDINGS.md`'s whole methodology exists for.

### Resolution: closed, not a bug — husk's current approach is already correct

Ran exactly the investigation plan above, this session, against the real
`bloodelffemale00.skin`/`bloodelffemale.m2` pair. Result: **decisive, and
the opposite of the leading hypothesis.**

An independent from-scratch Python decoder (not reusing husk's own parser)
parsed `.skin`'s own `bones` array, `M2SkinSection.boneComboIndex`, and the
M2 header's `boneCombos` table, then computed Vovangrat's own formula —
`boneCombos[submesh.boneComboIndex + skin.bones[i][j]]` — for every
weighted bone slot (skipping zero-weight slots, which carry no real bone
assignment) across all 70 real submeshes: **9,143 of 9,143 (100%) matched
`M2Vertex.bone_indices` read completely raw**, exactly what husk's current
code already does. Zero divergences, not "mostly matches with some real
exceptions."

The wiki dispute is now resolved with real bytes: **Skarn and Nieriel were
right** — `M2Vertex.bone_indices` is a direct index into the global bones
array, no indirection needed. Vovangrat's formula isn't wrong either — it
describes how to reconstruct that *same* value starting from `.skin`'s own
separate, redundant `bones` array (confirmed structurally distinct: only
5,101 of 8,061 local vertices have `skin.bones[i] ==
M2Vertex[global[i]].bone_indices` *before* applying the formula — i.e. the
two arrays really do differ raw, and only Vovangrat's resolution brings
them back into agreement) — but husk never reads that array at all, and
doesn't need to, since it already reads the correct field directly.

**This finding was already sitting in the codebase, undiscovered until this
session went looking**: `src/cmd_export.cpp`'s `buildSkinning` has its own
doc comment stating exactly this ("M2Vertex.bone_indices are direct indices
into the M2's `bones` array, confirmed against pywowlib's M2 writer") —
this session's own from-scratch verification is an independent
*second* confirmation, via a different method (real-byte cross-check
against `.skin`'s own arrays) and a different real fixture, not a repeat of
the same check. Worth recording as a process lesson: a `grep` for existing
doc comments near the code in question, before writing up a new
investigation, would have surfaced this in seconds — the investigation
still turned out to be worthwhile (an independent, stronger confirmation
than "one line in a comment"), but it didn't need to start from zero.

**No code change.** `src/cmd_export.cpp`'s vertex→joint mapping is correct
as written. §2's "body missing" symptom needs a different explanation now
— see §2 and §8 below.

## 2. Base body mesh appears to be missing while hair/ears/armor pieces show

**Symptom**: "the skin areas'/base mesh seem to be missing... the body
itself, that's supposed to sit underneath that is missing," while
hair/ears/armor extras (usually gated by in-game item selection, exported
unconditionally by husk) are visible and coherent.

**Grounding, this session**: re-ran `husk export` against the real
`test_data/bloodelffemale.m2`/`bloodelffemale00.skin` pair. The body
geoset **is** present in the output: `husk`'s own summary note lists 66
distinct `skinSectionId` values including `0` (WoW's own base-body geoset
convention), and the final tally is `8061 vertices, 10458 triangles, 119
bones... 70 materials (0 with an embedded texture)`. So husk is not
silently dropping the body submesh at the parsing/export level — whatever
Luna saw in Blender is either a rendering/material artifact or a geometry
*placement* artifact, not a missing-data artifact.

**Original hypothesis (this session's first pass): a symptom of §1.**
Ruled out — §1 investigated and closed as not a bug (see §1's own
Resolution subsection): husk's vertex→bone mapping is confirmed correct
against real data, 9,143/9,143 weighted slots, so a shattered/misassigned
mesh isn't the explanation here.

**New leading hypothesis, this session's second pass: a symptom of §8.**
§8 ("model and skeleton are upside down") is now confirmed real via
headless Blender — the whole mesh sits with its bounding box entirely on
the *wrong side* of the origin (spanning roughly -2 to 0 instead of the
expected 0 to +2 for a standing character). A body mesh skinned to a
skeleton whose bind pose is effectively inverted wouldn't just look
"wrong" — depending on exactly how the inversion interacts with the
animated pose Luna was viewing, it's plausible for the body to end up
folded, inverted, or positioned somewhere the eye doesn't expect a "body"
to be (e.g. compressed near the floor, or projecting through/behind
other geometry) while simpler, more rigid pieces (hair, which mostly
follows the head bone as a near-rigid unit) stay visually coherent even
under the same transform, just displaced along with everything else. This
is still a hypothesis, not confirmed — record it here so whoever picks
this up after §8 is resolved checks it directly, but don't treat it as
settled without actually looking again in Blender.

**Investigation plan**: re-check this visually in Blender only *after* §8
has a real disposition (its own fix, once reviewed and applied, changes
every exported position — re-testing before that lands would just be
re-diagnosing the same root cause twice). If the body is still missing
once §8's fix (or an accepted alternative) is in place, this needs its own
investigation — start with: is the body's specific `skinSectionId`'s
material assigned a blend mode/alpha that could render it invisible
(`husk info`'s per-material `blend_mode`/`flags` output makes this a cheap
first check), and whether the "3 batches with animated tint/fade" note in
husk's own export summary happens to include the body batch with a
static-default alpha of 0.

## 3. Collision mesh blocking visibility during debugging

**Symptom**: "1 that would massively help debuggability... make the
collision export conditional on a CLI flag" — Luna's own explicit ask, not
just an observation.

**Grounding**: confirmed in `src/cmd_export.cpp` (~line 2109 onward): the
collision mesh is currently **always** exported when present, tagged
`{"collision": true}` in that node's glTF `extras`
(`gltf::NamedMesh::isCollision`) — but Blender's stock glTF importer has no
built-in concept of that tag, so it renders like any other mesh, exactly
as Luna observed. This is the same gap the previous session's own commit
(`9105dbc`, "Note deferred Blender hide-script tooling...") already
flagged and explicitly deferred — Luna's note here is the "actual
interactive use" trigger that commit said this was waiting for.

**This is a small, well-scoped, no-real-unknowns implementation task**,
unlike §1/§2/§7/§8: add a three-state `husk export --collision
[auto|none]` flag (or similar — same shape as `--phys`'s own
three-state pattern, `DESIGN.md`'s CLI grammar table) defaulting to
today's unconditional-when-present behavior, with `none` skipping the
collision mesh entirely. A companion Blender hide-script (per the deferred
note) is a separate, independent piece of usability tooling and doesn't
block this CLI flag.

### Implemented

`husk export --collision none` (a two-state flag, not three -- there's no
path to give, collision data is inline in the `.m2` itself, not a sidecar
file; any value other than `none` is rejected with a clear message via
CLI11's own `->check()`, same mechanism `--skin`'s `none`-rejection
already uses). Unset (the default): unchanged, existing behavior. Wired
into `src/commands.hpp`'s `ExportOptions`/`addExportOptions`, gating the
existing collision-mesh block in `cmd_export.cpp` right at its own
`if (header.collisionPositions.count > 0 && ...)` condition. Completions
(`src/main.cpp`'s `bashValueCompletion`/`zshValueAction`/`zshFlagLabel`
tables, plus a new `_husk_none_only_value` zsh helper for the
"only `none`, no file/dir fallback" shape none of `--skel`/`--phys`/
`--textures` needed) regenerated and syntax-checked. Three new
`tests/test_cli.cpp` cases (default-attaches, `--collision none`-omits,
invalid-value-rejected) using a new `tinyValidM2WithCollision()` fixture
(one real triangle's worth of collision data appended to the existing
`tinyValidM2()` shape). Verified against a real export
(`bloodelffemale.m2`): default still prints the "attached a
8-position/12-triangle collision mesh" note; `--collision none` produces
byte-for-byte the same vertex/triangle/bone/animation/material counts with
that note absent. Full suite green (474/474 + 1 skip at the time this
landed).

## 4. No embedded textures reaching Blender (materials are bare stubs)

**Symptom**: "anything with a texture is just principled bsdf stub with no
texture and nothing... potentially to do with file paths."

Luna pushed back on this item's first draft directly: geosets are
"provide 100% of the data, let Blender/the human hide what they don't
want" (husk's own established, working pattern — every geoset is exported
unfiltered by design). Shouldn't textures work the same way — export every
candidate variant, reference them from the materials, let the wrong ones
get hidden rather than exported blank? Worth answering for real, not
restating the first draft's "external DB2 data, non-goal" claim more
firmly — checked against real source this time, not just `m2::Texture`'s
own doc comment.

**Grounding, this session, extended**: the real re-export confirms `0
materials with an embedded texture` for `bloodelffemale.m2`, and `husk
info`'s `texture_file_data_ids` line (`0, 0, 0, 0, 0, 0, 0, 1034713,
220043`) confirms 7 of 9 texture slots carry no FileDataID in the file at
all. The geoset comparison is the right question to ask, and it exposes a
real **structural** difference, not just a scope choice:

- **Geosets are self-contained data.** Every geoset's actual triangle data
  sits directly in the `.skin` file husk already reads — "export all,
  filter later" only requires reading data that's already there.
- **Hardcoded/customization-driven texture slots (`type != 0`) have no
  candidate data anywhere in the M2/`.skin` container at all — not a
  wrong one, not a placeholder, nothing.** Confirmed two independent ways
  against `reference/wow.export`'s real source this session (this repo
  already has a clone for exactly this kind of comparison, per
  `TOOL_COMPARISON.md`):
  - Player-customization types (`Skin`, `CharHair`, ...):
    `src/js/db/caches/DBCharacterCustomization.js` resolves a texture
    through a genuine ~9-table relational join —
    `ChrRaceXChrModel` → `ChrModel.CharComponentTextureLayoutID` →
    `ChrCustomizationOption`/`Choice`/`Element` →
    `ChrCustomizationMaterial` → `TextureFileData` → FileDataID — read in
    full this session. No naming convention, no shortcut; every step
    needs a live DB2 table husk doesn't read, by design (`DESIGN.md`
    non-goals). This is a deeper chain than `.bone`-slot-selection's
    already-documented DB2 gap (`TODO_correctness.md`), not the same
    thing restated.
  - Creature types (`Monster1`/`2`/`3`, `type` 11–13): a *different* but
    equally external table, `CreatureDisplayInfo.TextureVariationFileDataID`
    (`src/js/3D/renderers/M2RendererGL.js:1723-1729`,
    `applyReplaceableTextures`). Checked specifically because it's a
    simpler mechanism than player customization — still fully external,
    same boundary.
  - **Even wow.export, which *has* live DB2/CASC access, doesn't derive
    "the" correct texture from the M2 file alone.** It queries
    `CreatureDisplayInfo` for every display variant that references this
    model (`tab_models.js`'s `get_model_displays`), builds a picklist, and
    auto-selects the *first* entry as a default
    (`modelViewerSkinsSelection = skin_list.slice(0, 1)`) — i.e. even with
    full DB2 access, "which skin is correct" is fundamentally a choice
    from an externally-enumerated list, not something recoverable from the
    M2 container itself. husk can't build that list at all (no DB2), so it
    can't even offer the "first of N" fallback wow.export uses.

So the geoset pattern doesn't transfer here, but for a structural reason,
not a scope decision — there is no "all the data, hide the wrong ones" to
provide, because the M2/`.skin` files never had that data to begin with.

**A real, buildable middle ground that *does* match the spirit of Luna's
ask, staying inside husk's no-CASC boundary**: today `--textures` only
matches `<FileDataID>.png` — useless for a `type != 0` slot, since its
FileDataID is always `0`. A **`type_<N>.png` (or per-model
`<basename>_type<N>.png`) override convention** in the same directory
would let a user who *does* have a candidate texture — pulled once via a
separate DB2-aware tool, hand-picked, or even just "a plausible skin tone
so the mesh isn't bare" — drop it in and have husk wire it into that
material, exactly like a resolved FileDataID does today. husk still never
touches CASC/DB2 itself (the file has to already exist locally, same
`--textures` contract as every other resolved texture); it just stops
discarding a real file the user *can* supply, purely because the M2 itself
has no ID to key it by. This is worth scoping as a real follow-up, not
implemented blind — needs a design pass on the exact key convention
(per-slot `type` alone collides across models that reuse the same type for
different purposes; per-model-basename keying avoids that at the cost of
one override file per model rather than a shared library).

**Action**: (1) confirm with Luna directly whether `--textures` was even
passed during her manual test — if not, this item may need no code change
at all beyond a clearer note; (2) regardless, document the structural
"no in-file candidate data" finding above in README, since it's a
sharper, better-grounded explanation than the first draft's "external
data, by design" one-liner; (3) scope the `type`-keyed local-override
convention as a real, separate follow-up feature — buildable, bounded,
and the actual honest equivalent of the geoset pattern for this case.

### Implemented (a basename-matching fallback, not the `type`-keyed convention above)

Luna's own follow-up, while asleep-work was in progress: real texture
directories are sometimes named descriptively (her example:
`bloodelffemalefaceupper00_00_hd.png`), not by FileDataID — so "some sort
of string matching should be attempted" for material naming. Implemented
as a real filename-resolution fallback, not just naming: when a batch's
texture has no resolvable FileDataID (`fdid == 0`, the hardcoded-slot
case this whole section is about), `cmd_export.cpp` now tries, in order:

1. **Exact match on the M2's own embedded filename** (`m2::Texture::filename`,
   real data for `type == 0`/pre-Legion files with no `TXID` entry) —
   `<texturesDir>/<that filename's own basename>.png`. Not a guess: the
   filename is literally in the file, this is exactly as precise as the
   FileDataID path above, just for an older-format field.
2. **A basename-prefix fuzzy match** (`findSoleFuzzyTextureCandidate`/
   `scanFuzzyTexturePool`/`claimSoleFuzzyTextureCandidate`,
   `cmd_export.cpp`) — scans `texturesDir` once per skin/LOD for `.png`
   files whose stem (case-insensitive) starts with the model's own
   basename and isn't itself a bare FileDataID (already covered by the
   exact-match path). Claims the sole remaining candidate only when
   exactly one is left; two or more are reported (their existence made
   visible) but never guessed at — deliberately did **not** try to
   narrow ambiguous candidates by `textureTypeName`'s own keyword (the
   real 2026-08-01 note's own example, "faceupper", comes from
   `CharComponentTextureSections`, a wholly different vocabulary from
   `M2Texture::type`'s enum names — checked, they genuinely don't
   correspond, so a keyword-narrowing heuristic would just be a
   different, equally-unfounded guess).

**A real design bug this session caught in its own first draft, before
shipping it**: the first implementation re-scanned the directory fresh on
every batch, so a single real candidate file got matched and embedded into
*every* unresolved slot at once (skin, hair, all guild colors, ...) — 68
of 70 materials all showing the same one texture, worse than embedding
nothing since it looks confidently wrong rather than honestly blank.
Caught by testing the feature end to end against a synthetic scenario
shaped like Luna's own real example, not by inspection. Fixed by scanning
once per skin/LOD into a shared `FuzzyTexturePool` that every batch draws
from and depletes — verified the fix directly: one real candidate file now
claims exactly 1 material (not 68); two real candidate files claim 0 and
print `"2 texture file(s) in '<dir>' share this model's basename but husk
can't tell which hardcoded texture slot each belongs to"`.

Material naming (§5) uses the same resolution: a fuzzy-matched file names
its material from the real filename's own stem, an embedded-filename match
names from that stem, and the FileDataID path still uses `_fdid<N>` as
before — never a bare `_tex<N>` when *any* real name is available.

**Tests**: 3 new `tests/test_cli.cpp` cases (sole-candidate embeds +
names, real-filename-vs-fuzzy scoping via a new `oneTexturedModelWithType`
fixture, ambiguous-candidates embeds neither and reports the count).
Verified against the real `bloodelffemale.m2` fixture with a synthetic
`bloodelffemalefaceupper00_00_hd.png` dropped alongside it (exactly Luna's
own example): 1 material embedded with a single candidate present, 0 with
two candidates present (plus the note). Full suite green throughout.

**Not implemented, still open**: the `type`-keyed local-override
convention this section originally scoped (a deliberate, explicit
`type_<N>.png`-style key) — superseded in practice by the basename-fuzzy
approach for the common case, but could still be worth adding as a way to
*disambiguate* the "2+ candidates" case above without relying on
filenames alone. Not scoped further this session.

## 5. Material naming should reflect the texture in use

**Symptom**: "material/texture export should have clearly named slots
based on the texture they utilize... if the resource doesn't have that,
then fall back to the raw fileID."

**Grounding**: `src/cmd_export.cpp:989` names every material
`batch<N>_mat<M>`, appending `_tex<idx>` and (when resolvable) `_fdid<id>`
— but for the 7-of-9 hardcoded-type textures from §4, neither suffix
carries any semantic information (no fdid, and `_tex2` doesn't say
"skin"). husk already parses and prints the raw `M2Texture::type` value
(`husk info`'s `type=N`) — mapping that to its wowdev-documented name
(`TEX_COMPONENT_SKIN`, `TEX_COMPONENT_CHAR_HAIR`, etc. — full table in
`documentation/wowdev-wiki/md/M2.md`) into the material name is a small,
well-grounded, purely additive change (e.g. `..._skin` instead of
`..._tex0`, falling back to `_fdid<id>` exactly as Luna suggested when a
real FileDataID is available, and to the current bare `_tex<idx>` only
when neither applies).

**Action**: implement directly, no further investigation needed — this is
table-lookup work, not a design question. Worth a small `textureTypeName`
helper in `m2.hpp`/`m2.cpp` alongside the existing `globalFlagNames`-style
named-bit-table precedent (`RO_COMPLETENESS_TODO.md`'s former Item 2).

### Implemented

`m2::textureTypeName(uint32_t type)` (`src/m2.hpp`/`m2.cpp`) — the full
23-entry wowdev.wiki "Texture types" table (IDs 1-23; 24-26 are real per
the wiki but it gives no name for any of them, so they're deliberately
absent, same `bone_<index>`-style honest-gap treatment `keyBoneName` §6
uses). Wired into `cmd_export.cpp`'s material-naming code right after
`gm.textureType` is set: appends `_<type_name>` (e.g. `_skin`,
`_char_hair`) whenever the type is known, before the FileDataID/fuzzy-match
suffix. Verified against the real `bloodelffemale.m2` fixture: every one
of its 7 hardcoded-type-slot materials now names as e.g.
`batch0_mat3_tex0_skin` instead of the old bare `batch0_mat3_tex0`. New
`tests/test_cli.cpp` case (`oneTexturedModelWithType(6)` — `char_hair` —
checked directly against the exported `.glb`'s own JSON for the string).
See §4's "Implemented" section for the filename-based naming this combines
with when a real texture file is also found.

## 6. Bone/joint node names are unhelpful ("Node 1", "Node 2")

**Symptom**: "node names just being node 1 node 2 etc, while correct, is
massively unhelpful in mapping data to points."

**Grounding**: confirmed in `src/gltf.cpp` — joint nodes
(`jointNodes[i]`, built ~line 278 onward) never get `.name` set at all,
unlike attachment/event/light anchor nodes a few lines further down
(`appendAnchorNode`, which do set a real name like `attachment_<id>`).
Left unnamed, Blender's glTF importer falls back to its own
auto-numbered "Bone"/"Node" names — and since that numbering isn't
guaranteed to match husk's own bone-index order, it's not just unhelpful,
it's actively hard to correlate back to `husk info`'s own bone listing or
any `extras` payload keyed by joint index.

husk already parses real semantic data that could help:
`m2::Bone::keyBoneId` (`src/m2.hpp:348`, back-reference into WoW's ~24-slot
key-bone enum: Root, Pelvis, Spine, Head, hand/foot bones, etc.) is parsed
per-bone but never surfaced anywhere in glTF output.

**Action**: implement directly — name each joint node `bone_<index>` by
default, or a real key-bone name (e.g. `bone_<index>_pelvis`) when
`keyBoneId != -1`, matching the enum table on the same M2.md page cited
above. Even the plain `bone_<index>` fallback alone (no key-bone lookup
needed) already solves the "unhelpful in mapping data to points" complaint,
since it's directly correlatable with every other index-keyed thing husk
already exports (extras, `dump-chunks` JSON, `husk info`'s own bone list).

### Implemented

`m2::keyBoneName(int32_t keyBoneId)` (`src/m2.hpp`/`m2.cpp`) — the full
193-row wowdev.wiki "Key Bone Names" table (`M2#Key-Bone_Lookup`),
transcribed directly, real ID gaps (46/47/90-189/191-289/294/295) included
as genuine absences, not guessed at. `gltf::Skeleton::Joint` gained a
`name` field (a resolved string, matching `billboardMode`'s own
"resolved-string-in, gltf.cpp stays M2-agnostic" pattern — not the raw
`keyBoneId`, so `src/gltf.cpp` never needs to know about M2 semantics at
all). `cmd_export.cpp`'s `buildSkeleton` sets it from `keyBoneName`;
`gltf.cpp`'s joint-node loop uses it when non-empty, falling back to
`"bone_<index>"` otherwise (this fallback alone — no real name needed —
already satisfies the "unhelpful in mapping data to points" complaint,
since `bone_<index>` is directly correlatable with `husk info`'s own bone
listing and every extras payload keyed by joint index, unlike Blender's
own auto-numbering).

Verified against the real `bloodelffemale.m2` fixture: 14 of 119 bones
(+2 mesh/collision nodes) get a real semantic name (`Waist`, `ForearmL`,
`ForearmR`, `_Breath`, `_Name`, `_NameMount`, `$CSL`, `$CSR`, `$BTH`,
`$CHD`, `$CCH`, ...), the rest fall back to `bone_<index>` — this
particular file's own bones simply don't tag `Head`/`FootL`/`FootR` etc.
with those specific `keyBoneId` values, a real fact about this fixture,
not a bug in the lookup (confirmed by cross-referencing `husk info`'s own
bone list against the M2's raw `keyBoneId` bytes directly). Full suite
green (471/471 + 1 skip at the time this landed).

## 7. All bone tails point straight up

**Symptom**: "all of the bone tail directions point directly up."

**Grounding**: not yet checked against real code this session — flagged
as the weakest-grounded item, needs real investigation before assuming
anything. The strong prior, though: **glTF has no bone-length/tail-direction
concept at all** — a glTF skeleton is just a tree of nodes with
translation/rotation/scale; "tail direction" is purely a Blender-side
display heuristic (typically pointing toward a joint's first child, or a
fixed default axis when a joint has zero or multiple children). If that
prior is right, this may not be independently fixable from husk's export
at all — Blender would need real per-bone length/orientation hints glTF
simply has no slot for.

**One real candidate worth checking specifically**: the
multi-root-bone-forest synthesized parent node
(`gltf::writeGlbMulti`, `DESIGN.md`'s Key design decisions — a plain
non-joint node parenting every real root joint when a model has more than
one). `bloodelffemale.m2` has 90 of 119 bones as roots (per
`DESIGN.md`'s own multi-root investigation numbers) — if Blender's tail
heuristic degenerates when a joint has an unusually large number of
same-position siblings (many of those 90 roots may share or nearly share
the synthetic parent's own identity-transform position), that could
produce exactly this "everything points the same default direction"
symptom, independent of any real husk bug.

**Investigation plan**: re-check visually against both a single-root
fixture (a real weapon model, per `test_data/item/objectcomponents/weapon/`)
and the multi-root `bloodelffemale.m2` — if the single-root fixture's bone
tails look normal and only the multi-root one is degenerate, that
localizes it to the synthetic-root interaction rather than a general
export bug.

### Investigated (headlessly) — very likely confirmed, not the multi-root synthesized node

Wrote a headless-Blender probe (not committed — scratch, same pattern as
`tests/blender_import_check.py`) that imports the real `bloodelffemale.m2`
export and, for every bone with exactly one child, computes
`bone.tail_local - bone.head_local` (Blender's own armature-space
coordinates, independent of whatever local bone-space rotation each joint
has). Result: **every one of 12 sampled single-child bones — different
bones, different places in the hierarchy, different distances from any
synthesized root — reports the exact same normalized direction, `(0, 0,
1)`.** That's not "mostly the same, some real variation" — it's bit-for-bit
identical across bones that have completely different real child
positions, which rules out the multi-root-synthesized-node hypothesis
directly: if the synthetic root's identity transform were the cause, only
bones *near* that root would degenerate, not bones deep in an ordinary
single-parent chain equally.

This is consistent with the original prior: glTF nodes carry no
bone-length/orientation data at all (a glTF "bone" is just a translated
node), so Blender's importer has nothing real to derive a tail direction
from and falls back to a fixed default axis, uniformly, regardless of
hierarchy shape. **Not re-tested against a single-root fixture** (the
investigation plan's own second half) — the uniform-regardless-of-position
result already found is strong enough evidence on its own that a
same-session follow-up didn't seem necessary, but that comparison is still
open if a future session wants a second confirmation.

**Disposition: very likely not independently fixable from husk's export**
— there's no data to give Blender that would change this, short of
Blender's importer adopting a different heuristic (e.g. inferring length
from child position, which some glTF-adjacent tools do but Blender's own
importer evidently doesn't, at least not by default). Not closing this
with 100% certainty only because no human has confirmed it visually in
Blender's actual viewport — everything checked headlessly points the same
way.

## 8. Model and skeleton import upside down

**Symptom**: "model and skeleton are upside down."

**Grounding**: not checked against real code this session — this is the
single most surprising item in the whole list, since it directly
contradicts extensive, already-tested Z-up→Y-up conversion work this
project has (`WIKI_FINDINGS.md` §5's bounding-box containment check,
`test_conformance.cpp`'s headless-Blender vertex/bone-count and bounding-box
assertions, all passing today). A real regression here would be a big
deal — but so would a false alarm, and Luna's own notes contain a strong
candidate explanation for the latter: "There is several options with
gltf... import with defaults # just a raw import # massively incorrect
looking... On gltf 2.0 import, the rest pose actually works, on gltf 1 it
was borked as fuck." If "upside down" was observed via a *gltf 1* import
path or Blender's "import with defaults" (rather than the "gltf 2.0...
defaults" path she later confirms works), this may be a Blender-side
legacy-importer artifact from testing the wrong path, not a husk
regression at all.

**Investigation plan, before touching any export code**: re-import the
*same* `.glb` husk produces, specifically via "Import glTF 2.0" with
default settings only (the one path Luna's own notes call out as working
for rest pose) and confirm whether "upside down" reproduces there too. If
it does reproduce under the known-good import path, this becomes the
highest-priority item in this whole file and needs the same
screenshot-plus-real-coordinate-check rigor `WIKI_FINDINGS.md` §5 already
used for the bounding-box work. If it does *not* reproduce, downgrade this
to "confirmed Blender-import-path artifact, not a husk bug" and close it
with that disposition recorded.

### Investigated headlessly — real, confirmed, NOT a Blender-import-path artifact. Read this whole section before doing anything else in this repo.

Ran exactly the investigation plan above, via a headless Blender probe
using `bpy.ops.import_scene.gltf(...)` (Blender's real glTF 2.0 importer,
default settings — the exact path Luna's own notes say works for rest
pose, not the gltf-1 path her notes already flag as broken separately).
**It reproduces. This is real, not a Blender-import-path false alarm.**

**The measurement**: exported `bloodelffemale.m2` fresh this session,
imported the real `.glb` headlessly, and read two independent things —
neither relying on the other:

1. **The render mesh's own bounding box.** A standing human character
   should span roughly Z=0 (feet, near the ground) to Z≈2 (head, adult
   height) in Blender's Z-up world. It actually spans **Z=-1.9925 to
   Z=+0.0108** — entirely on the *wrong side* of the origin, the mirror
   image of what a standing character should look like.
2. **A real named landmark bone** (`_Name`, keyBoneId 22 — a real,
   `husk info`/`m2::keyBoneName`-confirmed bone at M2-space pivot
   `(x=0, y=0, z=2.05)`, i.e. genuinely near the top of the head in source
   data) lands at Blender world `z = -2.0548` — *below* the root bone
   (`bone_51`, M2 pivot `(0,0,0)`, lands at Blender world `z = 0.0000`).
   The head-height bone is below the feet-height bone. That is what
   "upside down" means, measured, not eyeballed.

**Root cause, as far as this session traced it**: `src/gltf.cpp`'s
`zUpToYUp(v) { return {v.x, -v.z, v.y}; }` — cited in its own doc comment
and in `tests/test_gltf.cpp`'s unit test title as coming directly from
`documentation/wowdev-wiki/md/M2.md` line 1564 ("Models... use a Z-up
coordinate system, so in order to convert to Y-up, the X, Y, Z values
become (X, -Z, Y)"). **The wiki citation and husk's implementation agree
with each other — this isn't a transcription bug.** But composing this
conversion with Blender's own glTF importer (which converts the glTF
file's Y-up convention into Blender's own native Z-up on import, a
standard, independent second axis conversion) does not net out to
identity the way round-tripping the *same* physical up-axis through two
conversions should: `M2(x,y,z)` → husk's `zUpToYUp` → `glTF(x,-z,y)` →
Blender's own Y-up→Z-up import conversion → `Blender(x,-y,-z)` — a full
180°-about-X flip end to end, not the expected no-op. Whether the actual
mistake is in the wiki's own documented formula, in what Blender's
importer really does on the other end, or in an interaction neither side
individually anticipated wasn't resolved with certainty this session —
what's certain, measured twice independently above, is the *net* result
opening a real husk export in real Blender via the one import path Luna's
own notes call "working."

**A fix was tested — empirically confirmed correct — then deliberately
reverted, not shipped:**

- Changed `zUpToYUp` to `{v.x, v.z, -v.y}` (flips which two components get
  negated), rebuilt, re-exported the same real fixture, re-ran the same
  headless probe: `_Name` now lands at Blender world `z = +2.0548` (above
  the root, correctly), and the mesh bounding box now spans `Z=-0.0108` to
  `Z=+1.9925` — a standing character shape, feet near the ground, head
  near 2 units up. Both measurements, independently, now read the way a
  standing character should.
- Test-suite blast radius from that one-line change: **476/477 passed**,
  only `tests/test_gltf.cpp`'s own `zUpToYUp` unit test failed — the one
  test that hardcodes and asserts the *current* formula's literal output.
  Every other test, including every real-fixture conformance/integration
  test that already runs a real headless-Blender check, kept passing —
  strong, independent confirmation of this document's own opening claim
  (and the prior session's own closing note) that **nothing in this
  project's existing test suite actually checks absolute up-direction**,
  only counts, containment (which is blind to a *consistent* sign flip
  applied to both sides of a containment check), and structural agreement
  between tinygltf and Blender. A years-old, "verified," foundational
  coordinate bug could hide behind 476 green tests precisely because none
  of them ever asked "which way is up."
- **Reverted before this session ended — not shipped.** This is the single
  highest-blast-radius change this whole investigation turned up: it
  touches every position husk has ever exported (mesh vertices, bone
  bind-pose positions, attachment/event/light/emitter-anchor placements,
  collision mesh, everything that flows through `toGltf`/`zUpToYUp`).
  `DESIGN.md`'s own Key design decisions explicitly document that the
  *rotation* quaternion conversion and the *scale* conversion were both
  **deliberately derived from this exact position permutation**
  ("`zUpToYUp`'s `(X,-Z,Y)` permutation has determinant +1... a rotation
  quaternion's vector part gets the same permutation... a scale vector
  gets the same permutation with signs dropped") — meaning a real fix
  almost certainly can't be a one-line position-formula change in
  isolation. The rotation/scale conversions would need to be re-derived
  alongside it and re-verified (this session only checked *positions*
  empirically — bone rotations, the actual thing that makes an animated
  pose look right or "borked as fuck" per Luna's own gltf-1 description,
  were not re-tested against the flipped formula at all). Shipping the
  position half alone could easily produce a mesh that's positioned
  correctly but *animates* wrong — limbs bending backwards, a plausibly
  worse and harder-to-notice bug than today's consistently-upside-down
  one.

**This needs Luna's own review before anything changes.** Not because the
finding is uncertain (it's about as decisively confirmed as this session
could make it, twice over, empirically) but because of what fixing it
touches: it would invalidate the literal numbers in every existing
position-bearing test assertion in this codebase, needs the rotation/scale
conversions re-derived and re-verified (not just copied over unchanged),
and is exactly the kind of "foundational, whole-tool-blast-radius" decision
that warrants a real look before it ships, not a same-night autonomous
fix. Recommended next step: a dedicated session, with Luna available to
sanity-check the *visual* result directly in Blender's own GUI (not just
headless numeric probes), covering positions **and** rotations **and**
scale together, before touching `src/gltf.cpp`.

## 9. Blender import-path guidance (pure documentation)

**Grounding**: directly from Luna's notes — of the real import paths she
tried, "import with custom settings" crashes on every attempt (Blender-side
bug, not husk's), "import with defaults" via drag-and-drop "massively
incorrect looking" (likely the gltf-1-vs-2 confusion from §8), and plain
"Import glTF 2.0" with default settings is the one path that produces a
working, recognizable rest pose and animation.

**Action**: once §8 is resolved (or confirmed a non-issue), add a short
"known-good Blender import path" note to `README.md`'s usage section —
"Import glTF 2.0" via defaults, not the drag-and-drop submenu's other
options — so this doesn't need re-discovering by eye every session.

---

## What already works (confirmed, not a TODO — recorded so it isn't lost)

- Animations are recognizable: real keyframe data plays back as
  identifiable movement (a two-handed attack, per Luna's own description).
- The skeleton/rest pose is *structurally* coherent under "Import glTF
  2.0" with default settings (not a mangled hierarchy the way a gltf-1
  import path produces, per Luna's own "borked as fuck" description) —
  **caveat added after this session's §8 finding**: "works" here means
  structurally sound, not correctly oriented. §8 confirms the whole thing
  is measurably upside down even via this exact import path — Luna's own
  "the rest pose actually works" observation and this session's finding
  aren't necessarily in conflict (a coherent-but-inverted skeleton can
  look "working" at a glance, especially next to a genuinely broken gltf-1
  import right beside it for comparison), but don't read this bullet as
  "orientation is fine" anymore.
- Attachment points import as a real set of Empties, positioned
  plausibly (again, positioned plausibly *relative to the rest of the
  upside-down skeleton* — not necessarily right-side-up in world space
  until §8 is resolved).
