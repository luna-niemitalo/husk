# ENGINE_TODO — external data a consuming engine still needs to source

**This file is not part of husk.** It documents, for whatever renders husk's
`.glb`/`dump-chunks` output next (a custom engine project, not husk itself),
the specific gaps between "husk exported everything the M2 file family
contains" and "looks/plays 1:1 with the original WoW client." Every item
below is data that genuinely does not exist in `.m2`/`.skin`/`.skel`/`.bone`/
`.phys` — it lives in client-side DB2/DBC tables or in client code, neither
of which husk will ever touch (`../DESIGN.md`'s Non-goals: no CASC/DB2 access,
ever, by design, not oversight). Closing these gaps is **not husk's job** —
this file is a spec for the engine project to work from, not a punch list
husk itself will implement. If husk's own coverage of the M2 format changes
(new fields exposed, new extras keys), this file may go stale — cross-check
against husk's own `../M2_COMPLETENESS.md`/`TODO_correctness.md` before relying
on the "what husk gives you" column below.

## How to read each entry

- **husk gives you** — the exact glTF/`dump-chunks` field to read, as of the
  husk version this was written against (commit `1b4b5ef`, see husk's own
  git history for anything newer). This file was refreshed once already,
  against commit `aa0df15` — items 3/4/5/6/7 had drifted from husk's actual
  coverage by then (husk had grown real extras/nodes for data this file
  still described as unparsed or unexported); see each item's own text for
  what changed. The external-data gaps themselves are unaffected — only the
  "what husk already gives you" framing was stale.
- **missing** — what a full reproduction still needs.
- **likely source** — where that data plausibly lives, with an honest
  confidence level; several of these are unconfirmed guesses, not verified
  DB2 schema.
- **resolution path** — what the engine project would actually have to build
  or acquire.

---

## 1. Geoset selection

Character models export every mutually-exclusive submesh variant in the
`.skin` (e.g. every hairstyle, every facial-hair option) with no filtering —
husk has no basis to pick one.

- **husk gives you**: every primitive's node `extras` carries `geoset_id`
  (the real `M2SkinSection.skinSectionId`), plus `geoset_group` (`id / 100`)
  and `geoset_variant` (`id % 100`) for convenience.
- **missing**: which geoset ID is "on" for a given character-customization
  choice (hairstyle N, facial hair N, ears on/off, etc.).
- **likely source** (unconfirmed): a `ChrCustomizationGeoset`-shaped DB2
  table mapping `chrCustomizationOptionID`/`chrCustomizationChoiceID` →
  geoset group/id to enable. Older expansions may use per-feature tables
  instead (`CharacterFacialHairStyles`, etc.) — schema not verified against
  a real dump, this is a plausible guess from general WoW-modding knowledge,
  not something husk's own investigation confirmed.
- **resolution path**: a DB2/DBC reader (out of scope for husk, in scope for
  the engine or a separate build-time tool) pulling the relevant
  customization tables, producing a per-character-config allow-list of
  `geoset_id` values to filter primitives by — either at asset-import time
  or per-frame at render time.

## 2. `.bone` correction-set selection

Multiple `.bone` files can apply to one model (its `BFID` array); husk
attaches every one it can resolve, unapplied.

- **husk gives you**: the exported `.glb` skin's `extras["bone_correction_sets"]`
  — one entry per resolved `.bone` file, each `{file_data_id, corrections:
  [{joint, matrix}, ...]}`. Full per-file detail also available via
  `husk dump-chunks <file.bone>` directly.
- **missing**: which `BFID`-array slot applies in which context.
- **What's already ruled out** (husk's own real-data investigation,
  `../WIKI_FINDINGS.md` §4): not LOD/render-distance (20 real `.bone` files
  don't fit `LDV1`'s 7-tier LOD count at all), not weapon-type or
  armor-type (corrected bones cluster tightly on Head/Jaw, nowhere near
  hand/grip/armor-fitting bones). The corrections collapse into a small
  number of distinct bone-index sets with heavy exact duplication across
  files — consistent with a small number of shape variants reused across
  many selectable slots.
- **likely source** (unconfirmed, lower confidence than item 1): plausibly a
  `ChrCustomizationBoneSet`-shaped DB2 table, going from general knowledge
  of WoW's customization system — not confirmed against a real DB2 dump by
  husk's own investigation.
- **resolution path**: real DB2 investigation needed (find the actual table,
  confirm the mapping) before this can be applied at all. Until then, safe
  fallback is identity (no correction applied) or a manual per-model
  override exposed to whoever's using the engine.

## 3. Hardcoded / replaceable texture resolution

Character skin tone, hair/fur color, tattoos, and item-tint-driven equipment
textures are not filenames in the M2 at all.

- **husk gives you**: `husk info` prints each texture's raw `type` field
  (0 = filename-based, non-zero = hardcoded/replaceable slot) and `flags`;
  `husk export` now surfaces the same signal in the `.glb` too — a nonzero
  `M2Texture::type` is attached as `texture_type` material `extras`
  (`gltf::Material::textureType`, `src/gltf_mesh.hpp`; set in
  `src/export_materials.cpp`, only present when nonzero, per
  `m2::textureTypeName`), so a material with no `baseColorImagePng` reads
  as "husk can't resolve this locally" rather than "the `--textures`
  directory just didn't have the file." husk also builds a real, typed
  `alternate_textures` candidate pool per ambiguous slot
  (`src/gltf_mesh.hpp`'s `AlternateTextureCandidate`, filtered by a
  filename-category heuristic in `candidateAllowedForType`/
  `filterCandidatesForType`) — still a same-basename-pool guess, not real
  DB2-driven selection, but real per-candidate metadata (width/height,
  parsed category) beyond what this item originally described.
- **missing**: the actual pixels for every hardcoded slot, and the mapping
  from character-customization choice → which BLP/texture fills that slot.
- **likely source**: `CharComponentTextureLayouts`/`ItemDisplayInfo`-family
  DB2 tables (exact table names vary by expansion; not verified against a
  real dump here), plus the actual replacement BLPs, neither of which the
  M2/`.skin` file family carries.
- **resolution path**: a full character-customization + item-display DB2
  pipeline, well beyond "read this one model's files" — this is arguably
  the single largest genuinely-external subsystem on this whole list.

## 4. `aliasNext` / animation-id resolution against `AnimationData.dbc`

- **husk gives you**: `aliasNext` is fully parsed and resolved now, not raw
  and not unparsed (`m2::Sequence::aliasNext`, `src/m2_animation.hpp`/`.cpp`;
  chain-walked in `src/export_animation.cpp`'s alias-resolution path). husk's
  own real-data investigation (`WIKI_FINDINGS/M2.md`, summarized in
  `M2_COMPLETENESS.md`'s Animation table) found `aliasNext` *is* a local
  index into the same file's own `sequences` array after all — chain-walked
  to its terminal non-alias sequence, whose keyframe data (inline or
  external) is reused for the alias clip, with `flags & 0x20` ("stored
  inline") correctly taking priority over `0x40` ("is alias") when a
  sequence carries both (31 of 38 real alias sequences in
  `bloodelffemale_hd.skel` do). The resolved target is attached as
  `alias_next`/`is_alias` per-clip extras (`gltf::Animation::SequenceMetadata`,
  `src/gltf_skeleton.hpp`/`.cpp`) alongside raw `id`/`variationIndex`/
  `duration`/`flags`. This closes the "genuinely unresolved even from the
  file alone" half of the original claim entirely — per `TODO_correctness.md`
  ("Former item 4 ... is resolved outright"), there is no longer an open
  question about what `aliasNext` indexes into.
- **missing**: human-readable animation names — `AnimationData.dbc`'s whole
  remaining purpose for husk's own output. The alias-chain-resolution
  mechanism itself is no longer part of this gap (see above); only the
  id-to-name lookup is genuinely external.
- **likely source**: `AnimationData.dbc`/`.db2`, the client's global
  animation-id table.
- **resolution path**: a DB2 reader for this one table is comparatively
  cheap next to items 1-3 — purely for human-readable names and cross-model
  animation-id consistency now that the alias-chain mechanism itself is
  resolved husk-side and needs no external data.

## 5. `blendTimeOperation`

- **husk gives you**: `blendTimeIn`/`blendTimeOut` are fully parsed
  (`m2::Sequence::blendTimeIn`/`blendTimeOut`, `src/m2_animation.hpp`/`.cpp`)
  and exported as raw `uint16_t` millisecond values, `blend_time_in`/
  `blend_time_out` per-clip extras (`gltf::Animation::SequenceMetadata`,
  `src/gltf_skeleton.hpp`/`.cpp`; sourced in `src/export_animation.cpp`).
- **missing**: the rule for *when*/*how* to apply blend-in vs. blend-out
  during a transition between two sequences.
- **source**: none — the wowdev.wiki spec states this plainly: "not stored
  in files, but code and context dependent." This is not an external-data
  lookup problem like items 1-4, it's client *logic*, full stop. No DB2
  table, no M2 field, nothing to acquire.
- **resolution path**: there is no data to find. The engine has to author
  its own blend-transition heuristic — check prior art in other open-source
  WoW client reimplementations before inventing one from scratch, since
  this exact problem is common to every M2 consumer, not specific to husk's
  output.

## 6. Sound linking (`M2Event` → actual sound)

- **husk gives you**: real glTF nodes now, not diagnostic-only — every
  `M2Event` becomes a child node named `event_<identifier>` (e.g.
  `event_$DTH`, not deduplicated — a real M2 can repeat one identifier)
  parented under its bone, positioned at the event's own bone-relative
  offset (`gltf::Skeleton::Event`, `src/gltf_skeleton.hpp`; emitted in
  `src/gltf_skeleton.cpp`'s `appendAnchorNode` call, named via
  `m2::eventName` in `src/export_skeleton.cpp` where a known name exists).
  `gltf::Skeleton::Event` carries `identifier`/`joint`/`position` plus, as
  of this session, `data` too (`m2::Event::data`, `src/m2_scene.hpp`, an
  opaque per-event `uint32_t` payload -- wowdev.wiki documents only that it
  "is passed when the event is fired," not what it means -- now attached as
  a `data` extras key on that same node) — none of that is a sound file
  reference, and `data` didn't turn out to decode into one.
- **missing**: which sound plays when a given event fires.
- **likely source**: partly `SoundKit`-family DB2 tables keyed off the
  event's identifier convention (e.g. `"STAND"`/`"DEATH"`-style strings),
  partly hardcoded client logic per identifier — a genuine mix of "external
  data" and "external code," not a clean lookup in every case.
- **resolution path**: only matters if full audio parity is a goal, not
  visual/animation fidelity. Lower priority than items 1-4 for a
  visuals-first engine.

## 7. LOD distance thresholds

- **husk gives you**: `LDV1`'s lod tier count only — `husk info` prints
  `lod_count` (`m2::Header::lodCount`, read at `LDV1` offset 0x02,
  `src/m2_primitives.cpp`), and `husk export --lod all` exports every `.skin`
  tier as its own `lod<n>` node sharing one skeleton (doesn't pick one).
  (`particleBoneLod` is not a real husk field — no such data is parsed or
  exported anywhere in this codebase; that was a stale/incorrect claim in
  an earlier version of this file, not a real capability that regressed.)
- **missing**: the actual distance at which the client switches tiers.
- **source**: client CVars (`entityLodDist`, `doodadLodDist`) — these are
  **user-configurable settings, not asset data**. There is no "correct"
  value baked into any file to recover; even the original client doesn't
  have one fixed answer.
- **resolution path**: not a lookup problem at all — pick your own
  thresholds (or expose them as engine settings, matching the client's own
  design rather than trying to hardcode "the" WoW value).

---

## Priority, for whoever picks this up

Roughly in order of "how much visual/behavioral fidelity you get per unit of
effort," from husk's own vantage point (not tested against real engine
work):

1. **Geoset selection** (#1) and **hardcoded texture resolution** (#3) —
   without these, character models render with every customization option
   simultaneously visible and missing skin/hair textures entirely. Highest
   visual impact, most well-understood DB2 shape (general WoW-modding
   knowledge, if unconfirmed here specifically).
2. **`.bone` correction selection** (#2) — lower visual impact than 1/3 (subtle
   pose corrections, not "wrong body parts visible"), and husk's own
   investigation already narrowed the search space (rules out several
   plausible hypotheses) even though the actual table is still unconfirmed.
3. **`aliasNext`/animation names** (#4) — purely a usability/naming problem
   now, not a visual-correctness one at all: husk's own alias-chain
   resolution already reaches every alias sequence's real keyframe data
   without external help (see item 4's own text), so all that's left here
   is human-readable names.
4. **`blendTimeOperation`** (#5) — no data exists to find; this is "author a
   reasonable heuristic," not "go acquire a table." Worth doing early
   anyway since it's pure engine-side work with no external blocker.
5. **Sound linking** (#6) and **LOD thresholds** (#7) — lowest priority for
   a visuals-first engine; #7 in particular isn't even a "missing data"
   problem, just a design decision to make.
