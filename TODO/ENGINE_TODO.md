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
--db2-dir/--dbd-dir/--char-layout-id`).

So most of what follows isn't "a spec for some other engine project" —
it's real, actionable husk scope that just hasn't been implemented yet,
gated on the same local-DB2-table-and-join-path investigation
`TODO_correctness.md` #2 already does for its own items. Most items below
are DB2-lookup problems in that sense; the LOD-threshold and
`blendTimeOperation` items are genuinely not data-acquisition problems at
all (client logic / a user setting, not a missing table), and keep their
own framing.

**Former items 1 (geoset selection), 2 (`.bone` correction-set
selection), and hardcoded/replaceable texture resolution are now resolved
outright, not just reframed — removed per this file's own convention.**
All three were genuinely external-data-acquisition problems (this file's
own scope), and all three are now closed: `husk export
--db2-dir/--dbd-dir/--customization-choice-ids` (2026-08-14) resolves a
real `ChrCustomizationChoiceID` to its real geoset selection (attached as
`enabled_geosets` skin extras) and/or its real `.bone` `BoneFileDataID`
(marking the matching `--bones-dir`-resolved correction set) —
`TODO_correctness.md` #2 has the bone-correction-set half's own detail.
`tools/husk_blender_geoset_mask.py` also now consumes `enabled_geosets`
directly, pre-selecting each geoset group's dropdown from real resolved
data instead of a human clicking blind. Hardcoded/replaceable texture
resolution (the real `ChrCustomizationOption -> Choice -> Element ->
Material -> TextureFileData` chain, `chr_enabled_materials`/
`chr_customization_options` skin extras) landed across several later
sessions — full narrative in `CLAUDE_HISTORY.md`. What's left for `.bone`
corrections specifically — whether/how to actually *apply* the resolved
correction matrix in Blender — is a different kind of question (unverified
composition math, gated on a real human ground-truth comparison against
the client, not a data-acquisition gap) and is tracked on its own in
`TODO/BONE_CORRECTION_APPLICATION_TODO.md`, out of this file's scope.
Remaining items renumbered accordingly — same one-time exception
`TODO_correctness.md` already establishes precedent for.

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

## 1. `aliasNext` / animation-id resolution against `AnimationData.db2` — dead end, closed

`aliasNext` itself is fully parsed/resolved (`alias_next`/`is_alias`
extras) with no external data needed. The one remaining ask — human-
readable animation names via `AnimationData.db2` — is genuinely
unreachable: checked 2026-08-14, the local table's real layout
(`layout_hash: 0xbbf66a3c`) dropped the `Name` column entirely somewhere
around 7.3.5 (confirmed against WoWDBDefs and wowdev.wiki's own schema
history), and no modern client extraction can recover it. Purely
cosmetic (clip naming, not visual correctness) — not worth chasing
further. Full investigation: `CLAUDE_HISTORY.md`.

## 2. `blendTimeOperation`

- **husk gives you**: `blendTimeIn`/`blendTimeOut` are fully parsed and
  exported as raw `blend_time_in`/`blend_time_out` per-clip extras.
- **missing**: the rule for *when*/*how* to apply blend-in vs. blend-out
  during a transition between two sequences.
- **source**: none, genuinely — wowdev.wiki states this plainly: "not
  stored in files, but code and context dependent." This is the one item
  on this list where the DB2-scope correction above doesn't apply — there
  is no table to find, local or otherwise, because the answer is client
  *logic*, not client *data*.
- **prior art (checked 2026-08-21)**: neither of this project's two real,
  vendored open-source reimplementations does anything with this field —
  `reference/wow.export` parses `blendTimeIn`/`blendTimeOut` in three real
  loaders (`M2Loader.js`, `M2LegacyLoader.js`, `SKELLoader.js`) but never
  reads the parsed value again anywhere in either of its two real
  renderers (`map-viewer/M2Renderer.js`, `3D/renderers/M2RendererGL.js`)
  — no crossfade, no `THREE.AnimationMixer` `crossFadeTo` call, nothing.
  `reference/wowser` doesn't reference `blendTime` at all, in its loader
  or anywhere else — doesn't even parse it. Neither project implements
  any animation-transition blending mechanism at all, of any kind
  (searched both for `crossFade`/transition logic generally, not just
  this field specifically — genuinely absent). **Real, if negative,
  answer**: the prior-art check doesn't hand over a ready heuristic, but
  it does hand over a real baseline — a hard cut between clips (no
  blending at all) already matches what both of this project's own
  reference implementations actually ship, so that's the honest default
  if/when this gets built, not an under-ambitious placeholder.
- **resolution path**: whoever renders the animation has to author a
  blend-transition heuristic from scratch — no real prior art exists to
  crib from in either reference project checked. Not gated on DB2 access
  either way. Low priority: a hard cut is already a shipping-quality
  baseline per the check above, so this is a polish item, not a
  correctness gap.

## 3. Sound linking (`M2Event` → actual sound)

- **husk gives you**: real glTF nodes, one per `M2Event`
  (`event_<identifier>`), positioned at the event's bone-relative offset,
  carrying `identifier`/`joint`/`position`/`data` (the last an opaque
  per-event `uint32_t` payload that doesn't decode into a sound reference
  itself).
- **missing**: which sound plays when a given event fires.
- **local DB2 status (checked 2026-08-21): split — the general table
  family is genuinely inaccessible; a real but narrower, creature-only
  path is open.** Two real, distinct table families exist locally:
  - `ModelSound`/`ModelSoundEntry`/`ModelSoundAnimEntry`/
    `ModelSoundOverride`/`ModelSoundSettings`/`ModelSoundTagEntry.db2` —
    the tables that would plausibly give a *general*, model-agnostic
    `M2Event` identifier → sound mapping. All six are **100%
    TACT-key-encrypted in the current local extraction** (`husk
    db2-info`: `record_count: 0`, "every section is genuinely still
    encrypted" for each) — genuinely unreachable with current local data,
    not a husk parsing gap. WoWDBDefs also has no real column names for
    any of them yet (`Field_10_2_5_52206_000`-style placeholders only),
    so even a re-extraction wouldn't be immediately readable without
    upstream WoWDBDefs work too.
  - `CreatureSoundData.db2` (7,673 real rows, fully readable, real
    WoWDBDefs column names) — a **fixed table of ~35 named sound-role
    fields** (`SoundDeathID`, `SoundStandID`, `SoundAggroID`,
    `SpellCastDirectedSoundID`, `SubmergeSoundID`, `SoundWingFlapID`,
    ...), each an `int<SoundEntries::ID>` pointing into the real, also-
    readable `SoundKit`/`SoundKitEntry.db2` (308,197/1,295,501 rows).
    Reachable today via husk's own existing `--creature-display-id` flag:
    `CreatureDisplayInfo.SoundID -> CreatureSoundData.ID` (confirmed via
    the DBD: `CreatureDisplayInfo` has a real
    `int<CreatureSoundData::ID> SoundID` column). **Real, direct name
    correlation confirmed against husk's own already-parsed `M2Event`
    table** (`src/m2_header.cpp`'s `eventName`): `$SCD` →
    `"PlaySoundKit_spellCastDirected"` matches `SpellCastDirectedSoundID`
    exactly; `$SMD`/`$SMG` → submerged/submerge match
    `SubmergedSoundID`/`SubmergeSoundID`; `$WNG` → wingFlap matches
    `SoundWingFlapID`. Not every documented `M2Event` code has a role
    match (most are player-character/spell-effect codes with no
    creature-sound-role equivalent) — this only ever closes a subset,
    and only for creatures with a `--creature-display-id` given, not
    player characters or generic doodads/effects.
- **resolution path**: the general, identifier-driven `ModelSound*` chain
  stays genuinely blocked (encrypted locally, undocumented upstream) —
  not husk's to fix without a re-extraction *and* WoWDBDefs coverage
  neither of which exist yet. The creature-role subset above is real,
  concrete, and already has every piece husk needs on the DB2 side
  (`--creature-display-id` derivation already exists,
  `creature_geoset_db2.hpp` is the precedent for this exact "given a
  `CreatureDisplayID`, resolve real per-creature data" shape) — a real,
  scoped follow-up if audio parity for creatures specifically becomes a
  priority, but still low priority overall (audio, not visual fidelity),
  and covers only a name-matched subset of event codes, not the general
  case.

## 4. LOD distance thresholds — a decision, not a task

`husk export --lod all` already exports every `.skin` tier; there's no
missing *data* here. The actual switch distance is a client CVar
(`entityLodDist`/`doodadLodDist`) — a user setting, not asset data, so
there's no table to recover it from even in principle. Whoever renders
picks their own threshold (or exposes it as a setting) rather than husk
resolving one.

---

## Priority, corrected

Roughly in order of "how much visual/behavioral fidelity you get per unit
of effort" — now genuinely husk's own priority list for what's left, not
a hypothetical engine's:

1. **`aliasNext`/animation names** (#1) — genuinely closed, not actionable.
2. **`blendTimeOperation`** (#2) — no data exists to find, local or
   otherwise; "author a reasonable heuristic," not "go acquire a table."
   Checked 2026-08-21: neither vendored reference implementation blends
   transitions at all (hard cut, confirmed by reading both), so a hard
   cut is already a real, shipping-quality baseline — this is polish, not
   a gap.
3. **Sound linking** (#3) — checked 2026-08-21: the general `ModelSound*`
   chain is genuinely blocked (100% TACT-encrypted locally, undocumented
   upstream); a narrower creature-only path via `CreatureSoundData`/
   `--creature-display-id` is real and open, confirmed by direct name
   correlation against husk's own `M2Event` table, but low priority and
   partial-coverage regardless.
4. **LOD thresholds** (#4) — not a missing-data problem, just a design
   decision to make; lowest priority regardless of DB2 scope.

Hardcoded/replaceable texture resolution, geoset selection, and `.bone`
correction-set selection (formerly items 1-3 here) are all fully resolved
and removed (see the note above) — geoset selection's remaining
Blender-side work is done too; `.bone` correction *application* is
tracked separately in `TODO/BONE_CORRECTION_APPLICATION_TODO.md`, out of
this file's scope.
