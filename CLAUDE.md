# CLAUDE.md — husk

Global rules apply (`~/.claude/CLAUDE.md`). Nix conventions: `.claude/rules/nix.md`.
Read `DESIGN.md` before any structural change.

## Purpose

CLI that reads WoW M2 model files (+ `.skin`/`.skel`/`.bone`/`.anim`/`.phys` sidecars) and
exports them to glTF 2.0 (`.glb`) for Blender import; `husk-blp` (separate Python
tool, `blp/`) converts BLP2 textures to PNG.

## Status

- **Current**: `husk info` (header/record-count/chunk-tag summary, incl. per-texture/
  material detail and sidecar FileDataIDs, plus a one-line ribbon/particle-emitter
  summary, `global_flags` decoded into its wiki-named bits alongside the raw
  hex value, and the conditional `textureCombinerCombos` header array when
  its flag bit is set — see Resume), `husk export` (static mesh → skeleton +
  skinning, inline or external `.skel` → materials with real embedded textures →
  animation, inline/external-`.anim`/`.skel`-sourced (external `.anim`
  resolution now falls back to the real `wow.export`-shaped same-basename
  filename convention, not just `<FileDataID>.anim`, when a FileDataID-mapped
  file isn't found — see Resume), verified against real
  `bloodelffemale.m2`/`bloodelffemale_hd.m2` data), `husk export --lod`
  (single-tier or `all`), `husk export --bones-dir` (real `.bone` correction
  data attached as inert `bone_correction_sets` glTF skin `extras`, never
  applied to the render), `husk export --phys` (real `.phys` physics/collision
  body data attached as inert `physics_bodies` glTF skin `extras` — minimal
  per-body placement anchors only, never applied to the render — see Resume),
  `husk export --db2-dir/--dbd-dir/--char-layout-id` (real DB2-derived
  character texture-layout geometry — base atlas size, real placement rects,
  real texture-layer blend info — attached as inert `chr_texture_layout`
  glTF skin `extras`, keyed by a caller-supplied `CharComponentTextureLayoutsID`
  since husk can't derive one on its own; see Resume),
  every export also attaching minimal ribbon/particle placement anchors
  (id/bone/position, `ribbon_emitters`/`particle_emitters` skin `extras`,
  unconditional), every export also emitting one inert geoset "tag" joint
  per distinct geoset ID (`Skeleton::geosetTags`, `JOINTS_1`/`WEIGHTS_1`)
  so Blender's stock glTF importer builds a real per-geoset vertex group
  with zero custom import tooling — `tools/husk_blender_geoset_mask.py`
  turns that into a Geometry Nodes Menu Switch dropdown per geoset group
  for WoW's mutually-exclusive geoset variants (hairstyles, boot cuffs,
  eye-glow, ...); two real bugs found interactively on 2026-08-08 (wrong
  geometry disappearing on an unrelated group's switch; the tabard dropdown
  never toggling) are now both root-caused, fixed, and verified — see
  Resume — `husk dump-chunks` (JSON dump of Legion+ chunks with no
  glTF equivalent, full `M2Ribbon`/`M2Particle` records including every
  resolved animation curve, present in every M2 version; `WFV1`/`WFV2`/
  `DPIV`/`AFRA` — no wowdev.wiki struct at all, byte-decoded from real
  files instead, see Resume — now get real structural parsing too, not a
  raw hex dump; or `.bone`/`.phys`
  files directly — `.phys`'s full body/shape/joint/`PHYV` record set, each
  shape/joint resolved to its real type-specific data inline, see Resume).
  `blp/`'s `husk-blp` (BLP2 → PNG:
  palettized/DXT1/DXT3/DXT5/BGRA — DXT3 turned out to already be wired
  through the same generic decode path as DXT1/DXT5, just unverified until
  this session's real-corpus scan, see Resume). `husk export`'s CLI grammar is CLI11-based named
  flags (`--input`/`--output` positional-fallback, everything else named,
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel`/`--bones-dir`/`--phys`
  three-or-four-state — see `DESIGN.md`'s "CLI argument grammar for
  `export`"), with generated bash/zsh completions in `completions/`. See
  `README.md`'s format-support matrix and roadmap for the exact per-feature
  state — that table is the source of truth, not this file.
- **Target**: a real Blender import path for modern (Legion+ chunked) M2 — see
  `DESIGN.md`'s Goal section. All 8 roadmap stages are now done, including stage 7
  (output hardening: real exports now run through the Khronos glTF-Validator *and*
  headless Blender itself, `tests/test_conformance.cpp` — see Resume). `AFSB`
  (`.skel`-linked models' real external-animation format, previously the single
  biggest animation gap) is now cracked and resolved end to end — see Resume.
  M2-source-vs-exported-glb-vs-Blender-readback cross-checks (former
  `VERIFICATION_IDEAS.md`, now deleted — its survey's job was done, every
  case had a final disposition, folded back into `tests/test_conformance.cpp`/
  `WIKI_FINDINGS.md` §5, same scratch-doc lifecycle `DESIGN_CHANGES.md`
  had) are now implemented too, plus a real collision-mesh export husk
  never had before. `M2Particle`/`M2Ribbon` (weapon glow trails, magic/fire/
  smoke — the single biggest remaining visual-identity gap this tool had) are
  now fully parsed, every field and every resolved animation curve, split
  between a minimal glTF placement anchor and `husk dump-chunks`'s full JSON
  output — see Resume. Remaining work is either scope expansion
  (WMO/M3, not started, by design) or the structural gaps `TODO/TODO_correctness.md`
  already tracks (`M2Camera`, low-priority by design; `.bone` correction
  *selection* — the extras-export half is done, see Resume; picking which
  slot applies is blocked on client-side DB2 data husk doesn't have, not on
  more investigation), or the corpus-hardening follow-ups a real 130k-file
  corpus sweep turned up this session --
  five real export-robustness bugs found and fixed (a geometry-less-model
  crash affecting 3,807 real files, a `.skin`-pairing collision bug, an
  undocumented `WFV3` short-chunk variant, a duplicate-animation-keyframe
  crash), two more findings confirmed genuinely unfixable in husk
  (mismatched shared batch data, an extraction-completeness gap), and one
  concrete follow-up identified and now implemented (the multi-root-bone-
  hierarchy gap, `MULTIROOT_SKELETON_TODO.md` -- `writeGlbMulti` now
  synthesizes a non-joint glTF parent node for the 35% of the corpus with
  more than one root bone, see Resume). `M2_GAPS_TODO.md`'s full item
  bundle (ten items across several sessions, most recently `PCOL`
  player-housing collision, diagnostic-only via `husk dump-chunks`,
  verified against all 2,354 real `PCOL`-bearing files -- see Resume) is
  now fully implemented and the file itself deleted -- nothing currently
  in flight. A real interactive Blender pass found the M2→glTF position/
  rotation/scale conversion was measurably upside down despite the whole
  conformance suite above passing -- root-caused, fixed, and covered by a
  new asset-agnostic orientation-correctness test tier. The one piece
  deliberately left for Luna rather than automated -- a real animated
  clip, visually confirmed in Blender's own GUI -- is now done too: "Animation
  looks OK" (2026-08-08), closing out the investigation. `TRANSFORM_TRIAGE.md`
  itself deleted per this project's own "survey's job is done" lifecycle;
  the two dangling source-code citations of it (`src/gltf_math.hpp`/`.cpp`)
  cleaned up in the same pass. One funny, non-actionable side note from
  that verification: a dead vertex sits in the middle of the two-handed
  swing animation, detached from the character, carrying FileDataID 31739
  -- genuinely invisible in the real game too (an "invisible texture"),
  not a husk export bug.
- Anything not listed under Current does not exist yet. In particular: `M2Camera`
  is still count-only (not dereferenced). Three FAILURES2.md gaps
  (geoset selection #1, multi-texture-layer rendering #6, global-sequence animation
  #7) all went further than a diagnostic this session: geoset `skinSectionId` and
  additional (`textureCount > 1`) texture layers are now real glTF `extras`
  metadata on every primitive/material (husk still doesn't *filter*/*render* either
  one — no DBC data to ground a default geoset choice in, no core-glTF slot for a
  second texture layer — but a custom renderer or Blender script now has everything
  it needs to implement its own selection/blend on top), and global-sequence
  tracks resolve to real, separate glTF animation clips
  (`global_seq_<n>`). Verified against real data: `bloodelffemale.m2` goes from 256
  to 258 animation clips, and its 66-geoset/1-multi-texture-batch `.skin` exports
  cleanly with the new extras attached.

## Boundaries

- Model file bytes (`.m2`) — chunk container + fixed-offset header/arrays
  (`src/chunk.cpp`, `src/m2.cpp`).
- `.skin` sidecar — triangle-index lookup, submesh/batch structure (`src/skin.cpp`).
- `.skel` sidecar — external bones + sequences (`src/skel.cpp`).
- `.bone` sidecar — per-bone correction matrices, reverse-engineered (`src/bone.cpp`).
- `.anim` sidecar — external per-sequence keyframe blob; `AFM2` (flat) and `AFSB`
  (`.skel`-linked models' real shape) both resolved (`m2::extractAnimBlob`,
  `cmd_export.cpp`'s `buildAnimations`).
- `--textures`/`--skin-dir`/`--anim` directories — user-populated,
  FileDataID-named, local filesystem only. **Never CASC** — husk has no
  CASC/listfile access and never will, by design (see `DESIGN.md`'s Non-goals).
- `.db2` files (real WDC5 container, `src/db2.hpp`/`.cpp`), real column
  names via an optional local WoWDBDefs checkout (`src/dbd.hpp`/`.cpp`),
  a generic named-column reader on top of both (`src/db2table.hpp`/`.cpp`),
  and real typed character-texture-layout structs on top of that
  (`src/chrmodel_db2.hpp`/`.cpp`, consumed by `husk export --db2-dir/
  --dbd-dir/--char-layout-id` and the separate `husk db2-export` side tool)
  — locally-extracted files only, same "user-populated, never CASC" tier as
  every other sidecar above (see `DESIGN.md`'s Non-goals' clarified
  wording).
- `.blp` texture files (separate `blp/` Python tool) — container hand-rolled, block
  decode delegated to Pillow via a synthetic DDS wrapper.
- No network access anywhere in this tool. No user input beyond CLI argv (parsed in
  `cmd_info.cpp`/`cmd_export.cpp`/`cmd_dump.cpp`, no interactive prompts).

Every boundary above is read via explicit bounds-checked parsing at named offsets,
throwing a descriptive `ParseError`/`std::runtime_error` on anything foreign data
claims that doesn't fit — never a silent misread (see git history for the specific
bugs found and fixed under this discipline), and `WIKI_FINDINGS.md` for every
real-file-driven spec correction found along the way.

## Resume

Full session-by-session narrative: `CLAUDE_HISTORY.md` (append new entries
there, most recent first). This section is a snapshot, not a log — update it
in place each session; append the full story to `CLAUDE_HISTORY.md` instead.

- **Current state**: Finished `src/dbd.hpp`'s own long-documented scope gap
  (a prior autonomous-session task that hit the session's API limit
  mid-implementation and was stashed rather than left broken on `master` —
  picked back up and completed this session, real WIP recovered via
  `git stash pop`, not restarted from scratch). `dbd::resolveFieldNames`
  used to match a `.dbd` LAYOUT's inline fields to a real WDC5 file purely
  by *position*, with only a coarse field-*count* safety net — a layout
  with the right count but the wrong per-field shape (wrong layout hash
  matched, or a stale/wrong WoWDBDefs definition) could silently return
  real-looking column names misapplied to the wrong bytes, exactly the
  class of bug `~/.claude/CLAUDE.md`'s "Coding Policy: Foreign Data" rules
  exist to catch. Now cross-validates each matched field's real
  WoWDBDefs-declared `<Size>`/`[Length]` shape (parsed by a new
  `dbd::parseFieldLine` extension, previously discarded) against that
  same-position `db2::FieldStorageInfo` entry, per real WDC5 storage type:
  exact bit-size match for `field_compression_none`; upper-bound-only for
  bitpacked/bitpacked-signed (real compression legitimately uses fewer
  bits than the declared width, never more); exact `array_count` for
  `bitpacked_indexed_array`; genuinely un-checkable (not guessed at) for
  `common_data`/`bitpacked_indexed`, whose `field_size_bits` describes
  something other than the field's own logical value width. Any
  disagreement fails closed to `nullopt` (generic `field_<N>` fallback
  naming), same as the pre-existing count check. Verified against real
  data, not just synthetic cases: `chrmodelmaterial.db2` +
  `reference/WoWDBDefs`'s real `ChrModelMaterial` layout resolves
  correctly under the new check (its own real bitpacked/bitpacked-indexed-
  array fields exercise the non-trivial branches, not just the `None`
  case), and neither of the two other real DBD-resolved chains this
  project already had test coverage for regressed (`chrmodeltexturelayer.db2`
  → `charcomponenttexturelayouts.db2`'s `--dir` FK-constraint chain; the
  `--db2-dir`/`--char-layout-id` character-texture-layout extras path). Six
  new synthetic tests (`tests/test_dbd.cpp`) cover each storage-type branch
  individually, including two that had a real authoring bug when first
  written (missing a `field_storage_info` entry for the layout's own `ID`
  field, so the coarse count check masked what the shape check was
  actually meant to exercise — caught by actually running the tests, not
  assumed correct from the diff). `dbd.hpp`'s module comment,
  `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 1 paragraph, and
  `README.md`'s `db2-export` section updated to describe the new,
  narrower scope precisely — including what's still genuinely
  uncheckable, not overclaimed. Full suite green, 590/590.
- **Prior session**: Closed the small real gap the prior session's
  `TODO/ENGINE_TODO.md` audit found and deliberately left open: `m2::Event::data`
  (`src/m2_scene.hpp`, an opaque per-event `uint32_t`) is now exported into
  the event node's own glTF extras, not just parsed. Per wowdev.wiki
  M2#Events, `data` is documented to exist ("This data is passed when the
  event is fired") but not what it means per event — genuinely opaque, same
  "expose raw, don't guess" treatment this project already gives PCOL's
  `flags` (`WIKI_FINDINGS/M2.md`). Threaded through the existing pipeline
  unchanged in shape: `gltf::Skeleton::Event` (`src/gltf_skeleton.hpp`)
  gained a `data` field, populated in `attachPlacementNodes`
  (`src/cmd_export.cpp`, where events are actually built — not
  `export_skeleton.cpp`, which only handles the separate `dump-chunks` JSON
  path via `m2::eventName`; corrected that assumption while reading the
  code), and attached as a `data` extras key on the same `event_<identifier>`
  node in `writeGlbMulti` (`src/gltf_skeleton.cpp`), same one-extras-object-
  per-anchor-node pattern `animate_attached`/`light_animation` already use.
  Verified against the real `mace_2h_bolvar_d_01.m2` weapon fixture (2 real
  events, `$WTB`/`$WTT`) — `husk info` already printed `data=0` for both
  (a pre-existing, independent code path, `src/cmd_info.cpp`); the new test
  in `test_integration_weapons.cpp` re-parses the same file directly via
  `m2::parseEvents` and asserts the exported node's `data` extras matches
  that independent read exactly, not just that it's present. Also extended
  the existing synthetic `writeGlb` event test
  (`tests/test_gltf_skeleton.cpp`) with a non-zero `data` value round-trip
  check. `M2_COMPLETENESS.md`'s Events row and `TODO/ENGINE_TODO.md`
  item 6 updated to mention `data` is now exported (item 6's own
  conclusion — no sound-file reference lives in this field — is unchanged).
  Full suite green, 586/586. Scope was deliberately narrow (this one field
  only) per the task's own instruction — noticed but did not touch:
  `TODO/ENGINE_TODO.md` item 6's own text still cites `m2::eventName`/
  `src/export_skeleton.cpp` as if that were the glTF node-naming path; it
  isn't (that's the `dump-chunks` JSON path) — real node names come
  straight from `"event_" + e.identifier` in `src/cmd_export.cpp`/
  `src/gltf_skeleton.cpp`. Pre-existing doc imprecision, not introduced
  this session, flagged for a future docs pass rather than fixed here.
- **Prior session**: `TODO/ENGINE_TODO.md` (a spec for a hypothetical
  downstream engine project, not husk itself — see its own header) refreshed
  against real current source, not just the 4-item starting list this task
  was given. Confirmed stale, and corrected in place: item 3 (hardcoded
  texture resolution) claimed `husk export` doesn't surface `M2Texture::type`
  at all — it does, `texture_type` material extras
  (`gltf::Material::textureType`, `src/gltf_mesh.hpp`, set in
  `src/export_materials.cpp`), plus a real typed `alternate_textures`
  candidate pool per ambiguous slot. Item 4 (`aliasNext`) claimed it "isn't
  even parsed" — it's fully parsed and chain-resolved
  (`m2::Sequence::aliasNext`, `src/export_animation.cpp`,
  `alias_next`/`is_alias` clip extras), closing what the old text called a
  "genuinely unresolved even from the file alone" open question; only the
  id-to-name lookup against `AnimationData.dbc` remains external. Item 5
  (`blendTimeOperation`) claimed `blendTimeIn`/`blendTimeOut` "aren't
  currently parsed at all" — they are, exported as `blend_time_in`/
  `blend_time_out` clip extras; the real remaining gap (the blend-transition
  *rule*, correctly framed as pure client logic with no data to find) was
  already accurate and untouched. Item 6 (sound linking) claimed `M2Event`
  is "diagnostic-only, not exported as glTF nodes" — real `event_<identifier>`
  child nodes are exported (`gltf::Skeleton::Event`, `src/gltf_skeleton.cpp`);
  found and noted one genuine sub-gap while verifying: `m2::Event::data`
  (`src/m2_scene.hpp`, an opaque per-event `uint32_t`) is parsed but not
  currently exported into the node's extras — real, small, left as a
  possible follow-up, not fixed here (this was a docs-only task, no `src/`
  changes made). Item 7 (LOD thresholds) cited a `particleBoneLod` field
  that doesn't exist anywhere in husk's codebase (verified by repo-wide
  grep) — corrected to describe what's actually exposed (`lod_count` via
  `husk info`, not `dump-chunks`). Items 1 (geoset selection) and 2 (`.bone`
  correction selection) were re-verified and found still accurate as
  written, untouched. Priority ranking at the bottom updated only where a
  claim it depended on changed (item 4's ranking no longer hedges on
  alias sequences carrying unreachable keyframe data — they don't, confirmed
  above); the external-data-acquisition priority itself is unaffected.
  Header's `commit aa0df15` citation updated to the commit this refresh was
  done against. Full test suite green, 586/586, expected for a docs-only
  change but re-run anyway per this task's own instruction. Committed as
  `[UNVERIFIED/STAGED]` per Luna's standing convention for unsupervised
  documentation passes.
- **Prior session**: `db2.hpp`'s long-standing named gap -- "full decoding of
  offset-map ('sparse', flags & 0x01) sections ... expose their raw
  variable-length record bytes but not per-field values" -- is now closed
  for the `field_compression_none` case (the only one any real local
  offset-map file actually uses). New `db2::decodeOffsetMapRecord`
  (`src/db2.hpp`/`.cpp`) decodes a whole record in one pass with a running
  bit cursor, NOT via `field_storage_info.field_offset_bits` (that value
  only describes the table's *non-sparse* layout -- confirmed wrong against
  real `scenescripttext.db2` bytes, where an inline string shifts every
  field after it). Wired into both `husk db2-info` (row dump now works for
  offset-map sections too) and `husk db2-export` (`LoadedFile::
  skippedOffsetMap` removed entirely -- these sections get a real SQLite
  table like any other now); `cmd_db2.cpp` gained a shared `FieldValues`/
  `decodeRecordValues` abstraction so printing/column-planning/binding is
  one code path regardless of section shape, not two parallel ones. Found
  and fixed three real bugs via actual corpus data, not by inspection, all
  before this shipped: (1) a scalar 32-bit field's own value could
  coincidentally look like a short printable string (real:
  `conversationline.db2`'s `SpeechType` field, value `0x78` = ASCII `'x'`)
  -- fixed by only attempting the inline-string heuristic on fields whose
  `field_size_bits == 32` (a real string field's non-sparse counterpart is
  always a 4-byte string-table offset) plus a 4-byte minimum match length;
  (2) that same minimum length then wrongly rejected two genuinely real
  short strings at the very end of a record (`scenescripttext.db2`: a
  literal empty `Script` field, and a literal 1-character `" "` `Script`
  field) -- fixed by dropping the floor to 0 specifically when there isn't
  room left in the record for the alternative (a raw 4-byte int read)
  either, the one case where "short string" is the *only* value consistent
  with the record's own declared length; (3) the character-class check
  reused from `resolveFieldString` (permissive of high/UTF-8-continuation
  bytes, safe there since it's only applied within an already-likely
  string-table region) let a real large/negative 32-bit value
  (`conversationline.db2`'s `AdditionalDuration = -2500`, bytes starting
  `0x3c 0xf6 0xff 0xff`) "read through" its own high bytes into the *next*
  field's bytes before finding a zero terminator, silently desyncing every
  field after it -- fixed by making the offset-map heuristic strictly
  ASCII-only (reject anything >= 0x7f), a deliberate, documented divergence
  from `resolveFieldString`'s own looser rule. Verified end to end against
  all 5 real local offset-map files (found via a full `db2-info` scan of
  `/media/luna/data/wow_export/dbfilesclient/`'s 1129 files):
  `conversationline.db2` (69,312 rows, real numeric fields incl. the
  negative-duration case above, zero decode errors) and
  `scenescripttext.db2` (36,498 rows, real Lua source text, incl. the exact
  `Name`/`Script` pair used as this session's own regression test) both
  export cleanly via `db2-export --dbd-dir`; the three
  `collectablesource*sparse.db2` tables decode every record with no errors
  and their one array-typed field (`float[3]` map-position columns)
  reinterprets to plausible real in-game coordinates. Non-None storage
  types (Bitpacked/CommonData/BitpackedIndexed(Array)) are implemented the
  same way for offset-map records but **not verified** against real bytes
  -- none of the 5 found locally use them; said so explicitly in `db2.hpp`'s
  module comment and `README.md`, not overclaimed. New tests: 7 synthetic
  cases in `tests/test_db2.cpp` (plain numeric record, real inline string
  shifting a later field, the two short-trailing-string cases, the
  high-byte-value case, a genuinely truncated record throwing, an
  out-of-range record index throwing) via a new `buildOffsetMapFile` helper
  (had its own bug caught by the suite itself: forgot the trailing
  `offset_map_id_list` block `db2::parse` always reads once `flags & 0x01`
  is set, regardless of relationship data -- every offset-map fixture in
  this file must include it), plus a real-data-gated CLI test in
  `tests/test_cli_db2.cpp` against the real `scenescripttext.db2` export
  (skips cleanly when the file isn't present locally). Full suite green,
  586/586.
- **Prior session**: Closed `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 1
  "non-inline relationship data decoded structurally but not yet folded
  into the exported table itself" gap -- `husk db2-export` (both single-file
  and `--dir` modes, `src/cmd_db2.cpp`) now emits a real SQLite column and
  `FOREIGN KEY` for a `$noninline,relation$` DBD field (occupies no WDC5
  field-array slot at all -- its value lives only in the section's own
  `relationship_map`), not just an inline one. New `LoadedFile::
  nonInlineRelationColumns` (resolved via `dbd::findNonInlineNonIdFieldNames`
  + a by-name lookup into the DBD table's own `COLUMNS` block for the
  relation target, same pattern `dbdNames`/`nonInlineIdColumnName` already
  use); `writeFileTable` decodes each section's `db2::
  nonInlineRelationValuesByRecord` once per section (shared across every
  non-inline relation column in that table, since a section carries exactly
  one `relationship_map`, not one per DBD field) and binds
  `sqlite3_bind_null` for any record with no map entry rather than
  fabricating a value. Both helper functions this relied on
  (`db2::nonInlineRelationValuesByRecord`, `dbd::
  findNonInlineNonIdFieldNames`) already existed from an earlier session's
  `chrmodel_db2.cpp`/`db2table.cpp` work -- this session's actual gap was
  narrower than it first looked: `db2-export` itself never called either
  one, despite both being already-proven. Verified against real local data,
  not just synthetic: copied the real `chrmodeltexturelayer.db2` (922 rows,
  a real `$noninline,relation$` `CharComponentTextureLayoutsID` under its
  own layout) and `charcomponenttexturelayouts.db2` (4 rows) into a scratch
  `--dir` batch against the real `reference/WoWDBDefs` checkout -- the
  resulting table gets a real `"CharComponentTextureLayoutsID" INTEGER`
  column and `FOREIGN KEY ... REFERENCES "CharComponentTextureLayouts"
  ("ID")` constraint; every one of the 922 real rows resolves a non-null
  value (`COUNT(*) == COUNT(CharComponentTextureLayoutsID)`), and a real SQL
  `JOIN` returns rows for every one of the 4 layout IDs actually present in
  this (necessarily partial) local export -- the low join-hit-rate (40/922)
  is the local export's own real incompleteness (`CharComponentTexture
  Layouts.db2` only has 4 rows here), not a bug in the relation resolution
  itself. Two new tests in `tests/test_cli_db2.cpp`: a synthetic
  `buildDb2WithNonInlineRelation` fixture (one inline ID field, one field
  stored purely in the `relationship_map`, position-based since
  `header.flags & 0x02` is clear) proving the column/value/FK/join all work
  end to end, and a real-data-gated test (skips cleanly, doesn't fail, when
  `reference/WoWDBDefs` or the real local `.db2` files aren't present)
  against the exact real chain above. `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s
  Stage 1 paragraph and `README.md`'s `db2-export` section both updated to
  describe the new behavior instead of the old "diagnostic-only" gap;
  Stages 2-5 of that TODO file untouched, out of this session's scope. Full
  suite green, 578/578.
- **Prior session**: `TODO/GEOSET_MASK_TODO.md`'s two "Known bugs" (real
  interactive-Blender findings from 2026-08-08 — arms disappearing when
  switching an unrelated hairstyle group; the tabard-flap dropdown never
  toggling) are now genuinely confirmed fixed, not just assumed — the file
  itself is deleted per this project's own "punch list, not historical
  record" convention. Root cause of why the file's own two prior headless
  verification attempts kept producing confusing, self-doubting results
  (an "ordinal-vs-identifier confusion," then a "0/26 tabard vertices
  matched in any state" dead end): both used position-matching against an
  `evaluated_get`'d depsgraph mesh, which this session found is invalid for
  this pipeline — a genuinely untouched, unposed Armature modifier still
  shifts "rest pose" vertex positions by 0.1-0.3 units (confirmed via a
  calibration check against a known-always-present vertex), nowhere near
  float-noise scale, so no evaluated position ever matched its pristine
  bind-pose counterpart within any sane tolerance, real match or not. Fixed
  by building a debug Geometry Nodes modifier on a separate duplicate
  object that computes the exact same `_build_group_hidden_term` boolean
  expression the real script uses but stores it as a per-point `BOOLEAN`
  attribute with **no** `Separate Geometry` call at all — point-domain
  index order is then guaranteed identical to the pristine mesh, so the
  attribute can be read back by plain vertex index, no position-matching
  needed. Also empirically nailed down the Menu Switch modifier's own raw
  int-to-item mapping the file's own investigation left as an unconfirmed
  "leading theory" (`mod[identifier]` is a plain int; real items start at
  raw value 2, i.e. `item_index + 2`; out-of-range ints clamp to the same
  behavior as the highest-index item) — confirmed by systematically probing
  every int from 0-27 against known ground-truth vertex-group membership,
  not asserted. With the correct mapping, group 12's own real per-item
  vertex sets are exactly disjoint and exactly cover what each item name
  implies: `variant_2` ("both") shows precisely its own 104 tagged
  vertices (0 of variant_3/4's), `variant_3` ("back") its own 64,
  `variant_4` ("front") its own 40, `none` (and every out-of-range int)
  shows none of the 208-vertex union — the file's own "both evaluated to
  fewer vertices than front-only" anomaly was entirely an artifact of the
  old investigation's wrong int/item mapping, not a real bug, once the
  correct mapping is used. Bug 1 (group 0 `SKIN_OR_HAIR`'s
  `ALWAYS_VISIBLE_VARIANTS` fix, landed in the now-also-deleted
  `TODO/BLENDER_SCRIPT_TODO.md`, entry below) is independently confirmed
  the same rigorous way: the base-body vertex group (260 vertices) stays
  visible across all 28 real/synthetic states tested for group 0's own
  selector, while a sample real hairstyle variant (159 vertices) appears
  only at its own correct state and nowhere else — no cross-contamination,
  no accidental hiding. Verification scripts are scratchpad-only, not
  committed (same tier as this project's other one-off headless-Blender
  probes) — real command used: `direnv exec . blender --background
  --factory-startup --python <script> -- tools/husk_blender_geoset_mask.py
  example_exports/character/bloodelf/female/bloodelffemale_hd.glb`.
  Cleanup: ~15 now-dangling `TODO/GEOSET_MASK_TODO.md` citations across
  `README.md`/`TOOLS.md`/`DESIGN.md`, this script's own module docstring,
  and 7 C++/test source files (`src/gltf_mesh.cpp`, `src/cmd_export.cpp`,
  `src/gltf_skeleton.hpp`/`.cpp`, `src/gltf.hpp`,
  `src/gltf_skeleton_internal.hpp`, `tests/test_conformance.cpp`,
  `tests/test_gltf_skeleton.cpp`, `tests/test_integration_weapons.cpp`)
  rewritten to explain the mechanism inline instead of citing a file that
  no longer exists — same precedent as `TRANSFORM_TRIAGE.md`'s own
  deletion pass. Nothing needed folding into `DESIGN.md`'s "Anecdotal
  geoset-group semantics" table — it already existed there, independent of
  the now-deleted TODO file. Full C++ test suite unaffected and confirmed
  green (this was a Python/doc-only session) — see the entries below for
  everything the actual geoset-switch mechanism/design already covers.
- **Prior session**: fixed both real findings from `TODO/BLENDER_SCRIPT_TODO.md`
  (that file now deleted per this project's own "punch list, not historical
  record" convention — both closed and verified). (1) The texture-layout
  overlay's three new nodes (`ShaderNodeGroup`/`ShaderNodeEmission`/
  `ShaderNodeMixShader`, `apply_texture_layout_overlay`) previously got no
  `.location` at all, landing at the node tree's origin — very likely
  overlapping this material's own existing graph, effectively invisible
  without manually dragging nodes apart. Now anchored below the existing
  graph's own lowest node (`min(n.location.y for n in node_tree.nodes) -
  400`), offset left of the Material Output — guaranteed clear of whatever
  this material's own layout already occupies. (2) The "skirt/tunic
  fragment stays visible with every geoset toggled off" report traced to a
  real, previously-undocumented design gap, not a rendering bug: geoset
  groups with only *one* real M2 variant (this model's groups 10/23/33/34)
  were skipped by `build_geoset_switch_node_group`'s old `len(variants) >=
  2` gate entirely, so there was no way to tell "always shown, no
  alternative exists" from "should be toggleable but the toggle is
  missing" — every group now gets a switch as long as it has at least one
  *switchable* variant (new `switchable_variants` helper), gaining a real
  synthetic "none" choice same as any multi-variant group, per Luna's own
  direct steer that Blizzard's own runtime customization system can offer
  "none" without it needing to exist as a real geoset ID in the file
  (same precedent as the already-closed tabard "no submesh for 'no
  tabard'" gap). Separately, real headless
  investigation (`husk`'s own `.glb` extras, not guessed) confirmed
  Luna's own second suspicion — "arms is still part of hair 0, hair 0
  needs to stay enabled while ALSO enabling other hair options" — as a
  real, distinct bug with a concrete root cause: DESIGN.md's own
  "Anecdotal geoset-group semantics" table already names geoset group 0
  `SKIN_OR_HAIR` (two independent community reference tables agree, not
  just "Hair" as this project's own earlier visual read assumed) —
  variant 0 within that group is the character's own base skin body
  (torso/arms/legs, confirmed via the real `.glb`'s own material
  assignment: geoset_id 0's two primitives use `skin_color`/`blindfold`
  materials, not the `hair_color` material every other variant 1-24 in
  that group uses), not a real hairstyle option — WoW's own geoset
  numbering just co-locates them under one group ID. Treating variant 0 as
  just another mutually-exclusive hairstyle choice made the base body
  vanish whenever a real hairstyle was picked. Fixed with a new
  `ALWAYS_VISIBLE_VARIANTS` table (currently only `{0: {0}}`, explicitly
  scoped to this one confirmed case, not generalized to every group
  without more evidence) — variant 0 is now excluded from group 0's
  switch entirely (never hidden, never a dropdown item), same as an
  untagged base vertex; `CURATED_DEFAULT_VARIANTS`'s own group-0 entry
  updated from `"variant_0"` to `"none"` to match (variant 0 is no longer
  a selectable item at all). Verified headlessly against the real curated
  `bloodelffemale_hd.glb` export: group 0's dropdown now offers only
  `variant_1..24`/`none` (never `variant_0`), 23 dropdown switches built
  (was 19 — the 4 newly-switchable single-variant groups), every overlay
  node lands at a real non-origin location distinct per material's own
  existing graph, full `main()` run completes with no errors. No `src/`
  changes — this was pure `tools/husk_blender_geoset_mask.py` Python work,
  C++ test suite unaffected.
- **Prior session**: `tools/husk_blender_geoset_mask.py` (already the
  established "post-import companion script" per Luna's own direct steer,
  not a new file) got a second, independent job this session: parses
  `chr_texture_layout` (previous entry) and adds a toggleable magenta
  section-boundary overlay to every material it concerns. Real finding
  along the way, confirmed empirically (headless-Blender introspection, not
  assumed): Blender's stock glTF importer has **no supported extras target
  for a glTF skin at all** — node/mesh/material/camera/light/scene extras
  all land as real Blender custom properties post-import, skin extras land
  nowhere — so `chr_texture_layout` is invisible to a plain File > Import,
  unlike every other extras this project attaches. The script now re-opens
  the exported file's own raw glTF JSON directly (`read_chr_texture_layout`,
  manual glb chunk parsing) to get at it, independent of whatever bpy's
  importer already did; material-level `texture_type` (used to decide which
  materials the layout "concerns") comes through as a real custom property
  normally, no raw-JSON reading needed for that half. One shared shader
  node group (`_build_section_overlay_group`) computes an axis-aligned
  box-test OR-of-ANDs mask over every real `CharComponentTextureSection`
  rect (Shader Editor's `ShaderNodeMath` has no boolean-logic mode unlike
  Geometry Nodes' `FunctionNodeBooleanMath`, so MIN/MAX substitute for AND/
  OR), gated by a real `NodeSocketBool` group input — a plain checkbox on
  the node once instanced, no Properties-panel promotion needed the way the
  Geometry Nodes Menu Switch case required. Per concerned material, a new
  `Mix Shader` sits between the *existing* (untouched) shader output and
  the Material Output, so switching the overlay back off exactly reproduces
  the original look. **Flagged, not verified**: whether WoW's real atlas Y
  axis is top-down (assumed here, flipped against Blender's bottom-up UV V)
  has not been independently ground-truthed the way the M2→glTF coordinate
  fix was — a human should confirm the overlay lands in the visually
  correct spot before trusting its exact placement. Verified structurally
  (headless Blender): real end-to-end run against `bloodelffemale_hd.m2`'s
  own `chr_texture_layout` (layout 1) touches the expected 3 materials
  (two share `texture_type` 6), builds exactly one shared node group
  instance, each material gets exactly one `Mix Shader` wired into its
  Material Output, toggling the boolean input doesn't crash; a same-model
  export with no `--char-layout-id` skips cleanly with a diagnostic, no
  crash. No automated test suite exists for this script (Blender-only,
  outside `husk`'s own C++/CTest scope, same as `husk_blender_geoset_mask.py`'s
  existing geoset-switch half) — verification is headless-Blender runs,
  same tier as the rest of this file's own testing discipline for Blender
  tooling. `README.md` updated (both the geoset-mask paragraph and the
  Stage-2 paragraph now cross-reference each other).
- **Prior session**: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stage 2
  ("real placement geometry") implemented — the first real consumer of
  locally-extracted DB2 data inside `husk export` itself, not just the
  separate `db2-export` side tool. New `src/db2table.hpp`/`.cpp` (generic
  named-column reader, built on `db2.hpp`/`dbd.hpp`) and `src/
  chrmodel_db2.hpp`/`.cpp` (real typed `ChrModelMaterial`/
  `CharComponentTextureSection`/`ChrModelTextureLayer`/
  `CharComponentTextureLayout` structs on top of it) back new `husk export
  --db2-dir/--dbd-dir/--char-layout-id` flags, attaching real placement
  geometry as `chr_texture_layout` glTF skin `extras`
  (`gltf::Skeleton::CharTextureLayout`) — verified end to end against the
  real `bloodelffemale_hd.m2` + its own real DB2 chain, headless-Blender-
  import-clean. Closed a real, previously-unreachable byte-format gap along
  the way: a WDC5 `$noninline,relation$` column's value (real for
  `ChrModelTextureLayer`'s own `CharComponentTextureLayoutsID`) now resolves
  via a new `db2::nonInlineRelationValuesByRecord`/`dbd::
  findNonInlineNonIdFieldNames` pair, reading the section's own
  `relationship_map` instead of a nonexistent field-array slot. Deliberately
  scoped down from the original TODO wording: does NOT attach placement
  data to individual `alternate_textures` candidates (needs `ChrModel.db2` +
  a real display-ID identity husk doesn't have, Stage 3's own open
  problem) — the caller supplies the layout ID directly instead. Full suite
  green, 575/575. See `CLAUDE_HISTORY.md`'s top entry for the full detail
  (new test files, completions regeneration, doc updates).
- **Prior session**: `TODO/DB2_SQLITE_SCHEMA_TODO.md` fully implemented and
  the file itself deleted (all four staged steps done, nothing left open in
  it) — `husk db2-export` now has a real relational schema, not just one
  flat table per `.db2` file. Step 1: `dbd::parseColumnType`'s `<Table::Col>`
  foreign-key-target suffix is captured into a new `dbd::RelationTarget`/
  `dbd::Column` (replacing the old bare `pair<name, type>`), verified
  against real `ChrModelMaterial.dbd` data. Step 2: WDC5's non-inline
  `relationship_mapping` (an alternate per-section foreign-key storage,
  `{foreignId, recordId}` pairs) is now decoded structurally
  (`db2::Section::relationshipEntries`, `db2-info` prints a summary) — found
  and fixed a real ordering bug along the way, caught only by checking
  actual bytes: DB2.md's own WDC5 struct pseudocode notes that
  `offset_map_id_list` moves *before* `relationship_map` when header flag
  0x02 is set, which the original `db2::parse` didn't implement (always
  read `relationship_map` first); verified against the real
  `collectablesourceencountersparse.db2` file (the only local fixture with
  both bits set) — 46,311 relationship entries decoded, foreign IDs
  sequential, record-ID values landing exactly in the table's own declared
  ID range, confirming both the byte layout and DB2.md's "uses record IDs
  instead of record index" semantic note for this flag combination. Step 3:
  new `husk db2-export --dir <db2-dir> <out.sqlite>` mode exports every
  `.db2` file in a directory into one SQLite database (a bad/empty file is
  skipped with a diagnostic, not fatal to the batch); a column with a real
  relation target gets a real SQLite `FOREIGN KEY` constraint whenever the
  target table is also part of the same batch, degrading silently to a
  plain column otherwise. Step 4: verified end to end against the real
  `chrmodelmaterial.db2` -> `charcomponenttexturelayouts.db2` chain — a real
  `--dir` export produces a working `FOREIGN KEY`, and a real SQL `JOIN`
  across the two tables returns correct, matching rows. That verification
  surfaced one more real gap, fixed in the same session: `Char
  ComponentTextureLayouts` has `header.flags & 0x04` ("has non-inline
  IDs") — its real ID lives in WDC5's own separate `idList` array, not in
  the field array at all, so the exporter previously emitted no `ID` column
  for it whatsoever, silently making the join impossible. Fixed with a new
  public `db2::recordId` (idList when present, else the inline field at
  `header.idIndex`) and `dbd::findIdFieldName` (resolves a `$noninline,id$`
  layout field's real name; deliberately excludes plain `$id$` fields,
  already covered by the normal by-position path) — every non-inline-ID
  table now gets a real, named ID column, only in `--dir` mode's own
  writeFileTable (single-file mode is unaffected). Full suite green,
  565/565; new tests include two `dbd::findIdFieldName` unit cases, two
  synthetic `db2::parse` relationship-map cases (one of which — the
  reorder case — was confirmed to actually fail without the fix, not just
  pass with it), a `db2::recordId` non-inline-ID case, and a real CLI test
  building two synthetic, DBD-related `.db2` files and running an actual
  `FOREIGN KEY` `JOIN` against the resulting SQLite output.
- **Prior session**: real DB2 column naming + a real DB2-to-SQLite exporter
  landed, same session as the DETL/DPIV/PCOL investigation and the root-doc
  reorg below. `src/dbd.hpp`/`.cpp` (new) is an independent parser for
  WoWDBDefs' own documented `.dbd` text grammar (`github.com/wowdev/
  WoWDBDefs`) — resolves a real `.db2` file's `table_hash`/`layout_hash`
  (already parsed by the existing `src/db2.hpp` POC) against a local,
  optional WoWDBDefs checkout to get real per-field names/types, matched by
  position (declaration order, skipping `noninline` fields) — never a hard
  dependency (husk doesn't clone/fetch/bundle WoWDBDefs; `reference/
  WoWDBDefs`, gitignored, is dev-only investigation scaffolding, matching
  `reference/wow.export`'s existing role, never read at runtime; `--dbd-dir`
  is a local, optional, user-supplied directory, same tier as `--textures`/
  `--skin-dir`). Verified against real data via a new manifest.json+.dbd
  lookup, tested against the real `ChrModelMaterial` table. New command,
  `husk db2-export <file.db2> <out.sqlite> [--dbd-dir DIR]`
  (`src/cmd_db2.cpp`), writes a real SQLite database (`pkgs.sqlite`, newly
  added to the flake with Luna's permission) — one table per file, real
  column names when resolved, generic `field_<N>` otherwise, one `<name>_<i>`
  column per real WDC5 array element (SQLite has no array column type).
  Verified end to end against real data: `chrmodelmaterial.db2` exports 336
  rows with real `ID`/`CharComponentTextureLayoutsID`/`TextureType`/`Width`/
  `Height`/`Flags`/`Field_9_0_1_34615_006` columns and plausible real atlas
  dimensions (2048x1024, 512x256, ...); `namesreserved.db2` round-trips real
  UTF-8 strings (including non-Latin text) correctly. Per Luna's own direct
  scope clarification, this SQLite exporter is an explicitly separate side
  project (human inspection/correctness-checking, and a future data source
  once world-data work starts) — `export`'s own runtime path still doesn't
  read DB2 data, and Stage 2 of `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
  (real per-table C++ structs feeding `export_materials.cpp`) is still not
  started. Also fixed a real doc-comment inaccuracy caught by Luna directly:
  `db2.hpp`'s old comment cited DB2.md's "Determining Field Types" section
  as the reason WDC5 carries no column names — that section is actually
  about inferring a field's *type* (int/float/string), says nothing about
  naming, and doesn't exist where the comment implied; corrected in
  `db2.hpp` and `README.md`. Full suite green, 557/557, 3 new test files
  (`tests/test_dbd.cpp` unit tests including one real-data case against
  `reference/WoWDBDefs`, `tests/test_cli_db2.cpp`'s two synthetic
  CLI-boundary tests reading the real SQLite output back via the sqlite3 C
  API). **Next step**: `TODO/DB2_SQLITE_SCHEMA_TODO.md` (new) — the SQLite
  exporter's own stated ambition, a real relational schema with mapping/
  join tables preserving cross-file foreign-key relationships (e.g.
  `ChrCustomizationOption` -> `_Choice` -> `_Material`), not just one flat
  table per `.db2` file. Staged four ways: capture the `<Table::Col>`
  foreign-key target `dbd::parseColumnType` currently parses and discards;
  decode non-inline `relationshipData` (currently only skipped, byte-offset
  bookkeeping only, `db2::parse`); a multi-file export mode emitting real
  `FOREIGN KEY` constraints; real join verification against whatever chain
  is actually populated locally (the fuller `ChrCustomizationOption` chain
  can't be fully verified yet -- several of its own tables are still
  0-byte). Not started in `src/` yet; today's exporter is genuinely
  one-file-in, one-table-out.
- **Same session, earlier**: root-directory cleanup — every self-described
  "open punch list" `*_TODO.md` file moved from repo root into `TODO/`
  (17 files), with the 11 world-specific ones plus
  `TEXTURE_TYPE_COLLISIONS_REPORT.md`/`NOTE_ABOUT_WORLD_HANDLING.md` moved
  one level deeper into `TODO/WORLD/`; every cross-reference across docs and
  source comments fixed (several hundred sites). `EYES_ON_FINDINGS.md`
  pruned of 3 fully-resolved items (bone naming, root-bone weighting, the
  `Submesh::Level` bug), 3 genuinely open ones kept and renumbered.
  `HUSK_CORPUS_FINDINGS{,2}.md` deleted (fully migrated into `README.md`/
  `TOOL_COMPARISON.md`); `INLINE_COMMENT_RULES_VIOLATIONS.md` condensed into
  a pending-cleanup note at the top of `README.md`, then deleted. Also a
  short DETL/DPIV/PCOL investigation: DETL's `scale`/`diffuseColorMultiplier`
  confirmed dead constants in every real file (only `flags` bit 3 varies,
  plausibly a shadow-casting toggle); DPIV's byte structure characterized
  (new `TODO/DPIV_TODO.md`, concrete next steps, not yet solved); `PCOL`'s
  bit-semantics gap reframed from "permanent data-source gap" to "real but
  currently unfulfillable DB2 dependency" after finding `housedecor.db2`
  (the obvious candidate table) is 0 bytes in the local export — the same
  finding that led into the DB2/DBD work above.
- **Investigation-only session**: closed `PCOL`'s long-standing "`flags`'
  per-record meaning is undocumented — exposed raw" gap as far as real
  corpus data allows. A full 2,354-file scan (`pcol_files_for_exploration.txt`,
  husk's own already-verified `dump-chunks` output) found `flags` is
  structurally a real per-triangle bitmask, not an enum (every distinct
  value decomposes into a small combinable bit set; 98.4% of files use
  only bit 0; every rarer value is a singleton confined to one specific
  decorative doodad) — individual bit *semantics* remain unconfirmed, a
  DB2/client-data gap, not an M2-side one. Also found `flagsCount` isn't
  always `faceNormCount` (3/2,354 real files differ) — already handled
  correctly by the existing parser, no code change needed. Pure
  documentation update: `WIKI_FINDINGS_HISTORY.md` §16 (new), `WIKI_FINDINGS/
  M2.md`, `DESIGN.md`, `M2_COMPLETENESS.md`, `src/dump_chunks_misc.hpp`'s
  doc comment. See `CLAUDE_HISTORY.md`'s top entry for the full detail.
- **Independent, unsupervised tasks, same session, all three committed as
  `[UNVERIFIED/STAGING]` per Luna's own instruction, awaiting her review**:
  (1) `TODO/TODO_correctness.md`'s former item 2 (five unconsumed M2
  lookup-table arrays) — `husk info` dereferences `sequenceLookup`/
  `boneLookup`/`textureLookup`/`attachmentLookup`/`cameraLookup`, verified
  against real `wolf.m2`/`bloodelffemale_hd.m2` data. (2) `M2Light`'s 7
  animated `M2Track` fields (ambient/diffuse color+intensity, attenuation
  start/end, visibility) — resolved into `type`/`light_animation` node
  extras, reusing `gltf::Material::AnimatedColorCurve`/`AnimatedScalarCurve`;
  verified against a new real fixture, `ui_mainmenu_pandaria.m2`
  (`test_data/interface/glues/models/`, gitignored per convention), the
  only real fixture in this repo confirmed to actually have `M2Light` data.
  (3) `M2Attachment::animate_attached` (same M2Track<uint8_t> shape as
  Light's `visibility`, noticed while doing (2)) — resolved via the same
  (renamed, generalized) helper; real fixtures (`wolf.m2`/two weapon
  models, 23 attachments) all resolve to empty curves, a checked real
  negative result, not a bug — covered by synthetic tests instead. Full
  suite green, 546/546. See `CLAUDE_HISTORY.md`'s top three entries for
  the full detail; unrelated to every thread below, none of which this
  session touched.
- **Current state**: `TODO/GEOSET_MASK_TODO.md` implemented (new this session)
  but **not yet correct** — real bugs found via actual interactive Blender
  use, investigation started but not resolved, continuing in a fresh
  session/thread. C++ side (`src/gltf_skeleton.{hpp,cpp}`, `gltf_mesh.cpp`,
  `cmd_export.cpp`) is solid and fully tested: every export now carries one
  inert "tag" joint per distinct geoset ID, appended to the skin strictly
  after every real bone, woven into a second `JOINTS_1`/`WEIGHTS_1` set
  named `group_<n>,variant_<n>` (comma-separated, prefix-tagged fields, not
  a single combined token, specifically so a consumer can comma-split +
  prefix-strip instead of doing `id/100`/`id%100` math). Blender's *stock*
  glTF importer turns this into a real per-geoset vertex group with zero
  custom import tooling, verified empirically before any of this was
  written (Armature modifier renormalizes total weight across joint sets
  regardless of what's stored, so a second full weight set doesn't distort
  deformation; vertex groups are mesh-owned data independent of the
  armature's bones, so a fake tag bone can be deleted post-import with the
  group/anything built from it left completely intact). Full C++ test
  suite green, 532/532, unaffected by anything below.

  `tools/husk_blender_geoset_mask.py` (new) went through two real
  designs. The first built a Mask-modifier stack (verified working, but
  90 modifiers on one real export — "an insane stack of mask modifiers,"
  a real usability complaint in its own right). Replaced with a real
  Geometry Nodes graph: one `Menu Switch` dropdown per geoset group (a
  chain of `Separate Geometry` nodes peels each variant off a running
  remainder), confirmed scriptable via `bpy` after several real API
  gotchas found empirically (`GeometryNodeMenuSwitch` starts with two
  placeholder items that must be cleared; its `Menu` input only becomes a
  real modifier-panel dropdown once promoted to a `NodeSocketMenu`
  interface entry and *linked before* its default is set; the exposed
  modifier value is stored by integer index, not name). Aggregate
  correctness looked solid — the default-state evaluated mesh matched the
  superseded Mask-modifier version's own vertex count exactly (4,232), and
  switching one group's dropdown changed the count as expected.

  **Real interactive use the same day found that aggregate-count
  verification wasn't enough**: (1) picking a different hairstyle (geoset
  group 0) makes unrelated arm geometry disappear; (2) the tabard back-flap
  geometry never disappears no matter what's selected. This session's own
  follow-up investigation (see `TODO/GEOSET_MASK_TODO.md`'s "Known bugs"
  section for the full detail) ruled out two things with hard evidence —
  husk's own C++ export has zero cross-*group* vertex tagging, and the
  node graph's wiring/`Compare`-node defaults are correct — and found one
  strong, unconfirmed lead: `GeometryNodeSeparateGeometry` with `domain=
  'POINT'` (the default, never overridden) does not cleanly partition
  geometry — a synthetic repro showed a face straddling a selection
  boundary vanishes from *both* the Selection and Inverted outputs
  entirely, a real structural risk in a design that chains 109 sequential
  separations. Also found a suspicious, unresolved discrepancy between the
  modifier's raw stored default value and this session's own assumption
  about ordinal-vs-identifier indexing for Menu Switch items, which may
  mean some of tonight's own verification scripts were reading their own
  results wrong rather than exposing a second real bug.

  **Update, same night**: Luna manually ground-truthed group 12 in
  Blender's real GUI — it does control the tabard flaps
  (`variant_2`=both, `variant_3`=back, `variant_4`=front), and found a
  real, separate gap: no "none" option exists, because the M2 itself has
  no submesh for "no tabard" (geoset ID 1201 absent from this file's own
  `.skin` data — a real fact about the model, not a husk bug). Also
  proposed the real architectural fix directly: don't chain `Separate
  Geometry` against a shrinking remainder; compute one boolean-math
  expression per vertex first, apply exactly one `Separate`/`Delete
  Geometry` at the end. Implemented as a single design: a `STRING`-typed
  `Menu Switch` per group outputs the *name* of the currently-selected
  variant's vertex group (or a sentinel matching nothing, for a new
  synthetic "none" item, closing the gap above for every group at once),
  which feeds `Named Attribute`'s `Name` input as a *link, not a
  constant* — confirmed scriptable — collapsing what used to be 109
  chained geometry operations down to one boolean tree plus exactly one
  final `Separate Geometry` against the pristine input mesh.

  **Verification hit real limits a second time, not resolved.** A first
  headless check showed vertex counts frozen at one value across every
  switch tried (impossible if working) — turned out to be a real
  scripting gotcha (`mod[identifier] = ...` doesn't propagate without
  also calling `mod.node_group.interface_update(bpy.context)`), not a
  graph bug. Fixed that and counts did start responding to switches. But
  a targeted check tracking all 26 real tabard-flap vertex positions
  across all four of group 12's states found **zero of 26 present in any
  state**, and `variant_2` ("both") evaluated to *fewer* vertices than
  `variant_4` ("front only") — backwards if "both" is really their union.
  Given headless position-matching has now produced one confusing result
  on this feature already (the ordinal-vs-identifier confusion above),
  this was handed back rather than chased further blind — **needs Luna's
  own real interactive Blender GUI testing**, the same method that
  correctly found both original bugs and correctly ground-truthed group
  12's real semantics tonight. See `TODO/GEOSET_MASK_TODO.md`'s "Real bug
  ground-truthed by hand..." section for the concrete next step (click
  through group 12's dropdown by hand in the Modifier panel, watch the
  actual viewport). Full C++ suite unaffected throughout, still green,
  532/532 — everything above is pure Python/Blender-script work.
- **Current state (prior session)**: Closed `TODO/TODO_correctness.md`'s former item 4
  (texture-transform pivot-correction math) end to end. `gltf_mesh.cpp`'s
  new `textureTransformToKhr` derives a real `KHR_texture_transform`
  (offset/rotation/scale) from a constant `M2TextureTransform`'s
  texture-center-pivoted rotation (`offset = R*S*translation + R*t_S +
  t_R`), applied on `baseColorTexture` whenever the record is genuinely
  constant (every track either empty or a true single value) and the
  rotation is planar (Z-axis only) -- verified three independent ways
  against real `bloodknightcharger.m2` data (its transform index 2, a
  180-degree rotation + non-uniform (1.0, 1.5) scale): by hand, against
  20,000 randomized trials of `reference/wow.export`'s own
  translate-rotate-translate matrix composition, and via headless
  Blender's own glTF importer producing an exactly-matching Mapping node
  (location (1.0, 1.25), rotation 180 degrees, scale (1.0, 1.5)). Found a
  real, useful negative case along the way: `brewfestmount.m2`'s own
  transform index 0 looks constant under a cruder single-keyframe check
  (`tools/find_texture_transform_files.py`, this session's own discovery
  tool) but actually carries per-sequence-structured translation/scaling
  tracks whose values just happen to all be identity -- husk's own
  stricter `trackHasAnimatedData` check correctly refuses to treat that as
  constant, so it stays extras-only, not a false positive. Two new real
  fixtures committed (`test_data/creature/brewfestmount/`,
  `.../bloodknightcharger/`, `.m2`+`.skin` only -- textures resolved via a
  synthetic 1x1 PNG in tests, real texture bytes weren't needed to verify
  the transform math), six new tests (four synthetic in
  `test_gltf_mesh.cpp`, two real-fixture integration tests). Full suite
  green, 530/530. Real `gltf_validator`/headless-Blender verification
  clean for the new extension specifically (a pre-existing, unrelated
  JOINTS_0/WEIGHTS_0 duplicate-influence data quirk in `bloodknightcharger.m2`
  itself produced validator errors on that one fixture -- confirmed
  unrelated by checking a known-clean fixture still validates 0 errors
  after this change; not investigated further, out of this task's scope).
- **Current state (prior session)**: Resolved the previous entry's own open question with
  two real screenshots -- `bloodelffemale_hd_skin_color_3500119`/`_3500115`
  each pixel-match one specific rectangular region of `_3500123` (the base
  atlas) exactly, non-transparent overlay *patches*, not junk or unrelated
  assets as the "tiny decal" framing implied. Investigated the real
  mechanism directly in `reference/wow.export`
  (`CharMaterialRenderer.js`/`DBCharacterCustomization.js`): real client
  compositing is driven by `ChrModelMaterial` (base atlas size),
  `CharComponentTextureSections` (`X`/`Y`/`Width`/`Height` placement
  rects), and `ChrModelTextureLayer` (blend mode per layer) -- confirmed,
  named DB2 tables, not a guess, and squarely CASC/DB2 data husk has no
  access to by design. What husk *can* do: `AlternateTextureCandidate`
  now carries real `width`/`height` (`src/gltf_mesh.hpp`/
  `export_materials.cpp`'s new `pngDimensions`, emitted as
  `alternate_textures[].width`/`.height`) so a human/script can tell a
  full atlas apart from a small patch without decoding each candidate by
  hand -- not the real placement data, but real, useful, already-load-
  bearing metadata. Full suite green, 524/524.
- **Current state (prior, same session)**: A fourth correction, prompted by Luna trying to
  manually locate `bloodelffemale_hd_skin_color_3500121` in Blender and
  getting confused about where it fit (a real full-body atlas variant per
  her description, "just the body, with the underwear... completely
  different uv layout"). Investigating turned up a real bug beyond
  answering the question: `bloodelffemale_hd`'s twelve `skin_color`-
  category files split into two size classes when actually decoded --
  eight are 256x128 small strap/underwear-decal graphics, four
  (`3500122`-`3500125`) are the real 1024x512 full-body atlases -- and the
  previous entry's "prefer skin_color" rule picked whichever sorted
  alphabetically first among *all* of them, landing on a tiny decal, not
  an atlas. Fixed with a new signal: `pngPixelArea` (`src/export_materials.cpp`)
  reads a candidate's real width x height from its own PNG IHDR chunk
  (already-decoded bytes, no extra pass), and `orderCandidatesForDefault`
  now ranks by pixel area first (largest wins), falling back to the
  `skin_color` category preference only as a same-area tiebreak (needed
  since `body_jewelry` is a correct, same-resolution candidate for this
  slot too). A real performance regression was caught in the same pass:
  the first version re-decoded every candidate once per batch (~27 batches
  x ~60 files) just to sort them, timing out past 120s -- fixed by sharing
  the existing `ambiguousCandidateCache` into the ranking function instead
  of a fresh local one, back down to ~4.6s. Two new regression tests using
  a new `solidColorPng` fixture generator (`tests/test_cli_fixtures.hpp`
  -- every prior fixture used one fixed 1x1 PNG, insufficient for testing
  size-based ranking), each proven to fail with its own signal disabled.
  Full suite green, 524/524. Still unresolved: whether `3500121`
  specifically (decoded: a small decal) is really what Luna meant, given
  her own description sounds like a full-body-scale asset -- flagged back
  to her, not assumed reconciled.
- **Current state (prior, same session)**: Immediate refinement to the entry below's own fix --
  told directly that `body_jewelry`/`bracelets` are texture *overlays*
  composited onto the skin texture (no UV map of their own, same family
  as `skin_color`/`face`), while `jewelry_color` textures a genuinely
  separate 3D jewelry mesh with its own UV map -- excluding
  `body_jewelry`/`bracelets` from type 20 was right, but leaving them
  unclassified was incomplete. `candidateCategoryTypes`
  (`src/export_materials.cpp`) now maps them to `{1, 8}` (skin/skin_extra)
  explicitly. Verified: the `skin`-type material's candidate pool includes
  them again as real overlay candidates, `char_jewelry` still sees only
  its own two `jewelry_color` files. Existing regression tests unaffected
  (none assumed *where* these tokens mapped, only that they weren't type
  20). Full suite green, 523/523.
- **Current state (prior, same session)**: One more real correction, same session --
  `LUNA_FINDINGS.md` (not `LUNA_NOTES.md`, a misnamed pointer corrected
  directly after this session reported the wrong file had no new content)
  confirmed the material-dedup and `char_hair`/`eyereflect` fixes below by
  real Blender verification, and found `candidateCategoryTypes`
  (`src/export_materials.cpp`) had also wrongly mapped `body_jewelry`/
  `bracelets` to type 20 (`char_jewelry`) alongside `jewelry_color` on an
  unverified English-name assumption -- viewed directly (`husk-blp`),
  `body_jewelry_3602029` is a visually distinct necklace-chain item, not
  a color variant of `jewelry_color`'s gold/silver collar design, no
  confirmed type-20 evidence for it. Fixed by removing both from the
  category table entirely (no reassignment without evidence). Verified:
  `char_jewelry`'s `alternate_textures` now lists exactly the two
  `jewelry_color` files, matching `LUNA_FINDINGS.md` exactly. New
  regression test, proven to fail without the fix. Full suite green,
  523/523.
- **Current state (two sessions ago, same session)**: Two more real bugs found and fixed, same investigation
  thread, prompted directly with a reference screenshot (correctly-matched
  tan skin/blue hair/silver jewelry) and a concrete complaint ("we REALLY
  need to get ridd of the 500 materials produced by batches... only 1
  material per mat<num>_tex<num>_<id> combination", plus repeated
  `..._body_jewelry_3602029.<N>`-suffixed duplicate images in Blender).
  (1) `src/export_materials.cpp` now computes a real content signature
  (`materialDedupKey`) per fully-built material and reuses an existing one
  via `materialByKey` instead of emitting a new `gltf::Material` per batch
  -- real `bloodelffemale_hd.m2` export: 114 materials → 10. (2) The
  primary embedded image now shares the same cross-material cache
  `alternate_textures` already used, closing the one remaining duplicate-
  image case dedup alone didn't (two genuinely *different* materials
  independently resolving to the same unrecognized-fallback file). (3) A
  real correction to the previous session's own "prefer bare over face"
  default logic: viewed directly via `husk-blp`, the bare
  `bloodelffemale_hd_3255415.blp` file that kept winning the `skin` slot's
  default turned out to be a tiny sparkle icon, not a skin texture -- the
  real full-body atlas was under the *recognized* `skin_color` category
  the whole time (confirmed: default's average color went from
  transparent-black to a real tan (0.44, 0.27, 0.15) matching the
  reference screenshot). `filterCandidatesForType` now always prefers
  recognized-category candidates over bare/unrecognized ones, falling back
  to unlabeled files only when nothing recognized exists at all -- no more
  guessing what an unlabeled file *is*. Two new regression tests, both
  proven to fail without their respective fix. Full suite green, 522/522.
- **Current state (prior, same session)**: Follow-up in the same session, reported directly from
  Blender: embedded images were showing up as auto-generated
  `Image_<N>` names instead of their real, useful source filenames (e.g.
  `bloodelffemale_hd_hair_color_5196731`), because none of
  `gltf_mesh.cpp`'s three image-embedding sites ever set `tinygltf::
  Image::name`/`Texture::name`. Fixed: `Material::baseColorImageName`
  (new field, `src/gltf_mesh.hpp`) is populated at every
  `export_materials.cpp` resolution site that sets `baseColorImagePng`
  (M2's own embedded filename, a `<FileDataID>` exact match, a sole
  fuzzy match, or the chosen candidate out of an ambiguous pool) and
  used to name the emitted image/texture; the `alternate_textures`
  candidates and `additionalTextureLayers` (FileDataID only, no
  filename tracked) get the same treatment. Verified two ways: a new
  unit assertion (`tests/test_gltf_mesh.cpp`) and a real headless-Blender
  import of the actual `bloodelffemale_hd.m2` export, both before/after
  -- 99 images all named `Image_<N>` before, 0 generic names after. Full
  suite green (520/520).
- **Current state (prior, same session)**: Fixed `EYES_ON_FINDINGS.md` #3/#6's ambiguous-texture
  cross-contamination (Luna's own concrete example: a face `.blp`
  showing up as a candidate for a shoes-region `skin`-type material).
  `src/export_materials.cpp` now filters each hardcoded slot's fuzzy-pool
  candidates by a real filename category token
  (`classifyCandidateCategory`, e.g. `"skin_color"`/`"face"`/
  `"hair_color"`/`"jewelry_color"`/`"blindfold"`) matched against which
  `M2Texture::type` values that category is actually compatible with
  (`candidateCategoryTypes`, transcribed from `reference/wow.export`'s
  own character-customization code, not guessed) — a hair-color file no
  longer leaks into an eyes or jewelry slot's `alternate_textures` just
  because both are independently ambiguous. Types 1/8 (`skin`/
  `skin_extra`) are a real, separate case: `wow.export`'s own
  `apply_skinned_model_textures` shows the real client composites
  several layers together for these two, which husk still can't do (no
  DB2 blend-order data, by design) — so `"skin_color"`/`"face"` both stay
  valid candidates there, but a bare/`skin_color` file is now preferred
  as the wired default over a narrower `face` overlay
  (`preferBaseLayerCandidate`), and every candidate's parsed category is
  now attached to its own `alternate_textures` extras entry so a human/
  Blender script can tell what each one actually is. Verified against
  the real `bloodelffemale_hd.m2` + its real CASC texture directory: the
  `skin` slot's pool went from 94 undifferentiated candidates to 57
  correctly-typed ones, `char_eyes`/`char_jewelry`/`ui_skin` slots each
  now see only their own real candidates (9/19/2 respectively), zero
  cross-category leakage. New synthetic regression test
  (`tests/test_cli.cpp`, two hardcoded slots of genuinely different
  `M2Texture::type`s sharing one pool) proven to actually fail without
  the fix before being confirmed green. Full suite 520/520
  (`./build/husk-tests`). See `EYES_ON_FINDINGS.md`'s finding #3/#6 for
  the full writeup, including what's still genuinely unresolvable
  (*which* composited skin/face layer is correct for a given character's
  real customization choices — needs DB2 data husk doesn't have) versus
  what this fix actually closes (structurally-impossible cross-category
  offers).
- **Current state (prior session)**: Fixed the M2→glTF "upside down" export bug for real
  (`TRANSFORM_TRIAGE.md`) — the historical three hand-typed position/
  rotation/scale conversion formulas are now one mechanically-derived
  system (`src/gltf.cpp`'s `kWowToGltf` matrix, corrected from `(x,-z,y)`
  to `(x,z,-y)`, with position/rotation/scale all derived from it rather
  than hand-typed separately). Corroborated three independent ways: the
  change-of-basis math, a real headless-Blender round-trip, and
  `reference/wow.export`'s own independently-written conversion code.
  Covered by a new asset-agnostic synthetic coordinate-frame probe test
  tier (proven to actually catch the bug, not just pass), a property-based
  rotation-matrix unit test, a real humanoid-landmark sanity check, and a
  new quadruped fixture (`test_data/creature/wolf/wolf.m2`). Full suite
  green, 484/484 (`./build/husk-tests`), zero hand-updated literals needed
  anywhere else in the suite. `DESIGN.md`/`README.md`/`TRANSFORM_TRIAGE.md`
  all updated to match. See `CLAUDE_HISTORY.md`'s top entry for the full
  narrative, including the two real corrections Luna made to the plan
  before any code was written.
- **Next step**: `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` (new this session) --
  the real, staged plan for full DB2-driven character texture compositing.
  Real WDC5 DB2 tables (`ChrModelMaterial`/`CharComponentTextureSection`/
  `ChrModelTextureLayer`, plus the full `ChrCustomization*` choice chain)
  confirmed present as local files in Luna's own real local `casc-tool`
  export (**not** `reference/wow.export`, an unrelated third-party JS tool
  checked out for source-code reference only -- don't conflate the two)
  -- in scope per Luna's own direct clarification ("the only hard boundary
  is not loading casc tool as a dependency," not "no DB2 data ever";
  `DESIGN.md`'s existing Non-goals wording needs a real update once this
  lands, see that TODO's own Background section). Not started in `src/`
  yet -- five stages (WDC5 parser, placement geometry, the customization-
  choice chain, real pixel compositing, Blender-side picker tooling),
  each independently useful, see the TODO file for why staged rather than
  one large change.
- **Next step (also open, from an earlier session)**: a genuinely open, freshly-found gap from this session --
  `bloodelffemale_hd.m2`'s three real (`textureType == 0`) FileDataID-based
  slots (`3536810`/`4530998`/`5210137`) have no matching local file at all
  in the real `/media/luna/data/wow_export` texture directory, so they
  fall back to the same ambiguous same-basename pool as the hardcoded
  slots and land on the fallback tier's arbitrary pick -- not a resolution
  bug (`EYES_ON_FINDINGS.md`'s newest addendum has the full detail), but
  whether these FileDataIDs are just absent from this export or live under
  a different naming convention entirely is unconfirmed, not guessed at.
  `M2_GAPS_TODO.md` and, as of an earlier session, `RO_COMPLETENESS_TODO.md`
  are both fully implemented and deleted (see Last state above and
  further above) — every item either ever bundled (`M2Sequence`
  fields/`aliasNext`, `PFDC`, `EXP2`, `Texture.type`, Attachments/Events/
  Lights, animated tint/fade, `DETL`, `PCOL`, header-metadata decode,
  `WFV1`/`WFV2`/`DPIV`/`AFRA`, `blp/` DXT3 verification, `resolveSkin`
  diagnostics, plus regression-test follow-ups) is done, tested,
  documented. There is no active TODO file for M2/`blp/` in this repo
  anymore — the only remaining tracked, undone work in that scope lives in
  `TODO/TODO_correctness.md` (`M2Camera`, `.bone` slot *selection* — both
  low-priority by design, not oversight) and the two carryover threads
  below. Both `ANIM_TODO.md`'s
  `--anim` same-basename fallback and `PHYS_TODO.md`'s full `.phys`
  physics/collision support are implemented, tested, and documented. The M2→glTF
  multi-root-bone-forest representation gap is real, tested code now, not
  a survey — see `DESIGN.md`'s Key design decisions (the
  synthesized-non-joint-parent-node entry). Genuinely open threads, all carried over from earlier
  sessions and untouched by this one: (a) the ~7
  `textureComboIndex`-out-of-range cases `CORPUS_TODO.md` #5 couldn't
  re-verify (`failures_unique.txt` strips paths) — almost certainly the
  same "mismatched shared batch data" root cause as the now-16x-confirmed
  `materialIndex` case, but genuinely unconfirmed; (b) `tools/
  corpus_checks.py` keeping at least one real example path per distinct
  failure *message shape*, not just the top-N codes by count, so a case
  like (a) doesn't stay unverifiable next time. `resolveSkin`'s failure
  messages now do name the specific candidate path/FileDataID they tried
  (this session, see Last state) — that specific gap is closed. Optional
  scope expansion (WMO/M3, Blender-side tooling for the various `extras`
  this project already exports) is still nominally open from this file's
  own perspective, but see Last state's own note: Luna appears to have
  already started a WMO/ADT/world-geometry scaffolding pass in a
  concurrent session (`WORLD_COMPLETENESS.md` and several new
  `*_TODO.md` files landed in the work dir mid-session, untouched by this
  one) — worth checking her intent directly before assuming this is still
  unclaimed, rather than duplicating or stepping on it.
- **Hazards**: geoset tag joints (`Skeleton::geosetTags`, new this session,
  see Last state) are the one deliberate *exception* to the very next rule
  below — they **are** added to `skin.joints` (unlike Attachment/Event/
  Light), always appended strictly after every real bone so real joint
  indices 0..N-1 are never renumbered, and always parented under whatever
  node is/would be the skin's own closest common root (the single real
  root joint, or the synthesized multi-root parent) so `gltf_validator`'s
  "closest common root" check keeps passing. If `emitSkeletonAndSkin`
  (`gltf_skeleton.cpp`) is touched again: the synthesized multi-root
  parent node's own index formula must account for
  `skeleton->geosetTags.size()` (it sits *past* the tag-node range now,
  not right after the real joints) — this exact class of stale-index bug
  shipped once this session (caught by the existing `gltf_validator`-
  backed multi-root weapon test, not by inspection) before being fixed.
  Also: a tagged vertex's `WEIGHTS_0` and `WEIGHTS_1` must be rescaled
  *together* so their combined sum stays 1.0 (`gltf_mesh.cpp`'s
  `emitMeshNode`) — leaving `WEIGHTS_0` at its original full sum while
  adding a second full-summing `WEIGHTS_1` produces a real
  `gltf_validator` `ACCESSOR_WEIGHTS_NON_NORMALIZED` error, even though
  Blender's own Armature modifier renormalizes at runtime regardless (this
  project's test suite gates on zero `gltf_validator` errors, so this
  matters even though Blender itself wouldn't visibly misrender). Separate,
  pre-existing, NOT caused by this session's work, flagged but not fixed:
  the real `bloodelffemale_hd.m2` fixture has 6,879 vertices with a
  duplicate joint index within their own raw `JOINTS_0` slots (husk only
  ever copies `m2::Vertex::boneIndices` through, never modifies it) —
  confirmed via a standalone tinygltf-linked scan tool (scratchpad only)
  that a clean fixture (`wolf.m2`) has zero such duplicates, so this is a
  real data property of that one specific model, not a systemic bug;
  whoever next touches raw M2 bone-index handling should know it's there
  before assuming a `gltf_validator` `ACCESSOR_JOINTS_INDEX_DUPLICATE`
  report on that fixture is new.
- **Hazards (continued)**: the Attachment/Event/Light glTF nodes added this session
  (see Last state) follow the exact same rule as the multi-root
  synthesized parent node below — **never add them to `skin.joints`**,
  they're plain translation-only child nodes of a real joint, not bones
  themselves (verified via headless Blender's `bone_count` probe staying
  exactly `header.bones.count` with these nodes present). `M2Light`'s
  `bone == -1` ("not attached to any bone," real per wowdev.wiki) is
  currently treated as an out-of-range-joint throw, same as any other bad
  index — no real fixture has exercised this case yet, so if one ever
  does and the throw is wrong, that's new information, not a regression.
  Per-clip `sequence_metadata` extras (`gltf::Animation::SequenceMetadata`,
  `M2Sequence`'s movespeed/frequency/replay/blend-time/bounds fields) are
  carried through unchanged even for an alias clip built from its
  terminal sequence's keyframe data — the alias's *own* metadata fields
  are what's attached, not the terminal sequence's, since those two are
  independent per-`M2Sequence` facts even when the keyframe data itself is
  shared. If `buildAnimations`'s alias-resolution branch
  (`resolveAliasChain`) is touched again: it must keep checking
  `flags & 0x20` ("stored inline") *before* treating a sequence as a "pure"
  alias needing chain resolution — a real fixture
  (`bloodelffemale_hd.skel`) has 31 of 38 alias-flagged sequences also
  carrying `0x20`, meaning they already have real inline data of their own
  that must not be overwritten by a different sequence's keyframes (see
  Last state for how this was caught before shipping). For the multi-root rework (now implemented), never insert a
  synthetic node into `Skeleton::joints` itself (see `src/gltf.hpp`'s
  `Skeleton` doc comment and `DESIGN.md`'s Key design decisions for why:
  every vertex/emitter-anchor/correction/animation joint
  index is a raw, unremapped M2 bone-array index, and a reordered
  `Skeleton::joints` would silently misattribute all of them with no
  crash and no validator error) — `writeGlbMulti`'s actual implementation
  confirmed to respect this (the change lives entirely in glTF-side node/
  scene/skin construction, `Skeleton::joints` itself untouched), covered
  by a real test (`test_gltf.cpp`'s mixed mesh-nodes-plus-multi-root case
  asserting vertex joint indices stay raw/unshifted), not just asserted
  safe by inspection. This session's own changes are each covered by real
  tests (see Last state for the specific test names). One thing worth
  knowing if
  `cmd_export.cpp`'s collision-mesh block is touched again: it always
  appends its `NamedMesh` *after* every render/LOD entry — anything
  indexing `namedMeshes` by position (like the "N LOD tier(s)" summary
  print, fixed in an earlier session via `renderMeshCount`) needs to
  account for that trailing entry, not assume `namedMeshes.size()` equals
  the render-mesh count. Carried over from earlier sessions:
  `completions/husk.bash`/`.zsh` are generated, checked-in
  artifacts (`husk --print-completion=<bash|zsh>`) — if `addExportOptions`'s
  flag table changes, regenerate both rather than hand-editing; **the
  completion generator's per-flag value-taxonomy tables in `src/main.cpp`
  (`bashValueCompletion`/`zshValueAction`/`zshFlagLabel`) are hand-maintained,
  separate from `addExportOptions`, and don't pick up a new flag's
  `none`/directory semantics automatically** — a new flag falls through to
  plain-filename completion until it's added to those tables explicitly
  (found the hard way in an earlier session; verify by actually sourcing
  the regenerated script and driving `_husk_completions`/`_husk`, not just
  diffing that a new flag
  name appears). `HUSK_TEST_DATA_DIR` (`CMakeLists.txt`) is baked absolute
  at configure time, so the default `test_data/`-fallback fixtures are
  immune to the old `ctest`-runs-from-`build/` relative-path trap — but if
  you override any `HUSK_TEST_*` env var by hand for `ctest` specifically
  (not `./build/husk-tests` directly), it still needs to be absolute, or
  that one test fails on a bad relative path, not a real regression.
  If `buildAnimations`'s external-sequence branch (`cmd_export.cpp`) is
  touched again: the FileDataID-vs-basename fallback logic must check
  `f.is_open()`, never `!f` — a default-constructed `std::ifstream` that
  never had `.open()` called on it (the `animFileIds == nullopt` case)
  reports `goodbit`, not `failbit`, so `!f` silently evaluates false and
  the code falls through to reading an unopened stream instead of trying
  the next fallback (a real bug this session's own implementation had
  before an existing test caught it — see Last state). `.phys` chunk tags
  are byte-reversed on disk (WMO/ADT convention) — the opposite of every
  other sidecar husk reads (`.bone`/`.skel`/M2 itself) — `src/phys.cpp`'s
  chunk-tag constants are already the reversed literals; don't pass a
  forward-spelled tag to `findChunk` when touching that file. The
  `mace_1h_warfrontsforsaken_d_0100.skin` fixture (`test_data/item/
  objectcomponents/weapon/`) was added this session specifically to pair
  with the already-committed `mace_1h_warfrontsforsaken_d_01.m2`/`.phys` —
  it's the only committed `.phys` weapon fixture with a matching `.skin`,
  used by `tests/test_integration.cpp`/`test_conformance.cpp`'s real
  `--phys` checks (`HUSK_TEST_WEAPON_PHYS`/`_SKIN`, `tests/
  test_data_paths.hpp`).
