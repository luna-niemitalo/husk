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

**Former items 1 (geoset selection) and 2 (`.bone` correction-set
selection) are now resolved outright, not just reframed — removed per
this file's own convention.** Both were genuinely external-data-acquisition
problems (this file's own scope), and both are now closed: `husk export
--db2-dir/--dbd-dir/--customization-choice-ids` (2026-08-14) resolves a
real `ChrCustomizationChoiceID` to its real geoset selection (attached as
`enabled_geosets` skin extras) and/or its real `.bone` `BoneFileDataID`
(marking the matching `--bones-dir`-resolved correction set) —
`TODO_correctness.md` #2 has the bone-correction-set half's own detail.
`tools/husk_blender_geoset_mask.py` also now consumes `enabled_geosets`
directly, pre-selecting each geoset group's dropdown from real resolved
data instead of a human clicking blind. What's left for `.bone`
corrections specifically — whether/how to actually *apply* the resolved
correction matrix in Blender — is a different kind of question (unverified
composition math, gated on a real human ground-truth comparison against
the client, not a data-acquisition gap) and is tracked on its own in
`TODO/BONE_CORRECTION_APPLICATION_TODO.md`, out of this file's scope.
Remaining items renumbered accordingly (1-4, was 3-7 minus the two
removed) — same one-time exception `TODO_correctness.md` already
establishes precedent for.

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

## 1. Hardcoded / replaceable texture resolution

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

## 2. `aliasNext` / animation-id resolution against `AnimationData.db2`

- **husk gives you**: `aliasNext` is fully parsed and resolved
  (`m2::Sequence::aliasNext`, chain-walked to its terminal non-alias
  sequence, `alias_next`/`is_alias` per-clip extras) — this part needs no
  external data at all, closed outright (`TODO_correctness.md`'s own
  "Former item 4... is resolved outright").
- **missing**: human-readable animation names, `AnimationData`'s only
  remaining relevance here.
- **local DB2 status (checked 2026-08-14): present, but the `Name` column
  is gone.** `animationdata.db2` is a real file in the local
  `/media/luna/data/wow_export/dbfilesclient/` extraction (`husk db2-info`:
  1,858 rows, `layout_hash: 0xbbf66a3c`). That layout hash matches WoWDBDefs'
  own `LAYOUT BBF66A3C` entry for `AnimationData.dbd`
  (`reference/WoWDBDefs/definitions/AnimationData.dbd`) —
  `$noninline,id$ID<32> Fallback<u16> BehaviorTier<u8> BehaviorID<16>
  Flags<32>[2]`, four fields, no `Name` anywhere. Cross-checked against
  wowdev.wiki's own `DB/AnimationData` history
  (`documentation/wowdev-wiki/md/DB/AnimationData.md`): `Name` (a
  `stringref`) was present through at least Warlords (6.x) but is absent
  from every WoWDBDefs layout from `LAYOUT 03182786` (7.3.5+) onward — a
  real client-side schema change, not a gap in the local extraction. Also
  confirmed structurally: the file's own `string_table_size` is 2 bytes
  (just the mandatory empty-string entry), too small to hold any real
  per-row name data even if a field pointed at it.
- **resolution path**: genuinely closed, not husk's to fix — the data this
  item wants doesn't exist in any DB2 table reachable from a modern
  extraction. A name-by-ID mapping is only recoverable from a pre-7.3
  client's own `AnimationData.dbc`/`.db2` (out of scope — husk never reads
  DB2 from anywhere but a local extraction of the client version it's
  pointed at) or from the client binary's own hardcoded animation-ID enum
  (not a data file at all). Purely cosmetic either way (clip naming, not
  visual-correctness) — not worth chasing further.

## 3. `blendTimeOperation`

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

## 4. Sound linking (`M2Event` → actual sound)

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
  check before assuming this is unreachable, same correction as above.
- **resolution path**: if a real local `SoundKit`-shaped table resolves
  the identifier convention, this becomes husk's job like the others
  above; if the mapping turns out to be hardcoded client logic instead,
  it stays genuinely external. Only matters for audio parity, not visual
  fidelity — low priority regardless.

## 5. LOD distance thresholds

- **husk gives you**: `lod_count` via `husk info`, and `husk export --lod
  all` exports every `.skin` tier as its own node sharing one skeleton
  (doesn't pick one).
- **missing**: the actual distance at which the client switches tiers.
- **source**: client CVars (`entityLodDist`, `doodadLodDist`) — **user-
  configurable settings, not asset data**. Like item 3, the DB2-scope
  correction above doesn't apply here: there is no "correct" value baked
  into any file or DB2 table to recover, even in principle.
- **resolution path**: not a lookup problem — pick your own thresholds (or
  expose them as a setting), matching the client's own design.

---

## Priority, corrected

Roughly in order of "how much visual/behavioral fidelity you get per unit
of effort" — now genuinely husk's own priority list for what's left, not
a hypothetical engine's:

1. **Hardcoded texture resolution** (#1) — the single largest remaining
   visual-correctness gap on this list: without it, character models are
   missing skin/hair textures entirely. Has a staged, in-progress
   implementation plan (`CHAR_TEXTURE_COMPOSITING_TODO.md`) with more code
   left to write (Stages 3-5). Geoset selection and `.bone` correction-set
   selection, formerly items 1/2 here, are fully resolved and removed (see
   the note above) — geoset selection's remaining Blender-side work is
   done too; `.bone` correction *application* is tracked separately in
   `TODO/BONE_CORRECTION_APPLICATION_TODO.md`, out of this file's scope.
2. **`aliasNext`/animation names** (#2) — checked 2026-08-14: genuinely
   closed, not actionable. The local `animationdata.db2`'s real layout
   (`0xbbf66a3c`) dropped the `Name` column entirely somewhere around
   7.3.5 — no local DB2 table can answer this anymore, full stop.
3. **`blendTimeOperation`** (#3) — no data exists to find, local or
   otherwise; "author a reasonable heuristic," not "go acquire a table."
4. **Sound linking** (#4) — unconfirmed whether local DB2 access actually
   closes this one or whether it's genuine client logic; worth a quick
   check before writing it off.
5. **LOD thresholds** (#5) — not a missing-data problem at all, just a
   design decision to make; lowest priority regardless of DB2 scope.
