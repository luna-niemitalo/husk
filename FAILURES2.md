# Failure log 2

**Status: items 1-9 are fixed**, each with regression tests (`tests/test_skin.cpp`,
`tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_m2.cpp`,
`tests/test_skel.cpp`, `tests/test_dump.cpp`) that run with no real game
files needed, so these can't silently regress; item 10 was a mistaken
finding, retracted below, not a real gap. This started as a read-only
inspection pass (no source files modified, no commands run against the
built binary; every finding came from reading `src/`, `tests/`, `README.md`,
`FAILURES.md`, `TODO_correctness.md`, `WIKI_FINDINGS.md`, and the
`documentation/` tree, including the local wowdev.wiki mirror) and was a
follow-on to `FAILURES.md` (five previously-found bugs, all fixed) and
`TODO_correctness.md` (coverage gaps already tracked): it went looking
specifically for (a) features a *comprehensive, user-friendly* M2 reader
should have that husk didn't yet, and (b) new ways husk breaks that
neither of those two files already named. Nothing here duplicates an item
already open in `TODO_correctness.md` (particles, `AFSB`, cameras, `.bone`
LOD integration, `.skel`'s `SKL1`/`SKA1`/`SKPD`/`key_bone_lookup`) — those
are real, known, and still true, just not repeated here. Several of these
fixes are diagnostics/warnings rather than full feature implementations
(e.g. geoset filtering, multi-texture batches, global-sequence animation) —
the underlying feature gaps are real and still open, but husk now says so
loudly instead of silently doing the wrong thing, consistent with this
project's own foreign-data-validation philosophy.

Findings are ranked most-impactful first. "Impactful" here means: how much
of a *real* model's visual/structural correctness is affected, and how
likely a normal user is to hit it without doing anything unusual — not
"is this reachable by a hand-crafted adversarial file."

---

## 1. [Fixed] [Critical] `M2SkinSection.skinSectionId` (the "geoset ID") is never read — `husk export` renders every submesh in a `.skin` file at once, including mutually-exclusive character-customization options

**What's expected:** per wowdev.wiki (`documentation/wowdev-wiki/md/M2/.skin.md`,
"Submeshes" section), `M2SkinSection`'s *first* field is:

```
struct M2SkinSection {
  uint16_t skinSectionId;  // Mesh part ID, see below.
  uint16_t Level;
  uint16_t vertexStart;
  ...
```

`skinSectionId` (aka "geoset ID" in the tooling community, cross-referenced
from `CreatureDisplayInfo.dbc`/`ItemDisplayInfo.dbc`) is exactly what a
client uses to decide *which* submeshes to actually draw for a given
character/creature configuration — hairstyle N, facial-hair style N,
gloves-on-vs-off, cloak-on-vs-off, and so on are all encoded as separate
submeshes sharing one `.skin` file, distinguished only by this field (the
wiki page's own "Mesh part ID" section and its `ApplyMonsterGeosets`
worked example spell this out explicitly). A correct reader has to filter
by this field (or expose it so a caller can) before assuming "every
submesh in the file belongs on the model."

**What actually happens:** `husk::skin::Submesh` (`src/skin.hpp:39-44`)
starts at `vertexStart`, `vertexCount`, `indexStart`, `indexCount` — the
*third* through *sixth* fields. `skinSectionId` (offset `0x00`) and `Level`
(offset `0x02`) are never read at all; confirmed against
`src/skin.cpp:149-157`'s `parseSubmeshes`, which reads its four fields
starting at `off + 0x04`, skipping the first 4 bytes of every 0x30-byte
record entirely. `cmd_export.cpp`'s `buildMaterialsAndPrimitives`
(`src/cmd_export.cpp:512-667`) then turns *every* batch in the `.skin`
file into its own glTF primitive, unconditionally — there is no
`skinSectionId`-based filter anywhere in the export path, and no way for a
caller to ask for one subset vs. another.

**Why it matters:** this isn't a hypothetical edge case — it's the norm
for exactly the kind of model this project's own test data and README use
as the running example. `bloodelffemale.m2` + `bloodelffemale00.skin`
export "70 batches... 70 glTF primitives" (README's Usage section) for one
humanoid character; a character model's `.skin` file routinely bundles
*every* selectable hairstyle/facial-hair/headpiece variant as separate
submeshes in the same file, because the client — not the file — decides
at runtime which ones to actually draw. Without `skinSectionId`, `husk
export` has no way to distinguish "this submesh is part of the base body"
from "this submesh is hairstyle #7, mutually exclusive with hairstyles
#1-6 and #8+" — it exports all of them, overlapping, into one mesh. The
practical result for a real character export is very likely several
hairstyles/facial-hair variants/gear-slot geosets rendered simultaneously,
intersecting at the scalp/chin/hands — a visibly broken model, not a
subtle inaccuracy. This is also very plausibly *why* nobody's caught it
yet: the README says outright, twice, "not yet verified: actually opening
the output in Blender" — this is the kind of defect that's invisible from
counting vertices/triangles/materials (all of which are still "correct,"
just for the union of every geoset instead of one consistent selection)
and only shows up the moment someone actually looks at the model.

**Suggested fix:** read `skinSectionId` into `skin::Submesh` (trivial,
same pattern as every other field in that struct), surface it in `husk
info` (or a new `husk export` flag) so a caller can filter/select geosets,
and at minimum document in the README that `export`'s current output is
"every geoset the .skin file lists, unfiltered" rather than implying
(as the current wording does) that the exported mesh is simply "the
model." Deciding *what* the default filter should be (all geoset group 0
+ nothing else? a `--geoset` selection flag mirroring `--lod`?) is a real
design question, not a one-line fix — but the underlying field needs to
exist in the parser before any of those options are even possible.

**Applied:** `skin::Submesh` now has `skinSectionId` (`src/skin.hpp`),
read by `parseSubmeshes` (`src/skin.cpp`). Discussed with Luna: no
DBC-free "correct default" exists (Armory/Wowhead's viewer/WMV/
`wow.export` all resolve *which* geoset variant is "the" one from DBC/DB2
data husk deliberately never reads — there's nothing to fall back on
without it), so husk still doesn't filter — but rather than stopping at a
diagnostic, every submesh's real `skinSectionId` is now carried all the
way into the exported `.glb` as inert glTF `extras` on its primitive
(`geoset_id`, plus a derived `geoset_group`/`geoset_variant` split), the
same "tag it, don't guess at semantics" treatment `billboardMode` already
gets — a custom renderer or Blender script (mesh mask, geometry nodes, a
driven material, ...) has everything it needs to build its own selection
UI on top, without husk inventing a policy it can't actually ground in
data. `exportGlb` still prints a loud `husk: note:` line naming every
distinct `skinSectionId` in a `.skin` whenever more than one shows up, so
the "every geoset, unfiltered, with no indication" failure mode stays
closed. Verified against real data: `bloodelffemale00.skin` reports "66
distinct geoset IDs" spanning the hairstyle/facial-hair/gear-slot ranges
the wiki's own `Character_Customization` groupings predict, and the full
export still round-trips cleanly with the new extras attached. Regression
tests: `tests/test_skin.cpp`'s "reads skinSectionId" case,
`tests/test_gltf.cpp`'s "a primitive's skinSectionId round-trips as
geoset_id/group/variant extras" and "...no skinSectionId... gets no
geoset extras", `tests/test_cli.cpp`'s "batches spanning more than one
distinct skinSectionId..." and "...print no geoset note" cases.

---

## 2. [Fixed] [High] `husk export --textures` can produce a `.glb` with misaligned glTF buffer views/accessors — a real spec violation, not a hypothetical one, for any embedded PNG whose byte length isn't a multiple of 4

**What's expected:** per the glTF 2.0 specification, a `bufferView` that
backs a vertex-attribute or index `accessor` (`POSITION`/`NORMAL`/
`TEXCOORD_*`/`JOINTS_0`/`WEIGHTS_0`/indices/animation sampler in/out) must
have a byte offset into its buffer that's a multiple of that accessor's
component size (4 bytes for `FLOAT`/`UNSIGNED_INT`) — this is exactly what
the Khronos glTF-Validator's `ACCESSOR_TOTAL_OFFSET_ALIGNMENT` check
flags, and what README.md's own roadmap stage 7 ("Validate actual `.glb`
output against the Khronos glTF-Validator... still open") is meant to
catch before this ships as "verified."

**What actually happens:** `gltf.cpp`'s `appendBufferView`
(`src/gltf.cpp:17-31`) appends raw bytes to one shared, growing
`tinygltf::Buffer` with `view.byteOffset = buffer.data.size()` and *no*
padding to any alignment boundary, ever. Every embedded texture image
(`mat.baseColorImagePng`, `src/gltf.cpp:337-343`) is appended the exact
same way — a real PNG file's byte length is essentially never a multiple
of 4. Once one odd-length image has been appended, `buffer.data.size()`
stops being a multiple of 4, and *every* `bufferView` created after it —
the next primitive's index accessor (`src/gltf.cpp:364-365`,
`TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT`), the next `NamedMesh`'s position/
normal/UV accessors in a `--lod all` export, every animation sampler's
input/output view (`addChannel`, `src/gltf.cpp:444-465`) — inherits that
same misalignment, since nothing ever re-pads the buffer back to a 4-byte
boundary afterward.

**Why it matters:** this is the one feature (`--textures`) specifically
built to make export output "real" rather than metadata-only (roadmap
stage 5), and it's realistic, not contrived — any directory of
`husk-blp`-converted PNGs will trigger this on virtually every model that
has more than one batch needing a texture, which per finding #1 above is
most real character models. The resulting `.glb` is very likely to fail
Khronos glTF-Validator's alignment check outright (the exact tool this
project's own roadmap already commits to running eventually), and some
stricter glTF consumers reject or mis-parse misaligned accessors rather
than silently tolerating them — a real risk to "does this actually open
correctly," which is the project's stated bar for success, not merely a
theoretical spec nitpick.

**Suggested fix:** pad `buffer.data` up to the next 4-byte boundary
(zero-filled) after every `appendBufferView`/inline-image/inline-animation-
value append, before recording the *next* view's `byteOffset` — a small,
local change to `appendBufferView` (and the two ad hoc append sites that
don't go through it: the image view and `addChannel`'s output view).

**Applied:** `gltf.cpp`'s new `padTo4()` zero-pads `buffer.data` up to the
next 4-byte boundary; called from inside `appendBufferView` itself (so the
embedded-image case, which already went through it, is covered
automatically) and from the one other ad hoc append site, `addChannel`'s
animation-sampler output view. Verified this actually reproduces and fixes
the bug, not just "looks plausible": temporarily reverted the fix, reran
the new test, and confirmed it fails with the exact predicted offset
(`byteOffset=166`, `166 % 4 == 2`) before restoring the fix and confirming
it passes again. Regression test: `tests/test_gltf.cpp`'s "every
bufferView stays 4-byte aligned even after an odd-length embedded image"
case, which checks every `bufferView` in the round-tripped document
generically (not just the one known-affected pair), so it also guards the
inverse-bind-matrix/animation-sampler buffer views against the same class
of regression.

---

## 3. [Fixed] [High] Bone/Sequence/Ribbon record sizes are hardcoded to a "Wrath+" shape, but `expansionForVersion` happily recognizes and labels genuine Classic/TBC files — feeding husk a real pre-Wrath M2 silently misreads bone/sequence/ribbon data at the wrong byte stride

**What's expected:** a tool that labels a file "Classic" or "The Burning
Crusade" (`m2::expansionForVersion`, `src/m2.cpp:1051-1078`, version ranges
256-257 and 260-263 respectively) should either read that version's actual
on-disk layout correctly, or refuse/warn rather than silently decoding it
as something else.

**What actually happens:** `m2::Bone`'s own doc comment
(`src/m2.hpp:266-267`) states its 88-byte (`0x58`) record size is ">= Wrath
shape (Wrath being every version this parser targets)"; `m2::Sequence`'s
doc comment (`src/m2.hpp:304-306`) states its 64-byte stride holds "for
every version this parser targets (WotLK+..."; `m2::Ribbon`'s
(`src/m2.hpp:397-399`) says the same (">= Wrath shape"). None of
`parseBones`/`parseSequences`/`parseRibbons` (`src/m2.cpp`) branch on
`header.version` at all — they use one fixed stride unconditionally,
regardless of what `expansionForVersion` just told the user about the
file. A real Vanilla (256) or TBC (260-263) M2 — which per wowdev.wiki
does have differently-sized/laid-out bone/sequence records for those
versions — would be decoded at the Wrath+ stride anyway.

**Why it matters:** this is precisely the failure class the project
already caught and fixed once for a different reason — the
`M2Sequence`-stride investigation (`WIKI_FINDINGS.md` §1, `m2.hpp`'s
`Sequence` doc comment): decoding a real array at the wrong per-record
byte size doesn't reliably throw a bounds error, it silently produces
"plausible-looking data for roughly every other record and outright
garbage for the rest" — small, sane-looking numbers that are actually
read from the wrong byte range. Nothing in `husk info`/`husk export` warns
when `header.version` falls outside the Wrath+ range these structs are
scoped to, even though `expansionForVersion` computes and prints exactly
that version/label pair already (`cmd_info.cpp:102`) — the information
needed to catch this is already on hand and simply isn't checked. A user
who runs `husk info`/`export` against a Classic-era model (still a
completely normal thing to have lying around from `casc-tool`-style
extraction against an older client) gets no error and no warning — just
quietly wrong bone pivots, sequence durations, or ribbon parameters.

**Suggested fix:** at minimum, have `husk info`/`husk export` print a
loud warning (not silence) when `header.version < 264` (below Wrath)
before trusting `parseBones`/`parseSequences`/`parseRibbons`'s output —
same spirit as the existing "0 inline bones + SKID present" note in
`cmd_info.cpp`. A full fix (branching the record stride by version) needs
real pre-Wrath sample files to verify against, the same "don't guess at an
undocumented/unverified layout" discipline this project already applies
elsewhere (`WIKI_FINDINGS.md`, `AFSB`) — but a warning costs nothing and
closes the "silently wrong, no indication anything's off" half of the gap
immediately.

**Applied:** exactly the warning, not the full version-branched parser
(the latter still needs real pre-Wrath sample files this pass doesn't
have, per the fix's own reasoning above). `m2::kMinVerifiedRecordStrideVersion`
(`src/m2.hpp`, = 264) names the cutoff once; both `cmd_info.cpp`'s `info()`
and `cmd_export.cpp`'s `exportGlb()` check `header.version` against it and
print a `husk: warning:` line naming the actual version and what might be
wrong, before any bone/sequence/ribbon data is trusted. Regression tests:
`tests/test_cli.cpp`'s "a version below Wrath (264) prints a loud warning"
(both `info` and `export` variants) and "a Wrath+ version prints no such
warning".

---

## 4. [Fixed] [Medium] `husk info` never prints resolved texture or material records, and never prints `TXID` (`textureFileDataIds`) at all — inconsistent with every other per-record array `info` surfaces, and with the README's own description of what `info` does

**What's expected:** `README.md`'s Usage section says `info` "resolves and
prints `attachments`/`events`/`lights` records," and separately "surfaces
the model's `SFID`... `LDV1`... `BFID`... and `AFID`" — the format matrix's
"Texture references" row additionally says the Legion+ `TXID` chunk's
FileDataIDs are "surfaced (`Header::textureFileDataIds`)," in the same
sentence describing that row's read support.

**What actually happens:** `cmd_info.cpp`'s `info()` (`src/cmd_info.cpp:74-217`)
calls `m2::parseAttachments`/`parseEvents`/`parseLights`/`parseRibbons`
and prints one line per record for each — but never calls
`m2::parseTextures` or `m2::parseMaterials` at all, despite both existing
and being used by `cmd_export.cpp`. `textures`/`materials` only ever get
their raw array *count* printed (`printArray("textures", ...)`,
`printArray("materials", ...)`, lines 122-123) — no type/flags/filename
for textures, no flags/blendMode for materials, unlike every other
resolved-record array `info` handles. Separately, `h.textureFileDataIds`
(populated correctly from the `TXID` chunk, used by `export`'s
`--textures` resolution) is never printed anywhere in `info()` at all —
`skinFileDataIds`/`lodCount`/`boneFileDataIds`/`animFileIds`/`physFileId`
each get an explicit `std::cout` line (lines 126-151); `textureFileDataIds`
gets none, even though it's the same shape of data (a chunked-file-only
optional FileDataID list) as every one of those.

**Why it matters:** for the stated goal of "comprehensive and
user-friendly M2 read coverage," `info` is supposed to be the
at-a-glance, no-export-needed summary of a model — and right now it's the
one place a user *can't* see what textures a model references (type,
filename for legacy models, or FileDataID for modern ones) or what blend
modes its materials use, without running a full `export`. This is a real,
checkable regression from the README's own claims, not a design choice —
nothing in the Design notes explains why textures/materials are treated
differently from attachments/events/lights/ribbons.

**Suggested fix:** add the same "printArray, then loop and print each
resolved record" pattern already used for attachments/events/lights/
ribbons, for both `textures` (type/flags/filename, cross-referenced against
`textureFileDataIds[i]` when present) and `materials` (flags/blendMode);
add a `texture_file_data_ids:` line matching the format of the existing
`skin_file_data_ids:`/`bone_file_data_ids:` lines.

**Applied:** exactly that pattern, in `cmd_info.cpp`. Per-texture lines
show `type`/`flags`, plus `filename` only for `type == 0` (a real embedded
path — a nonzero type is a runtime-substituted slot, see `m2::Texture`'s
doc comment, so printing a filename there would be misleading) and
`file_data_id` when `textureFileDataIds` has a nonzero entry for that
index; per-material lines show `flags`/`blend_mode`; a new
`texture_file_data_ids:` summary line matches the existing sidecar-ID
lines' format. Regression test: `tests/test_cli.cpp`'s "prints per-texture
type/flags/filename, per-material flags/blend_mode, and
texture_file_data_ids..." case, which specifically checks a `type != 0`
texture's filename/file_data_id are correctly *not* printed.

---

## 5. [Fixed] [Medium] The `TEXL` chunk tag is a complete blind spot — recognized by `husk info` (so it never triggers the "undocumented chunk" warning) but not handled anywhere by `husk dump-chunks`, silently contradicting the project's own "never silently drop a real chunk" policy

**What's expected:** README.md's Design notes state the explicit intent
behind `dump-chunks`: "Chunks with no documented byte layout, or a
wiki-acknowledged-uncertain one... are still included, as a raw hex dump
plus a note explaining why, rather than silently dropped."
`cmd_info.cpp`'s `documentedM2ChunkTags()` (`src/cmd_info.cpp:27-34`) lists
`TEXL` as one of the 30 documented top-level M2 chunk tags — so a real
file carrying it is treated as "husk already knows about this," not
flagged as a signal the format moved.

**What actually happens:** `cmd_dump.cpp`'s `kDocumented`
(`src/cmd_dump.cpp:452-458`, 13 tags with real field-level parsing) and
`kFallback` (`src/cmd_dump.cpp:470-488`, 8 tags with a hex-dump-plus-note)
lists together cover 21 of `documentedM2ChunkTags`'s 30 tags. Of the 9
not covered, 8 are chunks that feed into `info`/`export` directly instead
(`MD21`/`PFID`/`SFID`/`AFID`/`BFID`/`SKID`/`TXID`/`LDV1` — correctly out of
scope for `dump-chunks`, which is explicitly for chunks that *don't* feed
`export`'s output). `TEXL` is the ninth, and it fits neither category: it
isn't parsed by `info`/`export` (no code anywhere references it besides
the tag-recognition list), and it's absent from `dump-chunks`'s fallback
list too. A real M2 carrying a `TEXL` chunk (README.md's Design notes
mention it by name as a real, if niche, "light shadow/cookie data" chunk)
has that chunk's data completely invisible to every husk command — not
even a hex dump, not even a "husk sees this tag but doesn't understand it"
note, the two things every *other* unhandled documented chunk gets one of.

**Suggested fix:** add a `TEXL` entry to `cmd_dump.cpp`'s `kFallback` list
(same one-line pattern as `WFV1`/`WFV2`/etc.) with a short note explaining
why it isn't structurally parsed yet.

**Applied:** better than the suggested fix — `TEXL`'s own struct (two
floats, then an index into `TXID` for the light cookie texture, then one
more unknown int, 16 bytes/record) turned out to be completely
unambiguous, unlike `DETL`'s internally-inconsistent offsets, so it got
real field-level parsing (`dumpTexl`, added to `kDocumented` rather than
`kFallback`) instead of just a hex-dump-plus-note. Regression test:
`tests/test_dump.cpp`'s "TEXL (light-cookie texture lookups) reads its
four fields per record".

---

## 6. [Fixed] [Medium] Multi-texture batches (`M2Batch.textureCount` 1-4) always resolve to exactly one texture, with no diagnostic that a batch actually uses more than one

**What's expected:** per wowdev.wiki (`documentation/wowdev-wiki/md/M2/.skin.md`'s
"Texture units" section, and `documentation/wow-material-rendering.md`
§1.2), `M2Batch.textureCount` (1-4, "almost always 1 or 2") means the
texture/UV-mapping/transparency lookup indices are a **base index**, and
the real per-unit values are `textureComboIndex`, `textureComboIndex+1`,
... up to `textureCount` entries — used for real, visually meaningful
effects like a second env-mapped "shine" layer on armor/weapons
(`Diffuse_T1_Env`) or genuinely independent two-texture blends
(`Diffuse_T1_T2`), not just redundant duplicate data.

**What actually happens:** `skin.hpp`'s own `Batch` doc comment
(`src/skin.hpp:56-57`) already states the scope decision explicitly: "1..4;
only the first texture is used here." `cmd_export.cpp`'s
`buildMaterialsAndPrimitives` (`src/cmd_export.cpp:607-659`) checks
`b.textureCount > 0` only to decide *whether* a texture exists at all,
then resolves exactly one texture via `b.textureComboIndex` — the second
through fourth texture layers a `textureCount > 1` batch actually carries
are never read, never surfaced, and never mentioned in any output.

**Why it matters:** this is a real fidelity gap distinct from the
project's already-stated, reasonable non-goal of faithfully reproducing
WoW's fixed-function combiner math (`documentation/wow-material-rendering.md`
is explicit that husk isn't attempting that, which is a defensible scope
call). The gap here is narrower and cheaper to close: there's currently no
way for a user to even *know* a given exported material dropped a second
texture layer — `gm.name` (used as a debugging label,
`src/cmd_export.cpp:557,622,647`) never reflects `textureCount`, and
nothing in `husk info`/`export`'s stdout output counts or flags
multi-texture batches. A model with env-mapped shiny armor exports with
the shine layer completely absent and no trace that it existed.

**Suggested fix:** at minimum, surface `textureCount` and, when > 1, the
additional resolved texture FileDataIDs/filenames somewhere in `husk
info`'s per-batch-adjacent output (or a note in `export`'s summary line:
"N of M batches use more than one texture, only the first is exported").
Actually emitting a second glTF texture (e.g. a `KHR_materials_specular`-
style additional layer, or simply embedding it as an unused auxiliary
image with a name hinting at its role) is a larger, separate feature.

**Applied: more than the diagnostic half.** Discussed with Luna: resolving
the additional texture(s) is easy (loop `textureCount` times instead of
assuming 1), the real question was *representation*, since core glTF has
no generic multi-layer slot and faking a `KHR_materials_*` extension would
misstate WoW's actual combiner math (`Mod2x`/`Add`/env-map blending) as
something it isn't. Landed on the same answer as geoset filtering above:
tag it, don't guess. `buildMaterialsAndPrimitives` now resolves layers
`textureComboIndex + 1 .. textureComboIndex + textureCount - 1` (per
wowdev.wiki: "the texture ... indices ... are only the 'base' index")
best-effort — skipped rather than erroring if the base-index assumption
doesn't hold for some batch, since this is supplementary metadata, not
required for a usable export — and carries each into
`gltf::Material::additionalTextureLayers`. `gltf.cpp` emits them as
`extras.additional_textures` on the material (FileDataID + UV set per
layer), plus a real, separately embedded but core-material-*unused*
image/texture when `--textures <dir>` has a matching PNG — the same
"embed if available" policy the primary `baseColorTexture` already uses.
Still not wired into the actual rendered material (core glTF has nowhere
correct to put it); `exportGlb`'s note was reworded to say so precisely
rather than "dropped". Verified against real data: `bloodelffemale00.skin`
still reports its 1 multi-texture batch and exports cleanly with the new
extras attached. Regression tests: `tests/test_gltf.cpp`'s "a material's
additionalTextureLayers round-trip as extras..." (including the
with-image/without-image split) and "...no additionalTextureLayers gets no
such extras", `tests/test_cli.cpp`'s "a batch with textureCount > 1
prints a note..." and "...textureCount == 1 prints no multi-texture note"
(wording updated to match, substrings preserved so both still pass
unmodified).

---

## 7. [Fixed] [Low-Medium] Global-sequence-driven `M2Track`s (continuous glow/pulse/sway animation) always resolve to nothing, with no user-facing indication any track was skipped for this reason

**What's expected:** per wowdev.wiki M2#Interpolation ("Global Sequences":
"completely unrelated to animations... always loops"), a track whose
`global_sequence` field isn't the "none" sentinel drives a *continuously
looping* animation independent of any `M2Sequence` — used for things like
eye-glow pulsing, torch flicker, or idle cloth sway that plays regardless
of which emote/combat animation is active. `m2::TrackMeta`'s doc comment
(`src/m2.hpp:494-518`) and `resolveVec3TrackSequence`/
`resolveQuatTrackSequence`'s doc comments (`src/m2.hpp:528-578`) correctly
explain that husk deliberately returns empty for these rather than
misattributing the data to whatever `M2Sequence` happens to occupy outer
array position 0 (a real bug this exact type was introduced to fix — see
`TODO_correctness.md` item 1).

**What actually happens:** the fix is correct as far as it goes, but
nothing downstream tells the *user* this happened. `cmd_export.cpp`'s
`buildAnimations` (`src/cmd_export.cpp:289-412`) silently skips any joint
whose translation/rotation/scale all come back empty for a given
sequence — indistinguishable, from the exported `.glb`'s or `husk
export`'s stdout summary's point of view, from "this bone simply isn't
animated in this sequence" (the overwhelmingly common, expected case).
There is no code path anywhere that counts or reports "N tracks on this
model are global-sequence-driven and will never appear in any exported
clip."

**Why it matters:** for "user-friendly M2 read coverage," silently
dropping a real, intentional animation feature (as opposed to "this bone
just isn't animated here," which is normal and doesn't need a note) is a
meaningfully different situation that currently looks identical in the
tool's output. A user exporting a model with glowing eyes or an idle
sway effect has no way to learn from husk's own output that the reason
those don't appear in Blender is "global sequences aren't supported yet"
rather than "check your `.skin`/`.anim` pairing" or any other more
generic troubleshooting step.

**Suggested fix:** have `readTrackMeta` calls already made by
`buildAnimations` (for `translationStep`/`rotationStep`/`scaleStep`) also
tally how many bones/tracks have a non-`kNoGlobalSequence` value, and
print a one-line summary note (same style as the existing "(bind pose
only, no animation)" note) when that count is nonzero — e.g. "N bone
tracks use global sequences (continuous looping animation) and aren't
included in any exported clip."

**Applied: fully resolved, not just diagnosed** — on reflection this was
tractable without any new external data. `Header::globalLoops` was already
parsed as a bare `(count, offset)` descriptor but its `M2Loop` records were
never read; wowdev.wiki's own "blocks that use global sequences also only
have one track" note means a global-sequence track's outer
`M2Array<M2Array<T>>` always has exactly one sub-array (index 0), so real
keyframes are a `trackSequenceInnerArrays(..., /*sequenceIndex=*/0)` call
away — the identical shape `resolveVec3TrackSequence`/
`resolveQuatTrackSequence` already use for a specific `M2Sequence` index,
just fixed at 0 instead. New `m2::resolveVec3GlobalSequenceTrack`/
`resolveQuatGlobalSequenceTrack` (`src/m2.cpp`/`m2.hpp`) implement exactly
that, and `m2::parseGlobalLoops` resolves the `M2Loop` records themselves
(not yet wired into clip duration/naming — see below). `cmd_export.cpp`'s
new `buildGlobalSequenceAnimations` finds every distinct global-sequence
index actually used across a model's bones and builds one real glTF
animation clip per index (`global_seq_<n>`), covering every bone that
references it — the same per-bone `JointAnimation`-building logic
`buildAnimations` already used was factored into a shared
`buildJointAnimation` helper so both paths build identical glTF channel
data from whichever keyframes they resolved. Runs unconditionally alongside
(not gated by) the per-`M2Sequence` clips, for both inline-bones and
`.skel`-sourced models, since a global-sequence track is a property of the
bone itself, not of `M2Sequence`/`SKS1`. **Verified against real data**:
`bloodelffemale.m2` now exports 258 animation clips (256 real + 2 real
global-sequence clips), up from 256 before this fix — not a guess, an
actual count increase from real file data, with the full 254/254 test
suite (synthetic + real-data integration) passing throughout. Regression
tests: `tests/test_m2.cpp`'s `resolveVec3GlobalSequenceTrack`/
`resolveQuatGlobalSequenceTrack`/`parseGlobalLoops` cases (pure-logic,
including the "not global-sequence-driven resolves to empty" and
interpolation-type-2/3-throws cases), `tests/test_cli.cpp`'s "a
global-sequence-driven bone track resolves to a real animation clip" and
"a model with no global-sequence-driven tracks gains no extra clip"
(end-to-end through the real CLI). Not yet done: using `parseGlobalLoops`'
own duration for anything (clip naming/length hint) — glTF has no native
"loop independently of scene state" concept to map it onto anyway, so this
is left as future polish, not a correctness gap.

---

## 8. [Fixed] [Low] Several FileDataID/struct-array chunk readers silently truncate on a non-record-aligned chunk size instead of erroring, unlike every other fixed-record array parser in the codebase

**What's expected:** every `parse*` function in `src/m2.cpp` that reads an
array of fixed-size records validates the claimed range up front and
throws a descriptive `ParseError` on a mismatch (this is the exact
discipline `FAILURES.md` #2 hardened codebase-wide — "foreign data that
doesn't fit its own claims is an error, not a best-effort").

**What actually happens:** three chunk readers don't follow that pattern,
because they derive their own record count from the chunk's raw byte
length instead of a separate, foreign `count` field, and never check that
the byte length is actually an exact multiple of the record size:

- `m2::findFileDataIdArrayChunk` (`src/m2.cpp:210-228`, used for
  `TXID`/`SFID`/`BFID`): `size_t count = chunk->size / 4;` — a chunk whose
  size isn't a multiple of 4 silently drops its last partial entry.
- `m2::findAnimFileIdChunk` (`src/m2.cpp:234-253`, used for `AFID`):
  `size_t count = chunk->size / kEntrySize;` (8), same issue.
- `skel::findAnimFileIds` (`src/skel.cpp:118-138`, the `.skel`'s own
  separate `AFID` table): identical `afid->size / kEntrySize` division,
  same issue.

**Why it matters:** this is a minor instance of exactly the class of bug
this project cares most about per its own stated philosophy (silently
wrong beats loudly wrong) — a truncated download or corrupted extraction
that clips a few bytes off the end of a `TXID`/`SFID`/`BFID`/`AFID` chunk
doesn't get caught at all here; it just quietly resolves one fewer
FileDataID than the file actually intended, with nothing printed anywhere
to suggest the read was incomplete. Low severity because the consequence
is bounded (at most one dropped trailing entry, never memory unsafety —
`readU32`/`readU16`'s own bounds checks prevent reading past the chunk),
but it's a real, checkable inconsistency with how every *other* similarly-
shaped array in this codebase is validated.

**Suggested fix:** add a `chunk->size % recordSize != 0` check (throwing
the same descriptive-`ParseError` style used everywhere else) to all
three functions before computing `count`.

**Applied:** exactly that check, at all three sites, throwing the same
descriptive `ParseError` style. Regression tests: `tests/test_m2.cpp`'s
"TXID chunk with a byte length not a multiple of 4 throws..." and "AFID
chunk with a byte length not a multiple of 8 throws...", plus
`tests/test_skel.cpp`'s "AFID chunk with a byte length not a multiple of 8
throws..." for the `.skel`-specific copy.

---

## 9. [Fixed] [Low] No finiteness/monotonicity validation on animation keyframe timestamps or values — the same bug class `FAILURES.md` #4 fixed for vertex positions/normals, left open for animation data

**What's expected:** per `FAILURES.md` #4 (already fixed), the project's
own stated policy is that non-finite (NaN/Inf) data reaching glTF output
is a real defect worth guarding against at the point the offending
record's identity is still known, because "the glTF 2.0 spec requires
finite values" for accessor data and its `min`/`max` bounds, and a
downstream consumer failing on it is "a much more confusing way and
location" to discover a corrupted source file.

**What actually happens:** that fix only covers `Mesh::positions`/
`normals` (`cmd_export.cpp`'s `isFinite` check, `src/cmd_export.cpp:953-957`).
The identical exposure exists, unfixed, for animation data:
`m2::resolveVec3TrackSequence`/`resolveQuatTrackSequence`
(`src/m2.cpp:783-874`) read raw timestamp/value bytes with no
`isfinite`/ordering check at all — a `readU32` timestamp or a decompressed
`Quat`/`Vec3` value can be any bit pattern the source blob contains,
finite or not, and there is no check anywhere in the resolve path or in
`cmd_export.cpp`'s `buildAnimations`. Separately, `gltf.cpp`'s `addChannel`
(`src/gltf.cpp:444-465`) computes each animation sampler's input accessor
`min`/`max` as `times.front()`/`times.back()` — correct *only if* `times`
is actually sorted ascending, which is asserted in a doc comment
(`gltf.hpp:141`: "Times are seconds, strictly increasing") but never
verified anywhere in the code that builds `times` from file bytes.

**Why it matters:** a corrupted or truncated `.anim`/`.skel`/M2 file (the
exact "extraction went wrong" scenario this whole project is built to
survive gracefully, per `FAILURES.md`'s own framing) that flips a bit in a
keyframe timestamp or value lands here completely unguarded — the
resulting `.glb` can carry non-finite animation values, or an accessor
`min`/`max` that doesn't actually bound its data (if timestamps happen to
be out of order), both real glTF-spec violations of the same shape
`FAILURES.md` #4 already fixed for a different field, just not caught by
husk itself and only visible once a downstream tool (Blender, the
Khronos validator this project already plans to run) trips over it.

**Suggested fix:** the same `isFinite`-style check `FAILURES.md` #4 added
for vertex positions/normals, applied to each resolved keyframe's
timestamp and value in `buildAnimations` (where the sequence/bone/property
identity is still known, same reasoning as the original fix), plus an
explicit monotonic-timestamp check before trusting `front()`/`back()` as
true min/max.

**Applied:** `cmd_export.cpp`'s new `checkKeyframesWellFormed` (a small
template over `Vec3`/`Quat`) checks every resolved keyframe's value for
finiteness and every timestamp for being strictly greater than the
previous one, called once per bone/property right after
`resolveVec3TrackSequence`/`resolveQuatTrackSequence`, naming the bone
index, property, and offending keyframe index on failure. Verified against
real data: `bloodelffemale.m2`'s 256 real animation clips (73,465 rotation
keyframes) still export cleanly with this check active, confirming it
doesn't false-positive on well-formed real data. Regression tests:
`tests/test_cli.cpp`'s "a non-finite (NaN) translation keyframe value
fails with a real message..." and "a non-monotonic (out-of-order)
translation keyframe timestamp fails with a real message".

---

## 10. [Retracted] The documentation mirror is missing the actual M2 wiki page

**This finding was wrong.** `documentation/wowdev-wiki/md/M2.md` does
exist — a real, complete, well-formed rendering of the M2 page (frontmatter,
header struct, tables, chunk sections, all the way through the trailing
`References`/`Category` footer), confirmed by reading the file directly.
The original claim was based on a `find -iname "M2.md"` that came back
empty; either that check was run against a filesystem state where the file
genuinely wasn't there yet (this mirror is populated by an external
process, per `WIKI_FINDINGS.md`) or the check itself was mistaken — either
way, the file is there now and the cross-links from `M2/.skin.md` etc.
resolve correctly. `M-M2.md` (the unrelated 2008-era ADT page) still also
exists alongside it, under its own distinct name — that part of the
original finding was accurate, it just wasn't evidence of a missing page.
No action needed here.
