# REFACTOR_LOG.md — running log of REFACTOR/ migration work

Each entry: what was done, why, what it deliberately did *not* touch, and how
it was verified. Punch-list items get removed from the `REFACTOR/*.md` files
they came from once done (see those files' own "closed items get removed"
convention) — this log is the narrative git history doesn't give you at a
glance, not a duplicate of the plan.

**Entries older than ~2 weeks are trimmed from this live file** (2026-09-16
documentation-consolidation pass) — every prior version of this file is
already a git blob, so nothing is lost: `git log -p -- REFACTOR_LOG.md` (or
`git show <rev>:REFACTOR_LOG.md`) recovers anything trimmed. This keeps the
live file relevant to current work instead of an ever-growing append log.

---

## 2026-09-16 — Stage 3 texture-payload closures: DDS wired into bundle output, `compareMaterialBlendModes` additional-layer check

**Wiring real DDS output into a real export.** `AUDIT.md` §7.4 had named this
as the separate, larger follow-up once `blp::extractRawPayload`/`encodeDds`
existed (previous entry below) but nothing called them from a real export
path. Closed without touching `sources::Catalog::texture()`'s own return
contract or tier order/policy at all — the risk this task was scoped to
avoid. `canon::TextureRef` gained a second, independent field, `rawPayload`
(same `optional<Payload>` shape `payload` already has, `canon_material.hpp`)
— additive on purpose: `payload` stays the PNG projection `gltf_lean.cpp`
embeds (DDS is not a valid glTF image format, full stop, so it could never
hold this), `rawPayload` is the lossless DDS-housed source payload
`bundle_writer.cpp` now writes. Neither is computed from the other; a
producer may populate one, both, or neither.

The real fork this task's own brief flagged — "how does a caller get the
real resolved file path back out of `Catalog::texture()`'s answer" — turned
out to have a small, additive answer, not a contract change:
`sources::EncodedTexture` (`catalog.hpp`) gained a `sourcePath` field
(provenance, not payload, explicitly not part of I8's `{bytes, encoding}`
pair), populated at the three real bytes-producing sites
(`resolveLiteralTier`/`resolveListfileTier`'s own existence-checked
`.png`-then-`.blp` resolution, and `resolveFuzzyTier`'s sole-claimed-
candidate path; `resolveDb2CharacterTier` inherits it for free). Every
existing consumer of `EncodedTexture` ignores the new field; nothing about
`texture()`'s tier order, caching, or ambiguity handling changed.
`cmd_export_canon.cpp`'s `toCanonTextureRef` uses `sourcePath`'s extension
to decide whether a real `.blp` sits behind a `Resolved` hit — when it does,
`blp::extractRawPayload`+`blp::encodeDds` produce the DDS bytes; when it
doesn't (already a `.png` on disk, no real file behind the hit, or
`extractRawPayload` itself declines for Palette/JPEG/ARGB8888_DUP),
`rawPayload` simply stays unset, non-fatal — the PNG `payload` is untouched
either way. `bundle_writer.cpp`'s `writeTextureRef` now prefers `rawPayload`
over `payload` when both are present (writing `textures/<name>.dds`,
matching `BUNDLE_FORMAT.md`'s own shape sketch for the first time with a
real compressed encoding), falling back to `payload`'s `.png` otherwise.

**`gltf_lean.cpp`, `export_animation.*`, `export_skeleton.cpp`,
`export_materials.cpp`, and `cmd_export.cpp`'s own orchestration are
untouched** — confirmed via `git diff --stat` against each, empty. Re-ran
`husk export --compare-canon` against `bloodelffemale_hd.m2` — still "clean,
no deviations found," `.canon.glb` still embeds 4 real `image/png` images
(`\x89PNG` magic confirmed), same count as before this change. The bundle
side improved concretely: `.canon.bundle/textures/` now holds 4 real `.dds`
files with correct manifest `uri`s; one
(`bloodelffemale_hd_3536810.dds`) checked byte-for-byte against its real
source `.blp` — `"DDS "` magic confirmed, block region byte-identical to
the source's own mip0 region, re-sliced independently from the BLP2
header's own tables. New tests in `tests/test_writers_bundle.cpp`. Full
suite green, 1008/1008.

**`compareMaterialBlendModes`: additional texture layers (`layers[1..]`).**
Legacy's own secondary-layer shape (`gltf::Material::additionalTextureLayers`,
`gltf_mesh.hpp`) is a bare `{fileDataId, texCoord, imagePng}` tuple, built by
`export_materials.cpp`'s own `layerOffset` loop — the identical loop shape
`m2_material_input.cpp`'s `assembleMaterial` independently walks to build
`layers[1..]`, so `layers[j+1]` and `additionalTextureLayers[j]` name the
same real M2 texture unit by construction (confirmed by reading both loops
side by side). `compareMaterialBlendModes` now checks a layer-count
mismatch as its own deviation, plus per-layer texture identity and
`KnownUnresolved` state, same pattern layer 0 already had. Blend op and
tint/alphaFade/uvAnimation curves are honestly NOT checked for additional
layers (canon itself never populates those fields on anything but
`layers.front()`, and legacy's struct has no field for them either —
nothing real to compare on either side, not an oversight); `Ambiguous`
isn't checked either (legacy's additional-layer resolution never runs the
fuzzy tier that populates `alternateTextureCandidates`, so legacy has no
signal to check against — a structural asymmetry, not a missed case). 3
new tests. Verified against the real multi-texture-layer fixture
(`world/replaceabletextureprops/guild/pennant_guild_alliance_a_01.m2`) —
clean, no deviations found. Full suite green, 1007/1007 (landed just
before the DDS work above, at a 1004 baseline).

## 2026-09-15/16 — Real raw-BLP-block extraction + DDS housing (`blp.hpp`)

A wrong claim ("would need real raw-BLP-block extraction in C++, which
doesn't exist") was caught by Luna's own direct question: "how did husk
work previously without using the blp python tool?" `src/blp.hpp`/`.cpp` is
a real, complete, from-scratch C++ BLP2 decoder with nothing to do with the
separate Python `blp/` tool (`export_texture_resolution.cpp`'s
`readTextureFileBytes` already calls it) — it already parses the full BLP2
header/mip table and already knows each mip's real encoding (DXT1/3/5,
BGRA, palettized) while decoding to RGBA. The hard part (header/mip
parsing) was never missing; what was missing was stopping one step earlier
(return the real bytes instead of decoding them) and a DDS header writer.

`blp::extractRawPayload` returns mip level 0's real bytes verbatim — for
DXT1/DXT3/DXT5 (tagged `RawEncoding::Bc1/Bc2/Bc3`) and BGRA (tagged `Bgra`,
BLP's own in-file B,G,R,A byte order needing no reordering, since it's
already the standard DDS A8R8G8B8 memory layout) — reusing `decode()`'s own
header/mip-offset parsing, just returning the slice instead of decoding it.
Palette is deliberately NOT extracted (throws a clear `ParseError`): an
8-bit paletted image needs a real DDS-palette-format decision (legacy
D3DFMT_P8 has thin modern tool support, unlike BC1-3/BGRA) this project
hasn't made. `blp::encodeDds` wraps a `RawPayload` in a real, minimal,
standard Microsoft DDS container ("DDS " + 124-byte `DDS_HEADER` + bytes
unchanged), hand-verified field-by-field against the public DDS spec.

Verified against real data: ran both against this repo's own real 512x512
DXT5 `bloodelffemale_hd_hair_style_3500071.blp` — extracted bytes
byte-identical to the source file's own mip0 region (re-sliced
independently via the file's own header fields); the resulting `.dds`'s
block region byte-identical to those same bytes (a true lossless header
swap); and, interactively, Pillow 12.3.0 (a real, independent, public DDS
reader) opens the file and decodes it to a 512x512 RGBA image 99.6%+
byte-identical to `decode()`'s own reference decode (the small remainder is
normal cross-decoder DXT rounding variance, already documented, not a
defect in the stored blocks). 9 new tests in `tests/test_blp.cpp`. Full
suite green, 988/988.

Not wired into a real export at this point in the session — see the
2026-09-16 entry above for that follow-up, closed the next day.

## 2026-09-15 — `canon::TextureRef::payload`: real texture-embedding + the render-diff tool's first real catch

Built per Luna's own framing: "render could be automated in that one
generates 2 outputs, 1 via new, and 1 via old, and if they match, it's
likely correct... if they differ then flag for my review." New
`tools/render_lean_glb.py` (headless Blender, deliberately independent of
`tools/corpus_scan_tasks/render_glb.py` and every husk-extras fixup it
applies, since running that script against an extras-free `.canon.glb`
would score every content gap it papers over as a canon:: bug) +
`tools/compare_canon_render.py` (driver: renders a `--compare-canon` pair's
legacy `.glb` and `.canon.glb` through the identical minimal importer,
pixel-diffs the stills).

**First real run found a large diff (18.3 mean-channel, 12.5% of pixels)** —
canon's render showed a visibly different, crouched/distorted pose from
legacy's normal standing one. **A bug in the tool itself, not in canon::**,
caught by Luna directly: the render script cleared
`armature.animation_data.action = None` to force a comparable rest pose,
but Blender's glTF importer can push clips onto NLA tracks that keep
influencing pose evaluation regardless of `.action`. Fixed by setting
`armature.data.pose_position = 'REST'` instead — bypasses pose evaluation
entirely. Re-rendered: the two silhouettes now match closely, corroborating
`--compare-canon`'s own clean structural diff, not just "no crash."

**One real gap survived the fix**: canon's render was fully white/
untextured. Root-caused by reading `src/writers/gltf_lean.cpp` directly:
its material loop set only `baseColorFactor`/`alphaMode`, never an
`images`/`textures` entry — a real implementation gap, not a documented
design cut (`gltf_lean.hpp`'s "no extras" scope note is about extras
specifically, texture embedding is core glTF).

**Closed same day**: Luna pushed on the "writer re-fetches through the
catalog" framing directly — it didn't match `BUNDLE_FORMAT.md`'s own
settled design (canonical payload is the source-format compressed blocks,
rehoused into DDS, never re-fetched live at write time), and asked how the
other binary chunks (mesh/skeleton/animation) handle this. Every other
chunk holds its real payload data inline in the value type (`canon::Mesh::
positions`, `canon::Curve::keyframes`) — no parallel resource map anywhere.
`BUNDLE_FORMAT.md`'s "Embed or reference — one rule" answered the
modularity question: embed-vs-reference is a manifest/writer-time choice
per resource, not baked into the in-memory shape — so the payload belongs
directly on `canon::TextureRef`, as `optional<Payload>`, not mandatory and
not a separate map.

Implemented: `canon::TextureRef::payload` (`{bytes, TextureEncoding}`, the
settled `Bc1/Bc2/Bc3/Bgra/Palettized/Png` vocabulary, though only `Png` was
reachable at this point). `cmd_export_canon.cpp`'s `toCanonTextureRef`
populates it from the real `sources::Catalog` answer already in hand. Both
writers wired: `gltf_lean.cpp` embeds a real image/texture entry and sets
`baseColorTexture`, deduped by resolved name; `bundle_writer.cpp` writes
`textures/<name>.png` with a real manifest `uri`. Verified against real
data: re-ran `bloodelffemale_hd.m2` — still clean, 0 deviations; bundle
wrote 4 real `.png` files matching the 4 of 10 materials that actually
resolve; render-diff tool showed real texture on those 4 (previously
all-white), remaining diff fully attributable to the pre-existing,
documented `Ambiguous`-tier gap (confirmed by inspecting the diff image
directly — a flat uniform color-shift, the signature of "default white vs.
legacy's real fallback color," not a new defect). 2 new tests. Full suite
green, 979/979.

## 2026-09-15 — `canon::Material` texture resolution: API shape, orchestrator wiring, material dedup, and curve/state comparison

**Design settled**: canon:: is purely the representation of already-decided
facts, never performing resolution itself. `assembleMaterial`/
`assembleModel` gained a `TextureResolutions` parameter
(`unordered_map<uint32_t, TextureRef>`, keyed by M2 texture-array index),
defaulted empty so every existing caller stays byte-for-byte unchanged.
`husk export --compare-canon` (`cmd_export_canon.cpp`'s
`buildTextureResolutions`) resolves every distinct texture-array index
through the SAME `sources::Catalog::texture()` call the legacy pipeline
already makes — one shared catalog answer, never a second opinion — then
converts each `Resolved<EncodedTexture>` into a `canon::TextureRef`.

**Material dedup, the real structural gap this surfaced**: legacy dedupes
materials by content signature; `canon::assembleModel` originally didn't at
all (70 surviving batches vs. 8 distinct legacy materials on
`bloodelffemale.m2`). Fixed: `canon::Material` gained real identity (`Ref
ref`), `canon::Model::materials` is genuinely deduped by a batch-identity
tuple key (`materialIndex, textureCount, textureComboIndex,
textureCoordComboIndex, colorIndex, textureWeightComboIndex,
textureTransformComboIndex` — this fully determines what `assembleMaterial`
would build, so two batches sharing it share one `canon::Material`, found
by identity rather than legacy's hash-the-built-content-after-the-fact
approach), and `canon::Model::primitiveMaterials` + a new shared
`canon::resolveMaterialIndex(model, primitiveIndex)` (used by both real
writers and `canon_diff.cpp`) replace the old implicit 1:1 positional
assumption. Verified: canon's own deduped material count came out to
exactly 8 on `bloodelffemale.m2`, matching legacy's real count precisely —
independent confirmation the tuple-identity key lands on the same
equivalence classes legacy's content-hash key does.

**`compareMaterialBlendModes` texture-identity + curve + state checks**:
gained an optional `legacyMaterials` parameter — checks the canon-resolved
`FileDataId` against legacy's own `baseColorTextureFileDataId` when both
sides are `Resolved`. Same day, closed further: tint/alphaFade/uvAnimation
curve comparison (canon's primary layer resolved against exactly one
sequence, matched against legacy's own `AnimatedXCurve` vector entry with
the same `sequenceIndex`, compared keyframe-for-keyframe as raw M2-space
values — no `zUpToYUp`, these are colors/texture-space transforms, not
positions; `alphaFade` accepts a match against either of legacy's two
separate vectors since canon collapses both into one slot) and
`KnownUnresolved`/`Ambiguous` state checks (against legacy's own
`baseColorImagePng`-empty and `alternateTextureCandidates`-non-empty
signals respectively — both real legacy fields with directly matching
documented meanings). 10 new tests. Verified against real data:
`bloodelffemale_hd.m2` plus three real texture-transform-animated fixtures
(`brewfestmount.m2` — tint/fade + UV rotation; `bloodknightcharger.m2` — UV
scale; `7037014.m2` — UV translation) all stay clean. Full suite green,
998/998.

**Verified end to end against real, resolved-to-real-bytes texture data**
(closing the "no real fixture with a populated `--textures` directory" gap
the same day): ran the real local CASC export
(`/media/luna/data/wow_export`) with a real `--listfile` — 6 of 10
materials resolve to `Resolved` with a real FileDataID (4/10 `Ambiguous`,
an honest same-basename fuzzy-tier limitation, not a bug), and the
texture-identity check genuinely exercises all 6 against legacy's own
value — clean, no deviations, across mesh/skeleton/animations/materials
together. Closes the *resolution* half of end-to-end verification; a real
visual/rendered check was still separately open at this point (closed
later the same day — see the render-diff-tool entry above).

## 2026-09-15 — canon animation: external `.anim` resolution, global sequences, alias resolution, `.skel`-sourced coverage

**Global sequences and alias resolution**: `canon_model.cpp`'s three-pass
`assembleModel` (pass 1 real-inline, pass 2 global-sequence, pass 3
alias-to-terminal reuse), verified clean against `bloodelffemale.m2`. A
second bug found along the way: `buildAnimations` (legacy) only pushes a
clip when at least one bone produced real keyframe data for that sequence;
canon's pass 1 pushed a clip for every real-inline sequence
unconditionally. Fixed by adding the same `anyBoneAnimated` gate pass 2
already had.

**External `.anim` resolution**: `canon::assembleModel` gained an
`ExternalAnimBlobs` parameter (`unordered_map<uint32_t, vector<uint8_t>>`
keyed by sequence index, same pre-resolved-input-parameter shape as
textures). The file-search half (deciding which FileDataID's `.anim` bytes
to fetch) is reimplemented independently inside `cmd_export_canon.cpp`
itself, mirroring `buildAnimations`'s own logic rather than sharing code
with it — deliberate: an earlier pass of this work had factored the shared
logic directly out of `buildAnimations` into a new function it then called
too (verified behavior-preserving), but Luna correctly flagged that as
wrong regardless of correctness, since editing the legacy file at all means
the "legacy" side of every `--compare-canon` run is no longer the real
shipping pipeline. Reverted, reimplemented canon-side-only instead — `git
diff` against `export_animation.cpp`/`.hpp` stays empty. Verified against
real data: `bloodelffemale.m2` with a real external `.anim` file, legacy's
own clip count rises 258→260, `--compare-canon` stays clean.

**`.skel`-sourced coverage, Stage 3's next-biggest blocker**: previously
`m2input::buildCanonModel` read `model.bones`/`model.sequences`/`model.blob`
directly (the inline M2 source) regardless of `bonesAreInline` — a
`.skel`-sourced export (the common player-character case, including this
repo's own primary `bloodelffemale_hd.m2` fixture) either silently compared
against the model's own irrelevant, usually-empty inline data, or threw.
Fixed: `m2input::buildCanonModel` gained an `ExternalSkeletonSource*`
parameter (`{bones, sequences, blob}`, mirroring `skel::parseBones`/
`parseSequences`/`boneTrackBlob`'s own shapes, default `nullptr` — every
existing caller unchanged). When supplied, skeleton assembly, mesh-skinning,
and all three bone-animation passes read from it instead of `model`'s own
inline arrays — mesh geometry and materials stay `model`-sourced regardless
(`.skel` only ever replaces bones/sequences). Verified against real data:
`bloodelffemale_hd.m2` (245-bone, 338-animation, real `.skel`-sourced
character) reports clean, no deviations — not just "no crash." 2 new
tests. Full suite green, 977/977.

Narrow, named, not independently verified: `assembleMaterial`'s own
`sequenceIndex` for a `.skel`-sourced model's material tint/alpha-fade/
UV-animation curves is used as-is, on the documented assumption that inline
material M2Track arrays are sized to match whichever sequence source is in
effect — not confirmed against real data for the material-track case
specifically. **Still open.**

## 2026-09-15 — `canon::assembleModel` took `m2::`/`skin::` types directly (I1 violation, sub-assemblers + `Vec2`/`Vec3`/`Quat`)

Found while adding the material-dedup work above: `canon::assembleModel`
(the whole-model composition root) took `m2::Model`/`skin::Batch`/
`skin::Submesh`/`vector<uint32_t>` directly and walked them itself —
silently a second M2-input-module in disguise. `CANONICAL_MODEL.md`'s I1
never technically forbade this (it constrains struct *fields*, not
assembly *function signatures*) — I1 was extended with an explicit clause
naming it (Luna's own framing: "the assemble model... should not care if
the source format is a potato or m2").

Fixed: the old orchestration body moved to a new file,
`src/m2_canon_input.hpp`/`.cpp` (`husk::m2input::buildCanonModel`) — a
sibling of canon::, not part of it. `canon::assembleModel` shrank to real
composition only, no m2/skin type anywhere in it. Same-day follow-up: the
sub-assemblers (`assembleMesh`/`assembleSkeleton`/`assembleMaterial`/
`assembleBoneAnimation(Global)`) moved into `namespace husk::m2input` too
(`m2_mesh_input.*`, `m2_skeleton_input.*`, `m2_material_input.*`,
`m2_animation_input.*`), with their pure canon:: value types split out into
struct-only files (`canon_mesh.hpp`, `canon_animation.hpp`) that stay in
`husk::canon`. A second same-day follow-up: `canon::Mesh::positions`/
`normals`/`uv0`/`uv1`, `canon::Skeleton::Joint::globalPosition`, and
`canon::VecCurve`/`QuatCurve` were declared as `m2::Vec3`/`m2::Vec2`/
`Curve<m2::Vec3>`/`Curve<m2::Quat>` — the same I1 shape violation, one
level down. New `canon_primitives.hpp` gives canon:: its own `Vec2`/`Vec3`/
`Quat`; every real producer converts at its own boundary instead.

Verified against real data: exporting `bloodelffemale.m2`, canon's deduped
material count came out to exactly 8, matching legacy's real count
precisely (see the material-dedup entry above for the fuller account).
Full suite green, 975/975, before and after both follow-ups.

---

## 2026-09-04 — End-of-run comment-discipline pass + two doc-accuracy fixes

**What**: Trimmed every comment the five m2::Model-migration commits
(7f7c49a..HEAD) added across the 12 touched `src/` files, per
`~/nix/claude-rules/CODE_COMMENTS.md` -- removed every citation to
`AUDIT.md`/`REFACTOR/`/`REFACTOR_LOG.md`/`TODO/`/split numbers, all
historical narrative ("promoted from ... split 4a"), and restated-the-code
prose; kept every genuine trap tersely (the particle-emitter version gate
in `dump_emitters.cpp`/`export_extras.cpp`; `resolveBones`'
`bonesAreInline` hazard in `cmd_export.cpp`). Promoted the one durable
cross-cutting fact repeated at half a dozen call sites -- `husk info`/
`dump-chunks` diagnose-and-continue on a malformed field, `husk export`
fails fast -- to `DESIGN.md`'s "Key design decisions" as a single bold-
paragraph entry, replacing it at each call site with a short pointer-free
note. Everything else removed outright was already recorded either in
this file's own migration-commit entries or the commit messages
themselves, so nothing else needed a landing spot.

Also fixed the two doc-drift items `LOOP_STATE.md` flagged ("More doc
drift found, not fixed") for this pass: `REFACTOR/BLENDER_ADDON.md:25-32`
described six answers to "where is this texture," including a `PATH`/
`../build/husk` binary lookup and a `husk blp-export` subprocess --
confirmed absent from the real `tools/husk_blender_geoset_mask.py`
(removed 2026-08-29; grepped directly for
`_find_husk_binary`/`_convert_blp_to_png_cached`/`subprocess`, zero hits)
-- corrected to the real five answers, citing `AUDIT.md` §1.1 (which
already documents the removal) instead of the stale §4 citation. The
paired `render_glb.py`/`render_sample_driver.py`-path claim against
`AUDIT.md` turned out already fixed: `AUDIT.md` §4 was reduced to a bare
pointer at `TODO/RENDER_PIPELINE_DRIFT_TODO.md` by an earlier commit
(`97bc6630`) already landed before this pass started -- confirmed by
grepping every `REFACTOR/*.md` for a stale `tools/render_glb.py`/
`tools/render_sample_driver.py` path: zero matches anywhere in the tree,
so `AUDIT.md` needed no edit this pass.

**Why**: Luna caught the five migration commits' comments violating
`CODE_COMMENTS.md` on density (354 comment lines vs. 310 code lines --
more comment than code), duplication risk (21 references to plan
documents whose own future edits would strand the citation, which is
exactly what already happened once -- an earlier `AUDIT.md` §4 edit this
session stranded 12 of them), and historical narrative that belongs in a
commit message, not inline. The doc-drift items were two already-known
stale facts left for this pass rather than expanded into unasked-for
scope mid-migration.

**Verified**: Comment lines in the loop's `src/` diff (7f7c49a..HEAD),
counted as lines matching `^\+\s*//`: **354 before -> 171 after**.
Non-comment added lines unchanged at exactly **310** both before and
after (confirms no code line was touched, only comments). New
comment:code ratio 0.55 (was 1.14). `git diff -- src/ | grep '^[+-]' |
grep -v '^[+-][+-]' | grep -v '^[+-]\s*//'` shows only two lines, both a
floating field comment folded onto the same, otherwise-unchanged
declaration line (`globalLoops`/`boneCombos` in `m2_model.hpp`) -- no
code token changed. Rebuilt and reran the full suite after the comment
pass and again after the doc fixes:
`801 test cases | 801 passed | 0 failed | 1 skipped`, `6559 assertions`,
matching the measured baseline exactly both times.

---

## 2026-09-04 — `AUDIT.md` §8 and §4 closed/reduced, doc-accuracy pass, no code changed

**What**: `AUDIT.md` §8 ("Duplicated / drifting constants in corpus tooling")
removed outright. `AUDIT.md` §4 ("Blender-side coupling — every item violates
I3") reduced from three bullets to a one-line pointer at
`TODO/RENDER_PIPELINE_DRIFT_TODO.md` item 1, heading changed to drop the now-
false "every item violates I3" claim. No source under `src/`/`tools/`/`tests/`
touched; `git diff --stat` for this pass is `REFACTOR/AUDIT.md` and this file
only (`CLI_AND_TOOLING.md` needed no edit — see below).

**Why, §8**: re-verified the "Done" claim against the real tree rather than
trusting the prose. `black_additive_task.py`, `casc_size_mismatch_task.py`,
`unfillable_texture_task.py`, `texture_dedup_collision_task.py`,
`m2_full_validation_task.py`, `particle_only_task.py` — grepped each for a
top-level `ROOT`/`LISTFILE`/`HUSK_BIN`/`CORPUS_ROOT` assignment: zero matches
in any of the six. Every real reference in those files goes through
`csf.ROOT`/`csf.LISTFILE`/`csf.HUSK_BIN` (`texture_dedup_collision_task.py`/
`black_additive_task.py` alias `HUSK_BIN = csf.HUSK_BIN` at module scope, still
one source, not a copy). `tools/corpus_scan_framework.py` confirmed to expose
`ROOT`/`LISTFILE` as module-level `None`-until-`_init_worker` values and
`HUSK_BIN` as a real computed `shutil.which("husk") or <build path>`
expression, matching the doc's description exactly. `tools/corpus_scan_tasks/
render_sample_driver.py` (the actual current path — moved into
`corpus_scan_tasks/` since §8 was last written, not `tools/` directly)
confirmed to still hardcode all three (`CORPUS_ROOT = Path("/media/luna/
data/wow_export")`, `HUSK_BIN = Path(".../build/husk")`, `LISTFILE = Path(...
community-listfile.csv")`) — the claim holds exactly as written, nothing
drifted. Removed the section per `AUDIT.md`'s own stated convention ("An item
is removed from this file when it is fixed").

The `render_sample_driver.py` exclusion is not lost: `CLI_AND_TOOLING.md` §4
already states it, in more detail than §8 did (the same "driver script, not a
`ScanTask`, part of the human-gated render pipeline" reasoning, plus the two
real bugs found fixing the other six modules) — checked line by line before
concluding no edit was needed there. §8 was, on inspection, already a
compressed re-statement of facts §4 owned in full; deleting §8 is pure
subtraction, not a move that needed a landing spot written.

**Why, §4**: read each of the three bullets against the real files they cite,
not the prose.

- **Bullet 1 (four texture-discovery mechanisms in `tools/
  husk_blender_geoset_mask.py`)**: confirmed in code — `main()`'s
  `textures_dir` resolution chain is exactly `--textures` → the imported
  `.glb`'s own directory → the current `.blend`'s directory
  (`bpy.path.abspath("//")`) → a preferred `textures/` subdirectory next to
  it, and `grep -n "subprocess\|shutil.which\|blp-export"` across the file
  turns up zero live call sites (three remaining hits are all doc-comment
  prose describing the removal, not code). This is the same finding
  `AUDIT.md` §1.1's own "closed 2026-08-29" entry already carries in full,
  with the actual measurement (7/559 vs. 0/826 fallback firing rate) this
  bullet only summarized. Closed, reasoning already recorded at §1.1 —
  removed rather than left as a second, shorter copy of the same fact.
- **Bullet 2 (duplicated `_root_joint_extras`/`_deep_copy_id_property` in
  `tools/husk_blender_options_panel.py`)**: confirmed the load-bearing
  blocker is real and documented in place — the comment block directly above
  `_deep_copy_id_property` (now `:86-107`, drifted a few lines from the
  `:87-104` the doc cited, substance unchanged) states the `__file__`-is-
  synthetic-for-a-registered-embedded-Text-datablock finding concretely, and
  the geoset vertex-group prefix/regex constants are still duplicated a
  third time right above it for the identical reason. A consciously accepted
  trade-off with its reasoning recorded at the point of duplication, not an
  open item — removed.
- **Bullet 3 (`render_glb.py` skips the customization/geoset stages)**:
  partially drifted, still genuinely open. `tools/corpus_scan_tasks/
  render_glb.py` (also moved into `corpus_scan_tasks/` since §4 was last
  written) now calls `billboard_align.apply_geoset_switches(mesh_objs,
  armature_obj)` before framing/rendering — the geoset stage the old bullet
  said never ran, now does. It still never calls
  `apply_customization_texture_switch`/reads `chr_enabled_materials`, so a
  model's real customization-choice *texture* selection (skin/hair/eye
  color, ...) still previews as whichever candidate husk happened to embed
  as each slot's default — the exact live bug `TODO/
  RENDER_PIPELINE_DRIFT_TODO.md` item 1 already tracks in full, independent
  of this session. Rather than fix the drifted "billboard alignment only"/
  "never runs...geoset stages" prose in place, reduced to a one-line pointer
  at that TODO per the single-source-of-truth rule — the TODO is the
  authoritative, current description now, this file no longer carries its
  own copy to go stale again.

All three bullets addressed, so §4's heading no longer holds — "every item
violates I3" described three items, two of which are closed and the third of
which (a stale-preview bug from an incomplete pipeline call, not a directory-
searching/subprocess-shelling violation) was never really an I3 violation in
the first place. Heading changed to plain `## 4. Blender-side coupling`.

**Verified**: full suite still green after the doc-only edit —
`801 test cases | 801 passed | 0 failed | 1 skipped`, `6559 assertions`,
matching the recorded baseline exactly (`direnv exec . cmake --build build`
then `direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests`).

**Not touched, found stale in passing, out of this pass's own edit scope**:
`REFACTOR/BLENDER_ADDON.md:25-29` still describes "six answers" to "where is
this texture" (including the `PATH`/subprocess mechanisms this same §4 already
recorded as removed on 2026-08-29) and cites `AUDIT.md` §4 as its source —
that prose was already stale before this pass touched §4 at all, and is worth
a follow-up, but `BLENDER_ADDON.md` is outside this pass's edit scope
(`AUDIT.md`/`CLI_AND_TOOLING.md`/`REFACTOR_LOG.md` only).

---

## 2026-09-04 — `cmd_export.cpp`'s `resolveBones`/`resolveAnimationsForModel` migrated onto `m2::Model`, split 4c of 3 -- AUDIT.md §2.1 closed and removed (REFACTOR/README.md's Migration order step 2)

**What**: the last two `m2::parse*` call sites in `cmd_export.cpp` --
`resolveBones`'s `m2::parseBones(blob, header.bones)` and
`resolveAnimationsForModel`'s `m2::parseSequences(blob, header.sequences)`
(only in the `bonesAreInline` branch; the `haveSkel` branch's own
`skel::parseSequences` call is unrelated and untouched) -- now read
`model.bones`/`model.sequences` instead. Both functions' signatures
changed from a raw `(blob, header, ...)` pair to `(const m2::Model& model,
...)`. `exportOneModel`'s own former local `rethrowIfParseFailed` lambda
(split 4a) is gone -- every one of its 9 call sites now calls the shared
free function (`export_extras.hpp`, promoted in split 4b) directly, since
keeping both a same-named local lambda and the shared function side by
side in the same scope would have shadowed the free function for no
reason.

**The hazard, confirmed against the real code before implementing, exactly
as this task's own brief predicted**: `resolveBones` derives
`bonesAreInline` from `bones.empty()`. Under the old direct
`m2::parseBones` call, a malformed inline bones array threw ParseError and
aborted the export. Under `m2::loadModel`, the same malformed array
instead yields an empty `model.bones` plus a recorded
`FieldParseFailure` (`m2_model.hpp`'s doc comment) -- naively switching to
`model.bones` without a rethrow would make `bonesAreInline` false, and the
export would silently proceed to either fall back to an external `.skel`
(if `--skel` was given or one auto-detects next to the model) or export an
unskinned mesh -- a **wrong data source**, not just an empty field, with
exit 0 and no error at all. Same shape for `resolveAnimationsForModel`'s
`sequences`: a malformed inline sequences array would silently read as "no
animations for this model" instead of a loud parse error. Both are fixed
by calling the shared `rethrowIfParseFailed(model, field)` (split 4b) at
the exact point the old direct `parse*` call used to run -- `resolveBones`'s
own first line (before the `bonesAreInline` branch splits), and
`resolveAnimationsForModel`'s `bonesAreInline` branch's own first line
(before the `haveSkel` branch, which parses its own unrelated sequences)
-- so a malformed file still reports the same field's error first, at the
same relative position in `exportOneModel`'s overall sequence, not a later
field's.

**Design decisions from this task's own brief, verified against the real
code rather than re-litigated**:

- **Promoting `rethrowIfParseFailed` to a shared function was earned, not
  speculative**: confirmed six real call sites across two files
  (`exportOneModel`/`resolveBones`/`resolveAnimationsForModel` here, plus
  `attachEmitterAnchors`/`attachPlacementNodes`/`appendCollisionMesh` in
  split 4b) -- past this project's own "third occurrence, or a single
  canonical hub" bar. Already promoted in split 4b; this split just
  finishes adopting it everywhere in `cmd_export.cpp`, including
  `exportOneModel`'s own now-redundant local lambda.
- **`husk export`'s extras path needed fail-fast, not graceful
  degradation** -- confirmed directly, not assumed: reverting the two new
  `rethrowIfParseFailed(model, ...)` calls (a throwaway local edit,
  reverted before committing) and re-running the two new regression tests
  below reproduced exactly the predicted silent-misread: both tests failed
  with exit 0 (not 1) and no parse-error message in the output, proving
  the hazard is real and these tests actually catch it, not vacuous
  assertions.

**Tests**: 2 new CLI-tier regression tests in `tests/test_cli_errors.cpp`
(this file's own stated scope, following its existing convention --
placed as a direct extension of the file's own pre-existing "corrupted
huge bone count" test, which already covered the *unskinned-mesh* half of
this hazard but not the *silent-`.skel`-fallback* half):

- **"a malformed inline bones array is reported even when a real, valid
  external `.skel` is also available"** -- the test the pre-existing "huge
  bone count" test could NOT catch, since it gives no `--skel` at all.
  Corrupts `tinyValidM2()`'s `bones` header field to a huge bogus count,
  pairs it with a real, valid, well-formed external `.skel`
  (`buildSkel({{-1, -1}})`) passed via `--skel`. Before the fix: exit 0,
  the `.skel`'s own one bone silently used instead of the M2's own
  corrupted array being reported. After: exit 1, `"bones array claims"` in
  the output, `.skel` never silently substituted.
- **"a malformed inline sequences array fails cleanly, not silently
  exported with zero animations"** -- `tinyAnimatedM2()` (1 vertex, 1
  inline bone, 1 real sequence), corrupts the `sequences` header field to
  a huge bogus count. Before the fix: exit 0, 0 animations, no error.
  After: exit 1, `"sequences array claims"` in the output.

Both verified to actually fail without the fix (not just pass with it) by
temporarily commenting out the two new `rethrowIfParseFailed` calls,
rebuilding, and confirming both tests fail with exactly the predicted
silent-success shape (`CHECK(result.exitCode == 1)` got `0`), then
restoring the fix and reconfirming the full suite green again.

**`REFACTOR/AUDIT.md` §2.1 closed and removed.** Checked first, not
assumed: `grep -n "m2::parse" src/cmd_export.cpp src/export_extras.cpp`
now matches only comments (no live call), and the previous two entries in
this log already closed `cmd_info.cpp`/`cmd_info_json.cpp`/`cmd_dump.cpp`
-- all four commands §2.1's own table named are migrated onto
`m2::Model`. Removed the whole section outright per `AUDIT.md`'s own
stated convention ("An item is removed from this file when it is fixed --
git history is the record"), rather than leaving a stub. Per this task's
own explicit instruction, `§2.2`/`§2.3`/`§2.4` (`M2MaterialInputs`'s
back-pointer, `gltf::Skeleton` naming, population-order) were left exactly
as numbered, unrenumbered -- `## 2. Missing internal representation` now
goes straight from its own header to `### 2.2`, a deliberate numbering
gap rather than a renumbering that would have invalidated the several
existing citations to those exact section numbers elsewhere (this log's
own `§2.2` citation for `M2MaterialInputs::blob`'s lifetime, split 4a's
entry above). **One stale cross-reference this leaves, out of this task's
own edit scope to fix**: `REFACTOR/CANONICAL_MODEL.md:134` cites `AUDIT.md
§2.1` -- now a removed section. `REFACTOR/LOOP_STATE.md` (the
supervisor-loop's own process-tracking file, several rows) also cites
`AUDIT.md §2.1`, but every one of those rows already records a *past,
already-verified* split, so they read as an accurate historical record
regardless of the section's current removal -- same "log entries are a
snapshot, not a live cross-reference" reasoning this file's own historical
entries below rely on.

**Diff gate (REFACTOR/README.md's "every difference is explained and
attributed"), covering split 4b (previous entry above) and this split
together, per this task's own explicit "one gate run at the end covering
both commits is fine" allowance**: built the pre-migration `husk` binary
first, from the tree exactly as it stood before *any* of this task's own
edits (`0a016cb2`, the parent of both this split's and split 4b's commits),
and copied it aside before touching any source file, per this task's own
required gate procedure.

Reused a prior session's own already-built, already-verified diff-gate
sample-selection infrastructure found sitting in this task's scratchpad
(`final_sample.txt`, `run_gate.sh`, the corpus shuffle/deliberate-pick
scripts) rather than re-deriving it -- verified it first, not trusted
blindly: re-ran the recorded "before" gate against a freshly-rebuilt
`husk-before` binary and confirmed the recorded `before.tsv`/console
captures reproduce exactly (one real hiccup found and fixed in this
verification pass itself, not in husk: an accidental `cd` into the
scratch dir before re-running the gate script made a *relative* sample
path -- `test_data/bloodelffemale.m2` -- resolve against the wrong `cwd`
and fail; re-running from the repo root, matching how the original
capture was taken, reproduced the recorded output byte-for-byte, and the
one clobbered "before" `.glb` this mistake caused was regenerated and
sha256-confirmed to match the recorded value before trusting the rest of
the gate).

- **Sample**: 426 files total -- 400 via a seeded Fisher-Yates shuffle
  (`awk`'s `srand(42)`/`rand()`, same method every prior split in this
  migration used) over `find /media/luna/data/wow_export -iname "*.m2"
  -type f` (132,863 files, corpus unchanged since the last migration's own
  run), plus 26 deliberately chosen files (zero overlap with the random
  400, confirmed by set difference) covering every changed code path by
  design, not left to chance: real `ribbon_id` weapons (found by scanning
  `item/objectcomponents/weapon/*.m2` with the pre-migration binary's own
  `dump-chunks` output), real particle-emitter/light/collision-bearing
  creature and world-doodad files, and `test_data/bloodelffemale.m2`.
- **Confirmed both required bones cases are real, not synthetic**:
  `bloodelffemale.m2` has 119 real inline bones (the ordinary case); a
  real corpus file already in the deliberate set,
  `creature/bloodtick/bloodtick.m2`, has **0 inline bones and a real,
  same-basename `bloodtick.skel` sitting right next to it** -- confirmed
  directly via `husk info` (`bones: 0 ... this model has an external
  skeleton`) before trusting it -- so `husk export bloodtick.m2` (no
  `--skel` flag at all) auto-detects and resolves bones from that real
  `.skel`, exercising split 4c's own `haveSkel` path end to end on real
  data, not a synthetic fixture.
- **Coverage across the full 426-file sample**, confirmed by scanning
  every sample file's own `husk info`/`dump-chunks` output (not assumed
  from the deliberate picks alone): 45 files with real `attachments`, 71
  with real `events`, 5 with real `lights`, 13 with real `ribbon_id`
  entries, 115 with real `particle_emitters`, 115 with
  `collision_indices` > 0, 425 with inline bones, 1 with 0 inline bones +
  a real external `.skel` sidecar (`bloodtick.m2`, above).
- **Result: 426/426 byte-identical.** `.tsv` report (idx, exit code, `.glb`
  sha256, source path) diffed line-for-line between the pre- and
  post-migration binary: **0 differences**. Every one of the 426 captured
  console-output files (stdout+stderr) diffed pairwise: **0 differences**
  -- no path normalization needed, since `run_gate.sh`'s own design
  exports both runs to the exact same shared output path
  (`gate_out/sample_<n>.glb`) rather than separate before/after
  directories, avoiding the "different dirs make every line differ" trap
  by construction instead of by post-hoc `sed`. Exit codes: 425/426 exit
  0, 1/426 exits 1 -- the one failure
  (`item/objectcomponents/collections/leather_raiddruidulatek_d_01_go_m.m2`)
  is a real, pre-existing, unrelated `--skin auto` resolution gap (a
  declared SFID FileDataID's `.skin` genuinely missing from local
  extraction, `CLAUDE_HISTORY.md`'s own already-documented gap class),
  confirmed byte-identical in both binaries' stderr message and exit code
  -- not a regression from this task's own changes.

**Deliberately not touched**: no existing `m2_*.hpp`/`.cpp` parser
changed. No `src/formats/`, `src/canon/`, or `src/writers/` directory
created. `src/dump_chunks_misc.cpp`/`src/dump_phys.cpp` untouched (not a
subset of this gap -- see split 3/4's own entry below for why).

**Verified**: `direnv exec . cmake --build build`, then
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**801 test cases, 0 failed, 1 skipped (unchanged); 6559 assertions, 0
failed**, up from split 4b's own 799/6547 by this task's 2 new regression
tests (+12 assertions). Plus the diff gate above, run against the real
compiled `husk` binary, not just the test suite.

---

## 2026-09-04 — `src/export_extras.cpp`'s six parse sites migrated onto `m2::Model`, split 4b of 3, plus `rethrowIfParseFailed` promoted to a shared function (REFACTOR/AUDIT.md §2.1, REFACTOR/README.md's Migration order step 2)

**What**: `src/export_extras.cpp`'s remaining six `m2::parse*` call sites --
`attachEmitterAnchors` (`m2::parseRibbons`/`parseParticles`),
`attachPlacementNodes` (`m2::parseAttachments`/`parseEvents`/`parseLights`),
and `appendCollisionMesh` (`m2::parseCollisionMesh`) -- are now
`model.ribbonEmitters`/`particleEmitters`/`attachments`/`events`/`lights`/
`collisionMesh` reads. All three functions' signatures changed from a raw
`(blob, header, ...)` pair to `(const m2::Model& model, ...)`; their three
`cmd_export.cpp` call sites updated to pass `model` (already a local of
`exportOneModel` since split 4a) instead of `blob`/`header`.

**`rethrowIfParseFailed` promoted from a local lambda to a shared function**,
per this task's own design brief: split 4a (`06a08f8b`) introduced it inside
`exportOneModel` with one real user. It now has six real call sites across
two files (`exportOneModel`/`resolveBones`/`resolveAnimationsForModel` in
`cmd_export.cpp` -- the latter two land in split 4c, tracked below --
and this split's own three `export_extras.cpp` functions) -- past this
project's own "third occurrence, or a single canonical hub" bar for earning
an abstraction (CLAUDE.md). Declared in `export_extras.hpp` next to
`readFileBytes`, this file's other shared cross-cutting helper; defined in
`export_extras.cpp`. Kept deliberately tiny, exactly the same body the local
lambda had: a linear scan over `model.parseFailures` and a throw -- not a
general error-policy abstraction.

**Design decision, verified against the real code rather than assumed:
`husk export`'s extras path needed fail-fast too, not graceful
degradation.** The reasoning (this task's own brief, confirmed correct
against `m2_model.hpp`'s real contract): a malformed `attachments`/
`events`/`lights`/`ribbons`/`particles`/`collision_mesh` array under
`m2::loadModel` yields an empty vector plus a recorded
`FieldParseFailure`, not a throw. Without `rethrowIfParseFailed`, reading
that field directly would silently attach *fewer* placement anchors/nodes
than the file actually has -- no error, no diagnostic, nothing -- the same
silent-misread class split 4a's own vertices/materials hazard was, just
quieter (a missing ribbon anchor is easy to miss; a missing whole mesh is
not). Each of the three functions now calls `rethrowIfParseFailed(model,
field)` immediately before reading that field, in the same relative order
the old direct `parse*` call ran (ribbon_emitters before the particle
version gate; attachments before events before lights; collision_mesh only
once the pre-existing `collisionRequested`/count-`==`-0 early return
confirms the field will actually be read) -- so a malformed file still
reports the same field's error first, matching the old direct-call
behavior byte-for-byte.

**The version-gate subtlety split 4a's own template (git log 6367a4a5) named,
checked against the real code, confirmed to still apply**:
`attachEmitterAnchors`'s particle-emitter block still gates on
`model.header.particleEmitters.count`/`model.header.version` directly, never
`model.particleEmitters.empty()` -- `m2::loadModel` applies the identical
version check *before* attempting `parseParticles` at all (`m2_model.hpp`'s
doc comment), so an empty `model.particleEmitters` can't be told apart from
"below `kMinVerifiedParticleVersion`, nothing attempted" versus "genuinely
zero records." `rethrowIfParseFailed(model, "particle_emitters")` is called
inside that gate, not outside it -- same shape as `dumpEmitters`'s own gate.

**Verification that fail-fast really survived the migration**: 3 new
CLI-tier regression tests in `tests/test_cli_errors.cpp` (this file's own
stated scope: "malformed model-file content must fail cleanly, not crash or
silently misread") -- one malformed `ribbon_emitters` array (a real inline-
boned, sequenced, skinned model via `tinyAnimatedM2()`, corrupted
`ribbonEmitters` header field), one malformed `attachments` array (same
fixture, corrupted `attachments` header field), and one malformed
`collision_positions` array under `--collision` (`tinyValidM2WithCollision()`,
corrupted `collisionPositions` header field). Not exhaustive over all six
fields -- `events`/`lights` share `attachPlacementNodes`' exact code shape
as `attachments`, and `particle_emitters` shares `attachEmitterAnchors`'
shape as `ribbon_emitters`, so one representative case per function was
judged sufficient (same "one fixture per previously-confirmed-broken
behavior" convention this test file's own doc comment states). All three:
exit 1, no `bad_alloc`, the real `ParseError::what()` message
(`"ribbonEmitters array claims..."`/`"attachments array claims..."`/`"array
claims ... C3Vector entries..."`) verbatim in the output -- confirming a
malformed extras field still fails loudly, not silently.

**Verified**: `direnv exec . cmake --build build`, then
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**799 test cases, 0 failed, 1 skipped (unchanged); 6547 assertions, 0
failed**, up from the 796/6528 baseline by this task's 3 new regression
tests (+19 assertions). Diff gate (REFACTOR/README.md's "every difference
is explained and attributed") deferred to run once at the end, covering
this split together with split 4c below, per this task's own explicit
instruction -- full sample/method/results recorded in split 4c's own entry
below, not duplicated here.

**Deliberately not touched**: `cmd_export.cpp`'s `resolveBones`/
`resolveAnimationsForModel` (split 4c, tracked separately below) --
`AUDIT.md` §2.1 stays open until that split lands too. No existing
`m2_*.hpp`/`.cpp` parser changed. No `src/formats/`, `src/canon/`, or
`src/writers/` directory created.

---

## 2026-09-04 — `cmd_export.cpp`'s parse block migrated onto `m2::Model`, split 4a of 3 (REFACTOR/AUDIT.md §2.1, REFACTOR/README.md's Migration order step 2)

**What**: `exportOneModel` (`src/cmd_export.cpp`) hand-assembled its own
partial view of the M2 file from `m2::parseHeader`+`m2::extractBlob`+
`m2::parseVertices` plus ten more `m2::parse*` calls populating
`M2MaterialInputs` (materials, textures, textureCombos,
textureCoordCombos, colors, textureWeights, textureWeightCombos,
textureTransforms, textureTransformCombos, plus `textureFileDataIds`
straight off the header). All eleven parses are now one
`m2::loadModel(modelBytes)` call; `header`/`blob`/`vertices` stay as local
names in `exportOneModel`, now `const` references bound onto `model`'s own
storage (`model.header`/`model.blob`/`model.vertices`) rather than
independent locals, so every other call in the function that still takes
a raw `blob`/`header` pair (`resolveBones`, `resolveAnimationsForModel`,
`attachBoneCorrections`, `attachEmitterAnchors`, `attachPlacementNodes`,
`appendCollisionMesh` -- the other two splits, below) needed zero further
edits.

**Scope: this is split 4a of 3, by explicit task design.** Two more parse
sites in `cmd_export.cpp`'s own reach were deliberately left untouched
this pass:

- **`resolveBones`/`resolveAnimationsForModel`/`buildAnimations` (split
  4c)** still call `m2::parseBones`/`m2::parseSequences` directly on
  `model.blob`/`model.header`, not through `model.bones`/`model.sequences`.
  Deliberate, not an oversight: `resolveBones` derives `bonesAreInline`
  from `m2::parseBones(...).empty()`, and under `loadModel` a *malformed*
  inline bones array yields an empty vector plus a recorded
  `FieldParseFailure` rather than throwing -- reading `model.bones` there
  would silently misread "bones failed to parse" as "no inline bones, fall
  back to the external `.skel`," changing which *data source* the export
  uses, not just leaving a field empty. Left calling `m2::parseBones`
  directly preserves today's exact throw-on-malformed-bones behavior; split
  4c owns fixing the underlying ambiguity properly (e.g. giving
  `resolveBones` its own explicit signal instead of overloading "empty").
- **`src/export_extras.cpp`'s six parse sites** (`attachEmitterAnchors`,
  `attachPlacementNodes`, `appendCollisionMesh`, and three more) are split
  4b, untouched -- they still receive `model.blob`/`model.header` via the
  same call-site arguments as before, just now sourced from `model`
  instead of freed-at-scope-exit locals.

**A real regression test caught a wrong first attempt, corrected before
landing.** The first version of this migration read `model.materials`/
`model.textures`/`model.vertices`/etc. directly, accepting `loadModel`'s
per-field failure isolation the same way `cmd_info`/`cmd_dump` did (prior
two entries below) -- reasoning that `export_materials.cpp` already
bounds-checks every batch's `materialIndex`/`textureComboIndex`/
`colorIndex`/`textureWeightComboIndex` against these vectors' real sizes
and throws a clear `std::runtime_error` on mismatch, so an
empty-due-to-parse-failure field should still fail loudly once a batch
references it. Running the full suite immediately falsified that
reasoning: `tests/test_cli_errors.cpp`'s "husk export: corrupted huge
vertex count fails with a real message, not std::bad_alloc" test failed
its `output.find("vertices array claims")` assertion. Root cause: the old
`m2::parseVertices` call threw immediately, before `--skin
/nonexistent.skin` was ever opened; with vertices silently empty instead,
execution ran on into `resolveSkinsToExport`/`buildLodTierMeshes`, which
tried to open the (deliberately nonexistent) `.skin` path first and threw
*that* error instead -- still exit 1, still a real message, but not the
one this test (and, more importantly, a real user debugging a genuinely
corrupted file) needs to see. A second, narrower case found while
auditing the fix, never hit by a test: `export_materials.cpp`'s
`textureTransforms`/`textureTransformCombos` lookups are deliberately
*best-effort* (an out-of-range index there is skipped, not thrown --
that block's own comment says so), so a parse failure on just those two
fields would have silently dropped texture-transform extras with no error
at all -- the same hazard *class* as `resolveBones`'s own, just for a
different pair of fields.

**Fix**: `exportOneModel` now explicitly rethrows. A local
`rethrowIfParseFailed(field)` lambda scans `model.parseFailures` for a
named field and, if found, `throw`s a `std::runtime_error` carrying
`FieldParseFailure::what` verbatim (the original `ParseError::what()`,
per `m2_model.hpp`'s doc comment -- so the message text is byte-identical
to what the old direct `parse*` call would have thrown). Called once per
migrated field, in the *same order* the old sequential `parse*` calls ran
(`vertices` before the version-warning print, then `materials`,
`textures`, `texture_combos`, `texture_coord_combos`, `colors`,
`texture_weights`, `texture_weight_combos`, `texture_transforms`,
`texture_transform_combos` after it) -- order matters here because a real
file could in principle fail more than one field at once, and the old
code would only ever have reported the first one it reached. `bones`/
`sequences` are deliberately not in this list -- split 4c's own hazard,
above, not this one's to fix. Net effect: `cmd_export.cpp` keeps its
pre-migration fail-fast contract exactly, while `cmd_info.cpp`/
`cmd_dump.cpp` keep their own (deliberately different, already-landed)
graceful-degrade contract -- two real commands, two real answers to "what
should happen to a malformed field," neither one wrong, both intentional
and now visible in the code instead of implicit.

**`M2MaterialInputs::blob` lifetime (REFACTOR/AUDIT.md §2.2, not fixed
here, only re-pointed)**: `m2Inputs.blob = &blob;`, where `blob` is now
`const std::vector<uint8_t>& blob = model.blob;` -- so this is `&model.blob`,
the address of the vector object `model` itself owns. `model` is a plain
local of `exportOneModel`'s own `try` block (`m2::Model model =
m2::loadModel(modelBytes);`), never moved, copied, or reassigned after
construction (confirmed: grepped the whole function body for any `model
=`/`model.blob =` past that point -- none). Every use of `m2Inputs.blob`
happens synchronously within that same `try` block (through
`buildLodTierMeshes`, itself called well before the block's closing
brace) -- `model` outlives every dereference. Same lifetime shape the old
standalone `blob` local gave (also a `try`-block-scoped local, also only
referenced synchronously within that block), just relocated onto `model`'s
storage instead of its own. Sound, but §2.2 (`M2MaterialInputs` still
carries a raw parse-layer pointer at all) stays open -- out of this task's
scope, a separate `canon::`-stage cleanup.

**Diff gate (REFACTOR/README.md's "every difference is explained and
attributed")**: built the pre-migration `husk` binary first (`git stash`
of this task's own working change, rebuilt, binary copied aside, change
restored -- not a separate commit checkout, since the change hadn't been
committed yet) before writing a single line of the migration. Verified
`.glb` determinism first (a prerequisite the task explicitly asked to
check): the post-migration binary run twice against
`test_data/bloodelffemale.m2` with identical flags produced byte-identical
`.glb` output (sha256 matched) and identical stdout modulo the
(expected, differing-by-construction) output path string -- `husk
export`'s output is deterministic run-to-run, byte comparison is a valid
gate. Sample: 35 files, `HUSK_CONFIG=/dev/null` on both sides --

- 25 files via a seeded pseudo-random sample (seed 42, `awk`'s
  `srand(42)`/`rand()` assigning one key per line, sorted, `head -25`) over
  `find /media/luna/data/wow_export -iname "*.m2" | sort` (132,863 files).
  A smaller general sample than the two prior migrations' 400+ (`husk
  export` is materially slower than `info`/`dump-chunks` per file, per this
  task's own explicit "a few dozen is right, don't attempt hundreds").
- 10 files chosen deliberately to exercise the exact fields this split
  touches, reusing `tests/test_cli.cpp`'s own real-fixture roster (already
  known-good, already paired with matching `.skin` files) rather than
  guessing blind against the general corpus: `bloodelffemale.m2` (skinned
  character, real materials) and `bloodelffemale_hd.m2` (HD skinned
  character, `.skel`-sourced), `creature/wolf/wolf.m2` (skinned quadruped),
  `creature/brewfestmount/brewfestmount.m2` (texture-transform rotation),
  `creature/bloodknightcharger/bloodknightcharger.m2` (texture-transform
  scale), `models/spells/unk_exp11_7037014/7037014.m2` (texture-transform
  translation), `world/replaceabletextureprops/guild/
  pennant_guild_alliance_a_01.m2` (multi-texture-layer batch),
  `world/expansion05/doodads/ironhorde/6ih_ironhorde_siegeweapon03.m2`
  (pre-Cata `textureCoordCombos`), `item/objectcomponents/weapon/
  sword_2h_ashbringer_a_01.m2` (ribbon emitter), `item/objectcomponents/
  weapon/mace_1h_warfrontsforsaken_d_01.m2` (weapon w/ `.phys`).

Ran both binaries' `export <model> -o <out>` over all 35 (default flags
otherwise -- no `--textures`/`--skin-dir` override, matching how a real
`husk export <file>` invocation actually gets used against a populated
extraction directory), capturing stdout/stderr/exit code into sibling
directories, normalizing only the expected before/after output-directory
path difference out of the captured text before comparing. **Result:
35/35 byte-identical `.glb` (sha256/`cmp`), 35/35 identical stdout, 35/35
identical stderr, 35/35 identical exit code (all 0) -- zero differences,
zero attributed exceptions needed.** Confirmed the sample wasn't inert:
spot-checked stdout summaries show real material counts (2-17 materials
per file across the fixture roster) and real geometry (up to 27,619
vertices), not degenerate 0-material/0-vertex exports. The "corrupted huge
vertex count" scenario above (a real, deliberate difference before the
rethrow fix, zero difference after it) was verified via the existing CLI
regression test, not folded into this gate -- same precedent the prior two
entries set: a deliberately malformed file belongs in a targeted
regression test, not a gate meant to prove real-file parity. Reproduction:
seed/commands recorded in this entry; sample file lists and per-file
capture directories were scratchpad-only, not committed.

**Verified**: `direnv exec . cmake --build build`, then `direnv exec . env
HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**796 test cases, 0 failed, 1 skipped (unchanged
`test_listfile_mmap_real.cpp`); 6528 assertions, 0 failed** -- exactly the
pre-task baseline, no new tests added this pass (no new *observable*
behavior to pin: the whole point of the rethrow fix above is that nothing
changed from the caller's point of view). Plus the diff gate above, run
against the real compiled `husk` binary.

**Deliberately not touched**: `resolveBones`/`resolveAnimationsForModel`/
`buildAnimations` (split 4c) and `src/export_extras.cpp`'s six parse sites
(split 4b) -- both named above, both left calling `m2::parse*` directly on
`model.blob`/`model.header`. `REFACTOR/AUDIT.md` §2.1 stays open until
both land. No existing `m2_*.hpp`/`.cpp` parser changed. No `src/formats/`,
`src/canon/`, or `src/writers/` directory created.

---

## 2026-09-04 — `cmd_dump.cpp`/`dumpEmitters` migrated onto `m2::Model` (REFACTOR/AUDIT.md §2.1, REFACTOR/README.md's Migration order step 2)

**What**: `cmd_dump.cpp` (`husk dump-chunks`) was the one command AUDIT.md
§2.1's table listed as "header + blob only" -- it never dereferenced any
array through `m2::parse*` itself, leaving `dumpEmitters` (`src/
dump_emitters.cpp`) to call `m2::parseRibbons`/`parseParticles` directly on
every invocation. `cmd_dump.cpp`'s `m2::parseHeader(fileBytes)` +
`m2::extractBlob(fileBytes)` pair is now one `m2::loadModel(fileBytes)`
call; `dumpEmitters`'s signature changed from `(json::Writer&, const
vector<uint8_t>& blob, const m2::Header& header)` to `(json::Writer&, const
m2::Model& model)`, reading `model.ribbonEmitters`/`model.particleEmitters`
off the already-parsed result instead of re-parsing.

**The version-gate trap, named in this task's own brief, and how it was
avoided**: `dumpEmitters`'s `particle_emitters` key has a real guard --
`header.particleEmitters.count > 0 && header.version <
kMinVerifiedParticleVersion` prints a "below Cataclysm, not parsed" note
instead of records, because M2Particle's record stride isn't verified below
Cata and a wrong-stride read can silently misdecode adjacent bytes rather
than throwing. `m2::loadModel` applies the exact same version check
*before* attempting `parseParticles` at all (`m2_model.hpp`'s doc comment)
-- so `model.particleEmitters` is empty below the gate for the same reason
it always was (nothing was attempted), not a new parse failure. The guard
in `dumpEmitters` still reads `header.version`/`header.particleEmitters.count`
directly, unchanged -- not `model.particleEmitters.empty()`, which can't
distinguish "gated" from "genuinely zero records" and would have silently
dropped the real version number from the note. Verified directly against a
synthetic pre-Cata fixture (`flat_particle_version_gate.m2` below): both
binaries print the identical note, byte-for-byte.

**A second, real behavior change, not hidden**: `dumpEmitters`'s old
unconditional `m2::parseRibbons`/`parseParticles` calls sat inside
`cmd_dump.cpp`'s single whole-body `try`/`catch` -- so a malformed
`ribbon_emitters`/`particle_emitters` array previously aborted the entire
command (`husk: dump-chunks failed: ...`, exit 1, a truncated/incomplete
JSON document already written to stdout). `loadModel` isolates each
array's own parse failure into `Model::parseFailures` instead of
propagating it, so this can no longer happen -- but "no longer crashes"
must not mean "silently prints an empty array" (CLAUDE.md: "on failure,
always print expected and actual values"). Chosen fix, matching
`cmd_info`/`cmd_info_json`'s own precedent exactly: `cmd_dump.cpp` now
prints a `parse_failures` JSON array (`{field, what}` objects) right after
`w.beginObject()`, before any array content, present only when
`model.parseFailures` is non-empty. Exit code stays 0. Verified directly
against both binaries on a synthetic malformed-ribbons fixture: before --
`husk: dump-chunks failed: ribbonEmitters array claims 1 records (176
bytes each) at offset 999999, which needs more room than the blob's 304
bytes`, exit 1; after -- a complete JSON document, `parse_failures: [{
"field": "ribbon_emitters", "what": "<same message>" }]`, `ribbon_emitters:
[]`, `particle_emitters` unaffected, exit 0. Pinned with a new CLI-tier
regression test in `tests/test_dump.cpp` (this command's own top-level
dispatch test file, matching its stated scope -- the diagnostic is
assembled by `cmd_dump.cpp` itself, not `dumpEmitters`).

**Decision: yes, add `parse_failures` to `dump-chunks`, and it covers the
whole model, not just ribbons/particles.** `m2::loadModel` eagerly parses
every array in the file regardless of what `dump-chunks` itself displays
(vertices, materials, textures, ... none of which this command ever
prints) -- so `model.parseFailures` can in principle name a field this
command doesn't otherwise surface at all. Kept anyway: `dump-chunks`'s own
stated purpose (`cmd_dump.cpp`'s top doc comment) is surfacing M2 data
"rather than leaving it silently unread" -- a parse failure is exactly
that, and hiding it because the failing field happens to be one this
command doesn't print elsewhere would contradict the command's own reason
to exist. Same schema as `husk info --json`'s own `parse_failures` (not
factored into a shared helper -- two JSON occurrences is below this
project's own "third occurrence, or a single canonical hub" bar for
earning an abstraction).

**Diff gate (REFACTOR/README.md's "every difference is explained and
attributed")**: built the pre-migration `husk` binary first (commit before
this one) and copied it aside before touching any source file. Sample: 420
files total, all run with `HUSK_CONFIG=/dev/null` on both sides --

- 400 files via a seeded Fisher-Yates shuffle (seed 42, `awk`'s own
  `srand`/`rand`, same method as the previous entry) over
  `find /media/luna/data/wow_export -iname "*.m2" -type f | sort`
  (132,863 files -- corpus unchanged since the last migration's own run).
- 8 real ribbon-bearing files and 8 real particle-bearing files, chosen
  deliberately rather than hoping the random 400 covered them (this task's
  own instruction) -- ribbon anchors found by scanning
  `item/objectcomponents/weapon/*.m2` with the pre-migration binary itself
  for a `"ribbon_id"` hit (31 found across all 4,146 weapon files, 8
  sampled); particle anchors taken from `corpus_reports/
  particle_only_candidates.csv`, an existing real-corpus particle-emitter
  survey, filtered to paths that still exist post-patch.
- `test_data/bloodelffemale.m2` explicitly.
- 3 synthetic fixtures for the pre-Legion flat-MD20 path, after confirming
  the real local corpus has **zero** such files: an 8,858-file magic-byte
  sample (every 15th file, ~6.7% of the corpus) was 100% `MD21`
  (chunked) -- a live-patch CASC extraction has apparently fully migrated
  every asset to the chunked container regardless of original age, so
  `header.chunked == false` is unreachable from real local data at all.
  Built with a throwaway scratch-only C++ program mirroring `tests/
  test_dump_fixtures.hpp`'s own byte layout: a bare flat MD20 with no
  emitters, a flat MD20 with one real ribbon + one real particle (proving
  the pre-Legion path and the emitter-bearing path aren't mutually
  exclusive), and a flat MD20 below `kMinVerifiedParticleVersion` with
  `particleEmitters.count > 0` (the version-gate note path, on a genuinely
  unchunked file).

417 unique files from the four buckets above, deduplicated, plus the 3
synthetic fixtures = 420. Ran both binaries' `dump-chunks` over every file,
diffing stdout, stderr, and exit code. **Result: 420/420 byte-identical,
zero differences.** Confirmed the sample wasn't trivially inert: 14/420
files have real `ribbon_id` entries, 115/420 have real `particle_id`
entries, 3/420 exercise the pre-Legion `header.chunked == false` cerr path
(synthetic, since real local data has none), 1/420 exercises the
below-Cataclysm particle version-gate note (also synthetic, for the same
reason), and 420/420 exit 0 both before and after. The `parse_failures`
key never fired anywhere in the sample -- expected, matching the previous
entry's own finding that real game files aren't malformed; the
crash-to-clean-exit behavior change above was verified separately, by
direct before/after comparison on a synthetic malformed file, not folded
into this gate (same precedent the previous entry set: a deliberately
malformed file would produce an expected-but-noisy difference in a gate
meant to prove real-file parity, so that case lives in a CLI regression
test instead). Reproduction: seed/commands recorded in this entry; the
capture/fixture directories were scratchpad-only, not committed (same
"seeded sample, not a persistent artifact" convention as before).

**Deliberately not touched**: `cmd_export.cpp` -- not this task's scope,
`REFACTOR/AUDIT.md` §2.1 stays open until it migrates too (it is now the
*only* unmigrated command). `src/dump_chunks_misc.cpp`'s three
`m2::parse*` calls and `src/dump_phys.cpp` were investigated and
deliberately left alone, per this task's own explicit instruction: they
parse a Legion+ *chunk payload* (a local `payload` buffer) through a
`m2::Array` synthesized from the chunk's own `count`/`offset`, not the
MD20 blob and not a `Header` array field -- `m2::Model` holds MD20-blob-
derived arrays only, so it has nothing to offer those call sites. This
matches the task brief's own framing exactly; no disagreement found after
reading the real code. No existing `m2_*.hpp`/`.cpp` parser changed. No
`src/formats/`, `src/canon/`, or `src/writers/` directory created.

**Verified**: `direnv exec . cmake --build build`, then
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**796 test cases, 0 failed, 1 skipped (unchanged
`test_listfile_mmap_real.cpp`); 6528 assertions, 0 failed**, up from the
795/6518 baseline by this task's 1 new regression test (+10 assertions).
Plus the diff gate above, run against the real compiled `husk` binary
(md5-verified identical to the one the test suite exercises), not just the
test suite.

---

## 2026-09-03 — `cmd_info.cpp`/`cmd_info_json.cpp` migrated onto `m2::Model` (REFACTOR/AUDIT.md §2.1, REFACTOR/README.md's Migration order step 2)

**What**: Two commits.

*Commit 1* removed `m2::loadModelFile` (`src/m2_model.hpp`/`.cpp`), added in
the previous entry's own pure-addition pass and never actually called by
anything -- a third verbatim copy of the same file-read logic already in
`m2::loadFile` (`src/m2_primitives.cpp`) and `cmd_info.cpp`'s own
`readFileBytes` (identical `errno` reset, identical `ifstream`, identical
`istreambuf_iterator` read, identical error strings), violating
README.md's own invariant I2 inside the commit meant to close this
section's duplication. Its own dedicated test case went with it; the one
other test using it as a read-and-parse convenience (the real
`bloodelffemale.m2` fixture case in `tests/test_m2_model.cpp`) now reads
the file itself and calls `loadModel(bytes)` directly. Not replaced with a
shared helper -- nothing needs one yet.

*Commit 2* is the actual migration: `cmd_info.cpp`/`cmd_info_json.cpp` each
called ~15 loose `m2::parse*` functions (AUDIT.md §2.1's table, now
corrected to name `cmd_info_json.cpp` as a real fourth hand-assembled view
the table had simply omitted -- see AUDIT.md's own correction note). Both
now call `m2::loadModel()` once and read every field off the resulting
`Model` instead. `printInfoJson`'s signature changed from
`(out, path, const Header&, const vector<uint8_t>&)` to
`(out, path, const Model&)` -- `cmd_info_json.cpp` no longer parses
anything itself, it only reads already-populated struct members.

**The trap, and how it was avoided**: `cmd_info.cpp` parses conditionally
and inconsistently -- `sequenceLookup > 0` gates a `parseSequences` call on
a *different* array's count; `textureCombinerCombos`/`boneLookup`/
`textureLookup`/`attachmentLookup`/`cameraLookup` are each gated on their
own `count > 0`; `bones`/`textures`/`materials`/`attachments`/`events`/
`lights`/`ribbon_emitters` are parsed unconditionally, outside any guard.
`loadModel()` parses all of these unconditionally regardless. Every one of
cmd_info.cpp's original `if (...count > 0)` guards survives verbatim in
this migration -- only the data source inside each guard changed, from a
fresh `m2::parseX(blob, h.X)` call to a `model.X` field read. Nothing new
prints on a well-formed file; the diff gate below is the proof, not an
assertion.

**The other half of the trap -- a real, intentional behavior change,
not hidden**: `bones`/`textures`/`materials`/`attachments`/`events`/
`lights`/`ribbon_emitters`'s unconditional parse calls sat *outside*
`cmd_info.cpp`'s only try/catch (which wraps just the header/blob read),
and `main.cpp` has no outer catch around command dispatch -- so a
malformed array elsewhere in an otherwise-well-formed file crashed the
whole process on an uncaught `m2::ParseError` today, before this
migration. `loadModel()` isolates each array's own parse failure into
`Model::parseFailures` instead of propagating it (previous entry's own
design), so this can no longer crash -- but "no longer crashes" must not
mean "silently reads as an empty array" (CLAUDE.md: "on failure, always
print expected and actual values"). Chosen fix, applied identically to
both commands: a generic diagnostic, printed only when
`model.parseFailures` is non-empty, naming every failed field and its
real `ParseError::what()` message (already carrying the "expected N
bytes, blob is M" detail) -- prose gets a `parse_failures: N` block
printed right after `global_flags`; JSON gets a `parse_failures` array of
`{field, what}` objects in the same spot, present only when non-empty
(the same "absent, not null, for a conditional field" convention this
schema already used everywhere else). Exit code stays 0 either way --
consistent with the existing "particle_emitters below Cataclysm" case,
which already warns-and-continues rather than failing the whole command
over one unavailable detail section. Pinned with two new CLI-tier
regression tests (`tests/test_cli_errors.cpp`, `tests/test_cli_info_json.cpp`):
a synthetic file with a valid header but `bones`' own array descriptor
pointing 999999 bytes past a ~0x130-byte buffer -- both now assert exit 0,
no `terminate called`, the header's own declared `bones: 1` count still
printing (untouched, real data) alongside zero billboard-detail lines, and
`parse_failures`/`"parse_failures"` naming `bones` by name with a
non-empty reason.

**Diff gate (REFACTOR/README.md's "every difference is explained and
attributed", this task's own gate)**: built the pre-migration tree first
(commit 1's own state -- already had `m2::Model`, unused, so its `husk`
binary is the true pre-migration reference) and copied its `build/husk`
aside before touching `cmd_info.cpp`/`cmd_info_json.cpp`. Sample: 400
files drawn from the real local corpus
(`find /media/luna/data/wow_export -iname "*.m2" -type f | sort`,
132,863 files) via a seeded Fisher-Yates shuffle (seed 42, `awk`'s own
`srand`/`rand` -- `perl`/`python3` are both blocked in this environment
by design, see its own CLAUDE.md; `awk` isn't), taking the first 400 of
the shuffled order, plus `test_data/bloodelffemale.m2` and
`/media/luna/data/wow_export/creature/wolf/wolf.m2` appended explicitly
per this task's own brief -- 402 unique files total (verified via
`sort -u`). Ran both binaries' `info` and `info --json` over every file
(`HUSK_CONFIG=/dev/null` for both, avoiding this machine's own real
`~/.config/husk/config.toml`), diffing stdout, stderr, and exit code per
file. **Result: 402/402 byte-identical on both stdout and stderr and
exit code, for both `info` and `info --json` -- zero differences, let
alone unexplained ones.** Confirmed the sample wasn't trivially empty
models: 95/402 files have `particle_emitters > 0`, 4/402 have
`ribbon_emitters > 0`, and (expected, real game files aren't malformed)
zero files anywhere in the sample hit `parse_failures` in either
before or after output -- the new diagnostic path is real but inert
against this sample, exercised instead by the two new synthetic
regression tests above. Reproduction: seed/commands recorded in this
entry; the actual before/after capture directory was scratchpad-only,
not committed (throwaway, matching this project's own "seeded sample,
not a persistent artifact" convention already established for corpus
scan work).

**Deliberately not touched**: `cmd_export.cpp`/`cmd_dump.cpp` -- not this
task's scope, `REFACTOR/AUDIT.md` §2.1 stays open until they migrate too.
No existing `m2_*.hpp`/`.cpp` parser changed. No `src/formats/`,
`src/canon/`, or `src/writers/` directory created -- out of scope for this
step.

**Verified**: `direnv exec . cmake --build build`, then
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**795 test cases, 0 failed, 1 skipped (unchanged
`test_listfile_mmap_real.cpp`); 6518 assertions, 0 failed**, up from the
794/6500 baseline by commit 1's net-even test-count-minus-one (loadModelFile's
own test removed, its replacement gaining one `REQUIRE`) plus commit 2's
2 new regression tests (+18 assertions). Plus the diff gate above, run
against the real compiled `husk` binary, not just the test suite.

---

## 2026-09-03 — `m2::Model`: the whole-file parsed aggregate (REFACTOR/AUDIT.md §2.1), pure addition

**What**: Introduced `husk::m2::Model` (`src/m2_model.hpp`/`.cpp`, new
`tests/test_m2_model.cpp`, both added to `src/m2.hpp`'s aggregate
`#include` and `CMakeLists.txt`'s source lists) -- the whole-file parsed
aggregate REFACTOR/CANONICAL_MODEL.md's "Stage 1's missing piece" section
names, closing the *type* half of AUDIT.md §2.1 ("There is no
`m2::Model`"). Deliberately a **pure addition**: no `cmd_*.cpp` file was
touched, and no existing command's behavior changed -- migrating
`cmd_info.cpp`/`cmd_info_json.cpp`/`cmd_export.cpp` onto this type is three
separate, later tasks (README.md's migration-order step 2's own gate --
"husk info / dump-chunks output diffed on real fixtures, every difference
attributed" -- is explicitly not this task's job).

`Model` holds the real union of what the three commands parse today, re-
derived from the actual code rather than the task brief's own starting
map (which turned out incomplete -- see below), plus every array with a
real `parse*` function that no command currently reads at all
(`globalLoops`/`boneCombos`, via `parseGlobalLoops`/the existing generic
`parseUint16Array`). Two arrays are deliberately excluded, both reported
rather than silently dropped: `cameras` (Header's own `Array` descriptor
is still reachable via `model.header.cameras`, but no `M2Camera` struct or
`parseCameras` function exists anywhere in this codebase -- CLAUDE.md's
own Status already says so: "M2Camera is still count-only") and
`extendedParticles`/EXP2 (real `parseExtendedParticles` exists, but EXP2 is
a standalone top-level chunk with its own local `M2Array`, discovered via
`findChunk(chunks, "EXP2")` on the *raw chunk list* -- `cmd_dump.cpp`'s
`dumpExp2` -- not through any `Header` field the way every other array
here is; there is no routing path from `Header`/blob to it at all without
`Model` also holding the raw Legion+ chunk list and re-implementing
`findChunk`, a real structural expansion beyond "union of `Header`-array-
derived parses" this task's scope doesn't cover). Re-deriving the union
from code, not the brief, also surfaced one real omission in the task's
own starting map: `cmd_export.cpp` parses the collision mesh too
(`export_extras.cpp`'s `appendCollisionMesh`, called from
`exportOneModel`) -- included as `Model::collisionMesh` via the existing
`parseCollisionMesh`.

**The real design content -- the parse-failure contract**: today's three
commands parse *conditionally and differently* for the same file. Grepped
every guard directly rather than trusting the task brief's paraphrase:
`cmd_info.cpp`'s `parseSequences` call really does run only `if
(h.sequenceLookup.count > 0)` (cmd_info.cpp:162-168) -- gated on a
*different* array's count than the one it's parsing -- while
`cmd_export.cpp` calls `parseSequences` unconditionally whenever bones are
inline, no `sequenceLookup` check at all (cmd_export.cpp:166). Both are
real, already-shipped, already-divergent behaviors, not a hypothetical.
Meanwhile `cmd_info.cpp`'s own `bones`/`attachments`/`events`/`lights`/
`ribbon_emitters` loops are unconditional and sit *outside* its only
try/catch (cmd_info.cpp:102-116 wraps just `parseHeader`/`extractBlob`) --
confirmed by reading `main.cpp`: there is no outer catch around
`husk::commands::info` either, so a malformed `bones` array on a real file
crashes `husk info` uncaught *today*, this task didn't introduce that.

Given that landscape, an eager `loadModel()` that just propagates the
first `ParseError` would be *strictly worse* than today for the common
case (one malformed section in an otherwise-fine file, which a 130k+-file
corpus makes a real, not hypothetical, occurrence) -- it would newly
attempt arrays some commands' own guards currently never reach, and one
throw would blank out a file that's 95% readable. So: **chosen contract is
per-field failure isolation, not propagate-first-throw.** Each of the 26
array-derived fields is parsed inside its own `tryParse` (a single, earned
-- 26th real occurrence -- template helper), catching `m2::ParseError`
specifically (not `std::exception`, so a genuine bug elsewhere isn't
silently swallowed) and recording a `FieldParseFailure{field, what}` while
leaving that one field empty; every other field that parsed cleanly stays
intact. `parseHeader`/`extractBlob` themselves are *not* wrapped -- there
is no `Model` at all without a header/blob, matching every existing
caller's own behavior for a file that broken. This is what makes the
constraint the task set -- "migrations 2-4 must be able to reproduce
their own current per-array conditional behavior on top of this type" --
actually achievable: a future migrated `cmd_info.cpp` can check
`model.header.sequenceLookup.count > 0` itself and simply not print
`model.sequences` when it's false, reproducing today's exact "never
attempted, never noticed" behavior even though `loadModel()` itself always
attempts it; it isn't forced to inherit a crash-on-first-bad-field
either way.

One field needed more than a try/catch: `particleEmitters`. Below
`kMinVerifiedParticleVersion` (Cata, 272), the 492-byte record stride is
unverified for older files, and a wrong stride isn't guaranteed to
*throw* -- it can decode adjacent bytes as plausible-looking-but-wrong
values instead (the "silent misread" class CLAUDE.md's foreign-data
discipline treats as strictly worse than a loud bounds failure, since a
try/catch structurally can't see it). `cmd_info.cpp`/`cmd_info_json.cpp`
already skip the parse entirely below that version rather than risk it;
`loadModel()` replicates the exact same version check before attempting
`particleEmitters` at all, leaving it empty and recording **no**
`FieldParseFailure` (nothing was attempted -- same "count-only" policy,
not a new fact). `bones`/`sequences`/`ribbon_emitters` are deliberately
**not** similarly version-gated on `kMinVerifiedRecordStrideVersion`
(Wrath, 264): today's commands already parse them unconditionally
regardless of version, only warning to stderr that the stride is
unverified below Wrath -- that pre-existing silent-misread risk is
unchanged by this task, not newly introduced or fixed here (printing the
warning is presentation-layer, out of scope for a struct); the try/catch
still guards the bounds-failure half of that risk the same as every other
field.

**Verified**: `tests/test_m2_model.cpp`, 6 new `TEST_CASE`s -- a genuinely
empty-but-valid M2 parses cleanly with zero `parseFailures` (proves
"empty" and "failed" aren't conflated in the easy direction); the
`kMinVerifiedParticleVersion` skip leaves `particleEmitters` empty without
a `FieldParseFailure` for it; a header-level bad-magic file still throws
`ParseError` out of `loadModel` (no `Model` without a header);
`loadModelFile` on a nonexistent path throws `ParseError`; and the
malformed-array case reuses `test_m2_fixtures.hpp`'s existing
`buildMd20Blob()` unmodified -- it already declares every array at
offsets (1000+) past its own ~312-byte buffer, which turned out to be
exactly the "one bad file, many malformed sections" fixture this task
needed, with no new fixture to author or keep in sync: `loadModel` on it
doesn't throw, `model.header`/`model.blob` still come back fully correct
(`checkSentinelHeader`, reused as-is), and exactly 25 of the 26 attempted
fields land in `parseFailures` (the 26th, `texture_combiner_combos`,
genuinely never gets attempted with nonzero bounds, since
`buildMd20Blob()`'s `globalFlags` doesn't set
`GlobalFlag::kUseTextureCombinerCombos` -- confirmed by reading
`m2_primitives.cpp`'s `parseBlob`, which only wire-populates that field
when the bit is set). The 6th `TEST_CASE`, gated
`* doctest::skip(husk::test::testM2().empty())`, exercises the real
`bloodelffemale.m2` fixture: zero `parseFailures`, `vertices.size() ==
8061` / `particleEmitters.empty()` cross-checked against
`test_cli_info_json.cpp`'s own existing real-fixture assertions on the
same file (not new, possibly-wrong magic numbers), plus internal
count-consistency checks (`model.X.size() == model.header.X.count`) for
every other dereferenced array. Full suite:
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2
HUSK_TEST_SKIN=test_data/bloodelffemale00.skin ./build/husk-tests` --
**794/794 test cases, 0 failed, 1 skipped (unchanged
`test_listfile_mmap_real.cpp`); 6500/6500 assertions**, up from the
788/6323 baseline by exactly this task's own 6 new cases (+177
assertions).

**Cost, measured**: a throwaway in-process benchmark (built against
`build/libhusk-lib.a` directly, not part of the shipped tree) timed
`loadModel()` against real files versus a hand-replicated copy of
`cmd_info.cpp`'s and `cmd_export.cpp`'s own actual parse* call sequences
(guards included), same read-once bytes, 200-500 iterations for small
files / 20 for the large one. Three real data points: `bloodelffemale.m2`
(2.3 MB, this repo's own fixture) -- cmd_info-shaped 0.128 ms,
cmd_export-shaped 0.380 ms, `loadModel()` 0.380 ms; `sunwell_beamfx.m2`
(363 KB, close to a 3000-file random sample's real corpus average of
365 KB) -- cmd_info-shaped 0.018 ms, cmd_export-shaped 0.228 ms,
`loadModel()` 0.230 ms; `dracthyrmale.m2` (38 MB, the single largest `.m2`
in the local corpus) -- cmd_info-shaped 22.3 ms, cmd_export-shaped 31.6 ms,
`loadModel()` 31.5 ms. Zero `parseFailures` on all three (real files
aren't malformed), so none of this delta is try/catch overhead -- it's
genuinely more data parsed, an inherent property of a complete model, not
a flaw in the isolation mechanism. Against `cmd_export.cpp`'s own current
cost, `loadModel()` is a wash (same or marginally cheaper in all three
measurements) -- the arrays `Model` parses beyond `cmd_export.cpp`'s own
set (`attachments`/`events`/`lights`/`ribbon_emitters`/`particle_emitters`/
`boneLookup`/`textureLookup`/`sequenceLookup`/`globalLoops`/`boneCombos`/
`textureCombinerCombos`) cost near-nothing next to vertex/texture parsing.
Against `cmd_info.cpp`'s own current (much narrower) cost, `loadModel()`
is genuinely more expensive in *relative* terms (3x-13x across the three
files, since `cmd_info.cpp` skips vertices entirely today), but stays
trivial in *absolute* terms even at that ratio (sub-millisecond to
tens-of-milliseconds per file) -- against a 132k-file corpus scan at the
measured near-average-size cost (0.212 ms delta/file), the aggregate
added cost is on the order of **28 seconds total**, dwarfed by that same
scan's own per-file subprocess-spawn overhead. Stated plainly per this
task's own requirement rather than buried: this *is* a real, non-zero,
measured cost increase for `cmd_info.cpp`'s eventual migration
specifically, not free -- just not one that changes the contract decision
above. Caveat: measured against an unoptimized build
(`CMAKE_BUILD_TYPE` unset in this tree), so absolute numbers are
pessimistic versus a real release build; the relative comparison between
the three parse shapes should hold either way since all three ran through
the same build. Benchmark source and exact commands are in this session's
own report, not committed (throwaway, scratchpad-only per this task's
scope).

**Deliberately not touched**: every `cmd_*.cpp` file (no consumer
migration -- that's README.md's migration-order steps for `cmd_info.cpp`/
`cmd_info_json.cpp`/`cmd_export.cpp` separately, later); every existing
`m2_*.hpp`/`.cpp` parser (no signature or behavior changed, `Model` only
calls what already exists); `REFACTOR/AUDIT.md` §2.1 itself -- only the
*type* half is closed, the "three commands still each assemble their own
partial view" half remains true and unremoved until a later task actually
migrates a consumer, which this file's own log entry doesn't get to claim
yet. No design questions surfaced that need a human call beyond the
failure-contract one this entry documents the resolution of.

---

## 2026-09-03 — `red-baseline-fuzzy-pool`: the clean-tree failure was a stale test assertion, not a regression

**What**: Closed the loop's first task (`LOOP_STATE.md`'s
`red-baseline-fuzzy-pool`) — `tests/test_cli_textures.cpp:252`'s "two
basename-matching candidates ... embed BOTH as alternate_textures" case
failed on a clean `7f7c49a` tree: `result.output.find("fuzzytexskin00_00.png")`
came back `npos`. Reproduced directly (`husk-tests -tc=...`), then dumped
the real console text and the real `.glb` content by hand rather than
guessing between the two suspects `LOOP_STATE.md` named. Console text:
`husk: warning: 1 material(s) (e.g. 'batch0_mat0_tex0_skin_fuzzytexfaceupper00_00')
each had 2 same-basename texture candidate(s) (fuzzytexfaceupper00_00.png
picked arbitrarily as the default) -- all 2 are embedded as
'alternate_textures' extras on each material (...)` — the non-default
candidate's filename is genuinely absent from stderr now, by design
(`feed145`, "Group console warnings by candidate set instead of spamming
per batch": the full candidate list used to dump 400+ filenames to stderr
on a real character export; it's dropped in favor of the data already
living in the `.glb`). The `.glb` itself, loaded via tinygltf and dumped
field-by-field, has both candidates fully intact:
`alternate_textures[0].filename == "fuzzytexfaceupper00_00.png"`,
`alternate_textures[1].filename == "fuzzytexskin00_00.png"`, and 3 real,
distinct embedded images (the primary baseColorTexture plus one per
candidate) — not 2 with one name reused. This settles it as suspect (a):
the *test* was stale, not `src/`; `dfabdd2`'s pool-admission narrowing
(the other named suspect) never enters into it — both candidates were
admitted to the pool the whole time.

**Why**: Per the task's own explicit instruction, a behavior-shape change
gets its test updated to match the real, deliberate new output, leaning
on the test's existing real-content check (tinygltf-loaded `.glb`
inspection) as the behavioral assertion rather than grepping console
prose. Updated `tests/test_cli_textures.cpp`: the stale
`result.output.find("fuzzytexskin00_00.png")` assertion is gone (with a
comment explaining why, citing `feed145`), and the real-content section
below was strengthened to be the test's actual "neither candidate is
silently dropped" guarantee — it now collects every `alternate_textures[i].filename`
into a vector and `CHECK`s both real filenames are present by content
(not just `ArrayLen() == 2`), plus asserts `model.images.size() == 3` to
confirm three genuinely separate embedded images rather than a reused
name. No `src/` file touched — `dfabdd2`'s admission logic was correct
for this case, confirmed empirically, not assumed.

**Verified**: full suite green, 788/788 (0 failed, 1 skipped —
`test_listfile_mmap_real.cpp` wanting `HUSK_TEST_REAL_LISTFILE`, same as
baseline), 6323/6323 assertions passed — beats `LOOP_STATE.md`'s recorded
787/788 baseline. Manual repro command:
`direnv exec . env HUSK_TEST_M2=test_data/bloodelffemale.m2 HUSK_TEST_SKIN=test_data/bloodelffemale00.skin
./build/husk-tests -tc="*basename-matching candidates for one hardcoded slot*"`.
Deliberately not touched: `REFACTOR/LOOP_STATE.md` itself (updating the
loop's own state table is the supervisor's job, out of this task's stated
scope) and every other `LOOP_STATE.md` row (`m2-model-aggregate` etc.) —
this entry closes only the one red-baseline blocker.

---

## Entries before 2026-09-03 (trimmed 2026-09-16)

18 entries spanning 2026-08-28 to 2026-08-29 — the initial `sources::Catalog`/
`Resolved<T>` build-out (texture-resolution tiers 1-3, the DB2 parse/resolve
cache, `husk resolve`/`husk info --json`, the DDS texture-encoding decision,
`EXTRAS_SCHEMA.md`) — were here and are now trimmed per this file's own
"last ~2 weeks" policy (2026-09-16 documentation-consolidation pass).
Nothing is lost: `git log -p -- REFACTOR_LOG.md` (or `git show
<a-rev-before-2026-09-16>:REFACTOR_LOG.md`) recovers the full text. The
facts these entries established are already promoted into their real
living homes (`REFACTOR/RESOURCE_CATALOG.md`'s "Settled" section,
`REFACTOR/BUNDLE_FORMAT.md`'s "Texture encoding — settled" section,
`EXTRAS_SCHEMA.md` itself) — this log's own job (narrative git history
doesn't give at a glance) is already done for them.
