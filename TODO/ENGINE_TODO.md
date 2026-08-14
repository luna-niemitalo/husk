# ENGINE_TODO — external data gaps, and which are actually husk's to close

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

**Correction (2026-08-14): this file's entire original framing was stale
and wrong, not just individual items.** Every item below used to be
introduced as "genuinely external, husk will never touch this, per
`../DESIGN.md`'s Non-goals: no CASC/DB2 access, ever, by design" — but
`../DESIGN.md`'s own Non-goals section says explicitly that this exact
wording predates a 2026-08-08 clarification and "should be read as
superseded wherever it's quoted or paraphrased elsewhere in this file."
The real rule: husk never talks to *live* CASC/DB2 at runtime, and never
depends on the CASC tool itself — but a `.db2` file already extracted to
local disk (the same `casc-tool`-populated tree `--textures`/`--skin-dir`
already read from) is the same tier as any other sidecar, and parsing it
locally is in scope. Real infrastructure for this already exists and is
used elsewhere in this project: `src/db2.hpp`/`.cpp` (WDC5 parser),
`src/dbd.hpp`/`.cpp` (WoWDBDefs column naming), `src/db2table.hpp`/`.cpp`
(generic named-column reader), `src/chrmodel_db2.hpp`/`.cpp` (typed
character-texture-layout structs feeding `husk export
--db2-dir/--dbd-dir/--char-layout-id`) — see `TODO/CHAR_TEXTURE_
COMPOSITING_TODO.md` for the fullest example of this pattern actually
landing in `src/`.

So most of what follows isn't "a spec for some other engine project" —
it's real, actionable husk scope that just hasn't been implemented yet,
gated on the same local-DB2-table-and-join-path investigation
`CHAR_TEXTURE_COMPOSITING_TODO.md`/`TODO_correctness.md` #2 already do for
their own items. Items 1-4 and 6 below are all DB2-lookup problems in that
sense — reframed accordingly. Only items 5 and 7 are genuinely not
data-acquisition problems at all (client logic / a user setting, not a
missing table), and those two keep their original framing.

## How to read each entry

- **husk gives you** — the exact glTF/`dump-chunks` field to read, as of
  husk's current state (cross-check `../M2_COMPLETENESS.md`/
  `TODO_correctness.md` if this drifts).
- **missing** — what full reproduction still needs.
- **local DB2 status** — whether the relevant table has actually been
  confirmed present in a real local extraction, and how confident the
  schema/join-path guess is. "Unconfirmed" means general WoW-modding
  knowledge, not verified against a real dump by this project.
- **resolution path** — what closing this gap actually looks like, and
  whether that's husk's own job or belongs to whatever consumes husk's
  output.

---

## 1. Geoset selection

**Now has its own dedicated tracking — see `GEOSET_SELECTION_TODO.md`, not
here.** Implemented husk-side 2026-08-14: `husk export --db2-dir/--dbd-dir/
--customization-choice-ids` resolves a real `ChrCustomizationChoiceID` to
its real `geoset_id` (`ChrCustomizationChoice` → `ChrCustomizationElement.
ChrCustomizationGeosetID` → `ChrCustomizationGeoset` →
`geoset_id = GeosetType*100+GeosetID`), attached as `enabled_geosets` skin
extras, verified end to end against real local data. What's left is purely
Blender-side consumption (pre-selecting each geoset group's dropdown from
the real resolved data) — the husk/DB2 half of this item is done, not just
planned.

- **husk gives you**: every primitive's node `extras` carries `geoset_id`/
  `geoset_group`/`geoset_variant`; `tools/husk_blender_geoset_mask.py`
  turns each group into a real Blender dropdown; `--customization-choice-ids`
  now attaches the real resolved selection too. Full detail:
  `GEOSET_SELECTION_TODO.md`.
- **resolution path**: husk's own job, done for the resolution itself — see
  `GEOSET_SELECTION_TODO.md`'s "Remaining: expose this in Blender" section
  for what's left.

## 2. `.bone` correction-set selection

**Already tracked in more current detail elsewhere — see
`TODO_correctness.md` item 2, not here.** That file now documents the real
join, wired into husk 2026-08-14 (`husk export --db2-dir/--dbd-dir/
--customization-choice-ids`, `src/chrcustomization_db2.hpp`): a real
`ChrCustomizationChoiceID` resolves to a real `.bone` `BoneFileDataID` and
marks the matching `--bones-dir`-resolved correction set. What's still
open is Blender-side consumption, not the resolution itself. Keeping two
independently-drifting copies of this investigation isn't useful; this
entry is now just a pointer.

- **husk gives you**: the exported `.glb` skin's
  `extras["bone_correction_sets"]` — one entry per resolved `.bone` file,
  unapplied. Full detail: `TODO_correctness.md` #2.
- **resolution path**: husk's own job, real local DB2 data confirmed
  present — see `TODO_correctness.md` #2 for the actual next step.

## 3. Hardcoded / replaceable texture resolution

**Already tracked in far more current detail elsewhere — see
`CHAR_TEXTURE_COMPOSITING_TODO.md`, not here.** That file is the real,
staged implementation plan for exactly this gap (Stages 1-2 done: WDC5
parsing, real placement geometry attached as extras via `--db2-dir/
--dbd-dir/--char-layout-id`; Stages 3-5 open: the customization-choice
chain, real pixel compositing, Blender-side picker tooling). This entry is
now just a pointer, not a duplicate description.

- **husk gives you**: `texture_type` material extras, a typed
  `alternate_textures` candidate pool, and (via `--char-layout-id`) real
  DB2-derived placement rects — see `CHAR_TEXTURE_COMPOSITING_TODO.md` for
  the current state in full.
- **resolution path**: husk's own job, actively staged — see
  `CHAR_TEXTURE_COMPOSITING_TODO.md`'s Stages 3-5.

## 4. `aliasNext` / animation-id resolution against `AnimationData.db2`

- **husk gives you**: `aliasNext` is fully parsed and resolved
  (`m2::Sequence::aliasNext`, chain-walked to its terminal non-alias
  sequence, `alias_next`/`is_alias` per-clip extras) — this part needs no
  external data at all, closed outright (`TODO_correctness.md`'s own
  "Former item 4... is resolved outright").
- **missing**: human-readable animation names, `AnimationData`'s only
  remaining relevance here.
- **local DB2 status**: unconfirmed whether `animationdata.db2` is present
  in a real local extraction — not checked this pass. If it's there
  (same tier as every other `dbfilesclient/` table already confirmed
  present for the character-customization chain), this is a cheap,
  single-table `--dbd-dir` lookup, not an external dependency.
- **resolution path**: husk's own job if the local file exists — check
  `ls dbfilesclient/ | grep -i animationdata` before assuming this needs
  an outside tool. Purely cosmetic (clip naming), not visual-correctness,
  so low priority regardless of who implements it.

## 5. `blendTimeOperation`

- **husk gives you**: `blendTimeIn`/`blendTimeOut` are fully parsed and
  exported as raw `blend_time_in`/`blend_time_out` per-clip extras.
- **missing**: the rule for *when*/*how* to apply blend-in vs. blend-out
  during a transition between two sequences.
- **source**: none, genuinely — wowdev.wiki states this plainly: "not
  stored in files, but code and context dependent." This is the one item
  on this list where the DB2-scope correction above doesn't apply — there
  is no table to find, local or otherwise, because the answer is client
  *logic*, not client *data*.
- **resolution path**: whoever renders the animation has to author a
  blend-transition heuristic — check prior art in other open-source WoW
  client reimplementations first. Not gated on DB2 access either way.

## 6. Sound linking (`M2Event` → actual sound)

- **husk gives you**: real glTF nodes, one per `M2Event`
  (`event_<identifier>`), positioned at the event's bone-relative offset,
  carrying `identifier`/`joint`/`position`/`data` (the last an opaque
  per-event `uint32_t` payload that doesn't decode into a sound reference
  itself).
- **missing**: which sound plays when a given event fires.
- **local DB2 status**: unconfirmed whether `SoundKit`-family tables are
  present locally or whether the identifier-to-sound mapping is even a
  clean DB2 lookup at all (plausibly partly hardcoded client logic per
  identifier string, not a uniform table) — not investigated. Worth a real
  check before assuming this is unreachable, same correction as items 1-4.
- **resolution path**: if a real local `SoundKit`-shaped table resolves
  the identifier convention, this becomes husk's job like the others
  above; if the mapping turns out to be hardcoded client logic instead,
  it stays genuinely external. Only matters for audio parity, not visual
  fidelity — low priority regardless.

## 7. LOD distance thresholds

- **husk gives you**: `lod_count` via `husk info`, and `husk export --lod
  all` exports every `.skin` tier as its own node sharing one skeleton
  (doesn't pick one).
- **missing**: the actual distance at which the client switches tiers.
- **source**: client CVars (`entityLodDist`, `doodadLodDist`) — **user-
  configurable settings, not asset data**. Like item 5, the DB2-scope
  correction above doesn't apply here: there is no "correct" value baked
  into any file or DB2 table to recover, even in principle.
- **resolution path**: not a lookup problem — pick your own thresholds (or
  expose them as a setting), matching the client's own design.

---

## Priority, corrected

Roughly in order of "how much visual/behavioral fidelity you get per unit
of effort" — now genuinely husk's own priority list for items 1-4/6, not
a hypothetical engine's:

1. **Geoset selection** (#1) and **hardcoded texture resolution** (#3) —
   without these, character models render with every customization option
   simultaneously visible and missing skin/hair textures entirely. Highest
   visual impact. #1's husk-side resolution is done (`GEOSET_SELECTION_TODO.md`,
   2026-08-14) — only Blender-side consumption remains. #3 still has a
   staged, in-progress implementation plan (`CHAR_TEXTURE_COMPOSITING_TODO.md`)
   with more code left to write (Stages 3-5).
2. **`.bone` correction selection** (#2) — lower visual impact than 1/3
   (subtle pose corrections, not "wrong body parts visible"); husk-side
   resolution done (`TODO_correctness.md` #2, 2026-08-14) — same status as
   #1, Blender-side consumption is the only open piece, and lower priority
   there too since `.bone` corrections were never applied to the render.
3. **`aliasNext`/animation names** (#4) — purely a usability/naming
   problem, not visual-correctness; cheap if the local table exists,
   unconfirmed whether it does.
4. **`blendTimeOperation`** (#5) — no data exists to find, local or
   otherwise; "author a reasonable heuristic," not "go acquire a table."
5. **Sound linking** (#6) — unconfirmed whether local DB2 access actually
   closes this one or whether it's genuine client logic; worth a quick
   check before writing it off.
6. **LOD thresholds** (#7) — not a missing-data problem at all, just a
   design decision to make; lowest priority regardless of DB2 scope.
