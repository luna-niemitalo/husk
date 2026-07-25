# Failure log 2

**Status: all items below are open** — this is a read-only inspection pass
(no source files were modified, no commands were run against the built
binary; every finding below comes from reading `src/`, `tests/`, `README.md`,
`FAILURES.md`, `TODO_correctness.md`, `WIKI_FINDINGS.md`, and the
`documentation/` tree, including the local wowdev.wiki mirror). It's a
follow-on to `FAILURES.md` (five previously-found bugs, all fixed) and
`TODO_correctness.md` (coverage gaps already tracked): this pass went
looking specifically for (a) features a *comprehensive, user-friendly* M2
reader should have that husk doesn't yet, and (b) new ways husk breaks
that neither of those two files already name. Nothing here duplicates an
item already open in `TODO_correctness.md` (particles, `AFSB`, cameras,
`.bone` LOD integration, `.skel`'s `SKL1`/`SKA1`/`SKPD`/`key_bone_lookup`) —
those are real, known, and still true, just not repeated here.

Findings are ranked most-impactful first. "Impactful" here means: how much
of a *real* model's visual/structural correctness is affected, and how
likely a normal user is to hit it without doing anything unusual — not
"is this reachable by a hand-crafted adversarial file."

---

## 1. [Open] [Critical] `M2SkinSection.skinSectionId` (the "geoset ID") is never read — `husk export` renders every submesh in a `.skin` file at once, including mutually-exclusive character-customization options

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

---

## 2. [Open] [High] `husk export --textures` can produce a `.glb` with misaligned glTF buffer views/accessors — a real spec violation, not a hypothetical one, for any embedded PNG whose byte length isn't a multiple of 4

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

---

## 3. [Open] [High] Bone/Sequence/Ribbon record sizes are hardcoded to a "Wrath+" shape, but `expansionForVersion` happily recognizes and labels genuine Classic/TBC files — feeding husk a real pre-Wrath M2 silently misreads bone/sequence/ribbon data at the wrong byte stride

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

---

## 4. [Open] [Medium] `husk info` never prints resolved texture or material records, and never prints `TXID` (`textureFileDataIds`) at all — inconsistent with every other per-record array `info` surfaces, and with the README's own description of what `info` does

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

---

## 5. [Open] [Medium] The `TEXL` chunk tag is a complete blind spot — recognized by `husk info` (so it never triggers the "undocumented chunk" warning) but not handled anywhere by `husk dump-chunks`, silently contradicting the project's own "never silently drop a real chunk" policy

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

---

## 6. [Open] [Medium] Multi-texture batches (`M2Batch.textureCount` 1-4) always resolve to exactly one texture, with no diagnostic that a batch actually uses more than one

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

---

## 7. [Open] [Low-Medium] Global-sequence-driven `M2Track`s (continuous glow/pulse/sway animation) always resolve to nothing, with no user-facing indication any track was skipped for this reason

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

---

## 8. [Open] [Low] Several FileDataID/struct-array chunk readers silently truncate on a non-record-aligned chunk size instead of erroring, unlike every other fixed-record array parser in the codebase

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

---

## 9. [Open] [Low] No finiteness/monotonicity validation on animation keyframe timestamps or values — the same bug class `FAILURES.md` #4 fixed for vertex positions/normals, left open for animation data

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

---

## 10. [Open] [Low, meta] The documentation mirror is missing the actual M2 wiki page — `documentation/wowdev-wiki/md/` has no `M2.md`, only an unrelated, decades-obsolete page that happens to share a similar name

**What's expected:** since this project's entire correctness story rests
on "read directly from wowdev.wiki, verified against real files"
(README.md's "Why test-first" section), the local mirror
(`documentation/wowdev-wiki/`) ought to actually contain the main M2 page
in its readable (`md/`) form, for anyone using the mirror as a reference
rather than the live wiki.

**What actually happens:** `documentation/wowdev-wiki/wikitext_expanded/M2.wiki`
exists and is substantial (2,594 lines) — the raw page *was* fetched and
template-expanded successfully. But `documentation/wowdev-wiki/md/M2.md`
does not exist anywhere in the mirror. The only markdown file in that
directory whose name resembles "M2" is `M-M2.md` — which, on inspection,
is a completely unrelated, ~2008-2016-era page about `CMapObject`/ADT
in-game object memory offsets and a Warden-style "highlight the selected
object" client hack, with zero relation to the M2 *model file format*
this entire project reads. Every subpage (`M2/.skin.md`, `M2/.skel.md`,
`M2/Rendering.md`, etc.) links back to `../M2.md` for the main page — those
links are dead in this mirror.

**Why it matters:** low severity (husk's own source code clearly *was*
checked against the real page's content at some point — the offsets in
`m2.hpp`/`m2.cpp` match reality, and `wikitext_expanded/M2.wiki` has the
source text on disk even if it was never rendered to `md/`), but this
was surfaced directly by the task of "read the documentation": anyone
else opening this mirror's `md/` tree specifically to cross-check a
header-offset claim, expecting the main M2 page to be there the way every
other page is, will silently land on nothing (or worse, might not notice
`M-M2.md` is a different page and briefly think the mirror is corrupted
or that the M2 spec really is that page).

**Suggested fix:** re-run whatever step converts `wikitext_expanded/*.wiki`
to `md/*.md` for `M2.wiki` specifically (the source text is already
present, so this shouldn't need re-fetching anything) — a build/tooling
gap in the mirror, not a content gap.
