# CLAUDE.md — session history

Full session-by-session narrative log for husk, most recent first (the
same entries that used to live inline in CLAUDE.md's own Resume section).
`CLAUDE.md`'s Resume section now holds only a condensed current-state
summary plus Next step/Hazards — this file is where the full story lives.

**Append new entries at the top** (right after this intro, before the
existing most-recent entry) each session — this file is an append-only
log, historical entries are never rewritten (only living cross-references
inside them get repointed if something they name is deleted/renamed, per
this project's own established convention — see e.g. how prior TODO-file
deletions handled their own back-references).

---

- **Last state**: Investigation-only session, `PCOL`'s `flags` field
  (real, undocumented per-triangle data, flagged "exposed raw, not
  interpreted" since it was first implemented — WIKI_FINDINGS_HISTORY.md
  §10/§9). A dedicated scan (husk's own already-verified `dump-chunks`
  output, no new C++ parser needed) over all 2,354 real `PCOL`-bearing
  files (`pcol_files_for_exploration.txt`) found `flags` is structurally
  a real bitmask, not a sequential enum: every distinct value across the
  full corpus (`{0,1,2,3,4,5,6,7,8,23,221}`) decomposes into a small
  combinable bit set (0–7 is the exact power set of bits 0/1/2). 98.4% of
  files use only bit 0; every value above 5 is a singleton confined to
  one specific decorative doodad (a light sconce, a food prop, a
  player-housing lamp) — plausible per-object special collision
  behavior, but individual bit meaning is unconfirmed (no wiki field
  names, no DB2/client data — `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md` already
  has a staged, in-scope plan for real local WDC5 DB2 access for a
  different feature; the same path would apply here if this is ever
  worth chasing further, not a permanent dead end). Also found a real,
  separate structural fact the original implementation's own doc comment
  overstated: `flagsCount` is usually but not always equal to
  `faceNormCount` — 3 real files (`flagsCount == faceNormCount + 8`) are
  a genuine exception. No code changes were needed for either finding —
  `dumpPcol` (`src/dump_chunks_misc.cpp`) already reads every region via
  its own independent header field, exactly as the format requires — this
  was purely a documentation gap, now closed: `WIKI_FINDINGS_HISTORY.md`
  §16 (new, full byte-level format writeup), `WIKI_FINDINGS/M2.md` (new
  summary entry), `DESIGN.md`/`M2_COMPLETENESS.md`/`src/
  dump_chunks_misc.hpp`'s own doc comment all updated to match, including
  dropping the pre-existing "niche" characterization of player-housing
  content (a full expansion feature, not a niche corner case) and
  correcting a "DB2 data husk doesn't have, by design" overclaim now that
  DB2 access is real, staged, in-scope work elsewhere in this repo, not a
  permanent non-goal. Scan tooling itself (`pcol_flags_scan.py`, run via
  the established `direnv exec . uv run --python tools/venv/bin/python
  <script>` pattern) lived in the session's own scratchpad, not committed
  — a one-off investigation script, not a reusable corpus tool.

- **Previous state**: Third independent, unsupervised task in a row, same
  session (small follow-on to the Light-animation entry directly below,
  already committed as its own `[UNVERIFIED/STAGING]` commit). Noticed
  while resolving Light's `visibility` track that `M2Attachment::
  animate_attached` (M2Track<uint8_t>, "whether the attached model
  animates with this one, default true") has the exact same shape and was
  still explicitly marked "skipped ... not something husk's glTF export
  has a slot for yet" in `m2_scene.hpp`'s own doc comment -- a second,
  smaller, self-identified gap right next to the one just closed.
  Implemented: `m2::Attachment` gained `animateAttachedTrackOffset`;
  `gltf::Skeleton::Attachment` gained an `animateAttached` curve-vector
  field; `cmd_export.cpp`'s Light-specific `resolveLightVisibilityCurve`
  was renamed to `resolveRawByteTrackCurve` (it was already fully generic
  -- a raw M2Track<uint8_t> resolver, nothing Light-specific about its
  body) and reused for both, rather than a second near-duplicate function;
  `gltf_skeleton.cpp`'s attachment-node loop now writes an
  `animate_attached` extras key when there's real data.

  Checked against real data before assuming this was ever actually
  populated: 3 real fixtures with real attachments (`wolf.m2` -- 12,
  `sword_2h_ashbringer_a_01.m2` -- 6, `mace_1h_warfrontsforsaken_d_01.m2`
  -- 5, 23 total) all resolve to *zero* real `animate_attached` keyframe
  data -- a genuine negative result matching the wiki's own "only a bool
  is used, default is true" note, not a bug (same "real, checked absence"
  shape as several other findings this project has logged before, e.g.
  `chrcustomization*.db2`'s 0-byte files in the concurrent DB2 session).
  Correctness still covered by two new synthetic round-trip tests
  (`tests/test_gltf_skeleton.cpp`) since no real fixture exercises the
  populated case; `gltf_validator` confirmed 0 errors/0 warnings on the
  real `wolf.m2` export regardless. Full suite green, 546/546.
  `M2_COMPLETENESS.md`'s Attachments row updated (`deref`/`native` ->
  `full`/`native + extras`) with the real negative-result note attached so
  a future reader doesn't mistake empty output for an unresolved
  offset bug.

- **Last state**: Second independent, unsupervised task in a row (same
  session pattern as the lookup-table entry directly below, already
  committed as its own `[UNVERIFIED/STAGING]` commit). Picked
  `M2_COMPLETENESS.md`'s Lights row: `M2Light`'s 7 `M2Track` fields
  (ambient/diffuse color+intensity, attenuation start/end, visibility) were
  the one field this project's own doc comment
  (`gltf_skeleton.hpp`'s old `Skeleton::Light`) flagged by name as "a
  separate, larger problem (same sibling scope as the animated material
  tint/fade curves ... above)" -- a specific, self-identified next step,
  not a guess. Implemented: `m2::Light` (`m2_scene.hpp`/`.cpp`) now carries
  all 7 M2Track byte offsets (parse depth `deref` -> `full`, matching
  `m2::Color`'s own offset-storage precedent); `cmd_export.cpp` gained
  three light-specific curve resolvers
  (`resolveLightColorCurve`/`resolveLightFloatCurve`/
  `resolveLightVisibilityCurve` -- the third exists separately because
  `visibility` is a raw 0/1 `M2Track<uint8_t>`, not a fixed16-scaled value
  like the material fade curves, so it must NOT go through
  `decodeFixed16`); `gltf::Skeleton::Light` gained a `type` field plus 7
  curve-vector fields, reusing `gltf::Material::AnimatedColorCurve`/
  `AnimatedScalarCurve` directly (via a new `#include "gltf_mesh.hpp"` in
  `gltf_skeleton.hpp`) rather than duplicating the shape a third time;
  `gltf_skeleton.cpp`'s light-node-emission loop now writes a `type`/
  `light_animation` extras pair per light node, mirroring
  `gltf_mesh.cpp`'s own tint/fade extras JSON shape (mirrored, not
  reused -- different translation unit, building a node's extras rather
  than a material's).

  Verified against real data, not just synthetic: every weapon/creature
  fixture already committed to `test_data/` turned out to have
  `lights.count == 0` (a real, checked fact -- M2Light data is essentially
  absent from that model category in practice), so a real corpus scan
  of `interface/glues/models/ui_mainmenu_*` (login-screen models, the
  wiki's own documented use case for this data) found a genuine hit:
  `ui_mainmenu_pandaria.m2`, 2 real lights, both with genuine per-sequence
  keyframe data (plausible warm/cool RGB tuples, 0..1-range intensities,
  sane attenuation values) -- confirmed by hand via `strings` on a real
  export before writing any test (no working Python in this session's
  shell without going through the flake, worked around rather than
  bypassing the sandboxing rule). New fixture committed to (gitignored)
  `test_data/interface/glues/models/ui_mainmenu_pandaria/` (`.m2` + its
  auto-resolved `00.skin`, ~660 KB total, same size class as every other
  real fixture here), registered through `test_data_paths.hpp`/
  `test_main.cpp` the same way every other `HUSK_TEST_*` fixture is.
  New tests: two synthetic (`tests/test_gltf_skeleton.cpp`, the
  round-trip case and the "no animated data -> no `light_animation` key"
  case) plus one new real-fixture integration test in a new file
  (`tests/test_integration_lights.cpp`, added to `CMakeLists.txt` --
  split out rather than folded into `test_integration_weapons.cpp` since
  every fixture there is weapon-scoped and this one genuinely isn't).
  Full suite green, 544/544 (`./build/husk-tests`); real export also
  independently confirmed clean via `gltf_validator` (0 errors, 0
  warnings). `M2_COMPLETENESS.md`'s Lights row updated (`deref`/`native`
  -> `full`/`native + extras`). Deliberately left uncommitted for Luna to
  review at first, then committed as `[UNVERIFIED/STAGING]` per her own
  explicit instruction to follow the same pattern as the two independent
  tasks before it in this session.

- **Last state**: Independent, unsupervised task -- picked TODO/TODO_correctness.md's
  former item 2 (five uint16 lookup-table arrays -- `sequenceLookup`/
  `boneLookup`/`textureLookup`/`attachmentLookup`/`cameraLookup`, wowdev.wiki
  M2#Header -- parsed into `Array` descriptors but never dereferenced or
  printed anywhere, confirmed via a real grep sweep before starting). Fixed:
  `husk info` (`cmd_info.cpp`) now dereferences all five via the existing
  `m2::parseUint16Array`, resolving each entry's index to a name where husk
  already has one (`keyBoneName`/`textureTypeName`/`attachmentTypeName` --
  no new name tables, reused what M2's per-record printing already uses) and
  skipping 0xFFFF ("-1", "no entry") sentinels. `sequenceLookup` specifically
  is printed as its real hash-bucket shape (`bucket = anim_id % count`,
  quadratic-probe collision per the wiki), not pretended to be a direct
  id-indexed array -- printing it as "bucket N -> sequence[value] (id=...)"
  rather than mislabeling the bucket index as an animation id. Verified
  against two real fixtures, not just synthetic data:
  `test_data/creature/wolf/wolf.m2`'s `bone_lookup` resolves key bone 26 to
  bone 0 named "Root" and key bone 6 to the real Head joint (26/27/35 real
  key-bone slots depending on model, matches the wiki's own count note); its
  `attachment_lookup`/`camera_lookup` resolve cleanly too.
  `bloodelffemale_hd.m2`'s `texture_lookup` resolves texture type 1 ("skin")
  through type 20 ("char_jewelry") to the exact texture indices matching its
  own hardcoded per-texture `type=` fields printed just above. One new
  regression test (`tests/test_cli_info.cpp`, synthetic header built past
  `minimalMd20()`'s 0x130-byte end, one resolvable entry + one 0xFFFF
  sentinel per array) -- confirmed to exercise the sentinel-skipping and
  name-resolution paths, not just presence. `M2_COMPLETENESS.md`'s lookup-
  tables row updated from `descriptor`/`none`/"unclaimed" to `deref`/
  `diagnostic`/"pure indirection metadata, no independent renderable shape";
  `TODO/TODO_correctness.md`'s former item 2 removed outright per the file's own
  convention, remaining item renumbered (was 3, now 2). Full suite green,
  541/541 (`./build/husk-tests`). Deliberately left uncommitted for Luna to
  review, per this task's own instructions -- nothing here has had human
  eyes on it yet. No overlap with the concurrent `[UNVERIFIED/STAGING] WDC5
  DB2 parser` commit found already on `master` when this session started
  (`src/db2.*`/`src/cmd_db2.cpp`) -- different M2/DB2 scope entirely, not
  touched here.

- **Last state**: Continuation of the same `TODO/GEOSET_MASK_TODO.md` effort,
  same session as the entry below, prompted by more of Luna's own real
  interactive Blender testing. Ground-truthed the tabard bug from the
  entry below by hand, in Blender's real GUI, not headless scripting:
  group 12 does genuinely control the tabard flaps (`variant_2` = both
  flaps, `variant_3` = back only, `variant_4` = front only), and a real,
  separate gap was found the same way -- no "none" option exists, because
  `bloodelffemale_hd.m2` simply has no submesh for "no tabard at all"
  (geoset ID 1201 is absent from this file's own `.skin` data, a real fact
  about the model, not a husk omission).

  Proposed the real architectural fix directly, two ideas that turned out
  to combine into one design: (1) don't chain `Separate Geometry`
  sequentially against a shrinking remainder -- select independently from
  the original mesh and recombine with `Join Geometry`; (2) go further --
  build one real boolean-math expression per vertex first (no geometry
  operations at all), then apply exactly one `Separate`/`Delete Geometry`
  to the result, hypothesized as "mutually exclusive inside a group,
  inclusive OR between groups," which matches exactly what husk's own tag
  data guarantees. Implemented as a single combined design: a
  `data_type='STRING'` `Menu Switch` per group outputs the *name* of
  whichever variant's vertex group is currently selected (or a sentinel
  string matching no real attribute, for a new synthetic "none" item --
  closing the tabard gap above for every group generally, not just
  tabard), and that string feeds `Named Attribute`'s own `Name` input as a
  *link, not a constant* -- confirmed scriptable this session (the crux of
  the whole redesign, verified with a small synthetic probe before writing
  the real graph). One dynamic attribute read per group then tells you,
  per vertex, whether it belongs to whichever variant is currently active,
  without enumerating every variant's own comparison per switch. Combined
  with a per-group "does this vertex belong to this group at all" OR-chain
  (still enumerated once per group, but purely boolean, no geometry
  operations), the whole graph collapses to one boolean expression
  evaluated once, then exactly **one** `Separate Geometry` call against
  the pristine input mesh -- directly removing the "chain 109 sequential
  separations, each re-deriving selection against an already-shrunk
  remainder" shape that was the leading suspected cause of the arms bug
  from the entry below.

  **Verification hit real limits a second time in the same session, not
  resolved, reported honestly rather than claimed fixed.** A first
  headless check showed vertex counts frozen at exactly one value across
  every single switch tried -- an impossible result if the graph were
  working, which is exactly what made it clear something was wrong before
  trusting it. Root cause: a real Blender scripting gotcha, not a graph
  bug -- `mod[identifier] = value` alone doesn't propagate to the
  evaluated depsgraph without also calling `mod.node_group.interface_
  update(bpy.context)`, something the *previous* redesign's own
  verification script happened to call but this session's fresh scripts
  initially didn't. Fixed, and vertex counts did start visibly responding
  to switches after that. But a more targeted check -- tracking all 26
  real tabard-flap vertex positions (the same ones from the reference
  screenshot two entries below, captured from the pristine pre-modifier
  mesh) across all four of group 12's real states -- found **zero of 26
  present in any state**, including `variant_3`/`variant_4`, which should
  each show roughly half of them; `variant_2` ("both") also evaluated to
  *fewer* total vertices than `variant_4` ("front only"), backwards if
  "both" is really the union of the other two. Both are real, concrete,
  concerning signals -- but headless position-matching has already
  produced one confusing, hard-to-trust result on this exact feature this
  session (the ordinal-vs-identifier stored-value confusion in the entry
  below), so rather than chase a second layer of "is this a real bug or a
  flaw in how I'm checking it" without visual ground truth, this was
  handed back exactly where it was found: real interactive Blender GUI
  testing is what correctly found both original bugs and correctly
  ground-truthed group 12's real semantics tonight -- headless scripting
  has now gotten this feature specifically wrong more than once, and isn't
  the trustworthy verification path here. Full C++ test suite unaffected
  throughout, still green, 532/532 -- everything this entry describes is
  pure Python/Blender-script work, no export-path code touched.

---

- **Last state**: Same overall `TODO/GEOSET_MASK_TODO.md` effort, continued in
  the same session as the entry below — two real design/naming follow-ups,
  then real bugs found via actual interactive Blender use.

  (1) Tag-joint naming changed from a single `geoset_<id>` token to
  comma-separated, prefix-tagged `group_<n>,variant_<n>` fields, prompted
  directly as prep work for a future geometry-nodes rewrite ("splitting
  the group and the variant into neat separate text prefixed fields would
  be nice... that way i can just split the string by comma delimiter, and
  remove prefixes"). Updated everywhere the naming was produced or
  consumed (`gltf_skeleton.cpp`, `gltf.hpp`'s doc comment, the companion
  script's parser, the synthetic test, `TODO/GEOSET_MASK_TODO.md`/`DESIGN.md`),
  verified end to end against the real export (`group_0,variant_0` etc.,
  identical masking behavior to before the rename).

  (2) That "future geometry-nodes rewrite" arrived the same session,
  prompted directly ("utilize the switch nodes to do the filtering instead
  of a insane stack of mask modifiers"). Researched feasibility first (web
  search, since `docs.blender.org` 403'd every direct fetch attempt
  regardless of page for this session's fetch tool) and found a real
  citation from the actual Blender PR that implemented Menu Switch,
  confirming `enum_definition.enum_items` as the real scripting API before
  writing anything. Rebuilt `tools/husk_blender_geoset_mask.py` around a
  real Geometry Nodes graph: one `Menu Switch` dropdown per geoset group,
  fed by a chain of `Separate Geometry` nodes each peeling one variant off
  a running remainder. Found and fixed three real API gotchas by direct
  empirical probing before committing to the full build: `GeometryNodeMenuSwitch`
  starts with two dead placeholder items that must be `.clear()`-ed; its
  `Menu` input only becomes a real modifier-panel dropdown once promoted
  to a `NodeSocketMenu` interface entry and *linked before* `default_value`
  is set (setting it first throws `enum "..." not found in ()` since an
  unlinked socket has no known items yet); the exposed modifier value is
  stored by integer index, not name (confirmed by a real `TypeError` on a
  string assignment). Verified against the real `bloodelffemale_hd.m2`
  export: default-state evaluated mesh matched the just-superseded
  Mask-modifier version's own vertex count exactly (4,232), and switching
  one group's dropdown changed the count (4,232 -> 4,131), confirming
  functional correctness, not just structural plausibility. Fully replaced
  the Mask-modifier version rather than kept as a fallback.

  **Real interactive use the same day (not headless scripting) found that
  aggregate vertex-count checks weren't enough** — two real bugs, reported
  directly with a reference screenshot (Blender Edit Mode, vertex-index
  overlay, tabard back-flap region, vertex indices 20599-20661
  transcribed by hand into `TODO/GEOSET_MASK_TODO.md` since the pasted image
  itself has no accessible filesystem path this session's tooling could
  copy from): (1) picking a different hairstyle (geoset group 0) makes
  unrelated arm geometry disappear; (2) the tabard back-flap never
  disappears no matter what's selected. Same-session follow-up
  investigation, evidence-based, not fully conclusive:

  - Wrote a standalone tinygltf-linked scan tool (scratchpad only) proving
    husk's own C++ export has **zero** cross-*group* vertex tagging across
    the entire real export — ruled out as the cause, the raw glTF data is
    clean, whatever's wrong is in how Blender evaluates the graph built
    from it.
  - Directly inspected the actual built node tree (not just the Python
    that built it): `Compare`'s implicit "B" input really does default to
    exactly `0.0`, and no two `Separate Geometry` nodes share an upstream
    source — ruled out a wiring bug.
  - Found a real, evidenced, unresolved lead instead: a minimal synthetic
    repro (one quad, 2 of 4 verts selected, split across two triangles
    that each straddle the boundary) showed `GeometryNodeSeparateGeometry`
    with `domain='POINT'` (the default, never overridden) does **not**
    cleanly partition geometry — both triangles vanished from *both*
    Selection and Inverted outputs entirely. A structural risk in a design
    that chains 109 sequential separations, each re-evaluating selection
    across the whole remaining mesh — not yet confirmed as *the* mechanism
    reaching all the way to arms specifically, that needs real interactive
    GUI inspection, not more headless scripting.
  - Cross-referenced the exact vertex indices from the reference
    screenshot directly against the imported mesh's own vertex-group
    data: all 26 carry a real `group_12,variant_3` tag at the expected
    ~0.5 rescaled weight — ruled out "untagged geometry" as Bug 2's cause.
  - Found a suspicious, unresolved discrepancy: the modifier's raw stored
    default value for two very differently-sized groups (25 items vs. 3)
    showed the identical value `2` before any interaction — leading
    theory, unconfirmed, is that the two cleared placeholder items still
    occupy internal identifier slots 0/1, meaning "stored value == ordinal
    list index" (an assumption this session's own verification scripts
    made) may itself be wrong, which would mean at least some of tonight's
    checks were misreading their own results rather than exposing a
    second real bug.

  Full C++ test suite green throughout, 532/532 (unaffected by any of
  this — pure Python/Blender-script work). Explicitly **not fixed this
  session** — both bugs, and the stored-value/ordinal-index question, are
  handed off with real, concrete next steps (`TODO/GEOSET_MASK_TODO.md`'s
  "Known bugs"/"Follow-up needed" sections) needing actual interactive
  Blender GUI access to resolve, continuing in a fresh session/thread per
  Luna's own direct instruction.

---

- **Last state**: Implemented `TODO/GEOSET_MASK_TODO.md` end to end (new this
  session), prompted directly by Luna investigating `EYES_ON_FINDINGS.md`'s
  eye-glow finding and then asking how Blender's Mask modifier could hide
  WoW's mutually-exclusive geoset variants (hairstyles, boot cuffs,
  eye-glow, ...) that husk exports unfiltered (no DB2 customization data,
  `DESIGN.md`'s Non-goals). Landed on a real, verified mechanism: extra
  inert "tag" joints appended to the existing skin (never real bones,
  never posed) woven into a second `JOINTS_1`/`WEIGHTS_1` attribute set per
  geoset ID -- Blender's *stock* glTF importer creates one real vertex
  group per skin joint as an ordinary side effect of skin-weight import, so
  this needed zero custom Blender-side mesh-parsing (a competing "custom
  importer that bypasses Blender's own vertex compaction" design was tried
  and rejected first -- confirmed empirically that Blender's stock importer
  does *not* preserve a 1:1 accessor-index<->Blender-vertex-index mapping,
  195,498 raw positions became 32,939 Blender vertices on a real export,
  ruling out any "read the raw index buffers, poke Blender's post-import
  mesh" shortcut). Also empirically verified, before writing any code: (1)
  stacking a full second 1.0-summing weight set on top of real bone weights
  doesn't distort deformation, because Blender's Armature modifier
  renormalizes total influence weight across every joint set at evaluation
  time regardless of what's stored (posed a real bone, checked the actual
  deformed vertex position via `evaluated_get`'s depsgraph -- moved by
  exactly the pose delta, not doubled); (2) Blender vertex groups are
  mesh-owned data, independent of the armature's bones -- deleting a fake
  tag bone post-import leaves its vertex group/weights completely
  untouched and a Mask modifier targeting it keeps working, verified before
  and after deletion, plus a clean re-export afterward. A real, separate
  dual-armature alternative (avoid touching the real skin at all) was also
  raised, investigated, and ruled out with a concrete technical reason
  (different `JOINTS_0` data per node requires a genuinely separate glTF
  mesh entry, which Blender's importer does *not* auto-share vertex-group
  data across the way it does for two nodes pointing at the literal same
  mesh index) -- written up in the TODO doc rather than silently dropped.

  Implementation: `Skeleton::geosetTags` (`gltf_skeleton.hpp`) — one tag
  joint per distinct geoset ID, appended to `skin.joints` strictly after
  every real bone (the one invariant this whole codebase protects
  religiously -- multi-root synthesized-parent-node precedent followed
  exactly: parented under the single real root joint, or the synthesized
  multi-root parent, so the skin's "closest common root" property still
  holds for `gltf_validator`). `emitMeshNode` (`gltf_mesh.cpp`) builds
  `JOINTS_1`/`WEIGHTS_1` from `Primitive::skinSectionId`, splitting weight
  evenly across however many distinct tags touch a seam vertex.
  `cmd_export.cpp` populates `geosetTags` from the union of distinct
  `skinSectionId`s already collected for the existing geoset-extras
  feature (`BuiltMaterials::distinctSkinSectionIds`, no new collection
  logic needed). A real bug was caught by this project's own
  gltf-validator-backed test suite before landing, not after: a tagged
  vertex's combined weight total across both sets was 2.0, which
  `gltf_validator` correctly flags (`ACCESSOR_WEIGHTS_NON_NORMALIZED`)
  even though Blender's own runtime renormalizes regardless -- fixed by
  rescaling *both* sets down together per tagged vertex so the stored
  combined total is exactly 1.0 again, a pure file-format fix with a
  provable zero effect on Blender's actual rendering. A second real bug,
  same class of catch: the multi-root synthesized parent node's own index
  formula wasn't updated to account for the newly-inserted tag-node range,
  producing `gltf_validator` "not a common root"/"not a root node" errors
  on a real multi-root weapon fixture -- caught by the existing test suite,
  fixed by correcting the index arithmetic.

  Verified at real scale, not just synthetic fixtures: a standalone
  tinygltf-linked scan tool (scratchpad only, not committed) confirmed
  every vertex's combined `WEIGHTS_0`+`WEIGHTS_1` sum is exactly 1.0 across
  the real `bloodelffemale_hd.m2` export (113 geoset IDs, 245 real bones ->
  358 skin joints). That same real export surfaced 1.5M+ raw
  `gltf_validator` messages when run with full resource validation on --
  traced down to a pre-existing, unrelated data property (6,879 vertices
  with a duplicate joint index in their own raw `JOINTS_0` slots, husk
  never modifies those values, only copies them through from the M2's own
  `boneIndices`) and confirmed *not* caused by this session's work: a
  clean fixture already covered by an existing "zero errors" conformance
  test (`wolf.m2`, with the new tag joints active) scans with zero bad
  sums and zero duplicate joints via the same tool. Flagged in
  `TODO/GEOSET_MASK_TODO.md` for whoever next touches raw M2 bone-index
  handling, out of scope for this feature.

  Four existing conformance tests needed their hardcoded
  `skin.joints.size() == header.bones.count` assertions updated to account
  for the real, legitimate growth (`+ <distinct geoset ID count>`, counted
  independently from each primitive's own `geoset_id` extras as a real
  cross-check, not a tautology) -- expected per the TODO doc's own
  prediction, not a regression. Two new synthetic unit tests
  (`tests/test_gltf_skeleton.cpp`) lock in the mechanism directly: tag
  joint naming/parenting/`JOINTS_1`/`WEIGHTS_1` values including the
  rescale, and a no-`geosetTags` case proving zero footprint when unused.
  Full suite green, 532/532.

  Stage 6, the companion Blender script
  (`tools/husk_blender_geoset_mask.py`) -- explicitly anticipated back in
  an earlier session's `DESIGN.md` note ("a companion Blender-side script
  that hides extras-tagged-but-visible geometry post-import is real,
  deliberate usability tooling for later... deferred until someone
  actually wants to *use* exports interactively," which is exactly what
  this session's prompt was) -- walks every `geoset_<id>` vertex group,
  groups by `geoset_group` (`id // 100`, matching husk's own extras
  convention), adds one invert-mode Mask modifier per non-default variant
  (lowest ID kept visible, same disclaimed-placeholder-default precedent
  as `orderCandidatesForDefault` elsewhere in this project), then deletes
  every tag bone from the armature. Verified end to end against the real
  export: 358 armature bones before running the script, 245 after (tag
  bones fully removed, matching the real M2 bone count exactly); 90 Mask
  modifiers created correctly; all 358 vertex groups still present after
  bone deletion; the actual evaluated (post-modifier) mesh drops from
  32,939 raw vertices to 4,232 visible ones -- masking is genuinely doing
  something, not a no-op. This closes out every stage of the plan.

---

- **Last state**: Closed `TODO/TODO_correctness.md`'s former item 4
  (texture-transform pivot-correction math, constant case), picked as a
  self-contained task to work through solo (no DB2/casc-tool dependency
  like `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`, no fuzzy-matching design
  decision like `TODO/BONE_NAME_DEDUCTION_TODO.md` tier 2 -- both considered
  and passed over for that reason).

  Derivation: wowdev.wiki's M2#Texture_Transforms note says rotation
  pivots around the texture's own center (0.5, 0.5), and gives the recipe
  (translate to center, rotate, translate back) but not a closed form.
  `reference/wow.export`'s `M2RendererGL.js` (`_update_tex_matrices`) has
  the real client's own matrix composition: for each of rotation/scaling/
  translation independently present, `local_mat = local_mat *
  T(0.5,0.5)*R_or_S*T(-0.5,-0.5)` (rotation/scaling) or `local_mat =
  local_mat * T(tx,ty)` (translation), composed in that order. Expanding
  this out algebraically as one affine map `uv' = M*uv + t` gives `M =
  R*S` (matching `KHR_texture_transform`'s own scale-then-rotate order
  exactly) and `t = R*S*translation + R*t_S + t_R`, where `t_S = pivot -
  S*pivot` and `t_R = pivot - R*pivot` (pivot = (0.5, 0.5)) -- derived by
  hand, then verified two ways before writing any `src/` code: (1) by hand
  against `brewfestmount.m2`'s simplest real case (180-degree rotation
  only, `offset` should come out to exactly (1,1) -- did) and
  `bloodknightcharger.m2`'s combined case (180-degree rotation + (1.0,
  1.5) scale, `offset` should come out to exactly (1, 1.25) -- did); (2)
  against 20,000 randomized (angle, scale, translation, uv) trials
  comparing the closed form against a literal re-implementation of
  `wow.export`'s own translate-rotate-translate matrix composition (a
  scratch C++ program, not checked in) -- max error 1.8e-15, floating-
  point noise only.

  Real fixtures: `tools/find_texture_transform_files.py` (written a prior
  session, already known to have found `brewfestmount.m2`/
  `bloodknightcharger.m2`/`unboundairelemental_low.m2` as real-corpus
  candidates) was rerun against the two chosen files to get their exact
  transform indices/values before writing tests. Copying them into
  `test_data/creature/brewfestmount/`,`.../bloodknightcharger/`
  (`.m2`+`.skin` only, no `.blp` textures -- committing the texture bytes
  wasn't needed to verify the transform math, only a file existing at the
  expected FileDataID path, so tests write a synthetic 1x1 PNG into a
  scratch dir instead) surfaced a real, useful complication: mapping a
  `.skin` batch to its texture's FileDataID and resolved transform index
  needed a byte offset in `.skin`/M2 husk doesn't expose via any existing
  CLI surface, so a one-off scratch Python script (not checked in) did the
  same fixed-offset decode `find_texture_transform_files.py` already does,
  extended to also resolve `textureComboIndex` -> `TXID` FileDataID.

  Implementation: `gltf_mesh.cpp`'s new `textureTransformToKhr` (an
  anonymous-namespace helper next to `emitMaterial`) implements the closed
  form, gated on (a) the quaternion being planar -- `|x|,|y| <
  1e-4` -- since `KHR_texture_transform`'s rotation is a single scalar
  (Z-axis) angle and a genuine 3-axis rotation (never seen in real corpus
  data so far) has no honest equivalent, and (b) a real
  `baseColorTexture` existing to attach the extension to. Wired into
  `emitMaterial` right after `baseColorTexture.index` is set, only when
  `mat.textureTransform->constant` is true. `usedTextureTransformExtension`
  threaded through `emitMeshNode`/`gltf.cpp` the same way
  `usedUnlitExtension` already was, for the document-level
  `extensionsUsed` entry. The raw resolved values stay attached as
  `texture_transform` extras unconditionally, same as before this
  session -- additive, not a replacement (diagnostic, and the animated
  case's only representation).

  A real complication found mid-implementation, not anticipated by the
  plan: exporting `brewfestmount.m2` with a real texture for its
  transform-index-0 batch produced `constant: false` in the extras output,
  even though `find_texture_transform_files.py`'s cruder single-keyframe
  check called it constant. Root cause: husk's own
  `m2_animation.cpp`'s `trackHasAnimatedData` distinguishes a genuinely
  empty track (`outer.count == 0`) from a structured-but-trivial one
  (`outer.count > 1`, i.e. real per-sequence data, even if every
  sequence's resolved value happens to be identity) -- `brewfestmount.m2`'s
  translation/scaling tracks are the latter, so husk correctly refuses to
  treat the whole record as constant, unlike the scanner's cruder
  "exactly one keyframe or nothing" check. Confirmed this is husk being
  more correct, not a bug: applying a single static UV transform when the
  real per-sequence data could (in principle, on a different real file)
  differ per animation would be a silent misrepresentation. Kept
  `brewfestmount.m2` as a fixture anyway -- a real, useful negative-case
  regression (`tests/test_integration.cpp`'s new
  "brewfestmount.m2's real texture-center-pivot rotation... stays
  extras-only" test) that a synthetic fixture wouldn't have caught without
  already knowing to write it.

  Verification: `bloodknightcharger.m2`'s real export (a synthetic 1x1 PNG
  standing in for its real texture, same as the committed tests) produced
  `KHR_texture_transform` `{offset: [1.0, 1.25], rotation: -pi, scale:
  [1.0, 1.5]}` -- an exact match to the hand-derived expectation (`-pi`
  and `pi` are the same rotation). `gltf_validator` raised zero issues
  tied to the new extension (a `bloodknightcharger.m2`-specific batch of
  `JOINTS_0`/`WEIGHTS_0` errors showed up, confirmed unrelated and
  pre-existing by checking that `bloodelffemale.m2`'s already-verified
  export still validates with 0 errors after this change -- not
  investigated further, out of scope). Headless Blender's own glTF
  importer (a one-off scratch script, not checked in) parsed the file and
  built a real Mapping node for that exact material with `location=(1.0,
  1.25)`, `rotation=-180 degrees`, `scale=(1.0, 1.5)` -- an exact,
  independent third-party match. A second material (transform index 1, a
  ~135-degree rotation, not one of the two hand-derived fixtures) produced
  *different*-looking Mapping-node values in Blender than the raw glTF
  JSON numbers -- expected, not a bug: Blender's own glTF importer
  recomposes `KHR_texture_transform` with its own V-flip convention
  (`V_blender = 1 - V_gltf`), which only happens to leave the Mapping
  node's numbers textually identical to the raw JSON when `sin(rotation)
  == 0` (true for the 180-degree case, not the ~135-degree one) -- not
  independently reproduced byte-for-byte for that second material, but the
  extension itself is spec-conformant per `gltf_validator` and built from
  a formula already verified two other ways.

  Six new tests: four synthetic (`tests/test_gltf_mesh.cpp` -- the real
  extension appears for a constant+planar+textured case with exact
  expected values, and is correctly absent for the animated, no-texture,
  and non-planar-rotation cases respectively) and two real-fixture
  integration tests (`tests/test_integration.cpp`, gated on the two new
  fixtures via `HUSK_TEST_TEXTURE_TRANSFORM_SCALE_M2`/`_ROTATION_M2` and
  test_data/ fallback, following `tests/test_data_paths.hpp`'s existing
  `resolve()` convention). Full suite green, 530/530
  (`./build/husk-tests`). `TODO/TODO_correctness.md`'s former item 4 removed
  outright per the file's own convention; `M2_COMPLETENESS.md`'s "Texture
  transform (constant case)" row updated from `native-possible,
  unverified` to `native — 100%`; `DESIGN.md`'s Key design decisions,
  `README.md`'s format-support matrix, `src/m2_animation.hpp`'s
  `TextureTransform` doc comment, and `cmd_export.cpp`'s own stdout note
  for this feature all updated to match (the note previously claimed the
  UV transform was "not applied to the render" unconditionally, no longer
  true for the constant case).

---

- **Last state**: Same session, immediate second pass widening the SQLite
  side-project note directly below: "it's not gonna be just flat tables
  only, it's gonna have mappings tables and stuff... the actual sqlite
  export is a side project to confirm correctness, and to have data
  available for other relevant targets not just the engine... I think it
  will become massively relevant when the world data implementation
  starts." Two real corrections to the just-written note, both in
  `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO/TODO_correctness.md`: (1) the
  planned SQLite schema is real relational structure -- mapping/join
  tables for DB2 tables' actual foreign-key relationships (the same
  `ChrCustomizationOption` → `_Choice` → `_Material` chain Stage 3 already
  needs), not one flat table per `.db2` file with relationships discarded;
  (2) its purpose is wider than this one TODO -- a correctness cross-check
  for whatever WDC5 parser Stage 1 builds, *and* a general-purpose local
  data source for other consumers of this project's WoW-format work
  beyond `husk export` itself, called out as likely to matter a lot once
  WMO/ADT world-data implementation starts (`WORLD_COMPLETENESS.md` and
  its companion `*_TODO.md` files -- real placement/area/lighting data
  leans on DB2 tables at least as much as character customization does).
  Worth designing the schema with that wider audience in mind from the
  start. No code changed -- documentation only, both notes updated in
  place since neither had been committed yet.

- **Last state (prior, same session)**: Same session, two small closing items plus one scope
  correction, all prompted directly.
  - **`TRANSFORM_TRIAGE.md` closed out.** Its one deliberately-deferred
    item -- a real animated clip, visually confirmed by Luna in Blender's
    actual GUI viewport -- is done: "Animation looks OK." Asked earlier
    this session ("what's the status of transform_triage? if done,
    delete") and now genuinely done, so deleted, matching this project's
    own established "survey's job is done" lifecycle
    (`BLENDER_EXPORT_TODO.md` is the precedent -- already deleted in an
    earlier session, still cited by name in historical entries like this
    one without issue). The two source-code citations that were
    themselves already marked `// TODO: Remove: TRANSFORM_TRIAGE.md`
    (`src/gltf_math.hpp`'s `zUpToYUp` doc, `src/gltf_math.cpp`'s
    determinant `static_assert`) were cleaned up in the same pass, since
    deleting the file they cited is exactly the trigger those markers
    were waiting for -- substantive content (the formula, the invariant)
    kept, only the dev-trace-doc citation removed. Every other citation
    across `DESIGN.md`/`README.md`/`EYES_ON_FINDINGS.md`/
    `INLINE_COMMENT_RULES_VIOLATIONS.md` left untouched -- historical
    narrative citing a since-deleted file by name is this project's own
    established, accepted pattern, not a dangling reference to fix.
    `INLINE_COMMENT_RULES_VIOLATIONS.md` in particular already has its own
    much larger, separately-scoped cleanup pass planned for every
    `TRANSFORM_TRIAGE.md`/dev-trace-doc citation in `src/`/`tests/` --
    not executed here, out of scope for what was actually asked.
    One real, funny, genuinely non-actionable side note from the
    verification itself, worth preserving for the record: a dead vertex
    sits in the middle of the two-handed swing animation, detached from
    the character, FileDataID 31739 -- confirmed genuinely invisible in
    the real game too (an "invisible texture"), not a husk export bug.
  - **SQLite scope correction, `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/
    `TODO/TODO_correctness.md`'s own new notes tightened**: the DB2→SQLite idea
    from an earlier conversation was written up as if it might be part of
    the real pipeline; corrected directly -- "that is mainly for debugging
    and investigation, the real pipeline is the same as with modern
    blp's -- read the file, transform in memory, write to gltf." Fixed in
    both docs: SQLite is a `husk dump-chunks`/`husk-blp`-shaped side tool
    for a human to inspect DB2 contents by hand, not something Stage 1's
    real WDC5 parser round-trips through at runtime -- that parser reads
    `.db2` bytes directly into memory and feeds the rest of the pipeline
    straight from that, same architecture as every other sidecar format
    this project already has. The nested-array open question from that
    earlier conversation stays relevant to the *investigation* tool
    specifically, not the real pipeline.

- **Last state (prior, same session)**: Same session, one more real correction on top of the
  terminology fix directly below: called Luna's local `casc-tool` export
  "trustworthy" in chat and once in this file (now fixed). Her own
  pushback: "I would not classify it as trustworthy, considering the
  amount of bugs the mere m2 pipeline has had... more in the scope of if
  it is wrong, i don't get to blame others." Real distinction, not
  pedantry -- "hers, so any bug in it is her own to own" is a statement
  about *accountability*, not a claim that the data is verified-correct.
  This project's own M2/`.skin`/`blp/` pipeline has a long, real history
  of exactly this kind of bug (`skin::Submesh::Level` misread, the
  duplicate-alternate-texture blowup, several corpus-scan findings) --
  nothing about "it's a local file, not a live CASC query" implies it's
  bug-free, and nothing here should be read as claiming that. The real,
  narrower point (still true, still the reason this is in scope) is just
  that a local `.db2` file is the same *tier of data* as every other
  sidecar husk already reads at the user's own direction -- not a claim
  about its correctness.

- **Last state (prior, same session)**: Same session, immediate terminology correction to the
  entry directly below (and to `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`/
  `EYES_ON_FINDINGS.md`/`CLAUDE.md`, all edited in place since none of
  this was committed yet): "please do not mix wow.expor (the untrustworthy
  tool) and wow_export my local export of wow files via my casc-tool."
  Real mistake, not a nitpick -- the DB2-availability writeup called
  `/media/luna/data/wow_export` (Luna's own local `casc-tool` export) "a
  wow.export directory" in several places, conflating it with
  `reference/wow.export` (the unrelated, explicitly untrustworthy
  third-party JS tool this project already treats as "flaky,
  non-authoritative, corroborating signal only, never a gate" --
  `TRANSFORM_TRIAGE.md`'s own framing from an earlier session). Every
  instance fixed to name the real source precisely: Luna's local
  `casc-tool` export, never "wow.export" unqualified. Same shape as the
  `LUNA_NOTES.md`/`LUNA_FINDINGS.md` mixup earlier this session --
  worth remembering as a pattern, not just fixing each instance in
  isolation: two same-session naming mixups now, both caught by Luna
  directly rather than by careful reading on this end.

- **Last state (prior, same session)**: Same session, direct follow-up to the DB2-mechanism
  finding directly below: "Do we have those db2 tables in the wow_export?
  as if casc exports them we can use them, all data in wow_export is free
  for all, to be used, the only hard boundary is not loading casc tool as
  a dependency." Checked directly: yes -- `/media/luna/data/wow_export/
  dbfilesclient/` has all three tables the previous entry named
  (`chrmodelmaterial.db2`, `charcomponenttexturesections.db2`,
  `chrmodeltexturelayer.db2`) as real local files, confirmed `WDC5`
  format by header (`WOWSTATIC_12_0_7_67808`), plus the entire
  `ChrCustomization*` choice-chain family needed to fully resolve *which*
  file goes in a slot for a specific character (`chrcustomizationoption`/
  `_choice`/`_material`/`_element`/... all present too). This is a real
  scope clarification, not just a factual answer -- `DESIGN.md`'s existing
  "never talk to CASC/DB2 directly" non-goal was written about live
  queries (its own text: "husk only reads what's already on disk"), and a
  `.db2` file Luna's own `casc-tool` already extracted to a local
  directory is exactly that, same tier as `.m2`/`.skin`/`--textures`
  files already are.
  Asked (`AskUserQuestion`) how far to take this given the real size of
  full support (a new WDC5 binary-format parser, a multi-table
  choice-chain resolution design, real pixel compositing, all before any
  Blender-side tooling) -- answer: "Write up findings and stuff into a
  new TODO, the plan is to get full compositing pipeline, and if possible
  build a blender shader node graph where user can just pick from the
  existing options of textures that fit that slot." No code changed this
  turn, by design -- documentation only. New `TODO/CHAR_TEXTURE_COMPOSITING_TODO.md`
  (matching this project's established `*_TODO.md` template --
  `TODO/BONE_NAME_DEDUCTION_TODO.md` used as the structural reference) lays out
  five stages: WDC5 parser, real placement geometry
  (`ChrModelMaterial`/`CharComponentTextureSection`), the customization-
  choice chain (`ChrModelTextureLayer` → `ChrCustomizationOption` →
  `_Choice` → `_Material`), real pixel compositing (blend-mode math per
  `CharMaterialRenderer.js:345-372`, not guessed at), and — the stretch
  goal Luna named directly — Blender-side picker tooling once real
  placement rects exist, the correctly-UV-positioned version of her very
  first ask this session ("1 texture as default and rest... as unlinked
  texture nodes"). `EYES_ON_FINDINGS.md`/`CLAUDE.md` both cross-reference
  the new TODO rather than duplicating its content.

- **Last state (prior, same session)**: Same session, direct follow-up with two real
  screenshots -- resolving the previous entry's own open question
  ("still not confirmed whether 3500121 is really what Luna meant") and
  correcting a real mischaracterization in the process. Screenshot 1:
  Blender's own image editor, `bloodelffemale_hd_skin_color_3500119`
  (left) next to `_3500123` (right, the real base atlas) with a red arrow
  pointing at the exact chest region `_3500119`'s content matches
  pixel-for-pixel. Screenshot 2: same shape, `_3500115` against a
  *different* region of `_3500123`. Both non-transparent, precisely
  aligned -- real overlay patches meant to composite onto one specific
  rectangular region of the base atlas, not "tiny decals" as the previous
  entry's own inspection had characterized them (accurate about what the
  images *show* -- a strap graphic -- wrong about what that *means*, junk
  vs. a deliberate region-keyed patch). Prompted directly: "worth
  investigating how this is mapped originally."
  - **Investigated the real mechanism, not guessed at**: searched
    `reference/wow.export` for the actual client compositing code.
    `src/js/3D/renderers/CharMaterialRenderer.js:114-118` names the exact
    three DB2 structures involved -- `ChrModelMaterial` (`TextureType`,
    `Width`, `Height`: the base atlas's own dimensions),
    `CharComponentTextureSection` (`SectionType`, `X`, `Y`, `Width`,
    `Height`, `OverlapSectionMask`: the literal placement rectangle a
    patch composites into), and `ChrModelTextureLayer` (`TextureType`,
    `Layer`, `BlendMode`, `TextureSectionTypeBitMask`: which section a
    given layer targets and how it blends) -- and
    `src/js/db/caches/DBCharacterCustomization.js:203-215` confirms these
    are read as real DB2 tables (`db2.ChrModelMaterial`,
    `db2.CharComponentTextureSections`, `db2.ChrModelTextureLayer`), not
    inferred from filenames or heuristics. This is real, named,
    documented DB2 data, and squarely the kind of CASC/DB2 access
    `DESIGN.md`'s Non-goals already rules out for husk, by design -- a
    confirmed, dead-end-with-a-name rather than an open question.
  - **What husk can still usefully do, implemented this session**:
    `AlternateTextureCandidate` (`src/gltf_mesh.hpp`) gained real
    `width`/`height` fields, populated by new `pngDimensions`
    (`src/export_materials.cpp`, replacing the narrower `pngPixelArea`
    the previous entry added -- same IHDR-chunk read, now returning both
    values instead of just their product) and emitted as
    `alternate_textures[].width`/`.height` extras
    (`src/gltf_mesh.cpp`). Not the real placement rectangle (husk has no
    `CharComponentTextureSections` data to source one from), but real,
    already-load-bearing data (the same numbers `orderCandidatesForDefault`'s
    own ranking already depends on) that saves a human from decoding each
    candidate by hand to tell a full atlas apart from a small patch --
    exactly the manual cross-referencing work that surfaced this whole
    finding in the first place. Verified on the real export: a real,
    visible size spread now sits directly in the glTF extras (71 entries
    at 512x512, 20 at 256x256, 13 at 1024x512, 10 at 128x128), filterable
    by a human or Blender script without `husk-blp`. Full suite green,
    524/524 -- no behavior change to prove via a disable/re-enable cycle
    this time (pure additive metadata, the ranking logic itself untouched).

- **Last state (prior, same session)**: Same session, fourth round on the ambiguous-texture
  thread -- prompted by a genuine question, not a bug report: "one thing i
  am having trouble locating manually in blender, is
  bloodelffemale_hd_skin_color_3500121... i am confused, where does that
  go." Luna's own description of the asset roles: `3500123` is "the
  'base' skin color that gets rendered under armors... the whole
  character + face + face jewelry" (a full atlas, matching this session's
  earlier `husk-blp` inspection exactly), while `3500121` is "just the
  body, with the underwear... used when the character is not wearing any
  armor, but it has completely different uv layout" -- a second, real,
  mutually-exclusive full-body atlas variant, not an overlay on the first.
  Investigating to answer the question surfaced a real bug beyond it:
  decoded directly (`husk-blp`), `bloodelffemale_hd`'s twelve
  `skin_color`-category files split into two starkly different size
  classes -- eight (`3500114`-`3500121`) are 256x128 small strap/
  underwear-decal graphics (one inspected: a tiny bra-strap detail on a
  mostly-transparent background), the other four (`3500122`-`3500125`)
  are the real 1024x512 full-body atlases, matched skin-tone color
  variants of one design. The previous entry's "prefer skin_color" default
  rule had no way to tell these apart -- it just picked whichever
  `skin_color` file sorted alphabetically first among *all twelve*, which
  landed on `3500114`, one of the tiny decals, not a real atlas.
  - **`src/export_materials.cpp`**: new `pngPixelArea` reads a candidate's
    real width x height straight out of its own PNG IHDR chunk (bytes
    16..23, big-endian) -- no extra decode pass needed, since
    `readTextureFileBytes` already hands back real PNG bytes for both a
    `.png` source and a decoded `.blp` alike. `orderCandidatesForDefault`
    (previously a pure category-preference sort) now ranks candidates by
    this first, largest pixel area wins, falling back to the `skin_color`
    category preference only as a tiebreak *among same-area candidates* --
    needed because `body_jewelry_3602029` (itself a correct, real
    candidate for this slot per the entry above) happens to also be
    1024x512, tying with the real atlas on size alone, and Luna's own
    explanation of the asset roles (skin_color is the thing meant to stand
    alone; body_jewelry/bracelets/face are overlays layered on top of it)
    is the real, direct evidence for keeping that tiebreak rather than
    leaving it to chance.
  - **A real performance regression caught before shipping, not after**:
    the first working version of the pixel-area check read every
    candidate's bytes into a cache scoped *inside* `orderCandidatesForDefault`
    itself -- correct in isolation, but called once per ambiguous batch,
    so a real export with ~27 batches sharing this one candidate pool
    re-decoded the same ~60 `.blp` files from scratch on every single
    call, the identical "1786 redundant decodes" shape this project
    already found and fixed once before with a different feature (finding
    #6). Caught directly: a verification export against the real
    `bloodelffemale_hd.m2` + its real texture directory blew past the
    120s command timeout instead of finishing in its usual ~5s. Fixed by
    threading `buildMaterialsAndPrimitives`'s own `ambiguousCandidateCache`
    (already shared across every ambiguous batch for the real embed step)
    into `orderCandidatesForDefault` instead of a fresh per-call cache --
    same real export back down to ~4.6s.
  - **Two new regression tests**, each proven to fail with its own signal
    temporarily disabled (`return false` in place of the category
    tiebreak; `if (false && areaA != areaB)` in place of the area
    comparison) before being confirmed passing. Needed a real fixture that
    didn't exist yet: every prior test's embedded-image fixture was one
    fixed 1x1 PNG literal, useless for testing size-based ranking. New
    `solidColorPng(width, height, r, g, b)` (`tests/test_cli_fixtures.hpp`)
    builds a real, valid, stb_image/tinygltf-decodable RGBA PNG of any
    size without a zlib dependency -- its `IDAT` stream uses uncompressed
    ("stored") deflate blocks, a real spec-legal deflate encoding (RFC
    1951 §3.2.4) every decoder tested accepts identically to a compressed
    one, plus a from-scratch CRC32 (standard table-based algorithm) for
    each PNG chunk's trailer.
  - **Verified on the real export**: the `skin` material's default image
    changed from `bloodelffemale_hd_skin_color_3500114` (the tiny decal)
    to `bloodelffemale_hd_skin_color_3500122` (a real 1024x512 atlas).
    Full suite green, 524/524.
  - **The question that started this is still not fully answered**:
    whether `3500121` specifically is the file Luna meant is unconfirmed
    -- her own description reads as a full-body-scale asset, but the file
    at that exact name decodes to a small strap/underwear-decal graphic.
    Flagged back to her directly (`EYES_ON_FINDINGS.md`'s newest
    addendum) rather than silently assumed reconciled.

- **Last state (prior, same session)**: Same session, immediate refinement of the entry directly
  below's own fix — told directly, right after: "the [body_jewelry] and
  the bracelets are overlays to be overlayed on top of the skin texture
  files... they are textural options layered on top of the skin, not
  actual meshes, while the jewlery color are colorings for an actual 3d
  mesh jewlery object instead of just image texture layers on top of
  skin, which is why the jewlery color ones have their own UV map."
  Excluding `body_jewelry`/`bracelets` from type 20 (below) was correct,
  but leaving them unclassified was incomplete -- they have a real home:
  types 1/8 (skin/skin_extra), the same compositing family as `skin_color`/
  `face`, not a mesh-specific slot like `jewelry_color`.
  `candidateCategoryTypes` (`src/export_materials.cpp`) now maps
  `body_jewelry`/`bracelets` to `{1, 8}` explicitly, with a doc comment
  recording *why* (overlay-on-skin-texture vs. separate-3D-mesh-with-its-
  own-UV-map is the real distinguishing fact, not just "which type number
  happened to be wrong"). Verified on the real export: the `skin`-type
  material's candidate pool includes `body_jewelry`/`bracelets` again as
  real compositable overlay candidates (not lost), while `char_jewelry`
  still sees only its own two `jewelry_color` files, unchanged from the
  entry below. Existing regression tests (both this entry's and the one
  below) still pass unmodified -- the fix landed entirely inside
  `candidateCategoryTypes`' own data, no test assumed *where* these two
  tokens mapped, only that they didn't map to type 20. Full suite green,
  523/523.

- **Last state (prior, same session)**: Same session, immediate correction to the entry directly
  below — the "read `LUNA_NOTES.md`" instruction was a misnamed pointer to
  a *different* file, told directly after reporting back that
  `LUNA_NOTES.md` had no new content: "You should have asked me when it
  didn't have findings, i could have pointed out that i fucked up the
  naming, it's `LUNA_FINDINGS.md`." Fair correction, noted for next time
  (a file that's supposed to have new findings but doesn't is exactly the
  kind of surprising-enough-to-ask-about case, not one to just report and
  move past). `LUNA_FINDINGS.md` independently confirmed the material-
  dedup fix and the `char_hair`/`eyereflect` bug below by name/example,
  and added one real fact this session hadn't found on its own: real
  Blender verification against `bloodelffemale_hd`'s one `char_jewelry`
  material found only `jewelry_color_3613861`/`_3613862` are actually
  correct, not `body_jewelry_3602029` -- `candidateCategoryTypes`
  (`src/export_materials.cpp`) had mapped `body_jewelry`/`bracelets` to
  type 20 alongside `jewelry_color` on an unverified English-name
  assumption ("jewelry" sounds like it belongs with "jewelry_color").
  Viewed directly (`husk-blp`): `jewelry_color`'s two files are a real
  matched gold/silver color-variant pair of one collar-and-gem design,
  while `body_jewelry_3602029` is a visually distinct necklace-chain item
  -- not a color variant of the same design, no confirmed type-20
  evidence for it at all. Fixed by removing both from
  `candidateCategoryTypes` entirely rather than reassigning them to a
  type with equally no evidence -- they now fall to the unrecognized
  tier and get excluded from `char_jewelry`'s candidates by the existing
  "prefer recognized" rule, no new logic needed. Verified on the real
  export: `char_jewelry`'s `alternate_textures` now lists exactly the two
  `jewelry_color` files, matching `LUNA_FINDINGS.md` exactly. New
  regression test (`tests/test_cli.cpp`), proven to fail without the fix
  before being confirmed passing. Full suite green, 523/523.

- **Last state (prior, same session)**: Same session, third round on the ambiguous-texture
  thread — asked to investigate `LUNA_NOTES.md` for "concrete Blender
  matching information" first: checked directly, that file's own git diff
  showed nothing added beyond its existing 01.08.2026 notes (already
  superseded, `EYES_ON_FINDINGS.md`'s own intro says so) — reported this
  back plainly rather than fabricating findings from a file that didn't
  have new content. The real new information arrived as a reference
  screenshot (correctly-matched tan skin / blue hair / silver jewelry-
  bracelet close-up) plus a direct, concrete complaint: "we REALLY need to
  get ridd of the 500 materials produced by batches, so that there is only
  1 material per mat<num>_tex<num>_<id> combination and if other batches
  find a existing material TO USE THAT ONE not create a new one," and a
  report of repeated `bloodelffemale_hd_body_jewelry_3602029.<N>`-
  suffixed duplicate images in Blender.
  - **Material dedup (`src/export_materials.cpp`)**: `materialDedupKey`
    serializes every field of a fully-built `gltf::Material` that isn't
    purely batch-numbering -- deliberately including per-batch animation
    curves (`tintAnimation`/`alphaFadeAnimation`/`weightFadeAnimation`),
    since M2Color/M2TextureWeight combo indices are batch-level, not
    material-level, so two batches sharing (materialIndex, textureIndex)
    can still legitimately carry different tint/fade animation and must
    not be silently merged. `materialByKey` (content signature -> stored
    material index) reuses an existing entry instead of pushing a new one;
    the stored material's own name has its `batch<N>_` prefix stripped
    once dedup decides to keep it, so the surviving name is
    `mat<M>_tex<T>_<id>`, exactly what was asked for. Verified on the real
    `bloodelffemale_hd.m2` export: 114 materials -> 10.
  - **Primary-image cross-material cache (`src/gltf_mesh.cpp`)**: the
    duplicate-suffixed-image report turned out to be *mostly* the same
    root cause as the material-count bug (dedup alone fixed most of it),
    but one real, separate case remained even after dedup: two
    genuinely *different* materials (different `textureType`) can still
    resolve to the identical file when both fall back to the same
    unrecognized-category wildcard candidate. The primary
    `baseColorImagePng` embed now shares the same `alternateTextureCache`
    (filename -> texture index) the `alternate_textures` candidates
    already used, so this case shares one glTF image too. Verified via
    headless Blender: 0 `.NNN`-suffixed images left, down from 1 residual
    case with dedup alone.
  - **Real correction to the prior session's own default-picking logic** --
    caught by actually decoding and looking at the candidate images
    (`husk-blp`), not just their names/sizes. The bare
    `bloodelffemale_hd_3255415.blp` file that kept winning the `skin`
    slot's default (first via plain alphabetical sort, then via the
    immediately-prior session's own explicit "prefer bare over face" rule)
    turned out, viewed directly, to be a tiny mostly-transparent
    sparkle/glint icon -- nothing like a skin texture. The real full-body
    skin atlas (1024x512, torso/ears/face combined) was sitting the whole
    time under the *recognized* `skin_color` category. Same shape for
    `char_hair`: the unrecognized `eyereflect.blp` (a 128x128 pure-white
    eye-reflection sprite) was winning purely because `"eyereflect" <
    "hair_color"` alphabetically, over the real `hair_color` hair-strand
    textures. Root cause: `candidateAllowedForType`'s bare-file handling
    was *guessing* what an unlabeled file is (assumed "the base skin
    layer") rather than just correctly excluding what a labeled file
    isn't -- the exact kind of guess this project's own "filtering is
    safer than picking" principle was supposed to avoid, made anyway, now
    disproven by direct evidence. Fixed: `filterCandidatesForType`
    (replaces the old single-pass `candidateAllowedForType` filtering)
    always prefers recognized-and-compatible candidates over bare/
    unrecognized ones, falling back to unlabeled files only when nothing
    recognized exists at all for that slot -- the one case they're still
    genuinely needed (a non-character model with no category vocabulary
    in its texture directory at all). `orderCandidatesForDefault`
    (renamed from `preferBaseLayerCandidate`) keeps exactly one remaining
    preference, now evidence-backed rather than assumed: within the
    recognized set, `skin_color` (confirmed a real full-body atlas) ranks
    above `face` (confirmed real but narrower/darker) for the two
    compositing types specifically. Verified on the real
    `bloodelffemale_hd.m2` export: the `skin` slot's default average color
    went from (0.00, 0.00, 0.00) (the transparent sparkle icon) to (0.44,
    0.27, 0.15) -- a real tan skin tone, matching the reference screenshot
    -- and `char_hair`'s default changed from a pure-white sprite to a
    real hair-strand texture.
  - **Two new regression tests** (`tests/test_cli.cpp`,
    `twoBatchesSameComboSkin` added to `test_cli_fixtures_scenes.hpp`),
    both proven to actually fail without their respective fix before being
    confirmed passing -- the dedup test by temporarily hard-disabling the
    `materialByKey` lookup (2 materials instead of the expected 1), the
    bare-vs-category test by temporarily merging the recognized/fallback
    tiers back together (the bare file leaked back into
    `alternate_textures`). Full suite green throughout, 522/522
    (`./build/husk-tests`).
  - **Genuinely still open, found while investigating, not fixed**:
    `bloodelffemale_hd.m2`'s three real (`textureType == 0`)
    FileDataID-based slots (`3536810`/`4530998`/`5210137`) have no local
    file at all in the real export directory used this session, under
    either their exact FileDataID name or the model's own basename
    convention -- they fall back to the same ambiguous pool as the
    hardcoded slots and land on the fallback tier's pick for a different
    reason than before (genuinely missing local data, not a resolution
    bug). Whether that's specific to this local export or a real, wider
    gap is unconfirmed -- flagged for whoever picks this up next, not
    guessed at. See `EYES_ON_FINDINGS.md`'s newest addendum for the full
    detail.

- **Last state (prior, same session)**: reported
  from Blender after inspecting the fix's own output: "we are still
  getting in blender 'image_<number>' texture names instead of the
  actually useful bloodelffemale_hd_hair_color_5196731 that we get from
  the blp". Root cause: none of `gltf_mesh.cpp`'s three image-embedding
  call sites (`emitMaterial`'s primary `baseColorImagePng`,
  `additionalTextureLayers`, `alternateTextureCandidates`) ever set
  `tinygltf::Image::name`/`Texture::name` — Blender's glTF importer
  auto-generates `Image_<N>` for any unnamed image, which is exactly
  what was showing up.
  - **`src/gltf_mesh.hpp`**: new `Material::baseColorImageName` field —
    the real source filename (no extension) that supplied
    `baseColorImagePng`, purely cosmetic (doesn't affect which texture
    `baseColorTexture.index` points at).
  - **`src/export_materials.cpp`**: populated at all four
    `gm.baseColorImagePng = ...` sites — the M2-embedded-filename match
    (`embeddedStem`), the FileDataID-exact match (`std::to_string(fdid)`),
    the sole fuzzy match (`fuzzy->stem()`), and the chosen candidate out
    of an ambiguous pool (`chosen.filename`'s stem).
  - **`src/gltf_mesh.cpp`**: all three embedding sites now set
    `img.name`/`tex.name` — the primary texture from
    `mat.baseColorImageName`, `additionalTextureLayers` from
    `layer.fileDataId` (no filename tracked there, FileDataID is still
    better than nothing), and `alternateTextureCandidates` from each
    candidate's own `cand.filename` stem (needed `#include <filesystem>`,
    not previously included in this file).
  - **Verified two ways**: a new unit assertion
    (`tests/test_gltf_mesh.cpp`, the existing "baseColorImagePng is
    embedded" test plus the `additionalTextureLayers` test) checking
    `model.images[...].name`/`model.textures[...].name` directly, and a
    real headless-Blender import of the actual fixed
    `bloodelffemale_hd.m2` export (`bpy.ops.import_scene.gltf` +
    `bpy.data.images`) — 99 images, every one previously `Image_0`..
    `Image_98`, all now real names (`bloodelffemale_hd_3255415`,
    `..._eye_color_3608322`, etc.), 0 generic names left. Also added a
    same-check assertion to the two-hardcoded-slots CLI test from the
    entry below (each `alternate_textures` candidate's own embedded
    image now asserted named after its own filename stem, not just
    listed in the extras). Full suite green throughout, 520/520 (no new
    test cases, existing ones gained assertions).

- **Last state (prior, same session)**: Fixed `EYES_ON_FINDINGS.md`'s ambiguous-texture
  cross-contamination gap (finding #3's later addendum + finding #6's own
  "not yet fixed" follow-up), asked for directly with a concrete example:
  "we need to be able to map that a face (ex
  `bloodelffemale_hd_face_3500113.blp`) does *not* map to shoes mesh
  (mesh material name
  `batch36_mat5_tex2_skin_bloodelffemale_hd_3255415`)". Investigated
  before writing any code: `husk info` against the real
  `bloodelffemale_hd.m2` showed only *one* `M2Texture::type == 1` (skin)
  slot exists in the whole model — face and shoes triangles share the
  exact same M2 texture slot, sampling different UV regions of what the
  real WoW client composites at runtime from several separate layers
  (base skin tone + face + others). Traced through
  `reference/wow.export/src/js/modules/tab_characters.js` (per Luna's own
  pointer to use it as a non-authoritative reference) to confirm this:
  `apply_skinned_model_textures` explicitly composites types 1
  (`SKIN_TEXTURE_TYPE`) and 8 (`SKIN_EXTRA_TEXTURE_TYPE`) from multiple
  blended layers, binding every *other* replaceable type (hair, eyes,
  jewelry, blindfold) to one single raw file instead — and its own
  `option_map`/comments name the exact category vocabulary
  (`skin`/`face`/`hair color`/`hair style`/`facial`, `"blindfold = type
  9"`) that a real CASC-export directory's own filenames already carry
  (confirmed directly against `/media/luna/data/wow_export`, a full real
  export Luna pointed at mid-session: files like
  `bloodelffemale_hd_skin_color_3500123.blp`,
  `..._jewelry_color_3613861.blp`, `..._eye_color_3608330.blp`,
  `..._blindfold_7758264.blp` all follow this exact pattern). This
  reframed the task: husk can never *pick* the one correct composited
  layer (no DB2 blend-order data, by design, `DESIGN.md`'s Non-goals) but
  it *can* stop offering a hair-color file to an eyes slot, or a jewelry
  file to a skin slot, since those exclusions are grounded in real,
  already-parsed M2 data (`M2Texture::type`) plus real filename metadata,
  not a guess.
  - **`src/export_materials.cpp`**: new `classifyCandidateCategory`
    (parses the real category token out of a candidate's filename, empty
    for a bare `<model>_<FileDataID>` file), `candidateCategoryTypes` (the
    token → compatible-`M2Texture::type` map, transcribed from
    `tab_characters.js` as above), `candidateAllowedForType` (the actual
    per-slot filter — an unrecognized token stays a wildcard, unchanged
    old behavior for non-character models), `poolHasRecognizedCategory` +
    `bareMeansSkinOnly` (a bare file only gets restricted to skin/
    skin_extra when the model's own pool proves it's a real character-
    customization directory, so a simple equipment model's one plainly-
    named texture still resolves as before), and
    `preferBaseLayerCandidate` (within the two compositing types, orders
    a bare/`skin_color` file ahead of a narrower `face` overlay when
    picking the wired default — a full-body base tone is a far more
    plausible stand-in than a small face-only overlay, even though
    neither is the real composited answer). `claimSoleFuzzyTextureCandidate`
    now type-scopes its "exactly one candidate" check too, not just the
    ambiguous (2+) branch.
  - **`src/gltf_mesh.hpp`/`gltf_mesh.cpp`**: `AlternateTextureCandidate`
    gained a `category` field, populated from the same classification and
    emitted as `alternate_textures[].category` in the glTF extras — so a
    human or Blender script browsing the unlinked candidates can see what
    each one actually is (Luna's own "1 texture as default and rest ...
    as unlinked texture nodes" framing needs exactly this kind of label
    to be useful; the actual node-graph construction is Blender-side
    tooling this repo doesn't have yet, out of scope for `husk export`
    itself).
  - **Verified against real data**, not just synthetic fixtures: built
    husk, ran `husk export` against the real
    `/media/luna/data/wow_export` `bloodelffemale_hd.m2` + its real
    texture directory before and after. Before: every ambiguous
    material's default collapsed to the same one file regardless of
    slot type (the finding #6 bug), and slots got offered wildly
    unrelated categories (a `char_jewelry` slot's candidate list
    included face/hair/blindfold files). After: `skin` slot's pool 94 →
    57 (skin/skin_extra-only), `char_eyes` → its own 9 `eye_color_*`
    files, `char_jewelry` → its own 19 `jewelry_color`/`body_jewelry`/
    `bracelets` files, `ui_skin`(blindfold) → its own 2 `blindfold_*`
    files — zero cross-category leakage in any of them, confirmed by
    grepping the real export's own diagnostic warnings. Full suite green
    throughout, 519 → 520 (`./build/husk-tests`).
  - **New regression test, proven to actually catch the bug**:
    `tests/test_cli.cpp` (`twoHardcodedTexturedModel` fixture added to
    `test_cli_fixtures_scenes.hpp`) builds a synthetic model with two
    hardcoded slots of genuinely different `M2Texture::type`s (skin=1,
    char_jewelry=20) sharing one candidate pool, and asserts each
    material's `alternate_textures` only ever contains its own type's
    candidates. Before trusting it, temporarily hard-disabled
    `candidateAllowedForType` (`return true` first line) and reran just
    this test: failed exactly as predicted (`alt.ArrayLen() == 4` instead
    of `2` on both materials, real cross-contamination reproduced), then
    restored the real filter and reconfirmed green — the same "prove a
    regression test actually regresses" discipline this project's history
    already uses elsewhere.
  - **What's still genuinely open, not fixed and not fixable without more
    data**: within the skin/skin_extra compositing types specifically,
    *which* `skin_color` file and *whether* `face` should be layered in
    for a given character's actual customization choices remains
    unknowable without real `ChrModelTextureLayer`/DB2 data — this
    session narrows "which candidates are even offered" to a
    structurally-grounded set, it does not and cannot produce the one
    correct composited look. Documented as still-open directly in
    `EYES_ON_FINDINGS.md`'s updated finding #3/#6, not left implicit.

- **Last state (prior session)**: Fixed the "upside down" M2→glTF export bug — real code,
  tested, shipped this session, not the reverted one-line patch a prior
  session left off at. Requested directly, after that prior session's
  `BLENDER_EXPORT_TODO.md` §8 finding: not a quick patch, but "a more
  robust system that can test the correctness of the mesh regardless of
  the rotation... if the code has a plethora of hardcoded signals, that is
  prone to break the instant we get a model in an unexpected
  orientation... research and explore how to fix this permanently, so if
  Blizzard changes what their models' up means, it will not be this
  rework again." Wrote `TRANSFORM_TRIAGE.md` first (a full root-cause /
  process-failure / durable-fix investigation, no code touched) — Luna
  pushed back hard on two parts of the first draft before anything was
  built, both real corrections: (1) `reference/wow.export` was drafted as
  a "standing cross-validation check" — corrected to "flaky,
  non-authoritative, corroborating signal only, never a gate," per her own
  "don't assume wow.export is correct... it works *somewhat*"; (2) the
  proposed semantic ground-truth check ("head bone above foot bone") was
  drafted as the primary orientation invariant — corrected after her
  direct "weapons? Other meshes with skeletons? ... weapon orientations
  are not necessarily up as the correct axis" into an asset-agnostic
  synthetic coordinate-frame probe (a fabricated skeleton, not a real
  model, tested for round-trip-identity through a real headless-Blender
  import) as the primary check, with the humanoid-landmark idea demoted to
  an explicitly optional, non-load-bearing secondary signal. Both
  corrections are recorded inline in `TRANSFORM_TRIAGE.md` itself, not
  just in this log.
  - Luna then answered all four of the document's own open questions
    directly and gave explicit go-ahead to implement autonomously: "yes,
    you build while i nap" (tests before the formula fix, per the
    document's own recommended sequencing); "part of this" (fold the
    single-matrix refactor into the same change, don't scope it
    separately); "worth adding, but not critical" (the humanoid-landmark
    secondary check); "no preferences, any will do" (a quadruped fixture).
    Closed with an explicit scope boundary for what she'd verify herself:
    "start implementing, and after all of it is tested and implemented i
    will verify... until then you'll have to rely on headless Blender" —
    everything below was built and verified exactly within that boundary,
    nothing claimed beyond it.
  - **`src/gltf.hpp`/`gltf.cpp`**: the historical three independently
    hand-typed conversion functions (`zUpToYUp`, and `cmd_export.cpp`'s
    separate `toGltf(m2::Quat)`/`toGltfScale`) are now one mechanically-
    derived system — a private `Mat3` plus one matrix (`kWowToGltf`,
    corrected from `(x,-z,y)` to `(x,z,-y)`), with `zUpToYUp`/
    `rotationZUpToYUp`/`scaleZUpToYUp` all derived from it (position/
    normal: direct application; rotation: quaternion → matrix → conjugate
    by the matrix → quaternion; scale: the matrix's permutation, signs
    dropped). A `static_assert` on the matrix's determinant enforces "must
    be a proper rotation" at compile time. `cmd_export.cpp`'s own
    `toGltf(m2::Quat)`/`toGltfScale` are now thin wrappers, not separate
    formulas — the exact fix for the root cause `TRANSFORM_TRIAGE.md`
    traced this bug to (rotation/scale were hand-derived *from* the old,
    wrong position formula, on paper, then never independently
    re-verified against anything real).
  - **The corrected formula is now corroborated three independent ways**,
    not just the prior session's single headless-Blender empirical test:
    the hand-derived change-of-basis math (already existing), the
    headless-Blender round-trip (already existing, re-confirmed), and —
    new this session — `reference/wow.export` (already checked out in this
    repo, never previously mined for this), which has its own,
    independently-written coordinate-conversion code for position,
    normal, rotation, *and* scale, matching the corrected formula exactly
    on every one (scale needed zero code changes — it was already correct,
    informative about the bug's own shape: a sign error, not a wrong axis
    pairing, since scale is sign-insensitive).
  - **New tests, and each proven to actually catch the bug, not just
    proven to pass** — the same "prove a regression test actually
    regresses" discipline this project's history already uses elsewhere:
    a synthetic, asset-agnostic coordinate-frame probe
    (`tests/test_conformance.cpp`, a fabricated `gltf::Skeleton` with no
    dependency on any real M2 file) asserts local X/Y/Z offsets survive a
    real husk-export → Blender-import round trip as the *identical*
    coordinate — before trusting it, `kWowToGltf` was temporarily reverted
    to the historical formula and rerun: it failed exactly as the
    root-cause math predicts (`+X`, the rotation's own invariant axis,
    still correct; `+Y`/`+Z` both flipped), then the fix was restored and
    reverified green. A property-based unit test
    (`tests/test_gltf.cpp`) independently confirms `rotationZUpToYUp`'s
    own matrix-conjugation implementation is self-consistent for several
    real test rotations and probe vectors, regardless of which underlying
    matrix is used — catches a bug in the conversion *machinery*, not in
    which matrix is chosen. (One real false alarm this same test caught in
    itself, before either mattered: a hand-typed "arbitrary rotation" test
    quaternion wasn't quite unit-length, silently violating
    `quatToMat3`'s implicit unit-quaternion assumption and producing a
    small, confusing failure that looked like a real bug — fixed by
    normalizing every test quaternion at test time rather than trusting a
    literal's precision.) A second, explicitly non-load-bearing check
    confirms a real humanoid landmark bone (`_Name`, keyBoneId 22) lands
    above the armature origin on the real `bloodelffemale.m2` fixture —
    caught one more real bug in its own first draft before shipping:
    Blender is natively Z-up, not Y-up, so the check's first version
    compared the wrong raw component (`.y` instead of `.z`) and would have
    silently asserted the wrong thing; caught because the real fixture's
    own landmark prints as `(0, 0, 2.05)`, obviously wrong against a
    `.y > 0` check and obviously right against `.z > 0`.
  - **A real quadruped fixture** (`test_data/creature/wolf/wolf.m2`,
    gitignored, same personal-extraction convention as every other
    `test_data/` fixture — 66 bones, 557 vertices, pulled from the local
    corpus per Luna's own "no preferences, any will do") plus
    `HUSK_TEST_QUADRUPED_M2`/`_SKIN` wiring
    (`tests/test_data_paths.hpp`, `test_main.cpp`'s banner) and two new
    `test_conformance.cpp` cases (gltf_validator zero-errors,
    headless-Blender bone/vertex-count agreement) — explicitly *not*
    additional orientation coverage (the synthetic probe already covers
    any asset type by construction), but real pipeline coverage for a
    body-plan/bone-hierarchy shape `bloodelffemale.m2` doesn't represent.
  - **Full suite green with zero hand-updated literals**: every existing
    test touching a position/rotation/scale value passed unmodified
    against the corrected formula the moment it was flipped — 335 →
    484/484 (+1 permanently-inapplicable skip) via `./build/husk-tests`,
    485/485 via `ctest`. Nothing needed updating, which is itself a real
    signal: no other test in this codebase was silently depending on the
    old formula's specific wrong values.
  - **Docs**: `TRANSFORM_TRIAGE.md` itself updated throughout with
    "Implemented" notes per subsection (not deleted — unlike this
    project's usual fully-closed-TODO lifecycle, one real item is
    deliberately still open, see below, so the file stays as the living
    record for it). `DESIGN.md` (the rotation/scale Key design decision
    corrected to describe the current mechanically-derived implementation
    rather than the stale hand-derived-formula description; a new,
    detailed Follow-up entry after the original upside-down finding).
    `README.md` (the roadmap stage 1 paragraph's literal formula citation
    corrected — it still quoted the wrong, pre-fix formula as current
    fact; the Testing section's Conformance-tier paragraph extended with
    the new probe/landmark/quadruped coverage).
  - **What's deliberately still open, not an oversight**: a real animated
    clip, visually confirmed by Luna in Blender's actual GUI viewport —
    every check this session added is numeric (headless probes, a
    property test, a JS/C++ diff); nothing here substitutes for that last
    look, and it was never meant to be automated away. `TRANSFORM_TRIAGE.md`
    §7/§8 both say this explicitly. Whoever picks this up next — likely
    Luna herself — should start there.
- **Previous state**: Implemented all four items in `RO_COMPLETENESS_TODO.md`
  (a punch list Luna wrote grounding four of README's own 🚧-marked
  format-matrix rows against current source, then handed off with "shouldn't
  be a big task") — every item done, tested, documented, and the TODO file
  itself deleted per this project's own "survey's job is done" lifecycle.
  Worked in the file's own priority order.
  - **Item 2 (header metadata)**: `global_flags` now decodes into every
    wiki-named bit (`m2::globalFlagNames`, `src/m2.hpp`/`m2.cpp`'s new
    `GlobalFlag` namespace — bit positions derived by counting the wiki's
    own reserved `uint32_t : 1` slots, not guessed from hex comments),
    printed by `husk info` alongside the existing raw hex. Two real-file
    cross-checks the item's own plan asked for, both confirmed: (1)
    `flag_load_phys_data` correctly tracks real `.phys` presence —
    set on `mace_1h_warfrontsforsaken_d_01.m2` (has a committed `.phys`
    sidecar), unset on `bloodelffemale.m2` (doesn't); (2) whether
    `flag_new_particle_record` is a reliable proxy for the 492-byte
    `M2Particle` shape, or whether `kMinVerifiedParticleVersion`'s
    version-only gate could disagree with it on a real file — it can:
    `mace_2h_bolvar_d_01.m2` (version 274, the 64-particle-emitter stress
    fixture) does *not* set the flag, confirming the wiki's own text is an
    OR ("if 0x200 is set **or** if version is bigger than 271") and
    `kMinVerifiedParticleVersion`'s existing version-only check was already
    the correct half of that OR, not a bug. `Header::textureCombinerCombos`
    (the header struct's own last field, `M2Array<uint16_t>` at offset
    0x130, only present when its flag bit is set) is now parsed and
    surfaced via `husk info` too — a full 130,576-file local-corpus scan
    found zero real files with the flag set, so this one specific table's
    real-file layout is unverified even though the parse itself is
    low-risk (same well-tested `parseUint16Array` five other lookup tables
    already share). The wiki's own "use this instead of index+1 for
    multitexture blending" cross-reference into `cmd_export.cpp`'s
    material resolution was deliberately **not** wired up — no indexing
    key documented at all, and (per the scan) no real file to verify a
    guess against either.
  - **Item 3 (`WFV1`/`WFV2`/`DPIV`/`AFRA`)**: all four — no wowdev.wiki
    struct at all — now get real structural parsing in `husk dump-chunks`
    (`dumpWfv1`/`dumpWfv2`/`dumpDpiv`/`dumpAfra`, `src/cmd_dump.cpp`) built
    from the real corpus files already sitting in this repo's root
    (`*_files_for_exploration.txt`, from an earlier session's corrected
    scanner-bug finding, `WIKI_FINDINGS.md` §10). `AFRA`/`WFV1`: a single
    fixed 16-byte struct (one real float32 + 12 zero bytes). `DPIV`: the
    wiki's own "always 32 bytes" undersold it — chunk size is *always* an
    exact multiple of 32 (1-4 records seen across 2,632 real hits), a real
    record array (`chunk.size / 32` records, 8x float32 each), not a
    single fixed struct; the last 4 floats are zero in every one of 2,951
    real records decoded, kept as real fields rather than assumed
    reserved. `WFV1`/`WFV2` are a genuinely thin, 2-file,
    byte-identical-content sample (both the same Nazjatar-zone waterfall
    doodads) — flagged tentative rather than confidently typed field-by-
    field (two `WFV2` fields show signs of not really being floats — a
    plausible packed-RGBA-color byte pattern, and a small-integer-as-float
    denormal `DPIV`'s own field_3 also shows — exposed as plain floats
    rather than guessing a reinterpretation from so thin a sample).
    `kFallback`'s raw-hex-dump path (`dumpRawFallback`) was removed
    outright once nothing used it anymore, and its stale notes ("AFRA...
    not observed in any files yet") — already known-wrong since an earlier
    session's scanner-bug correction, just never updated in this specific
    file — went with it.
  - **Item 1 (`blp/` DXT3/JPEG)**: a real corpus scan, not the small
    open question the item's own plan expected to resolve cheaply — this
    ran **779,056** real `.blp` files (not `.m2`-scoped, the biggest and
    single longest-running corpus check this project has done, ~2h55m
    wall-clock, almost entirely disk I/O opening three-quarters of a
    million individual small files one at a time). Result: **DXT3 is real
    and needed** (6,759 real files — character hair/skin textures among
    them), **JPEG is genuinely absent** (0 real files, recorded as a real
    negative result per this project's own "checked, zero real files, not
    implemented blind" discipline, not attempted). The real surprise:
    DXT3 needed **no new decode code at all** — `blp/src/husk_blp/
    decode.py`'s `_decode_dxt`/`_DXT_BLOCK_SIZE`/`_DXT_FOURCC` were
    already generic over `PixelFormat.DXT1`/`DXT3`/`DXT5`, wired through
    the exact same synthetic-DDS-wrapper path DXT1/DXT5 use — it had
    simply never been exercised by a real test or verified against a real
    file, so `README.md`'s own "DXT3... unimplemented" claim was stale
    documentation, not a missing feature. Verified two ways before
    trusting that: a new synthetic single-block test
    (`test_decode_dxt3_solid_green_explicit_alpha_block`, `blp/tests/
    test_decode.py`, same shape as the existing DXT1/DXT5 single-block
    tests) round-trips exactly; a real file
    (`character/troll/hair00_01.blp`, 128×128) decodes to a visibly
    correct troll-hair texture (red strands + braid, 2,333 unique
    colors) — not a crash, not garbage.
  - **Item 4 (Sidecar FileDataID resolution)**: `README.md`'s format-
    matrix row bumped 🚧 → 📖 (the CASC-resolution half this row measures
    against is a deliberate non-goal, not a deferred read — local-file
    resolution, the row's actual full scope, is already complete for all
    six IDs). The one real diagnostics gap found: `resolveSkin`
    (`--skin auto`'s SFID-based resolution stage, `src/cmd_export.cpp`)
    used to report only the *directory* it searched on a "not found"
    failure, not the specific `<FileDataID>.skin` path it actually
    checked — a direct miss against this project's own Foreign Data
    policy ("on failure, always print expected and actual values"). Now
    names the exact candidate path; three existing `tests/test_cli.cpp`
    cases whose assertions depended on the old, vaguer wording were
    updated to check for the specific path instead. Checked the sibling
    resolvers the item's own plan named alongside it
    (`--anim`/`--bones-dir`/`--textures`) and found they don't share the
    gap: all three are deliberately silent-skip-per-item by design
    (matching `--textures`'s already-established "quiet when nothing
    applies" precedent), with no "not found" failure message to improve
    in the first place — the gap was real but narrower than the item's
    own framing suggested.
  - Also bumped 🚧 → 📖 for the "Chunk container / magic detection" and
    "Header / global metadata" format-matrix rows (Item 3's/Item 2's own
    work, respectively, directly closes the gap those symbols described).
  - **Verification discipline**: every claim above was checked against
    real bytes before being written down or shipped — the two real-file
    header-flag cross-checks, the 130,576-file `textureCombinerCombos`
    scan, the 779,056-file BLP scan, and the real troll-hair-texture
    decode all happened *before* the corresponding doc text or code
    change was finalized, not after. Full suite green throughout: 471/471
    `./build/husk-tests` (1 permanently-inapplicable skip), 472/472
    `ctest`, 17/17 `blp/`'s own pytest suite (3 pre-existing, unrelated
    env-var-gated skips).
  - **Docs**: `WIKI_FINDINGS.md` (§10 gained a "Follow-up: implemented"
    subsection for `WFV1`/`WFV2`/`DPIV`/`AFRA`; new §14 for the
    `global_flags`/`textureCombinerCombos`/BLP-scan/`resolveSkin`
    findings; "Where these live in husk" table extended two rows),
    `DESIGN.md` (three new Key design decisions bullets — `WFV1`/`WFV2`/
    `DPIV`/`AFRA` parsing, `global_flags`/`textureCombinerCombos`,
    `resolveSkin` diagnostics — plus a fourth for the DXT3 finding),
    `M2_COMPLETENESS.md` (new `WFV1`/`WFV2`/`DPIV`/`AFRA` row, Header row
    updated), `README.md` (three format-matrix symbol bumps, the `blp/`
    usage paragraph rewritten for DXT3, the BLP `Texture pixel data` row
    rewritten). `RO_COMPLETENESS_TODO.md` deleted outright, same lifecycle
    every prior fully-closed TODO file in this project has used — its five
    remaining code/test cross-references (`src/cmd_dump.cpp`,
    `tests/test_cli.cpp`, `tests/test_dump.cpp` x2,
    `blp/tests/test_decode.py`) were already phrased as `former Item N`
    historical citations before the deletion, so none needed rewriting
    (same "historical log entries aren't rewritten" precedent every prior
    TODO-file deletion here has used).
  - **A real, unrelated observation, not acted on**: partway through this
    session's long-running BLP scan, ten new untracked files appeared in
    the work dir that this session didn't create —
    `TODO/WORLD/ADT_LOD_TODO.md`/`TODO/WORLD/ADT_TERRAIN_TODO.md`/`TODO/WORLD/COLLISION_CULLING_TODO.md`/
    `TODO/ENGINE_TODO.md`/`TODO/WORLD/FOG_VOLUMES_TODO.md`/`TODO/WORLD/LIGHTING_TODO.md`/
    `TODO/WORLD/LIQUID_TODO.md`/`LUNA_NOTES.md`/`TODO/WORLD/WDT_TODO.md`/`TODO/WORLD/WMO_GEOMETRY_TODO.md`/
    `WORLD_COMPLETENESS.md`/`TODO/WORLD/WORLD_PLACEMENT_TODO.md` (plus a
    `README.md` intro-paragraph edit pointing at the new
    `WORLD_COMPLETENESS.md`) — evidently Luna's own concurrent work in a
    separate session, scaffolding a WMO/ADT/world-geometry expansion,
    landing while this session's background scan ran for several hours.
    Confirmed via `git status`/`git diff` that none of it conflicts with
    or was touched by this session's own edits (the one shared file,
    `README.md`, had her intro-paragraph addition and this session's
    format-matrix/`blp/`-paragraph edits land in disjoint sections,
    cleanly coexisting) — left entirely alone, per this project's own
    "Luna-created content, not mine to touch" rule, including five
    "same disposition `RO_COMPLETENESS_TODO.md`... already established"
    -style precedent citations inside her new files that now point at a
    file this session deleted (`TODO/WORLD/LIQUID_TODO.md`/`TODO/WORLD/WDT_TODO.md`/
    `TODO/WORLD/ADT_TERRAIN_TODO.md`/`TODO/WORLD/WMO_GEOMETRY_TODO.md`/`TODO/WORLD/ADT_LOD_TODO.md`) —
    flagged here rather than silently fixed, since they're her files, not
    read closely enough to know if she'd even want them touched.
  - **Environment note, reconfirmed**: the BLP scan needed `time direnv
    exec . uv run --python tools/venv/bin/python <script>`, backgrounded
    (it exceeded the default 120s tool timeout almost immediately and
    took ~2h55m total) — checked on via `/proc/<pid>/fd` (which real file
    it currently had open) rather than polling its own stdout, since the
    script only prints once at the very end; a `Monitor` task
    (`while kill -0 <pid>; do sleep ...; done`) was used for the final
    long stretch so a task-completion notification would arrive instead
    of manual re-checking. `uv run --python .venv/bin/python <script>`
    (not `tools/venv/bin/python`) is `blp/`'s own venv path, needed for
    the two ad hoc real-file verification scripts this session wrote
    (checking Pillow's own decode against a real DXT3 file, saving a PNG
    to eyeball) — `blp/`'s Python package and the top-level `tools/`
    scripts each have their own separate venv, confirmed by `-c` failing
    against the wrong one with an unrelated import error before catching
    it.
- **Previous state**: Closed out the remaining `M2_GAPS_TODO.md` work
  autonomously (Luna: "start implementing the changes independently
  starting from the easiest... continuing to the harder ones," then went
  offline) — two units of work, each committed separately.
  - **Items 9/10 (real-data regression tests for the previous session's
    EXP2/PFDC/BLP2 findings)**: wired the three already-pulled real fixtures
    (`test_data/verification/exp2_126382.m2`/`pfdc_1003471.m2`/
    `blp2_7507381.m2`) into `tests/test_data_paths.hpp`, then wrote real
    `doctest::skip()`-gated `TEST_CASE`s — exact field assertions
    (re-derived fresh from a live `husk dump-chunks` run, not copied from
    the TODO's own orientation numbers) for both EXP2-only and EXP2+PFDC
    fixtures in `tests/test_dump.cpp`, plus three `BLP2`-anomaly
    throws-cleanly cases (`info`/`export`/`dump-chunks`) in
    `tests/test_integration.cpp` (not `test_cli.cpp` as the TODO's own plan
    suggested — that file's own header comment explicitly states none of
    its cases need real fixtures, so the real-fixture-shaped test belongs
    in `test_integration.cpp` instead, which already has the
    `test_data_paths.hpp`/`doctest::skip` infrastructure for exactly this).
    456 → 460 test cases, both items removed from `M2_GAPS_TODO.md` per
    this project's TODO lifecycle, permanent record folded into
    `M2_COMPLETENESS.md`/`WIKI_FINDINGS.md` §13. Committed separately
    (`84a16d9`) before starting Item 4, so a rate-limit or interruption
    mid-PCOL-work wouldn't have put the already-finished Items 9/10 work at
    risk.
  - **Item 4 (`PCOL`, player-housing collision, War Within 11.1.7+) — the
    last remaining item, now implemented.** The wiki gives a full,
    byte-accountable struct (four independent `(count, offset)` regions:
    `vertexPositions`/`faceNormals`/`indices`/`flags`) but flags it
    "preliminary" — verification against real bytes came first, not
    guessed at. `pcol_files_for_exploration.txt` (already sitting in the
    repo root from the previous session's investigation, 2,354 real
    paths) fed a new from-scratch Python decoder
    (independent of husk's own C++ parser, same discipline every prior
    corpus check here uses): **all 2,354 real files decode with every
    region fully in-bounds, zero exceptions** — plus two facts the wiki
    doesn't state: `indexCount == faceNormCount * 3` on all 2,354 (each
    `faceNormal` is a per-triangle normal, the same shape M2's own core
    `collisionFaceNormals` already has — `indices` are triangle triples),
    and every decoded index is in range for that same file's own
    `vertexPosCount` (zero out-of-range references). The wiki's own
    warning — "there can be extra bytes between the data, use the
    offsets" — is real, not defensive boilerplate: a real file
    (`pa_kite_lamp_creature.m2`) has an 8-byte gap between `faceNormals`'
    own end and `indices`' own offset, so the implementation reads each
    region via its own offset field, never accumulated sequentially the
    way `.phys`'s `PLYT` header+data walk is.
    - **Design call made autonomously, not escalated**: diagnostic-only
      (`husk dump-chunks`), no glTF slot — same class as `EXP2`/`PFDC`/
      `DETL` (the TODO's own docs note hedged this: "likely n/a glTF-
      ceiling... unless a real file surfaces and a translation... makes
      sense"). Real files do exist and the shape is genuinely translatable
      (position/index/normal triangles, structurally identical to how M2's
      own core collision mesh already gets a real glTF translation) — but
      `PCOL` is niche (War Within 11.1.7+ player-housing furniture only,
      2,354/130,576 files) sidecar-shaped data, not core render geometry,
      matching every sibling item in this same TODO file (`EXP2`/`PFDC`/
      `DETL` all shipped diagnostic-only despite being translatable in
      principle too) — picked the conservative, precedent-consistent
      option rather than introduce a new mesh into `.glb` output
      unprompted.
    - Implemented as `dumpPcol` (`src/cmd_dump.cpp`), moved from
      `kFallback` to `kDocumented`. Ran husk's own compiled binary against
      all 2,354 real files directly (not just the Python decoder): zero
      exceptions. New `tests/test_dump.cpp` cases: a synthetic fixture with
      deliberately non-contiguous regions (proving the offset-based read,
      not a PLYT-style sequential accumulation) and negative int16 values
      (proving signed, not unsigned, reads for `indices`/`flags`), plus a
      real-data regression test against a newly-committed fixture
      (`test_data/verification/pcol_pa_kite_lamp_creature.m2`, chosen for
      its small size — 2,016-byte chunk, 40 vertices/74 triangles — while
      still real). 460 → 462 test cases, both `./build/husk-tests`
      (462/462 + 1 permanently-inapplicable skip) and `ctest` (463/463)
      green.
    - **`M2_GAPS_TODO.md` deleted outright** once Item 4 (its last item)
      closed — same "survey's job is done" lifecycle every prior TODO file
      here has used. Permanent record: `M2_COMPLETENESS.md`'s Collision &
      physics section, `WIKI_FINDINGS.md` §10's new Follow-up subsection,
      `DESIGN.md`'s Key design decisions (new `PCOL` bullet) and Open work
      section (rewritten now that the file is gone), `README.md` (Usage
      section's `dump-chunks` paragraph, Collision/physics format-matrix
      row).
    - **Full cross-reference sweep**: grep-verified every one of the
      ~50 `M2_GAPS_TODO.md`/`M2_GAPS_TODO` mentions across `src/`/`tests/`/
      `tools/`/docs. Left ones already phrased as historical narrative
      alone (`...'s former Item N`, `Follow-up (...'s item N, now closed)`
      — same "historical log entries aren't rewritten" precedent every
      prior TODO-file deletion here has used) but fixed every mention that
      read as a live pointer to a file that no longer exists (bare
      `M2_GAPS_TODO(.md) Item N` citations in `tests/test_dump.cpp`,
      `tests/test_gltf.cpp`, `tests/test_m2.cpp`, `tests/test_integration.cpp`,
      `src/cmd_export.cpp`, `src/gltf.hpp`, `src/cmd_dump.cpp`,
      `tools/find_m2_unknown_chunks.py`) — same discipline the
      `CORPUS_TODO.md`/`MULTIROOT_SKELETON_TODO.md` deletions already
      established, applied at real scale here (many more live references
      than either of those had, since this TODO file bundled 10 independent
      items across several sessions).
  - **Environment note, reconfirmed, no repeat of a prior mistake**: one
    stray bare `python3 -c ""` (immediately followed by the correct
    `direnv exec . uv run --python tools/venv/bin/python <script>` form) —
    it errored harmlessly (no global Python, same guard as always) and
    nothing was built on its output; still worth noting since a previous
    session's whole correction was specifically about this exact mistake.
    `uv run --python tools/venv/bin/python -c "..."` (inline `-c`, as
    opposed to a script file) does **not** work — `uv run` doesn't accept
    `-c` as a passthrough flag to the interpreter the way bare `python3`
    does (`error: unexpected argument '-c' found`) — write ad hoc checks to
    a scratchpad file and pass the file path instead, confirmed working
    throughout this session.
- **Previous state**: Explored 4 untracked casc-tool scan outputs Luna dropped in
  the work dir (`m2_chunk_discovery.csv`/`.log`, `m3_corpus_scan.csv`/`.log`
  — a separate thread's full-corpus scans against a live CASC install,
  product `wow` build 68887) and turned them into real doc corrections, per
  Luna's own explicit "explore results and summarize into a coherent action
  plan" request. Found and verified three things, none previously known:
  - **`EXP2`/`PFDC`'s "zero real files" claim was a local-extraction gap,
    not a real absence.** `M2_COMPLETENESS.md`/`src/m2.hpp`/`cmd_dump.cpp`
    all previously stated husk's local corpus (`/media/luna/data/
    wow_export`) has zero real files for either tag — both parsers were
    implemented from the wiki struct alone, unverified. The new
    live-CASC chunk census (all 130,576 real `.m2` files, 31 distinct
    tags — broader than the earlier 5-tag `--watch` cross-check `WIKI_
    FINDINGS.md` §10 already used) found **17,065** real `EXP2` files and
    **2,430** real `PFDC` files — too big a gap to be the ~1% extraction
    slack §10's `PCOL`/`DPIV` case already accounted for. Confirmed by
    directly pulling two real files via `casc-tool extract` (storage
    `/media/luna/games/World of Warcraft`, requested from Luna
    mid-session): both parse cleanly through husk's existing, unmodified
    code — one shows a real monotonic 3-keyframe `EXP2` `alphaCutoff`
    curve, the other a real version-6/`phyt`-3 `PFDC` body record
    matching `WIKI_FINDINGS.md` §9's already-verified `.phys` shape. No
    parser changes needed, only the stale "unverified"/"zero files"
    claims — corrected in `M2_COMPLETENESS.md`, `src/m2.hpp`, `src/
    cmd_dump.cpp`.
  - **A genuine anomaly (`BLP2` as a 1-byte top-level M2 chunk, 1 real
    hit) resolved as a listfile mismatch, not an M2 finding at all.**
    `husk` itself refused to open the file outright (a real
    `ParseError`, not a silent misread — the boundary discipline working
    as designed). Pulled the file directly and hex-dumped it: its actual
    content **is** a genuine BLP2 texture (real magic + compression/
    width-height/mipmap-offset header, plausible 512×256), not an M2
    file — FileDataID 7507381 isn't in this project's own listfile
    snapshot, consistent with the upstream chunk-census tool's
    `*.m2`-masked enumeration trusting a stale/wrong listfile-derived
    extension rather than sniffing content. Not a husk bug, not a real
    M2 chunk — written up and closed in one pass.
  - **8 real `.m3` files exist** (a full-storage, non-`.m2`-scoped
    `M3DT`-magic byte-signature scan, 1,891,552 files) — an entirely
    different, undocumented model format, unresolved listfile names
    (`models\unknown\unk_exp*\<fdid>.m3`). Explicitly scoped by Luna as
    "note it, stay out of scope" (not a new investigation) — recorded as
    a `DESIGN.md` Non-goals addendum only.
  - Also reconfirmed, no new information: the full 31-tag census's
    `WFV1`/`WFV2`/`DPIV`/`AFRA`/`PCOL` counts land on the *exact* same
    numbers `WIKI_FINDINGS.md` §10's earlier 5-tag `--watch` cross-check
    already found — an independent second run via a broader tool,
    converging on the same result, not new news but a stronger
    confidence signal for that section.
  - **Explicitly deferred at Luna's direction**: `PCOL` (`M2_GAPS_TODO.md`
    Item 4, the one item already fully unblocked and ready) was *not*
    implemented this session — "not yet," per her own answer when asked
    directly. Next session picking this up should start there; nothing
    else blocks it.
  - **One real process correction mid-session**: reflexively ran
    `find / -maxdepth 4 ...` looking for the CASC storage path before
    asking — correctly blocked by the sandbox/user per this file's own
    "never run commands against system root" hard rule. Stopped, asked
    Luna directly for `--storage`/`--listfile` instead of guessing
    further. Separately corrected for using a bare `python3 -c` (no
    global Python on this system) instead of this project's own
    established `direnv exec . uv run --no-project python3` pattern —
    caught immediately, no repeat; used `jq` (already on `PATH`,
    installed via Luna's own profile, not project-scoped) for the rest
    of this session's JSON inspection instead.
  - The 3 pulled real files (`exp2_126382.m2`, `pfdc_1003471.m2`,
    `blp2_7507381.m2`) were moved into `test_data/verification/` and wired
    into real regression tests in the very next session — see the newer
    Last state entry above, this note is stale as of that session.
  - The 4 untracked CSV/log files that prompted this session
    (`m2_chunk_discovery.*`, `m3_corpus_scan.*`) are still sitting
    untracked in the work dir, not cleaned up or committed — Luna's own
    artifacts from the separate casc-tool thread, hers to dispose of.
- **Previous state**: Implemented 7 of `M2_GAPS_TODO.md`'s 8 items (everything
  except Item 4, `PCOL`, blocked on real data — see below) in one session,
  via **parallel subagents** rather than sequentially — requested directly:
  "start implementing @M2_GAPS_TODO.md," then, mid-triage, "considering it's
  individual tasks, could do subagents." Grouped the 8 items into 4
  worktree-isolated agents by file-overlap (to keep merge conflicts
  tractable, not by the TODO's own priority order): Item 1 alone
  (`M2Sequence` fields + `aliasNext` chain resolution, the biggest/highest-
  value piece); Items 5+7 together (both touch material-extras plumbing);
  Item 6 alone (Attachments/Events/Lights as real glTF nodes, its own
  glTF-schema surface); Items 2+8+3 together (all three are
  `cmd_dump.cpp`-only diagnostic additions). Did the two real-corpus checks
  Items 3/4 explicitly needed *before* dispatching, not after: a fresh
  130,576-file top-level-chunk-tag scan (same scanner shape as
  `tools/find_m2_unknown_chunks.py`) found **zero** real files with either
  `EXP2` or `PCOL` — per each item's own contingency plan, Item 3 still
  shipped (simple, unambiguous struct, synthetic fixture, flagged
  unverified) while Item 4 stayed parked (a wiki-flagged "preliminary"
  struct with zero real bytes to ground it, explicitly not to be
  implemented synthetic-only) and got folded into the Items-2+8+3 agent's
  brief accordingly.
  - **All four agents hit a shared API rate limit simultaneously and were
    killed mid-work** (a genuine platform limit, not a code problem) —
    each had made real, uncommitted progress in its own worktree at the
    moment of the cutoff. Confirmed via `git status`/`git log` in every
    worktree before doing anything else: nothing was lost, nothing had
    been committed prematurely. Resumed all four via `SendMessage` from
    their own transcripts (not fresh respawns — a respawn would have
    re-derived context from scratch) with an explicit "you were cut off by
    a rate limit, not a real failure, resume exactly where you left off"
    framing; all four finished cleanly on resume.
  - **Each agent independently found a real bug or a real, non-obvious
    finding while implementing its own plan** — this project's own
    "verify against real bytes, don't trust the plan document blindly"
    discipline held up under delegation, not just under direct work:
    - Item 1's agent caught that chain-resolving *every* `flags & 0x40`
      ("alias") sequence unconditionally would have been a real
      regression — 31 of `bloodelffemale_hd.skel`'s 38 real alias
      sequences *also* carry `flags & 0x20` ("stored inline"), meaning
      they already have real keyframe data of their own; `0x20` has to
      keep winning priority exactly as it did before `aliasNext`
      resolution existed, or those 31 real clips would have silently had
      the wrong sequence's data substituted in. Caught before shipping,
      not found by a later regression test. Also found, honestly: the
      fix's *measured* effect on the committed fixture is **zero net new
      clips** (all 7 genuinely-alias sequences resolve to a terminal
      sequence needing an external `.anim` file not among the ~104
      already committed) — the original plan's own "don't assume every
      alias necessarily gains a clip" caveat held exactly.
    - Items-5+7's agent caught that `M2Color::alpha`/`M2TextureWeight
      ::weight` are `M2Track<fixed16>` (2-byte wire values), not
      `M2Track<float>` (4 bytes) — the TODO's own suggested plan said to
      reuse `resolveFloatTrackSequence` for these, which would have
      silently misread the 2-byte wire bytes as garbage 4-byte IEEE
      floats. Used `resolveRawIntTrackSequence(..., elementSize=2)`
      instead, decoding fixed16 → 0..1 the same way the existing
      constant-value path already does.
    - Items-2+8+3's agent found that `DETL`'s defensive floor needs to be
      `min(lightCount, chunk.size/12)`, not `chunk.size/12` alone — a real
      3-light file pads 36→48 bytes for 16-byte alignment, and 48/12
      happens to equal exactly 4, silently overcounting by one record if
      the floor isn't taken against the header's own `lights.count` too.
  - **Merging required real, careful conflict resolution, not blind
    `git merge`** — 4 branches all touched the shared `M2_GAPS_TODO.md`
    (each removing its own item's section) and several touched
    `M2_COMPLETENESS.md`/`DESIGN.md`/`WIKI_FINDINGS.md` (each adding its
    own row/entry) and `gltf.hpp`/`gltf.cpp`/`cmd_export.cpp` (each adding
    its own feature). Merged and rebuilt+retested after *every* branch,
    not all 4 at once, so a bad merge would be caught immediately rather
    than compounding: `git branch --merged`-verified fully clean before
    deleting. Two real hand-resolution mistakes happened and were caught
    by re-reading the file afterward, not assumed correct from the diff
    alone: (1) `test_integration.cpp`'s Items-5+7 merge conflict was
    actually two independent branches both appending a "load the exported
    glb, then assert" test case at the same location — git's diff matched
    the two tests' identical boilerplate as shared context, so the naive
    conflict markers implied *interleaving* two unrelated test bodies;
    reconstructed by hand into three separate, complete, non-overlapping
    `TEST_CASE`s (Item 5's, Item 6's already-merged one, Item 7's). (2) The
    final Item-1 merge's `M2_GAPS_TODO.md` conflict was resolved wrong on
    the first pass — kept HEAD's still-full Item 1 section body instead of
    collapsing it now that Item 1 is done, leaving a stale, already-
    obsolete section sitting in the file; caught by re-reading the merged
    file end-to-end afterward (`grep "^## Item"`) rather than trusting the
    conflict resolution had done the right thing, then fixed by trimming
    the section out and rewriting the priority-order list and its
    "here's where the finished items live" note to name all 7 finished
    items, not just the ones each individual merge happened to know about.
  - **Final state, verified via a full clean rebuild** (`rm -rf build`,
    reconfigure, rebuild, both `./build/husk-tests` and `ctest`): 335 → 455
    test cases (456/456 via `ctest`, 1 permanently-inapplicable skip),
    zero failures. All 4 worktrees/branches removed after confirming
    `git branch --merged master` covered every one of them — nothing left
    behind.
  - **Docs**: `M2_GAPS_TODO.md` now holds only Item 4 (`PCOL`), with a
    "checked: 0/130,576 real files" note added to its own Blocker section
    and a combined note naming all 7 finished items and where their
    permanent record lives (not deleted outright, since one real item is
    still genuinely open — unlike every prior fully-emptied TODO file in
    this project's history). `M2_COMPLETENESS.md` (Attachments/Events/
    Lights rows to `native — 100%`; new `M2Sequence`-metadata,
    hardcoded-texture-slot, animated-tint/fade, `DETL`, and `PFDC` rows;
    `EXP2` folded into the particle/ribbon side-chunks row; `Alias
    sequences` row corrected from "n/a, upstream-spec gap" to
    `native — 100%`), `DESIGN.md` (5 new Key design decisions, one per
    shipped feature area), `WIKI_FINDINGS.md` (§11/§12's "Where these live
    in husk" table rows filled in), `README.md` (Materials paragraph).
- **Previous state**: Ran `M2_UNKNOWNS_EXPLORATION.md`'s investigation brief to
  completion — six targets (wowdev.wiki chunk types/fields with no
  field-level struct, or an internally-inconsistent one), each given a real
  disposition grounded in real corpus bytes, not guessed at. Requested
  directly: "Start on @M2_UNKNOWNS_EXPLORATION.md." Same methodology every
  prior wiki-correction session here has used (independent from-scratch
  scanner, cross-checked against many real files, full byte-accounting
  before trusting a stride) — see `WIKI_FINDINGS.md` §10/§11/§12 for the
  full writeups.
  - **Targets 1–4 (`WFV1`/`WFV2`/`DPIV`/`AFRA`) — confirmed absent, a real
    negative result.** New `tools/find_m2_unknown_chunks.py` walked the
    full real corpus (`/media/luna/data/wow_export`, all 130,576 `.m2`
    files, one top-level-chunk-tag pass, ~30s) and found **zero** real
    files carrying any of the four tags. Sanity-checked the scanner's own
    chunk-walk logic against `test_data/bloodelffemale.m2`'s known-good
    `MD21`/`TXAC`/`AFID`/`LDV1`/`SFID`/`TXID` sequence first, so the
    corpus-wide zero isn't a scanner bug — confirmed all 130,576 files are
    `MD21`-chunked (no pre-Legion flat files in this corpus to explain the
    zero as "wrong file era" either). `WFV3` (`WFV1`/`WFV2`'s later,
    fully-documented successor) was found in exactly 9 real files
    elsewhere in this same corpus, already implemented — so the zero here
    is "this corpus's own extraction doesn't happen to have one," not
    "the format never existed." Written up as `WIKI_FINDINGS.md` §10.
    Per Luna's own explicit follow-up request ("also write the unknown
    chunks if not solvable with this data as todo list for casc-tool...
    write it here, i will personally move it to correct place"), a
    **standalone `CASC_TOOL_TODO.md`** (repo root, deliberately
    **not** committed, not referenced from any husk doc) hands this
    negative result to Luna's separate `casc-tool` project as a "worth a
    broader CASC pull across other builds/regions" lead, with the one
    concrete FileDataID the wiki names (`WFV1`, 2445860) and husk's own
    scanner script ready to point at any other corpus root if a hit ever
    turns up.
  - **Target 5 (`DETL`) — fully resolved, a real byte-layout correction
    plus one wholly new finding.** The wiki's own struct lists fields
    summing to 0x0c bytes but ends with a `/*0x0a*/` comment — a
    pre-existing 6-byte discrepancy `cmd_dump.cpp`'s `kFallback` table
    already flagged as the reason this wasn't parsed. New `tools/
    check_detl_stride.py` found 1,043 real `DETL`-bearing files (mostly
    player-housing lighting fixtures, War Within-era). A first crude
    `chunk.size / lights.count` division looked like a confusing 3-way
    split (1,012 files "clean" at 16 bytes, 18 at 12 bytes, 13 at neither)
    — until a direct byte decode on a real multi-light file
    (`goblinspidertank.m2`, 4 lights) showed stride 16 produces garbage
    past the first record while stride 12 decodes all 4 records
    identically clean, revealing the "16-byte" bucket was a numerical
    coincidence (`48 = 12×4 = 16×3` both hold), not a second real struct
    variant. Testing the corrected hypothesis — **real stride is 12 bytes,
    whole chunk zero-padded up to the next 16-byte alignment boundary**
    (undocumented on the wiki) — against all 1,043 files at once: **100%
    match**, vs. 998/1043 and 1012/1043 for the two wrong candidate
    strides. Decoding all 1,386 real records at the confirmed stride found
    `flags` takes only two real values (0/0x8), and `scale`/
    `diffuseColorMultiplier` are a **constant** half-float value
    (0.013885498046875 / exactly 1.0) in every single real record sampled
    — about as clean a confirmation as real data gets. Written up as
    `WIKI_FINDINGS.md` §11; a full implementation plan (trivial — reuses
    `M2Particle`'s existing half-float decoder, diagnostic-only
    `dump-chunks` output, no glTF slot needed) added as `M2_GAPS_TODO.md`
    Item 8, not implemented in `src/` this session per the investigation
    brief's own "investigation, not implementation" scope.
  - **Target 6 (`M2Sequence.aliasNext`) — fully resolved, and it corrects a
    real bug in an *earlier* husk investigation, not just the wiki.** The
    existing `TODO/TODO_correctness.md` #4 (from a prior session) had already
    tried and failed to resolve this on `bloodelffemale_hd.skel` alone,
    finding `aliasNext` values in the 48,861–48,983 range that didn't
    resolve as a local index or a same-file id match. New `tools/
    check_alias_next.py`, reading the field at `M2Sequence`'s real,
    `WIKI_FINDINGS.md` §1-corrected 64-byte stride (`aliasNext` at offset
    **0x3E**, not the wiki's literal, pre-correction 0x22 — 0x22 lands
    inside `M2Range replay`'s second field at the real stride, exactly
    explaining the earlier session's nonsensical 5-digit values), across
    all four real blood-elf family files (`bloodelffemale.m2`/
    `bloodelfmale.m2` inline, `bloodelffemale_hd.skel`/
    `bloodelfmale_hd.skel` external — 1,483 sequences, 157 real aliases):
    **157/157 (100%) resolve as valid local indices into the same file's
    own `sequences` array**, and following the wiki's own documented
    chain-walk (`flags & 0x40` → jump to `sequences[aliasNext]` → repeat)
    terminates cleanly at a non-alias sequence for all 157, zero cycles,
    zero runaways. `aliasNext` is a plain local array index, not an
    `AnimationData.dbc` id and not anything cross-file, despite its own
    "id in the list of animations" doc comment — the wiki's older "I have
    no clue" bullet is simply stale, and the earlier husk session's
    "unresolvable" conclusion was a stale-offset bug, not a real dead end.
    A secondary cross-file `id`-match check (101/157 `aliasNext` values
    also happen to match some sequence's `id` in a sibling file) was
    checked and set aside as very likely coincidental — small integers
    collide constantly with small real ids in a several-hundred-entry
    space, and the match rate exactly tracks the already-explained
    local-index values, not an independent signal. A bounded 2-query web
    search (matching this project's own "don't over-spend" precedent from
    the multi-root-skeleton investigation) found no public prior-art
    resolution to corroborate against, but the real-byte result stands on
    its own. Written up as `WIKI_FINDINGS.md` §12, including the explicit
    "what went wrong the first time" section explaining the earlier
    session's bug. `TODO/TODO_correctness.md` #4 **removed outright** per that
    file's own stated convention (fixed items don't linger as `[Fixed]`
    noise) — it was the last item, no renumbering needed — with a
    summary folded into the file's own intro paragraph. `M2_GAPS_TODO.md`
    Item 1's `aliasNext` bullet rewritten from "parse raw, don't resolve"
    to "parse and resolve" — a real, not-yet-implemented follow-up now
    unblocked: `buildAnimations` currently skips every alias sequence
    outright, and could instead reuse the resolved terminal sequence's own
    animation data to produce a real clip.
  - **Docs**: `WIKI_FINDINGS.md` (three new sections, §10/§11/§12, full
    "current text / proposed addition / evidence" format matching every
    prior section on this page; "Where these live in husk" table extended
    3 rows), `M2_GAPS_TODO.md` (Item 1's `aliasNext` bullet rewritten, new
    Item 8 for `DETL`, priority-order list extended), `TODO/TODO_correctness.md`
    (#4 removed, intro paragraph updated), `DESIGN.md` (Open work section
    gained a `M2_GAPS_TODO.md` pointer it was oddly missing even before
    this session, plus a closing paragraph for
    `M2_UNKNOWNS_EXPLORATION.md`'s own now-completed disposition).
    `M2_UNKNOWNS_EXPLORATION.md` itself **deleted outright** once every one
    of its six targets had a final disposition — same "survey's job is
    done" lifecycle every prior investigation-then-TODO file in this repo
    has used. Its ~3 live cross-references inside the three new `tools/
    *.py` scripts' own docstrings were grep-verified and repointed to
    `WIKI_FINDINGS.md`'s new section numbers rather than left dangling —
    same discipline every prior file-deletion session here has used.
  - **New standalone tools, kept** (same "small, self-contained, one-off,
    independent of husk's own C++ parser" convention `tools/
    find_multiroot_skeletons.py` already established): `tools/
    find_m2_unknown_chunks.py`, `tools/check_detl_stride.py`, `tools/
    check_alias_next.py`. Their generated `*_for_exploration.txt`/
    `*_report.json` output files at repo root are already covered by this
    repo's existing blanket `*.txt`/`*.json` `.gitignore` rules (same as
    `phys_files_for_exploration.txt`/`multiroot_skeleton_files_for_
    exploration.txt` before them) — no cleanup needed, left as local
    scratch artifacts.
  - **Environment note, reconfirmed**: `direnv exec . uv run --python
    tools/venv/bin/python <script>` for the three full-corpus scanner runs
    (all under 30s each), `direnv exec . uv run --no-project python3 -c
    "..."` for ad hoc byte-level verification one-liners (the stride
    cross-check, the half-float decode, the goblinspidertank direct-decode
    sanity check) — same split this project's environment notes have used
    every prior session, inline `-c` fine for quick checks per this
    session having no standing instruction against it.
- **Previous state**: Implemented both `ANIM_TODO.md` and `PHYS_TODO.md` end to
  end, independently, in one autonomous overnight session — requested
  directly: "read both PHYS_TODO and ANIM_TODO, implement them
  independently but carefully... extend the tests to actually cover stuff,
  not just be 'we have more than 100 animations, that counts as a pass
  right?'." No interactive user available partway through, so the two
  plan-mode-flagged open questions each document left for a real design
  pass (`ANIM_TODO.md`'s implementation was already fully speced;
  `PHYS_TODO.md`'s extras-vs-dump-chunks split/CLI flag shape was not) were
  resolved by following each document's own stated recommendation rather
  than blocking — `PHYS_TODO.md`'s explicitly: "the same pattern as
  particles and ribbons, attachment points in glb but data i separate,"
  confirmed by the user mid-session, matching the doc's own Architecture
  recommendation already.
  - **`ANIM_TODO.md` (the `--anim` same-basename fallback)**: implemented
    exactly as planned (`findAnimFileByBasename`, `M2AnimInputs::modelPath`,
    `buildAnimations`'s external branch rewired to try `<FileDataID>.anim`
    then `<model-basename><animId>-<subId>.anim`) — with one real bug the
    plan itself had that only surfaced once the existing test suite ran
    against the change: the file-open fallback logic used `if (!f)` to
    decide whether the FileDataID attempt succeeded, but a default-
    constructed `std::ifstream` that never had `.open()` called on it (the
    `animFileIds == nullopt` case) reports `goodbit`, not `failbit` — `!f`
    is false, so it silently skipped the basename fallback and tried to
    read an unopened stream, producing empty bytes fed straight into
    `extractAnimBlob`/chunk parsing, which threw a real "claims more
    keyframes than this blob holds"-style error. Caught by a genuine
    pre-existing test (`tests/test_cli.cpp`'s "a sequence without
    flags&0x20 ... produces no animation clip" case, which doesn't pass
    `--anim` at all so `animArg` defaults to `auto` and resolves `animDir`
    to a real, non-empty directory) going from pass to fail the moment the
    rewired branch landed — not found by inspection. Fixed by checking
    `f.is_open()` instead of `!f` at both fallback points. Verified as a
    real, non-cosmetic bug (not just "the fix looks more correct") by
    `git stash`-ing the fix, confirming the exact failure, then restoring
    it and confirming green — same "prove a regression test actually
    regresses" discipline the multi-root/collision-mesh sessions already
    established for their own changes.
    - **4 new `tests/test_cli.cpp` cases** (basename fallback with no AFID
      at all, basename fallback when the AFID-mapped file is missing,
      FileDataID-file priority over a basename file when both exist —
      proven by making the basename file deliberately too short to
      resolve, so a wrongly-reversed priority would crash instead of
      silently reading wrong data — and neither resolving), all built on
      the existing `tinyExternalAnimM2`/`AFID`-chunk fixture already used
      by two adjacent tests, not a new fixture shape.
    - **`tests/test_integration.cpp`'s existing AFSB-follow-up case
      strengthened**, exactly per the plan and per this session's own
      explicit instruction to stop asserting fragile loose counts: it used
      to only check `model.animations.size() > 100` (true from inline +
      global-sequence clips alone, proving nothing about the fix). Added
      exact-name assertions for `anim_69_0`/`anim_69_1` (the two real,
      committed `bloodelffemale_hd0069-00/-01.anim` fixtures) — verified as
      a real regression test the same `git stash` way: **both** names are
      absent pre-fix (336 total clips, matching the inline-only baseline
      exactly), both present post-fix (338 — the fix adds exactly the 2
      real external clips this fixture set has, nothing else).
    - **Docs**: `DESIGN.md` (AFSB design note gained the resolution-vs-
      decode distinction; CLI grammar table's `--anim` row), `WIKI_FINDINGS.md`
      §2's follow-up (corrected the "336 clips, verified three independent
      ways" claim to note that verified the decode via a separate script,
      not `--anim`'s own CLI resolution — the actual pre-fix reachable
      count through the CLI was 336, i.e. zero of the genuinely-external
      clips), `README.md` (`--anim` usage paragraph, Animation-sequences-row,
      Sidecar-FileDataID-resolution row). `ANIM_TODO.md` deleted outright
      once every doc-sync item had a final disposition, its own two live
      code comments repointed to `WIKI_FINDINGS.md`/`DESIGN.md`.
  - **`PHYS_TODO.md` (`.phys` physics/collision sidecar support)**: the
    full implementation plan, built essentially as specified. New
    `src/phys.hpp`/`phys.cpp` (mirrors `bone.hpp`'s shape, not `skel.hpp`'s
    — chunk-tag-selected record arrays, not a multi-array header): every
    documented chunk type (`PHYS`/`PHYT`/`BODY`/`BDY2`/`BDY3`/`BDY4`/
    `SHAP`/`SHP2`/`BOXS`/`CAPS`/`SPHS`/`PLYT`/`JOIN`/`WELJ`/`WLJ2`/`WLJ3`/
    `SPHJ`/`SHOJ`/`SHJ2`/`PRSJ`/`PRS2`/`REVJ`/`REV2`/`DSTJ`/`PHYV`) parsed,
    chunk-tag-preference variant selection (`BDY4`→`BDY3`→`BDY2`→`BODY`,
    etc.), `SHOJ`'s real stride ambiguity (0x6c vs. 0x74, same tag)
    disambiguated by which stride the chunk's own size divides evenly —
    throws if a real file ever divides evenly by both (never seen in 103
    real files, per the investigation). `PLYT`'s self-describing variable-
    length header+data region implemented with a full byte-accounting
    check (expected total size computed field-by-field must exactly equal
    the chunk's real size), the same cross-check the original investigation
    used to catch the 0x38-vs-0x50 header-stride bug in the first place.
    Every `Body.shapeBase`/`shapeCount`, `Shape.index`, `Joint.bodyA`/
    `bodyB`/`index` reference validated in-range at parse time (real files
    have zero violations per the investigation, so a real one is corruption
    or a parser bug, not data to accept).
    - **Architecture**: followed the plan's own recommendation, confirmed
      directly by the user mid-session ("we want the same pattern as
      particles and ribbons, attachment points in glb but data i
      separate") — `husk export --phys` (three-state, mirroring `--skel`
      exactly, since `PFID` is a single scalar FileDataID like `SKID`, not
      an array like `BFID`/`AFID`/`SFID`: unset auto-detects a
      same-basename `.phys` next to the model, `none` skips, an explicit
      path overrides) attaches a **minimal** per-body placement anchor
      (`gltf::Skeleton::PhysicsBody` — id/joint/position/bodyType) as
      `physics_bodies` skin `extras`; `husk dump-chunks <file.phys>` (new
      direct-file-input path, sniffed by the reversed `PHYS` tag before the
      `.bone`/M2-magic checks) dumps the **full** body/shape/joint/`PHYV`
      record set, each shape/joint resolved to its real type-specific data
      inline (a body's shapes fully expanded; a joint's `bodyA`/`bodyB`
      left as plain indices, matching how the source data itself relates
      them).
    - **Real test fixture gap found and fixed mid-session**: none of the 7
      already-committed `.phys` weapon fixtures had a matching `.skin`
      committed alongside them (only `.m2`+`.phys`), so no real file could
      exercise the full CLI→gltf-extras→gltf_validator path end to end.
      Checked the real corpus (`/media/luna/data/wow_export`, read-only)
      and found every one of those 7 `.m2` files does have a real `.skin`
      sibling there, just not extracted into `test_data/` yet — copied
      `mace_1h_warfrontsforsaken_d_0100.skin` in (gitignored, same
      "personal WoW extraction, never committed" convention every other
      `test_data/` fixture already follows), giving one real, fully-paired
      `.m2`+`.skin`+`.phys` fixture. Confirmed by hand: 10 real bodies,
      `boneIndex` values `{0..9}` of 17 real bones, exactly matching what
      `PHYS_TODO.md`'s own test plan had predicted from the investigation
      but never verified against a live export.
    - **Verification, not just "it compiles"**: ran the real, already-
      committed 7-file weapon `.phys` set *and* the full 96-file real-corpus
      exploration sample (`phys_files_for_exploration.txt`,
      `/media/luna/data/wow_export`, read-only) through `husk dump-chunks`
      — zero failures across all 103 files, the same sample size and same
      zero-violation result the original investigation reported for its own
      independent Python decoder, now reproduced by husk's real C++ parser.
      Spot-checked the one real `PLYT`-bearing file the investigation named
      by path (`.../8xp_heartofazeroth_prop_floatychain.phys`): decodes to
      the exact `vertexCount=8/count_10=6/nodeCount=24` the investigation's
      own worked example reported. The real paired fixture's `.glb` export
      passes the actual Khronos `gltf_validator` with 0 errors.
    - **Tests**: `tests/test_phys.cpp` (new, 13 cases — happy-path
      round-trip, `BODY`-vs-`BDY4` selection-order preference, `SHOJ`
      stride disambiguation both directions plus the genuinely-ambiguous
      throw path, out-of-range shape/joint/body-index throws, malformed
      stride throws, `PLYT` round-trip and truncation), `tests/test_gltf.cpp`
      (4 new: `PhysicsBody` extras round-trip, absent-means-no-key,
      out-of-range-joint throws, coexists with `correctionSets`/
      `ribbonAnchors`/`particleAnchors`), `tests/test_cli.cpp` (4 new:
      `--phys` default/`none`/explicit-path/out-of-range-throws, synthetic
      fixtures matching `--bones-dir`'s own established fixture-building
      style), `tests/test_dump.cpp` (1 new: full JSON shape round-trip
      through a real capsule shape + weld joint, checking specific resolved
      field values, not just presence), `tests/test_integration.cpp` (1
      new: the real paired fixture, exact 10-body count, exact bone-index
      set `{0..9}`, joint-range bounds), `tests/test_conformance.cpp` (1
      new, `#ifdef`-gated both ways like every other conformance case: the
      real fixture's export passes `gltf_validator` with 0 errors). Full
      suite: 394 → 422 cases, both `./build/husk-tests` (422/422 + 1
      permanently-inapplicable skip) and `ctest` (423/423) green, verified
      via a full clean rebuild (`rm -rf build`), not an incremental one.
    - **Completions**: `--phys` added to `src/main.cpp`'s hand-maintained
      `bashValueCompletion`/`zshValueAction`/`zshFlagLabel` tables (same
      `--skel`-shaped file-or-none treatment — the existing `_husk_skel_value`
      zsh helper was shared and renamed to `_husk_file_or_none_value` since
      it's no longer skel-specific), `completions/husk.bash`/`.zsh`
      regenerated via `--print-completion`. Verified by `bash -n`/`zsh -n`
      syntax-checking both (this sandbox's nix bash build has no
      `compgen`/`complete` builtins compiled in, so the usual "drive
      `_husk_completions` with scripted `COMP_WORDS`" functional check
      wasn't possible this session) plus a direct structural diff against
      `--skel`'s own already-verified-working block, byte-for-byte
      identical shape.
    - **Docs**: `DESIGN.md` (new Key design decisions bullet mirroring the
      ribbon/particle one; Boundaries list; CLI grammar table; Open work
      section's `PHYS_TODO.md` pointer removed, replaced with the same
      "used to live here, now implemented, standalone file removed"
      framing `MULTIROOT_SKELETON_TODO.md`'s own removal used), `WIKI_FINDINGS.md`
      §9 (pointer to `PHYS_TODO.md` replaced with the real Code/Tests
      columns in "Where these live in husk"), `README.md` (Collision/
      physics format-matrix row bumped 📖, Sidecar-FileDataID-resolution
      row, new "`.phys` physics/collision data" Usage paragraph, flag
      table row, `dump-chunks` section heading/paragraph), `M2_COMPLETENESS.md`
      (`.phys` sidecar content row: `none/none/n/a-unscoped` →
      `full/extras+diagnostic/extras-capped-permanent`), `src/cmd_info.cpp`
      (the `phys_file_id` note's stale "not yet resolved by husk" text
      corrected to describe the real `--phys`/`dump-chunks` paths now —
      deliberately did **not** add a new sidecar-content-reading capability
      to `husk info` itself, since `info` has never opened *any* sidecar's
      content, `.skel` included, only ever printed the FileDataID scalar —
      inventing that only for `.phys` would have been unscoped, inconsistent
      new behavior, not a doc-sync fix). `PHYS_TODO.md` deleted outright
      once every doc-sync item had a final disposition, its ~9 live code/test
      comment cross-references repointed to `DESIGN.md`/`WIKI_FINDINGS.md`
      the same way `MULTIROOT_SKELETON_TODO.md`'s deletion repointed its own.
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>` for every ad hoc real-file verification pass this
    session (the 96-file corpus sweep, the `PLYT` spot-check, the JSON
    field inspection) — scripts run inline via `-c`, not written to the
    scratchpad, since none needed more than a few lines and this session
    had no standing instruction against it (unlike an earlier session's
    explicit "write scripts to files, inline `-c` prompts on every
    iteration" note, which applies to *iterative* byte-level derivation
    work, not one-shot JSON inspection).
- **Previous state**: Closed out `MULTIROOT_SKELETON_TODO.md` the same way
  `CORPUS_TODO.md` was closed out below — requested directly: "explore
  MULTIROOT_SKELETON_TODO.md, and make sure appropriate documentation is
  in DESIGN and README, for items that are done/resolved... document
  remaining items and decisions and unfixables in DESIGN, and remove the
  file once empty." Confirmed the file's own "Implemented" framing against
  the actual repo state (not just trusted the file's own claim): its
  Decision, Implementation plan (all 5 steps), and the invariant section
  were all already faithfully reflected in `DESIGN.md`'s Key design
  decisions and `src/gltf.hpp`'s `Skeleton`/`writeGlbMulti` doc comments —
  confirmed by reading both directly, not by inspection of the TODO file
  alone. The one gap: `README.md`'s format-support matrix — this project's
  own source-of-truth table for per-feature state — had zero mention of
  multi-root handling at all, unlike `M2_COMPLETENESS.md`'s parallel row,
  which already had one. Added a matching sentence to README's "Skeleton /
  bone hierarchy" row. `DESIGN.md`'s Open work section gained a new
  paragraph for the three things the original survey explicitly left
  unchased (kept, not discarded, since they're genuinely still open, just
  not blocking): what `gltf_validator`'s `SKIN_NO_COMMON_ROOT` check
  actually measures (empirically ~7% of a random multi-root sample, no
  hypothesis tested explains the rate), why an 11-file hit sample skewed
  toward one item family, and what `M2CompBone.flags & 0x200`
  ("transformed") actually distinguishes among root bones — all three
  low-priority, awareness-only, recorded so a future session doesn't
  re-derive them from scratch.
  `MULTIROOT_SKELETON_TODO.md` itself was then deleted outright, same
  "survey's job is done" disposition `VERIFICATION_IDEAS.md`/
  `DESIGN_CHANGES.md`/`CORPUS_TODO.md` already got. Its ~19 live
  cross-references across `src/gltf.cpp`, `src/gltf.hpp`,
  `tests/test_gltf.cpp`, `tests/test_cli.cpp`, `tests/test_conformance.cpp`,
  `tools/find_multiroot_skeletons.py`, `M2_COMPLETENESS.md`, `PHYS_TODO.md`,
  and this file's own living Next-step/Hazards bullets (below) were each
  grep-verified and rewritten to describe the fact directly or point at
  `DESIGN.md`/`src/gltf.hpp` instead — comment/string-literal-only changes,
  no logic touched. This session's own historical Resume entries further
  down (and their references to `MULTIROOT_SKELETON_TODO.md` by name) were
  deliberately left as-is, same "historical log entries aren't rewritten,
  only living cross-references are repointed" precedent every prior
  file-deletion session here has used. No `src/` behavior changed — pure
  documentation and cleanup, comment-only edits to `src/`/`tests/`, no
  rebuild performed (none of the edits touch code, only comments and
  string literals inside `TEST_CASE`/docstrings).
- **Previous state**: Closed out `CORPUS_TODO.md` — requested directly: "read
  CORPUS_TODO.md, discard items that are genuinely done and documented,
  document rest of them in DESIGN and README." Re-read all 7 items plus
  their DEVELOPER NOTES: every single one already carried a `[DONE]` (or,
  for #7, "Noted") disposition from the earlier punch-card session (commit
  `9c52615`), and every item that changed behavior or established a new
  fact already had a permanent home — #1 (zero-mesh), #3b (2-digit `.skin`
  suffix preference), and #4 (duplicate-keyframe nudge) in `DESIGN.md`'s
  Key design decisions and `M2_COMPLETENESS.md`; #6 (`WFV3` short variant)
  in `WIKI_FINDINGS.md` §8; #2's extraction-gap finding in `README.md`.
  Confirmed the one loose end from the developer notes ("I will manually
  fix this," for `tools/corpus_checks.py`'s truncating
  `_last_meaningful_line`) really is fixed by reading the current source —
  `[:400]` is gone. The only genuinely undocumented items were #3c
  (mismatched `.skin`/`.m2` vertex counts) and #5 (`materialIndex`/
  `textureComboIndex` one-past-the-end) — both confirmed-unfixable
  bad-source-data findings with no behavior change, so no `DESIGN.md`
  entry, but worth the same public-facing honesty #2 already gets: added a
  paragraph to `README.md` right after #2's existing extraction-gap note
  (0-byte files folded in alongside, same extraction-completeness class).
  With every item accounted for, nothing was left to discard piecemeal —
  the whole file's job was done, same "survey's job is done" disposition
  `VERIFICATION_IDEAS.md`/`DESIGN_CHANGES.md`/`PHYS_SIDECAR_FINDINGS.md`
  already got, so `CORPUS_TODO.md` was deleted outright rather than left
  as an all-`[DONE]` husk. Unlike those three, though, its item numbers
  were baked into ~20 live `CORPUS_TODO.md #N` comments across `src/`
  (`cmd_export.cpp`, `gltf.hpp`, `cmd_dump.cpp`), `tests/` (`test_cli.cpp`,
  `test_gltf.cpp`, `test_dump.cpp`), and `tools/find_multiroot_skeletons.py`
  — every one grep-verified and rewritten to describe the fact directly
  (or point at `WIKI_FINDINGS.md`/`DESIGN.md` where the permanent record
  already lives) rather than left dangling, same discipline the
  `VERIFICATION_IDEAS.md` deletion used for its own much smaller
  reference count. `M2_COMPLETENESS.md`'s two `CORPUS_TODO.md #N`
  citations repointed to `DESIGN.md` the same way. This session's own
  historical entries below (and this file's Status section's one
  narrative mention) were deliberately left naming `CORPUS_TODO.md` by
  name where they're describing what happened in the past — same
  "historical log entries aren't rewritten, only living cross-references
  are repointed" precedent `VERIFICATION_IDEAS.md`'s own deletion already
  set. No `src/` behavior changed this session — pure documentation and
  cleanup, verified by a full rebuild + `./build/husk-tests` afterward.
- **Previous state**: Read-only investigation into `.phys` (physics/collision
  sidecar, `M2_COMPLETENESS.md`'s Collision & physics section, previously
  completely unscoped -- husk only ever read the `PFID` FileDataID scalar,
  never the file's own content), requested directly: "do an read only
  investigation on this ... poke around, ask if something is unclear."
  Two-part follow-up in the same session, once the investigation confirmed
  implementation was viable: "would we be able to implement the .phys
  handling into husk with this information? if so, go ahead and write the
  WIKI_FINDINGS, and convert the PHYS_SIDECAR_FINDINGS into a comprehensive
  and testable todo." No `src/` changes -- investigation and documentation
  only, same "findings and plan before code" shape `MULTIROOT_SKELETON_TODO.md`
  used for the multi-root gap above.
  - **Different starting position than every prior sidecar investigation**:
    `.phys` is not undocumented. `documentation/wowdev-wiki/md/PHYS.md`
    (wiki_revision 30458) already gives byte offsets for nearly every
    field, so this was verify-against-real-bytes, not reverse-engineer-
    from-nothing (`.bone`'s situation) or crack-a-format-the-wiki-doesn't-
    cover (`AFSB`'s).
  - **Independent scratch decoder** (Python, not committed, no dependency
    on husk's own not-yet-written parser -- same discipline
    `tools/find_multiroot_skeletons.py` already established), run against
    103 real files: the 7 already-committed weapon fixtures under
    `test_data/item/objectcomponents/weapon/`, plus 96 real corpus files
    Luna had already listed in `phys_files_for_exploration.txt`
    (world doodads, item components, creatures, spell-effect arena flags).
  - **One real transcription bug found and fixed in understanding**:
    `PLYT`'s self-describing header struct is 80 bytes (0x50) per entry,
    not 38 (0x38) -- the wiki's own struct listing has the extra trailing
    `float unk_38[6]` field, but it's easy to misread the struct as ending
    one field earlier. Caught by the second header entry in a real 4-
    polytope file decoding to garbage at the wrong stride and to clean,
    wiki-comment-matching values (`vertexCount=8 count_10=6 nodeCount=24`,
    "mostly 8/6/24" per the wiki's own text) at the corrected one.
  - **One real semantic correction**: `BODY`/`BDY3`/`BDY4`'s `type` field
    comment ("only one body should be of type 0, the root") is contradicted
    by 78 of 98 real files with a body chunk -- multiple type-0 bodies is
    the common case (up to 27 of 44 in one creature file), cross-tabulated
    against `BDY3`'s own `unk1`-as-kinematic-weight field with a 96% clean
    correlation across 1256 real body records, consistent with type-0
    meaning "kinematic, bone-driven" as a real per-body classification,
    not a single distinguished root.
  - **Everything else in PHYS.md's struct listing verified clean**: chunk-
    tag byte-reversal (WMO/ADT convention, opposite of M2's own inline
    chunks -- confirmed via hex dump), the `PHYV` chunk's mutual-
    exclusivity claim and worked example (confirmed on the exact file the
    wiki names by filename, `7vs_detail_nightmareplant01_phys.phys`, plus
    its sibling), version↔chunk-name-variant pairing (zero exceptions),
    `SHOJ`'s documented-but-ambiguous version-2 stride cutover (0x6c vs.
    0x74 -- every one of 86 real chunks divided evenly by exactly one,
    never both), and -- the strongest single piece of corroborating
    evidence -- a full cross-chunk index/bounds validation pass
    (`BODY`/`BDY3`/`BDY4`'s shape ranges, `SHAP`/`SHP2`'s `shapeIndex`,
    `JOIN`'s `bodyAIdx`/`bodyBIdx`/`jointId`) across all 103 files found
    **zero** out-of-range references anywhere.
  - **Findings written to `WIKI_FINDINGS.md` §9** (new), following the
    page's own "current text / proposed addition / evidence" convention,
    with a "Follow-up" subsection for the full verification sweep --
    same shape §2 (`AFSB`) and §8 (`WFV3`) already use. `PHYS_SIDECAR_FINDINGS.md`
    (this session's own intermediate scratch-investigation file) was then
    deleted outright once its content had a permanent home split two ways
    -- same "survey's job is done" disposition `VERIFICATION_IDEAS.md` and
    `DESIGN_CHANGES.md` got in earlier sessions, not left in place with
    `[DONE]` tags.
  - **`PHYS_TODO.md`** (new) is the actionable half -- a concrete
    implementation plan, not another open-ended survey, since the
    investigation resolved essentially every structural question. Covers:
    a verified-vs-unverified coverage table per chunk type (driving
    implementation priority -- `PLYT`/`CAPS`/`SHP2`/`BDY4`/`SHOJ`/`REVJ`/
    `WLJ2` all verified against real files; `BOXS`/`SPHJ`/`PRSJ`/`PRS2`/
    `DSTJ`/`SHJ2`/`WLJ3`/`REV2`/`BDY2` never observed anywhere in the
    103-file sample, flagged for the same "verified floor, warn below it"
    treatment `kMinVerifiedParticleVersion` already uses elsewhere, per
    chunk type rather than per file version); an architecture
    recommendation (the ribbon/particle hybrid pattern -- minimal
    placement-anchor `extras` unconditional in every `.glb`, full body/
    shape/joint/`PHYV` records in `dump-chunks`'s JSON, `.phys` files
    also accepted directly by `dump-chunks` like `.bone` already is --
    reasoned from `.phys` bodies already being `position`+`boneIndex`
    anchors structurally closer to `M2Ribbon`/`M2Particle` than to
    `.bone`'s flat correction-matrix table), explicitly flagged as a
    recommendation for a real plan-mode design pass, not a decision
    already made; a `src/phys.hpp`/`phys.cpp` data-model sketch mirroring
    `bone.hpp`'s shape; a concrete real-fixture test plan, including an
    honest gap callout that zero committed fixtures currently carry
    `PLYT`/`SPHS`/`BOXS`/`SPHJ`/`PRSJ`/`PRS2`/`DSTJ`/`SHJ2`/`WLJ3`/`REV2`/
    `PHYV` (candidate real corpus paths named for the ones this session's
    sample did find, e.g. `PLYT` in
    `world/expansion07/doodads/8xp_heartofazeroth_prop_floatychain.phys`)
    -- same "real test data was the actual blocker" pattern the particle/
    ribbon session hit, flagged proactively this time rather than
    discovered mid-implementation; and a full doc-sync checklist
    (`M2_COMPLETENESS.md`, `README.md`, `DESIGN.md`, completions tables'
    hand-maintained-gotcha) for whenever implementation actually happens.
  - **Docs**: `DESIGN.md`'s Open work section now also points at
    `PHYS_TODO.md`, alongside `TODO/TODO_correctness.md`/`WIKI_FINDINGS.md`/
    `MULTIROOT_SKELETON_TODO.md`.
  - **Environment note, reconfirmed**: `direnv exec . uv run --python
    tools/venv/bin/python <script>` for every ad hoc analysis pass this
    session (the decoder, the index/bounds cross-check, the `husk info`
    bone-count cross-reference) -- scripts lived in the scratchpad, not
    committed, matching every prior session's convention.
- **Previous state**: Implemented `MULTIROOT_SKELETON_TODO.md`'s Implementation
  plan end to end -- the multi-root-bone-forest → glTF representation gap
  (35% of a real 130k-file corpus, per the previous state's own
  measurement) is no longer a decision-and-survey document, it's real code.
  Requested directly: "start working on the implementation step of this
  file."
  - **`src/gltf.cpp`'s `writeGlbMulti`**: exactly the previous state's
    Option 1 -- when `rootJointNodeIndices.size() > 1`, one
    `tinygltf::Node` (default/identity transform) is synthesized with
    `.children = rootJointNodeIndices`, appended past the end of the
    joint-node range (`meshCount + skeleton->joints.size()`), and becomes
    the sole `scene.nodes` entry standing in for those roots;
    `skin.skeleton` is set to it. Single-root models (`size() <= 1`,
    the overwhelming majority): completely unchanged, verified by the
    full pre-existing test suite passing unmodified. `Skeleton::joints`
    itself was never touched, per the file's own "one invariant that
    must never break" -- the whole change lives inside `writeGlbMulti`'s
    node/scene/skin construction.
  - **Empirically resolved the one thing the Decision section had
    explicitly deferred, not guessed at**: does Blender's glTF importer
    count the synthesized node as a bone? Ran the real fixture
    (`offhand_1h_revendreth_d_01.m2`, 15 bones/10 roots) through
    `husk export` then both the real `gltf_validator` and headless
    Blender by hand before writing any test assertion. Confirmed:
    `gltf_validator` reports 0 errors (`SKIN_NO_COMMON_ROOT` gone,
    previously present); Blender's `bone_count` probe reports exactly 15
    -- the synthesized node is *not* counted as a bone, `skin.joints.size()`
    stays exactly `header.bones.count`. Option 1 and Option 2 are
    confirmed *not* equivalent in practice; Option 1 has no Blender-visible
    downside. Both `MULTIROOT_SKELETON_TODO.md`'s Decision section and its
    design-question-A writeup were updated with this finding rather than
    left as an open hedge.
  - **Tests**: 387 → 394 cases (both `./build/husk-tests` and `ctest`
    green, 1 permanently-inapplicable skip unchanged). New
    `tests/test_gltf.cpp` cases (a `buildMultiRootSkeleton()` fixture, 3
    independent roots): synthetic-node-exists-with-correct-children/
    skin.skeleton/untouched-transform, single-root-output-unaffected
    (explicit regression case, not just "the old tests still pass"), and
    a mixed mesh-nodes-plus-multi-root-skeleton case proving vertex joint
    indices stay raw/unshifted. New `tests/test_conformance.cpp` cases
    (real `testWeaponParticleB()` fixture, gated the same
    `doctest::skip`/`#ifdef HUSK_GLTF_VALIDATOR`/`HUSK_BLENDER` way every
    other conformance case is): the `gltf_validator`
    zero-errors-no-`SKIN_NO_COMMON_ROOT` check, and the Blender
    bone-count-matches-header-exactly check, each gated on a
    `countRealRootBones()` sanity check (parses the real bone array
    directly, independent of husk's own code) so the test fails loudly
    rather than passing vacuously if a future fixture swap ever replaces
    this file with a single-root one. New `tests/test_cli.cpp` cases for
    the two combinations `MULTIROOT_SKELETON_TODO.md` flagged as
    genuinely untested: `--lod all` + a synthetic 3-independent-root
    `.skel` (via the existing `buildSkel` helper, since no real fixture
    combines multi-LOD and multi-root), and `--bones-dir` + the same
    multi-root `.skel`, with the `.bone` file deliberately correcting the
    *last* root joint (not joint 0) to prove `CorrectionSet::joint`
    indices are unaffected by the synthesized node's presence, not just
    "should be unaffected in principle."
  - **Docs**: `src/gltf.hpp`'s `Skeleton`/`writeGlbMulti` doc comments
    (the new synthesized-root behavior, now the authoritative contract,
    not just this TODO file's prose); `DESIGN.md` (new Key design
    decisions bullet, matching this session's own corpus numbers and the
    Blender finding; Open work section's multi-root paragraph rewritten
    from "still open" to "implemented"); `M2_COMPLETENESS.md`'s "Skeleton
    / bone hierarchy" row (note now mentions multi-root synthesis, status
    unchanged at `native — 100%`); `MULTIROOT_SKELETON_TODO.md` itself
    (opening framing now says "Implemented," every Implementation-plan
    step and every now-resolved hazard bullet marked `[DONE]`, the
    Decision section's "still genuinely unverified" paragraph rewritten
    with the real Blender numbers).
  - Nothing else in `src/` touched -- `cmd_export.cpp`/`buildSkeleton`
    exactly as before, per the plan's own step 2.
- **Previous state**: Corrected the framing on `MULTIROOT_SKELETON_TODO.md`'s
  whole premise, did bounded prior-art research, and recorded a real
  decision — Luna, not implemented yet, handing off from here. Prompted by
  a direct question after the previous state's corpus-scale measurement:
  "is it right to call it an issue, if the source material does not have a
  problem with multi root files... we want as close as possible 1 to 1
  representation." Correct catch: multi-root bone forests are legitimate,
  common M2 data (WoW's engine never required a single tree), not a
  defect — the file's whole opening section now states this explicitly
  and judges every design option against "closest possible 1:1 fidelity to
  the source," not "make gltf_validator happy." Bounded web research (2
  searches + 1 fetch, deliberately capped per Luna's own "don't spend too
  much time" instruction) found real prior art: glTF's own spec/tooling
  explicitly anticipates a skeleton root's parent not being a joint itself
  (Khronos issue #1270), and `wow.export` (the established community
  WoW-export tool) has a real, shipped "prefix bones" inclusion toggle for
  this exact shape (v0.2.0) — added as a third design option (filter,
  `wow.export`-style) alongside the two from the previous state's survey.
  Luna then chose directly: **Option 1** (a plain non-joint glTF node
  parenting every real root joint, `skin.skeleton` pointed at it) — the
  spec-anticipated, fully-fidelity-preserving choice, overriding the
  previous state's own "needs empirical Blender verification before
  deciding" recommendation (that verification now happens *during*
  implementation's real test-writing, not as a precondition to choosing).
  `MULTIROOT_SKELETON_TODO.md` restructured around this: a new Decision
  section up top, both design questions marked decided (kept for their
  reasoning, not deleted), a concrete numbered Implementation plan
  (supersedes the old "Recommended first steps"). Nothing in `src/`
  touched this turn — pure documentation, per Luna's own "I'll take over
  from there" close. **Whoever picks this up next should start at
  `MULTIROOT_SKELETON_TODO.md`'s Implementation plan section directly.**
- **Previous state**: Measured `MULTIROOT_SKELETON_TODO.md`'s scope for real,
  corpus-wide, and found the previous state's own framing needed a real
  correction. Requested directly: "generate a script that prints all of
  the multiroot skeleton files into a newline separated txt file... similar
  to ./phys_files_for_exploration.txt." New `tools/
  find_multiroot_skeletons.py` (self-contained, same independent-parse
  discipline `corpus_checks.py` uses -- not calling husk) scanned the full
  real corpus in ~40s: **45,804 of 130,576 `.m2` files (35%) have more than
  one root bone**, written to `multiroot_skeleton_files_for_exploration.txt`
  (repo root, plain newline-separated paths, same format as its
  `phys_files_for_exploration.txt` precedent).
  - **The real correction**: Luna asked directly whether "a lot of models
    have multiroot but pass [gltf_validator] because of skinning
    shenanigans, and a common root fix could theoretically apply to every
    file for correctness" -- confirmed, empirically, not just plausibly.
    `bloodelffemale.m2` (the project's own primary fixture) has **90 root
    bones out of 119** and exports with **zero** `gltf_validator` errors;
    `offhand_1h_revendreth_d_01.m2` has only **10** and *does* trigger
    `SKIN_NO_COMMON_ROOT`. Neither raw root count nor root count restricted
    to vertex-weighted joints explains the difference -- both hypotheses
    directly falsified against real bytes before being written down. A
    real 150-file random sample (from the 45,804), each actually exported
    and checked with the real `gltf_validator` (not a proxy), found only
    **11/150 (≈7.3%) currently trigger the error** -- extrapolated, ~3,300
    files corpus-wide are visibly flagged today, a small fraction of the
    45,804 that are genuinely structurally multi-root. `gltf_validator`'s
    own exact trigger condition wasn't reverse-engineered (flagged as an
    open question, not guessed at) -- what *is* now established is that
    "passes the validator" isn't the same as "has one real joint root,"
    which reframes the whole rework: the fix target is the full 35%, not
    just the ~2-3% currently visible, and testing it only against
    known-currently-erroring files would badly under-scope it.
  - **Docs**: `MULTIROOT_SKELETON_TODO.md` rewritten (opening section, a
    new "Open questions this session didn't chase down," step 1 of
    Recommended first steps marked done) to reflect the corrected,
    corpus-measured scope rather than the earlier 2-fixture/26-file
    estimate.
  - Nothing committed this turn -- the new script, the generated `.txt`
    file, and the `MULTIROOT_SKELETON_TODO.md`/`CLAUDE.md` edits are
    sitting in the working tree, same as the previous state's own
    (already-committed) work being built on.
- **Previous state**: Committed the `CORPUS_TODO.md` work below (single commit,
  `git log` for the message), then wrote `MULTIROOT_SKELETON_TODO.md` — a
  pre-implementation risk survey for the `SKIN_NO_COMMON_ROOT` gap the
  previous state below flagged but didn't fix, requested directly: "this
  would need a more robust workaround... write a todo file for this
  rework, what it could affect, and where it would be likely to fail
  invisibly... attempting to pre-empt failure modes we might miss by
  doing something the original file format didn't do." Pure investigation
  and documentation this turn — no `src/` changes, nothing to test-run.
  - **The core risk, identified and documented as the file's own opening
    section**: `Skeleton::joints`' ordering is raw M2 bone-array indices,
    copied verbatim into glTF `JOINTS_0` (`buildSkinning`), and into
    `EmitterAnchor`/`CorrectionSet`/`JointAnimation`'s own `joint` fields
    — none of these are ever remapped, only bounds-checked. A synthetic
    "common root" node inserted into `Skeleton::joints` itself (rather
    than purely on the glTF-node side, past the end of the real range)
    would silently misattribute every one of those to the wrong bone —
    no crash, no validator error, just visually wrong. Confined the fix's
    entire footprint to `gltf.cpp`'s `writeGlbMulti` for exactly this
    reason.
  - **Grounded against real bytes before writing anything else** (a
    from-scratch bone-array parent-chain parser, not reusing husk's own
    code — same independent-check discipline `WIKI_FINDINGS.md`'s other
    entries use): `offhand_1h_revendreth_d_01.m2` (15 bones, 10 roots —
    a mixed shape, a few real small hierarchies plus isolated bones) and
    `mace_2h_bolvar_d_01.m2` (78 bones, **78 roots** — every bone its own
    tree, no hierarchy at all, consistent with "one bone per particle
    emitter, no relation needed between them"). Real, intentional M2 data
    either way, not corruption — and root count can be the *entire* bone
    count, not "usually 2, sometimes a few," which rules out any design
    that only comfortably handles a small fixed number of extra roots.
  - **Two design questions deliberately left open, not decided**: (a)
    whether the synthetic node joins `skin.joints` as one more real bone
    (identity IBM, appended past every real M2 index) or stays a plain
    non-joint parent node outside the skin — the actual deciding factor
    is empirical (does Blender's importer count it as a bone either way?
    `tests/blender_import_check.py`'s `bone_count` probe would answer
    this directly, but only once a real spike exists to run through it —
    not guessed at this session); (b) whether `tinygltf::Skin::skeleton`
    (currently never set at all) should point at the synthetic node —
    glTF spec text and `gltf_validator`/Blender's actual behavior around
    it weren't checked this session, flagged as genuinely unresearched
    rather than assumed either way.
  - **Other concrete invisible-failure scenarios documented**: single-
    root models must produce byte-identical output (the fix has to be
    strictly gated on `rootJointNodeIndices.size() > 1`, and every
    existing `test_gltf.cpp` skeleton test hard-codes exact node
    counts/indices assuming a single root); `test_conformance.cpp`'s
    exact-match bone-count assertions only run against the single-root
    bloodelf fixture today, so a multi-root regression wouldn't be caught
    by anything currently passing; a stray non-identity transform on the
    synthetic node would silently shift every former-root joint's whole
    subtree; `--lod all` and `--bones-dir` combined with a synthesized
    root are both untested combinations with no fixture today.
  - **Recommended sequencing, in the file itself**: characterize the
    shape more broadly first (the 10 geometry-less-VFX files this
    session's own #1 fix uncovered, not sampled for bone-hierarchy shape
    yet), answer the Blender-bone-count question empirically with a
    throwaway spike before committing to a real implementation, check
    glTF's `skin.skeleton` semantics for real, only then implement --
    gated strictly, in `gltf.cpp` alone, with new tests using the two
    real fixtures above (`testWeaponParticleB()`/
    `testWeaponParticleStress()`, already wired up) through both
    `gltf_validator` and the real headless-Blender probe, not just "no
    crash."
  - **Docs**: `DESIGN.md`'s Open work section now points at
    `MULTIROOT_SKELETON_TODO.md` alongside `TODO/TODO_correctness.md`/
    `WIKI_FINDINGS.md`.
- **Previous state**: Worked `CORPUS_TODO.md` as a punch card — a from-scratch
  grounding of an earlier raw sweep (`HUSK_CORPUS_FINDINGS.md`) across a
  real 130k-file corpus (`/media/luna/data/wow_export`), re-checked against
  actual code and real bytes, with Luna's own DEVELOPER NOTES per item
  giving direction/approval and an explicit bottom-of-file priority order.
  Requested as "use this file as a punch card... prio order in the
  bottom." Every item in the file now has a final disposition — the same
  signal that triggered `VERIFICATION_IDEAS.md`'s deletion in an earlier
  session — but `CORPUS_TODO.md` itself was left in place rather than
  deleted unilaterally: it's Luna's own working punch-card doc with her
  manual annotations throughout, a different situation from a purely
  generated scratch survey.
  - **#1 (empty-primitive crash, 3,807 real files, highest-priority
    item) — fixed.** `buildMaterialsAndPrimitives`
    (`src/cmd_export.cpp`) used to manufacture one glTF primitive with
    empty `indices` for a genuinely geometry-less `.skin` (real corpus
    shape: pure particle/ribbon VFX models, 0 vertices at the M2 level,
    not just an empty batch table) — glTF has no valid "primitive with
    zero indices" representation, so every one of these failed outright.
    Went with the "zero meshes" design Luna approved directly: skip
    adding a `NamedMesh` for a LOD tier that resolves to zero primitives
    (both the whole-file-empty case and the rarer per-submesh
    `indexCount == 0` case), and relaxed `gltf::writeGlbMulti`
    (`src/gltf.cpp`/`gltf.hpp`) to accept an empty `meshes` list as long
    as a real skeleton (≥1 joint) exists to fall back to -- the model's
    skeleton and ribbon/particle emitter anchors (already unconditional,
    prior session) still export with zero mesh nodes; `Error`s outright
    only when both are empty (nothing to export at all). Verified: a
    random 25-file sample of real `FAIL-0001` corpus files all had 0
    vertices at the M2 level (confirms the dominant-shape assumption),
    all 25 now export cleanly and pass `gltf_validator` with 0 errors.
    **Real side-finding, out of this item's own scope:** 10 of those 25
    (+1 more checked individually, 26 total) hit a *different*,
    pre-existing `gltf_validator` error (`SKIN_NO_COMMON_ROOT`, "Joints
    do not have a common root") -- the same multi-root-bone-hierarchy gap
    `DESIGN.md`'s Hazards section already documented for 2 of 4 real
    weapon fixtures, here showing up in ~38% of geometry-less VFX models
    too. Not fixed this session (unscoped, needs a real bone-hierarchy-
    reconciliation design) -- flagged in both `CORPUS_TODO.md` and here
    for a future session.
  - **#3b (`findSameBasenameSkins` prefix-collision bug) — fixed.** A
    same-basename numeric-suffix `.skin` match used to accept a digit
    run of any length, which a real corpus scan found genuinely
    ambiguous whenever one model's basename is itself a numeric-suffix
    prefix of a sibling model's basename in the same directory (real
    files: `mogu_library_crate_10.m2` vs. `mogu_library_crate_1.m2`,
    `vebgrs10.m2` vs. `vebgrs1.m2`/`vebgrs11-17.m2`, `vebbsh10.m2` vs.
    `vebbsh1-9.m2`) -- the shorter model's own real 2-digit-suffix file
    parses as a spurious 1-digit match for the longer model's basename
    too, and used to win `std::sort`'s lexicographic tie-break over the
    correct file. Fixed by preferring an exactly-2-digit suffix match
    (WoW's own real convention) whenever at least one exists for a given
    basename, discarding 1-digit/3+-digit matches as collisions -- kept
    as a fallback, not a hard reject, when no 2-digit match exists at
    all (no real-corpus evidence either way for that shape, and a hard
    reject risks a new false-negative regression). Checked against the
    real collision directory directly (`world/nodxt/detail/`, all 26
    `vebgrs`/`vebbsh` siblings) rather than a full corpus walk -- every
    genuine skin there resolves correctly under the new rule, no
    exceptions. Also implemented the doc's second approved idea: the
    vertex-out-of-range error message now reports the *count* of
    out-of-range indices and the *worst offender*, not just the first
    one iteration happened to hit (real wrong-`.skin` pairings reference
    hundreds of out-of-range indices, not one -- the old message made two
    identical bugs look like different shapes purely as an iteration-order
    artifact). Verified against both real confirmed-collision files:
    `mogu_library_crate_10.m2` now resolves to `...crate_1000.skin` (68
    vertices, matches the doc's own figure) and `vebgrs10.m2` to
    `...vebgrs1000.skin` (8 vertices), both exporting cleanly.
  - **#6 (`dump-chunks` `WFV3` short-chunk variant, 9 real files) —
    fixed.** `dumpWfv3` (`src/cmd_dump.cpp`) assumed every `WFV3` chunk
    is a fixed 80-byte struct; all 9 real files carrying one (1
    Shadowlands "Maw"-zone doodad, 8 Nazjatar-zone water-effect doodads
    -- corrected from this doc's own file-list, which had all 9
    mis-attributed to the Maw zone alone) are consistently 64 bytes,
    missing exactly the trailing `unk1`-`unk4` floats. Fixed by reading
    those four conditionally on `c.size >= 0x50`, emitting `null` for
    the short variant (same "genuinely absent, not a parse failure"
    treatment `dumpTextureWeights`'s optional fields already use)
    instead of throwing. **New, previously-undocumented-on-the-wiki
    finding** (`WIKI_FINDINGS.md` §8, new): the wiki's own `WFV3` struct
    listing is unconditionally 80 bytes with no mention of a shorter
    variant at all -- every field before `unk1` decodes cleanly on all 9
    real files, the chunk simply ends exactly 16 bytes short of the
    documented size, every time. Verified: all 9 real files now dump
    cleanly, `unk1`-`unk4` all `null` as expected.
  - **#4 (duplicate-timestamp animation keyframes, 5 real files) --
    fixed.** `checkKeyframesWellFormed` (renamed
    `repairDuplicateTimestampsAndValidate`, `src/cmd_export.cpp`) used
    to reject *any* non-strictly-increasing keyframe timestamp
    identically, whether genuine disorder (a timestamp that actually
    decreases -- real corruption) or an exact duplicate (real, shipped
    Blizzard data: an authored "hard cut" pose, always on `rotation`,
    confirmed on all 5 real files named in the doc -- 2 world bosses,
    2 base character rigs, 1 world doodad). Chose **nudge over collapse**
    (the doc's own two options, left an open decision pending a
    reader cross-check that turned up nothing specific to M2): collapsing
    either keyframe would silently discard one of the two real authored
    values, while nudging the later duplicate's timestamp forward 1ms
    (cascading, so a run of N duplicates spreads out N-1ms apart) keeps
    both -- correct under both glTF LINEAR and STEP sampler
    interpolation, and general glTF-authoring precedent (Blender's own
    exporter deliberately inserts near-zero-gap duplicate keyframes to
    force STEP-like behavior) supports nudge as the standard shape for
    this exact problem. A genuinely *decreasing* timestamp still throws,
    classified against each keyframe's *original* (pre-repair) timestamp
    -- comparing against an already-nudged value would misfire the
    disorder check on a cascading run's second entry, a real bug caught
    while implementing (fixed before it reached any test). Verified
    against all 5 real files: all export cleanly now, `gltf_validator`
    shows zero animation-sampler-related errors on any of them (remaining
    errors on 2 of the 5 are pre-existing/unrelated --
    `ACCESSOR_JOINTS_INDEX_DUPLICATE`/`SKIN_NO_COMMON_ROOT`, same classes
    already documented elsewhere in this repo).
  - **#5 (`materialIndex` out of range, investigated further per Luna's
    request to check more examples before declaring unfixable) --
    confirmed unfixable, now with much stronger evidence.** Original
    doc spot-checked one file; this session checked all 16 real files
    identifiable via current `failures.txt`/`failure_codes.txt`
    (renumbered since the doc was written). All 16 confirm the exact
    same striking, perfectly uniform signature: `materialIndex` is
    always **exactly** the model's own material count (never further out
    of range), and `husk info` confirms each file's own material array
    really does stop one short. No sibling-basename digit collision on
    any of the 16 (all end in letter race/gender codes), so independent
    of #3b's bug -- confirmed, not assumed, since #3b's fix was already
    live when these were re-checked. No wiki-documented sentinel value
    explains an "index == count" `materialIndex` either. Genuinely bad/
    mismatched shared batch data across collections/recolor item
    variants -- not fixable in husk. The doc's ~7 additional
    `textureComboIndex` cases couldn't be re-verified the same way --
    `failures_unique.txt` strips file paths during anonymization, and no
    example path is available from current tooling output -- flagged
    honestly as unverified (structurally identical shape, almost
    certainly the same root cause, but not re-confirmed) rather than
    quietly assumed.
  - **#2's remaining half (missing spell-effect/item `.skin` files) --
    explored, confirmed genuinely unfixable in husk, README note added.**
    Requested as "explore if possible to fix... or if genuinely no way to
    find the correct one, add explanation note in README." Widened the
    sample first and found the doc's own "spell-effect models" framing
    undersold the real scope -- the current `FAIL-0003` bucket also
    includes ordinary item pieces (`item/objectcomponents/shoulder/`,
    `.../collections/`). Checked two real files (the doc's own spell
    example, plus a shoulder-armor item) the same rigorous way: real
    geometry confirmed via `husk info`, then a *targeted* `find` (not
    another 130k-file walk -- see the environment note below) for each
    declared `SFID` FileDataID, decimal-to-hex converted, checked both
    next to the model (already known absent) and in `_unresolved/`
    (`wow_export`'s own "extracted but couldn't place" bucket,
    `FILE<8-hex>.dat` naming -- the one place a "misplaced, not really
    missing" file would surface) -- zero matches anywhere, for every
    FileDataID on both files. Genuinely absent from the extraction, not
    a husk-side false negative. Added a paragraph to `README.md`'s
    `--skin`/`auto` section explaining this is a known extraction-
    completeness gap, not a husk bug, and that re-running the extraction
    tool (not husk) is the only real fix.
  - **Tests**: 378 → 387 cases (`./build/husk-tests`: 387/387 + 1
    permanently-inapplicable skip; `ctest`: 388/388). New cases per fix
    above in `tests/test_gltf.cpp` (empty-meshes-with-skeleton,
    empty-meshes-without-skeleton-throws) and `tests/test_cli.cpp`
    (basename-collision reproduction both directions, out-of-range-count
    error message, single + 3-way-cascading duplicate-timestamp repair),
    `tests/test_dump.cpp` (`WFV3` short-variant round-trip).
  - **Docs**: `CORPUS_TODO.md` (every item's own DEVELOPER NOTES section
    now has a `[DONE]`-tagged disposition, matching this repo's existing
    "fixed items get a disposition, not silently dropped" convention),
    `WIKI_FINDINGS.md` (new §8, `WFV3`'s undocumented short variant),
    `README.md` (the `.skin`-not-found extraction-gap note above),
    `DESIGN.md` (3 new Key design decisions bullets: zero-meshes,
    2-digit-suffix preference, duplicate-timestamp nudge-repair),
    `M2_COMPLETENESS.md` (2 rows -- mesh geometry, animation tracks --
    annotated with the new edge-case handling, no status-symbol changes
    since both were already at `native — 100%`).
  - **Environment note, reconfirmed and reinforced**: started an
    unscoped full-130k-file `os.walk` Python scan (checking
    same-basename-suffix-length distribution for #3b) before Luna
    interrupted directly -- "you do realize there is 130 THOUSAND m2
    files in that tree, which is exactly why i provided the exact
    failures.txt file to map to relevant files." Stopped the background
    task immediately, rescoped to the specific directories/files
    `failures.txt`/`failure_codes.txt` already flagged for the rest of
    the session (every subsequent investigation in this session --
    #5, #2's remainder -- used this same targeted approach, not another
    broad sweep). `direnv exec . uv run --no-project python3 <script>`
    remains the sanctioned ad hoc-analysis pattern for the cases that
    did need a script (reading a `gltf_validator` JSON report), scripts
    left in the scratchpad, not committed.
- **Previous state**: Particles/ribbons (`M2Particle`/`M2Ribbon`) — the single
  biggest remaining visual-identity gap this tool had (weapon glow trails,
  magic/fire/smoke) — went from 0%/static-fields-only to fully parsed:
  every static field, plus every M2Track/FBlock animation curve, for both
  types. Requested as "what's the next biggest step in M2 coverage to WoW
  feel/look" with the user's own hunch (particles and ribbons) confirmed
  and pursued.
  - **Real test data was the actual blocker, and got solved mid-session.**
    Every M2 fixture previously in `test_data/` (blood elf female, base +
    HD) has zero particle/ribbon emitters. Luna extracted the game's full
    weapon set into `test_data/item/objectcomponents/weapon/` (gitignored,
    1.6G, 4112 `.m2` files, same "real, personally-owned extraction, never
    committed" convention as the character fixtures) mid-session, in
    response. A scan found 1270 files with real particle/ribbon data, all
    sampled at M2 version 272/274 (Cataclysm+) — four fixtures selected
    and now permanently referenced by `tests/test_data_paths.hpp`
    (`kWeaponRibbon` = Ashbringer, 3 ribbons/0 particles;
    `kWeaponParticleA`/`B` = two combined ribbon+particle weapons;
    `kWeaponParticleStress` = a 64-particle-emitter mace).
  - **Architecture went through a real design pivot, twice, before
    implementation** (both via `EnterPlanMode`/`AskUserQuestion`, not
    silently decided): first draft put everything in glTF `extras`
    (matching `.bone`-correction/geoset/texture-transform precedent) —
    the user pushed back, asking whether an auxiliary file (the BLP→PNG
    precedent) fit better given `dump-chunks` already exists for "M2 data
    with no glTF slot." Second draft routed everything through
    `dump-chunks` — but that command's own stated scope (`src/
    cmd_dump.cpp`'s doc comment, its usage text) is Legion+ chunk tags
    only, and `M2Ribbon`/`M2Particle` are core `MD20` header arrays
    present in *every* version, a real, narrower boundary than "no glTF
    slot." Landed on a hybrid, confirmed by the user directly ("point it
    at a dir, get generic file equivalents... completeness and
    automatability are the key"): a **minimal placement anchor**
    (id/bone/position, `gltf::Skeleton::EmitterAnchor`) unconditionally in
    the `.glb` skin's `extras`, and the **full record** (every field,
    every resolved curve) in `dump-chunks`'s JSON output — which required
    deliberately broadening that command's own documented scope (usage
    text and doc comment both rewritten to state it explicitly, not left
    to imply chunk-tags-only while secretly doing more).
  - **Offset derivation was hand-done, not guessed, and cross-checked
    twice** — the wiki gives `M2Particle`'s explicit hex offsets only up
    to `childEmittersModelFilename`; everything after (all
    version-conditional branches: late-BC field-width change, Cata's
    `multiTexScale`, Wrath's extra floats and `FBlock`-based curves) was
    summed field-by-field by hand. That derivation landed on exactly the
    wiki's own independently-stated total record size (476 bytes default,
    492 with the Cata+ wrapper) without being fudged to fit — a real
    independent check, not circular. Then cross-checked a second way,
    against real bytes (`mace_2h_bolvar_d_01.m2`, 64 particles): decoded
    colors form a genuine fire/ember gradient, alpha/scale curves are
    clean envelopes, and the `MultiTexture` flag bit correlates exactly
    with non-zero `multiTexScale` — see `WIKI_FINDINGS.md` §6 for the full
    writeup, including a real bug an early ad hoc verification script had
    (skipped the real per-sequence `M2Track` inner-array indirection,
    producing a plausible-but-wrong near-zero alpha value instead of the
    real resolver's correct 0.8) that the "verify against real data before
    trusting a claim" discipline caught before it reached any shipped
    code. Also found and confirmed independently: `FBlock` (the Wrath+
    particle color/alpha/scale/UV curve shape) timestamps are `uint16_t`,
    not the `uint32_t` a real `M2Track` uses — matches the wiki's own "the
    timestamps are shorts" text and decodes to a clean monotonic
    `0..0x7FFF` run against real bytes; interpreted as likely a normalized
    lifetime fraction (hypothesis, not confirmed against an authoritative
    source) but exposed raw regardless, per this project's own "don't
    guess at semantics" discipline.
  - **New shared infrastructure** (`src/m2.hpp`/`m2.cpp`): `readU8`,
    `resolveFloatTrackSequence`/`resolveFloatGlobalSequenceTrack` (a real
    named function, not another hand-duplicated Vec3/Quat-style copy —
    `M2Particle` alone has ~10 real `M2Track<float>` fields, past this
    codebase's own "third occurrence earns an abstraction" bar),
    `resolveRawIntTrackSequence`/`resolveRawIntGlobalSequenceTrack`
    (`elementSize`-as-runtime-parameter, matching `checkInnerArrayFits`'s
    own existing style, for the lower-occurrence uint8_t/uint16_t/fixed16
    cases that didn't individually earn a named function), and
    `FBlockMeta`/`resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16` (a
    private templated `resolveFBlockGeneric` helper backing all four —
    the one place this session used a template, since the alternative was
    four near-identical hand-copies of a flat, no-indirection curve
    reader, more duplication than even this codebase's own
    duplication-tolerant style usually accepts).
  - **`m2::Ribbon`/`m2::ParticleEmitter` (`src/m2.hpp`)**: Ribbon gained
    `textureIndices`/`materialIndices` lookup arrays, 6 new track-offset
    fields, and the Wrath+ trailing `priorityPlane`/`ribbonColorIndex`/
    `textureTransformLookupIndex`. `ParticleEmitter` is new outright —
    every Cata+-shape field, ~30 in total, gated to a new
    `kMinVerifiedParticleVersion = 272` (same "verified floor, warn below
    it" policy `kMinVerifiedRecordStrideVersion` already established for
    Bone/Sequence/Ribbon, just a newer floor since `M2Particle`'s own byte
    layout genuinely changed at Cataclysm, unlike those three).
  - **`src/cmd_info.cpp`**: ribbon printout extended (track/lookup
    counts); new particle one-line-per-emitter summary, gated the same
    way, with a loud warning (matching the existing below-Wrath one)
    for real `particle_emitters` data below Cataclysm.
  - **`src/cmd_dump.cpp`**: `dumpEmitters` (+ `writeRibbon`/`writeParticle`/
    `writeTrackCurve`/`writeFloatTrack`/`writeVec3Track`/`writeRawIntTrack`/
    `writeFBlockCurve`) — full JSON, written unconditionally (before the
    existing `header.chunked` early-return, which now only gates the
    Legion+ chunk-tag section, not the whole command) so pre-Legion flat
    files still get real `ribbon_emitters`/`particle_emitters` output.
  - **`src/gltf.hpp`/`gltf.cpp`**: `Skeleton::EmitterAnchor` (one shared
    struct for both `ribbonAnchors`/`particleAnchors` — structurally
    identical, so not duplicated into two types) serialized as
    `ribbon_emitters`/`particle_emitters` keys on the same `skinExtras`
    object `bone_correction_sets` already uses (verified all three
    coexist without clobbering); `writeGlbMulti` gained the matching
    out-of-range-joint validation `correctionSets` already had.
  - **`src/cmd_export.cpp`**: unconditional (no new CLI flag, unlike
    `--bones-dir`) — builds ribbon/particle anchors right after the
    `--bones-dir` block, reusing the already-parsed `header`/`blob`, with
    a real bug caught by the compiler, not by inspection: the first
    attempt aggregate-initialized `EmitterAnchor` directly from
    `m2::Vec3`/`m2::Ribbon::position` without the existing `toGltf()`
    conversion helper — `m2::Vec3` and `gltf::Vec3` are distinct
    aggregate types (no implicit conversion), so it failed to compile
    rather than silently skipping the Z-up→Y-up remap every other
    exported position already goes through. Fixed by using `toGltf()`,
    same as every other position in this pipeline.
  - **Tests**: 337 → 376 cases. New `test_m2.cpp` cases for every new
    resolver (`resolveFloatTrackSequence`/`resolveRawIntTrackSequence`/
    `resolveFBlockVec3`/`Vec2`/`Fixed16`/`Uint16`, global-sequence variants,
    bounds-checking throws), `parseRibbons`'s new fields, and
    `parseParticles` (happy path, extra fields, empty, out-of-bounds).
    New `test_gltf.cpp` cases for `EmitterAnchor` round-trip (present/
    absent/out-of-range-throws/coexists-with-correctionSets). New
    `test_dump.cpp` cases for the JSON output shape, plus a real-byte-
    offset synthetic fixture on a flat (non-chunked) file proving
    `ribbon_emitters`/`particle_emitters` aren't chunk-gated. New
    `test_cli.cpp` cases for the `kMinVerifiedParticleVersion` warning.
    New `test_integration.cpp` real-data cases (`doctest::skip`-gated on
    the new weapon fixtures): exact ribbon/particle anchor counts against
    all four real files via tinygltf, plus a `dump-chunks` NaN/finite
    sanity check on the 64-particle stress file. Both `./build/husk-tests`
    (376/376, 1 permanently-inapplicable skip) and `ctest` (377/377) green.
  - **Verification discipline**: a `gltf_validator` sweep across all four
    real exports found one pre-existing "Joints do not have a common
    root" error on two of the four weapon models — confirmed via
    `git stash`/rebuild/re-export against the unmodified baseline that
    this predates the session entirely (this session's diff never touches
    joint-parent assignment), then `git stash pop`/rebuilt/re-verified
    376/376 green before continuing, rather than assuming.
  - **Docs**: `README.md` (format matrix row rewritten from 🚧 to 📖, `husk
    info`/`dump-chunks` usage sections, a new roadmap-stage-6 paragraph),
    `M2_COMPLETENESS.md` (Ribbons/Particles rows), `DESIGN.md` (new Key
    design decisions bullet on the anchor/full-data split and why,
    Boundaries/data-flow bullets, the flag-gating table), `WIKI_FINDINGS.md`
    (new §6: the offset derivation, the `FBlock`-`uint16_t`-timestamp
    finding, the real bug an early verification script had),
    `TODO/TODO_correctness.md` (former item 1, particles, removed outright per
    this file's own "fixed items get removed" convention — not marked
    `[Fixed]` — remaining items renumbered 2-5 → 1-4, every
    `TODO/TODO_correctness.md #N` cross-reference across `src/`/`tests/`
    grep-verified and updated to match, same careful-renumbering
    precedent the AFSB removal already set).
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>`, scripts written to files in the scratchpad rather
    than passed inline (`python3 -c ...`), per explicit instruction this
    session — inline `-c` invocations otherwise prompt for confirmation
    on every single iteration, which adds up fast during real-data
    byte-level verification work like this session's offset derivation.
- **Previous state**: `VERIFICATION_IDEAS.md`'s survey (source-M2-counts vs.
  exported-glb vs. Blender-readback cross-checks) went from "none of this
  is implemented" to cases 1/2/3/5 all real, in exactly the file's own
  triviality-ranked order (case 4 stayed deliberately skipped, per its own
  reasoning). Requested as "implement the rest of the verification ideas
  findings, in order of triviality." Once every case had a final
  disposition, `VERIFICATION_IDEAS.md` was deleted outright in a same-session
  follow-up (initially left in place with `[IMPLEMENTED]` tags and
  duplicated writeups — a real inconsistency with this project's own
  stated punch-list convention, caught by Luna asking "did you update it
  according to that?" rather than caught proactively) — its survey's job
  (decide what to build) was complete, every real fact already lived in
  its permanent home (`tests/test_conformance.cpp` comments,
  `WIKI_FINDINGS.md` §5, `README.md`, `DESIGN.md`, `M2_COMPLETENESS.md`),
  exactly the situation `DESIGN_CHANGES.md` was in when *it* got deleted.
  Every cross-reference to the file (source comments included, not just
  docs) got repointed rather than left dangling.
  - **Case 1 (vertex count) + case 2 (bone count)**: exactly as
    scoped — two `CHECK`s added to `tests/test_conformance.cpp`'s existing
    Blender `TEST_CASE`, comparing `m2::parseHeader(...)`'s own
    `vertices.count`/`bones.count` against Blender's/tinygltf's readback.
    Getting these *exact* (not `> 0`) surfaced a real, previously-invisible
    contamination bug in `tests/blender_import_check.py`: Blender's
    `--factory-startup` scene (default Cube/Camera/Light) survives into the
    probe unless cleared first, and `bpy.ops.import_scene.gltf`'s own
    `armature_display()` creates a real 42-vertex Icosphere mesh object per
    armature import (a bone custom-shape widget, parked in a hidden
    collection but still a real `bpy.data.objects` entry) unless
    `disable_bone_shape=True` is passed — found by writing the exact-match
    assertion and getting `8111 == 8061` instead of a pass, not by
    inspection. Both fixed in the probe script before either `CHECK` could
    hold.
  - **Case 3 (bounding box)**: the file's own "tolerance match" premise
    was wrong, found by actually computing both sides against real data
    (both `bloodelffemale.m2` and `bloodelffemale_hd.m2`) before writing
    the assertion rather than after — the header's `bounding_box` runs
    roughly 2x–4x the bind-pose mesh's own extent per axis, consistent
    with it covering the model's full *animated* range rather than a tight
    rest-pose fit (documented as a hypothesis, not confirmed against an
    authoritative source — `WIKI_FINDINGS.md` §5, new). Shipped the
    corrected, still tolerance-free invariant instead: the bind-pose
    mesh's own AABB is fully *contained* inside the header's box, per
    axis, after the same Z-up→Y-up remap (`transformedM2BoundingBox`,
    remapping all 8 corners — the axis swap negates one component, so
    naively pairing `zUpToYUp(min)`/`zUpToYUp(max)` would silently produce
    an inside-out box on that axis). Verified the check itself actually
    catches a regression, not just passing vacuously: temporarily
    perturbed `cmd_export.cpp`'s `toGltf` by +50 units on X, confirmed the
    new `TEST_CASE` fails with the exact expected numbers, reverted.
  - **Case 5 (collision mesh)**: the biggest piece — collision data used
    to be `Array` descriptors only (`husk info` counts, nothing
    dereferenced). New `m2::parseVec3Array`/`m2::parseCollisionMesh`
    (`src/m2.hpp`/`m2.cpp`, unit-tested in `tests/test_m2.cpp`) dereference
    `collisionPositions`/`collisionIndices`/`collisionFaceNormals` into
    real data; `cmd_export.cpp` writes it as one more `gltf::NamedMesh`
    (positions via the existing `toGltf`; per-vertex normals *approximated*
    by averaging each vertex's adjacent face normals, since the source is
    one normal per triangle, not per vertex — acceptable since a collision
    mesh isn't shaded, this only satisfies `gltf::Mesh`'s own same-length
    invariant with real data), tagged via new `gltf::NamedMesh::isCollision`
    → `{"collision": true}` in that node's glTF `extras`. Real, unambiguous
    glTF translation (unlike geoset selection/`.bone` corrections/texture
    transforms, which stay `extras`-only because no such translation
    exists) — the geometry itself is native, only the "don't draw this"
    purpose tag is `extras`.
    - **One real API relaxation this forced**: `gltf::writeGlbMulti`
      previously required *every* `NamedMesh` entry to be skinned whenever
      any shared skeleton was in scope (`hasSkeleton && mesh.skinning
      .size() != n` → unconditional `Error`) — too strict for an unskinned
      collision mesh sharing a skinned render mesh's armature. Now each
      entry independently opts in (non-empty, matching-length
      `mesh.skinning`) or out (empty — no glTF `skin` reference on that
      node, not deformed by the armature); the real error case (skinning
      *present* but the wrong length) still throws. Two existing
      `tests/test_gltf.cpp` cases whose whole premise was "mixed
      skinned/unskinned entries must throw" got rewritten (their premise
      is now the supported case) rather than deleted, plus two new cases
      proving both the new positive path and that the real error case
      still fires.
    - `tests/blender_import_check.py` gained `collision_mesh_count`/
      `collision_mesh_vertex_count`/`collision_mesh_triangle_count` probes
      (found via the `collision` extras tag, not by name), checked exactly
      against `header.collisionPositions.count`/`header.collisionIndices
      .count / 3` in `test_conformance.cpp` — small enough (8 positions,
      12 triangles for the real fixture) that exact match is realistic,
      no tolerance needed, pure count/topology.
    - **One real regression this surfaced and fixed in the same pass**:
      `cmd_export.cpp`'s own "N LOD tier(s)" summary print used to key off
      `namedMeshes.size()` directly — with a collision mesh now always
      appended when present, that over-counted by one and would have
      mislabeled it as another LOD tier. Fixed by capturing
      `renderMeshCount` before the collision entry is appended, used for
      both the branch decision and the per-entry print loop.
  - **Case 4 (sequences)**: left alone, exactly as the file's own
    reasoning says — the metric needs to change shape (a resolved/
    skipped/aliased breakdown) before a comparison would mean anything,
    not a suspected bug.
  - **Verification discipline throughout**: every premise got checked
    against real data (`bloodelffemale.m2`/`bloodelffemale_hd.m2`) before
    being written into an assertion, not assumed from the survey doc's own
    text — this is what caught case 3's wrong premise and case 1/2's
    Blender-importer contamination, both invisible from reading code alone.
  - **Tests**: 338 → 345 cases (5 in `test_m2.cpp` for
    `parseVec3Array`/`parseCollisionMesh`, 1 new `test_conformance.cpp`
    bounding-box `TEST_CASE`, 1 new `test_gltf.cpp` mixed-skinning case;
    2 more `test_gltf.cpp` cases rewrote their premise without changing
    count). Both `./build/husk-tests` and `ctest` green (346 total
    including 1 permanently-inapplicable skip).
  - **Docs**: `VERIFICATION_IDEAS.md` deleted outright once every case had
    a final disposition (see this entry's own opening paragraph for why —
    the punch-list convention this repo already uses for
    `TODO/TODO_correctness.md`/`DESIGN_CHANGES.md`, not additive `[IMPLEMENTED]`
    tags), its content folded into: `WIKI_FINDINGS.md` (new §5, the
    bounding-box-isn't-tight finding, tagged hypothesis-confidence since
    the *why* isn't confirmed against an authoritative source), `README.md`
    (Collision/physics format-matrix row bumped from 🚧 to 📖, Testing
    section's Conformance paragraph rewritten), `DESIGN.md` (new Key
    design decisions bullet for the collision-mesh/`writeGlbMulti`
    relaxation, Testing architecture section gained the previously-missing
    4th "Conformance" tier), `M2_COMPLETENESS.md` (Collision & physics
    rows bumped to `full`/`native`/`native — 100%`), and self-contained
    comments in `tests/test_conformance.cpp`/`tests/blender_import_check.py`/
    `src/cmd_export.cpp` (every one of those files' comments used to point
    at `VERIFICATION_IDEAS.md` by name — all repointed rather than left
    dangling once the file was gone).
  - **Environment note, reconfirmed**: `direnv exec . uv run --no-project
    python3 <script>` for ad hoc byte-level scratch analysis (this
    session's minimal-glTF/Blender-object-introspection scripts lived in
    the scratchpad, not committed) — used this session to isolate the
    Blender Icosphere/Cube contamination down to its exact source
    (`io_scene_gltf2/blender/imp/node.py`'s `armature_display()`) before
    trusting the fix, not just patching around the symptom.
- **Previous state**: `TODO/TODO_correctness.md`'s former #1 — `.skel`-sourced
  external `.anim` files' undocumented `AFSB` chunk shape, the single
  biggest remaining animation gap (essentially 0% external-animation
  coverage for any modern character model) — is now **cracked and fully
  resolved**, not just detected-and-skipped. Session ran autonomously
  overnight per explicit standing permission (read-only web search
  pre-approved; no new flake packages, since no one was available to
  approve them) picking up right after the `--bones-dir` work above.
  - **Prior-art search first, properly exhausted before guessing.**
    `WebSearch`/`WebFetch` against wowdev.wiki (direct fetches 403 — same
    bot-blocking this project already knew about, no local proxy available
    this session), GitHub code search, `warcraft-rs`/`wow.export`/
    `WoWDBDefs` repos, and a couple of WoW-modding forums. Found only a
    *semantic* confirmation (wowdev.wiki's own indexed summary: `AFSA` =
    attachment animation, `AFSB` = bone animation) — no byte-level struct
    anywhere reachable. Moved to from-scratch analysis once that was
    genuinely dry, not before.
  - **The crack, in one sentence: `AFSB` isn't a new format at all.** A
    full 104-file chunk survey of `bloodelffemale_hd_*.anim` (correcting an
    earlier claim in `WIKI_FINDINGS.md` §2 that `AFM2`'s stub is always 64
    bytes — it's actually 16–1344, always a multiple of 16) found `AFSB`'s
    first bytes are a clean, monotonic keyframe-timestamp run (0 up to the
    sequence's own `duration`, in ms) — not the "per-bone offset table" an
    earlier shallow peek guessed. Cross-referencing `bloodelffemale_hd.skel`'s
    own `SKB1` bone records against the real `SKS1` sequence array (mapping
    each `.anim` filename's `<animId>-<subId>` to its `SKS1` position) found
    that `src/m2.hpp`'s own doc-comment claim — "every M2Track [a `.skel`
    bone points at] is expected to be genuinely empty" for external
    sequences — is simply wrong: **211 of 245 real bones have non-zero
    per-sequence `(count, offset)` tuples**, and for real bone/sequence
    pairs, that `offset` lands *exactly* on a clean timestamp run inside
    that sequence's own `.anim` file's `AFSB` payload. `husk::m2::
    trackSequenceInnerArrays`/`resolveVec3TrackSequence`/
    `resolveQuatTrackSequence` (unchanged, existing code — the same
    mechanism already used for `AFM2`-external files via their
    `externalDataBlob` parameter) needed zero new parsing logic; the value
    region past each timestamp run (byte length padded to the next multiple
    of 16, confirmed by the next track's offset always starting exactly
    there) decodes as a raw 12-byte `C3Vector` (translation/scale) or the
    existing 8-byte `M2CompQuat` decoder (rotation) — every decoded
    rotation quaternion comes out unit-length to 4 decimal places, every
    translation curve smooth and finite.
  - **Verified three independent ways**, not just "the numbers looked
    consistent": (1) a full self-consistency sweep across all 54
    non-`_sdr` `bloodelffemale_hd*.anim` files (every bone × every track ×
    its own matching sequence) found zero bounds/monotonicity/finiteness
    problems; (2) `husk export` itself, pointed at the real `--anim`
    directory, now reports **336 real animation clips** for
    `bloodelffemale_hd.m2` (up from whatever inline/global-sequence-only
    count was possible before); (3) the Khronos `gltf_validator` reports
    zero *new* errors on that export (the fixture's own pre-existing,
    unrelated `JOINTS_0` duplicate-value issue is identical with or
    without `--anim`, confirmed by diffing against a `--anim`-less
    export); (4) Blender's own glTF importer, run headlessly the same way
    `test_conformance.cpp` already does, independently reports **336
    actions** — an exact match from a completely separate glTF
    implementation.
  - **Code change was small and surgical, given how much existing code
    already generalized correctly**: `src/cmd_export.cpp`'s
    `buildAnimations` — the `AFSB`-peek branch that used to `continue`
    (skip) now extracts `AFSB`'s own chunk payload directly as
    `externalDataBlob` (taking priority whenever both `AFM2` and `AFSB` are
    present, since `AFM2`'s stub still isn't real data — confirmed via the
    same "claims more keyframes than this blob holds" bounds error a prior
    session already found); the `AFM2`-only and neither-chunk-present
    branches are otherwise unchanged. No changes at all to `src/m2.cpp`'s
    resolution functions.
  - **Tests**: rewrote the two `test_cli.cpp` cases that used to assert
    "`AFSB` present → no animation clip" (that assertion is now false) into
    cases asserting a real clip *is* produced — one plain `AFSB`-only file,
    one with a genuine `AFM2` stub alongside real `AFSB` data (proving
    priority) — plus a new third case for the one remaining skip path
    (neither `AFM2` nor `AFSB` present, an unrecognized future shape).
    New `tests/test_integration.cpp` case (`HUSK_TEST_ANIM_DIR`-gated, new
    env var + `testAnimDir()`/banner line, defaults to the same directory
    the `.skel` fixtures already live in since that's where the real
    `.anim` files sit) runs the real 104-file corpus end to end and checks
    every decoded rotation/translation keyframe via tinygltf — asserts a
    conservative `> 100` clip lower bound, not the exact 336, so it doesn't
    become a silent tripwire if the fixture set changes slightly. Full
    suite: 337 → 337 cases (no new cases needed beyond the 3 rewritten + 1
    real-data one — the fix reuses existing resolution machinery, not new
    surface), but assertion count went 368,997 → 7,164,311 (the new
    real-data test checks every keyframe across all 336 clips). Both
    `./build/husk-tests` and `ctest` green.
  - **Docs**: `WIKI_FINDINGS.md` §2 rewritten with the corrected `AFM2`-size
    claim and a full "Follow-up: cracked" section (the receipts above, in
    more detail); `TODO/TODO_correctness.md`'s former item 1 removed outright
    (per this file's own "fixed items get removed, not marked `[Fixed]`"
    convention) and items 2-6 renumbered to 1-5 — a deliberate exception to
    "don't renumber, it touches live code strings," done carefully with a
    full grep-verified sweep across every `TODO/TODO_correctness.md #N`
    reference in `src/`/`tests/` (bone-corrections references moved
    `#6`→`#5`); `README.md` (Usage section's `.anim`/roadmap-stage-6
    prose, format matrix row, Testing-architecture paragraph — the
    "there's no repeatable real-file `.anim` test, a real gap" line was
    itself stale after this session and got corrected), `DESIGN.md` (Key
    design decisions entry rewritten from "skipped outright" to "resolved,
    here's how," Boundaries list, Open-work pointer).
  - **Environment note, reconfirmed**: same `uv run --no-project python3`
    pattern as prior sessions for ad hoc byte-level scratch analysis (this
    session's scripts lived in the scratchpad, not committed); `nu` used
    for a couple of quick chunk-offset dumps needing no Python at all.
- **Previous state**: `TODO/TODO_correctness.md` #6's extras-export half is now
  implemented — real `.bone` correction data attaches to `husk export`'s
  glTF output as inert `extras`, never applied to the render. New
  `husk export --bones-dir <dir>` flag (three-state, same shape as
  `--textures`/`--skin-dir`): resolves every FileDataID the model's/
  `.skel`'s `BFID` array declares to a real `<dir>/<FileDataID>.bone` file
  (silently skipping any that don't resolve, same policy `--textures`
  already uses for a missing PNG), parses each with the existing
  `husk::bone::parse`, and attaches every resolved slot as a
  `bone_correction_sets` key on the glTF **skin**'s own `extras` — one
  entry per `.bone` file, each a `(file_data_id, [{joint, matrix}, ...])`
  record. Deliberately *not* applied to the bind pose or any animation:
  which slot is "correct" for a given character is external,
  client-side customization-choice data husk still doesn't have (this
  session's own prior investigation, see Previous state below, and
  `WIKI_FINDINGS.md` §4/`TODO/TODO_correctness.md` #6) — same "tag it, don't
  guess at semantics" treatment as geoset selection/`textureTransform`.
  Went through a full plan-mode design pass before implementation, given
  the CLI-grammar/parsing-pipeline/glTF-schema surface touched; the
  approved plan is what got built, no deviations. All verified: clean
  rebuild, full 335-case `husk-tests` suite green via both
  `./build/husk-tests` and `ctest` (up from 324), plus a real
  `bloodelffemale_hd.m2`/`.skel` export re-checked by hand (20/20
  `.bone` slots attached, round-tripped through `gltf_validator` — the
  1.7M pre-existing `JOINTS_0` duplicate-value errors on that specific
  fixture are unrelated, confirmed identical with `--bones-dir none`,
  not a regression from this work).
  - **`src/skel.hpp`/`skel.cpp`**: new `findBoneFileDataIds` reads
    `.skel`'s own `BFID` chunk (previously explicitly out of scope, per
    this file's own doc comment) — same flat-`uint32`-array shape as an
    M2's own `BFID`, duplicated locally rather than shared from `m2.cpp`'s
    anonymous-namespace helper, matching this file's existing
    `findAnimFileIds` precedent (same "small parser helper, one per
    translation unit" pattern already established here, not a new one).
  - **`src/gltf.hpp`/`gltf.cpp`**: new `gltf::Skeleton::CorrectionSet`
    (`fileDataId` + `vector<{joint, matrix[16]}>`) as a field on
    `Skeleton` alongside `joints`; `writeGlbMulti` validates every
    correction's `joint` is in range (same `Error` shape as the existing
    parent-range check) and serializes non-empty `correctionSets` into
    `skin.extras["bone_correction_sets"]`, nested `tinygltf::Value`
    construction following the exact existing pattern
    `additional_textures`/`texture_transform` material extras already use.
  - **`src/commands.hpp`/`src/cmd_export.cpp`**: `ExportOptions::
    bonesDirArg` + `--bones-dir` registered in `addExportOptions` (so the
    completion generator picks it up automatically); resolution/
    attachment logic sits right after the existing skeleton-building
    block, keyed off the same `bonesAreInline`/`haveSkel` branch already
    used for choosing inline-vs-`.skel` bones/animation elsewhere in this
    function. Prints a summary note (`attached N/M '.bone' correction
    set(s)...`) only when `N > 0` — silent otherwise, matching
    `--textures`'s existing "quiet when nothing applies" behavior.
  - **`src/main.cpp`**: the hand-maintained bash/zsh completion-generator
    tables (`bashValueCompletion`/`zshValueAction`/`zshFlagLabel` — these
    are *not* derived from CLI11 introspection alone, a real gap this
    session had to discover by testing the regenerated completion
    function directly rather than assuming the flag-table change alone
    was sufficient) needed `--bones-dir` added explicitly, same
    `none`-plus-directory treatment as `--textures`/`--skin-dir`.
    `completions/husk.bash`/`.zsh` regenerated and functionally verified
    the same way prior sessions did (sourcing the script, driving
    `_husk_completions` with scripted `COMP_WORDS`/`COMP_CWORD` — confirmed
    `--bones-dir` offers `none` + real directories, not plain filenames).
  - **Tests**: `tests/test_skel.cpp` (`findBoneFileDataIds`: found/absent/
    malformed-length-throws, mirroring `findAnimFileIds`'s existing
    cases), `tests/test_gltf.cpp` (3 new cases: `correctionSets` round-trip
    as skin extras, no-`correctionSets`-means-no-key, out-of-range joint
    throws — same style as the existing billboard/geoset/textureTransform
    extras tests), `tests/test_cli.cpp` (4 new cases: explicit
    `--bones-dir` attaches + notes, `--bones-dir none` suppresses, unset
    defaults to the model's own directory, an out-of-range `.bone`
    correction fails the export naming the file/bone index — new local
    `buildBoneFile`/`boneCorrectionSkel` fixture helpers), `tests/
    test_data_paths.hpp`+`test_integration.cpp` (new `autoBonesDir`
    mirroring `autoSkinDir`: reads real `BFID` entries out of
    `bloodelffemale_hd.skel` and copies a few of this repo's own real
    `.bone` fixtures under those FileDataIDs — comment notes the NN→
    `BFID[NN]` positional assignment is arbitrary for test purposes, not a
    claimed real mapping; one new `doctest::skip`-gated real-data
    `TEST_CASE` checks the produced `.glb`'s skin extras directly via
    tinygltf). `test_main.cpp`'s startup banner gained a
    `HUSK_TEST_BONES_DIR` line.
  - **Environment note carried over, reconfirmed**: bare `python`/`python3`
    is still guarded off even under `direnv exec .`/`nix develop ./nix -c`
    — `direnv exec . uv run --no-project python3 <script>` is the
    sanctioned path for ad hoc Python in this repo (used again this
    session for the real-file `--bones-dir` smoke test), `nu` remains fine
    for direct byte-level work with no `uv` involved at all.
  - **Docs updated**: `README.md` (`.bone` corrections paragraph + flag
    table row + defaults/`none` lists + sidecar-resolution format-matrix
    row), `DESIGN.md` (CLI grammar table + three-state section + a new
    Key design decisions bullet matching the geoset/texture-transform
    precedent + a Non-goals clarifying sentence: an out-of-band
    CASC/DB2-scraping build tool is fine, husk itself talking to CASC at
    runtime never is), `TODO/TODO_correctness.md` #6 (extras-export marked
    done, remaining gap reframed as "external lookup, not more
    investigation"), `M2_COMPLETENESS.md` (`.bone` row + the sidecar
    FileDataID-resolution rows), `WIKI_FINDINGS.md` §4 (added the
    previously chat-only weapon-type/armor-type ruling-out finding — the
    corrected bones cluster on Head/Jaw, not hand/wrist — since
    `TODO/TODO_correctness.md` #6 now cites it as an established fact and it
    needs real receipts backing it, not just a claim).
- **Earlier state** (condensed — full detail in git history/`WIKI_FINDINGS.md`/
  `DESIGN.md`/`README.md`, which all already captured the durable facts):
  a `.bone`-slot-selection investigation ruled out the LOD/render-distance
  hypothesis by real data (20 `.bone` slots don't fit a 7-tier LOD count,
  collapse into only 5 distinct bone-index sets with heavy exact
  duplication) — the real selector is external client-side DB2 data husk
  has no access to, per `DESIGN.md`'s non-goals (`WIKI_FINDINGS.md` §4,
  `TODO/TODO_correctness.md` #5). Earlier still, `export`'s CLI grammar
  migrated from a positional parser to named CLI11 flags (a breaking
  change to every invocation's argument order, done in one deliberate
  pass) — CLI11 added as a new flake dependency with sign-off,
  `addExportOptions` became the one place the flag surface is declared
  (shared by real parsing and the `--print-completion` generator), and
  `--skin`/`--textures`/`--skin-dir`/`--anim`/`--skel` got the
  three/four-state (`auto`/explicit/`none`) treatment `DESIGN.md`'s CLI
  grammar section still documents in full.
