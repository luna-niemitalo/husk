# Animation in the model bundle: a consumer's guide

What a model bundle (`schema/model.schema.json`, `resources.rig`) holds
about animation, where each value comes from, and how to pick a clip such as
walk, run or death. Code: `src/m2_canon_input.cpp` (`buildCanonModel`, clip
passes), `src/cmd_export_canon.cpp` (`buildExternalAnimBlobs`, AnimationData
names).

## Clips

`resources.rig.animation[]` holds one clip per playable sequence. Each clip
has `sequence_index`, `sequence_kind` and per-joint curves (`joints[]`:
translation, rotation, scale, each `times` + `values` + `interpolation`).

| `sequence_kind` | `sequence_index` names | Metadata on the clip |
|---|---|---|
| `sequence` | the model's `M2Sequence` array (or the `.skel`'s `SKS1` for a model whose bones live in a `.skel`) | `animation`, `variation`, `duration`, `move_speed`, `frequency`, `blend_in`, `blend_out`, `source{flags, variation_next}` |
| `global_sequence` | the model's global-loop array | `duration`: the loop's length (`M2Loop`) |

### Which animation a clip is

- `animation` is a ref to an `AnimationData` row: `{"kind": "db2_row",
  "table": "AnimationData", "row": <M2Sequence.id>}`. The row id is the
  animation's identity, the same id in every model: 0 Stand, 1 Death,
  4 Walk, 5 Run, 13 WalkBackwards, 37-40 JumpStart/Jump/JumpEnd/Fall,
  41/42 SwimIdle/Swim (wowdev.wiki `M2/AnimationList`).
- The ref's `name` has two sources, told apart by `name_source`:
  - `db2`: the client's own `AnimationData.Name`, when the table read has
    that column (pre-12 layouts). Since layout `BBF66A3C` (12.x: `ID`,
    `Fallback`, `BehaviorTier`, `BehaviorID`, `Flags[2]`) it has none.
  - `community`: wow.export's index-based list
    (`src/js/3D/AnimMapper.js`, itself from wow.tools.local's `anims.js`),
    read from a local checkout given as `--anim-names`. Used for ids the
    client table doesn't name; a client name always wins. Provenance
    source `animation names` records the checkout's file and commit. The
    list lags new client ids (2026-11: 1776 names, 12.1.5 has 1866 rows);
    a clip past its end stays unnamed.
  Without either, the clip carries only its id.
- `variation` is the sequence's `variationIndex` (the "sub-animation id"):
  one animation can have several clips (stand variations, idle fidgets).
  `frequency` is each variation's play weight; `source.variation_next` links
  the variations (−1 = the last one).
- **Picking walk/run/death**: the clips whose `animation.row` is 4, 5 or 1;
  `variation` 0 is the base clip, further variations are picked by
  `frequency`. `move_speed` (yard/second) is the ground speed the clip is
  authored for, to scale playback to the character's speed.
- A model that lacks an animation has no clip for it: see "Fallbacks".

### Fallbacks

What plays when a model has no clip of the animation asked for (Run on a
creature that only walks) is an `animation_fallbacks` document
(`schema/animation_fallbacks.schema.json`), which the model reaches
through its one `references` entry of that kind:

- `fallbacks[]`: `{id, animation, fallback}`, one per animation whose
  fallback isn't `otherwise`. `animation` and `fallback` are animation refs
  named exactly as a clip's `animation` (identity `AnimationData` row, name
  and `name_source` from the client or the community list); `id` is
  `nameUuid("animation_fallback/<identity key of animation>")`.
- `otherwise`: what every animation without an entry falls back to
  (Stand, row 0). It can have an entry itself: in 12.1.5 Stand falls back
  to row 147.
- To pick a clip: if the model has a clip whose `animation` identity is the
  one wanted, play it; else look the animation up in `fallbacks` (or take
  `otherwise`) and repeat. Match identities, never names. What the client
  does on a chain that comes back to where it started without a clip is
  not known.

**One table per client build, shared by every model.** The source is
`AnimationData.Fallback`, a column of a client-wide table whose rows are
keyed by animation id alone (WoWDBDefs `AnimationData.dbd`: `ID`,
`Fallback`, `BehaviorID`, `BehaviorTier`, `Flags`; no model, skeleton or
creature column). No model, `.skel` or `.anim` file carries a fallback of
its own. So two skeletons never differ in their fallback set: the grouping
key is the client build, one document per corpus, and every model with
clips references it. The 12.1.5 table: 1866 rows, 1580 entries (every
row whose Fallback isn't 0). Inlined, that is about 1580 entries in every
model bundle, which is why a tree holds it once.

**Inline or external**, the rule appearances use (BUNDLE_FORMAT.md
"Appearances"): the reference's id is the same in both forms
(`animation_fallbacks/name/animation_fallbacks`).

- `husk export` with `--db2-dir`/`--dbd-dir` writes it inline
  (`document`), so a single bundle stays self-contained.
- `export-world` (and `export-corpus`, which runs it) with
  `--db2-dir`/`--dbd-dir` reads AnimationData once, writes
  `<tree>/animation_fallbacks.json`, and its model pass writes each model's
  reference in the external form (`asset.uri`
  `../../animation_fallbacks.json`). The model writer writes both forms
  through one function (`writers::beginListReference`), so no splice like
  `export-appearances`' is needed: the shared table exists before any
  model is written. Without the DB2 flags neither form is written.

Readers: `validate-bundle` validates the document in either form and
reports a missing external file with the path it expected; the Blender
addon reads it with `docs.list_document` and walks it with
`docs.fallback_animation`.

### Inline, external and alias sequences

`source.flags` is `M2Sequence.flags` as stored; three cases decide where the
curves came from:

| Flags | Curves | What the bundle holds |
|---|---|---|
| `0x20` set | inline: in the `.m2` (or `.skel`) itself | the clip |
| `0x20` clear, `0x40` clear | external: a `.anim` file per (animation id, variation), named by the model's `AFID` chunk (the `.skel`'s for `.skel` models) | the clip, when the `.anim` was found; no clip otherwise |
| `0x40` set, `0x20` clear | alias: plays another sequence (`aliasNext`, followed to its end) | a clip under the alias's own `sequence_index` and metadata, with the target's curves |

- External `.anim` lookup (`--anim auto`, the default): `<anim dir>/<fdid>.anim`,
  then the corpus store by FileDataID (`--listfile-root`), then wow.export's
  `<model><animId:4>-<subId:2>.anim` beside the model. `--anim inline` skips
  external sequences; `--anim none` writes no clips.
- A model's `AFID` names which ids are external. On creatures these are
  mostly emotes and secondary animations; the locomotion and death clips
  are usually inline (`WIKI_FINDINGS/M2.md` "Which sequences are external").
  A consumer that drops external sequences still gets walk/run/death on
  most creatures, but not their emotes.
- A sequence with neither inline data nor a found `.anim` produces no clip,
  so `sequence_index` values have gaps. Never index clips by position.

### Global sequences

A `global_sequence` clip is a loop on its own timer, independent of the
playing sequence (flags, idle machinery, rotating parts). Its curves' times
are within one loop. The clip's `duration` is the loop's length (`M2Loop`),
the period its curves repeat with; the last keyframe can come before it.
`resources.rig.global_sequences[i].duration` is the model's own loop table,
which material, scene and effect curves with `sequence_kind:
global_sequence` index. A `.skel` model's joint clips index the `.skel`'s
table, so for those use the clip's own `duration`.

Material tracks (layer tint, alpha, texture weight, UV transform) and
effect curves use the same `sequence_index` + `sequence_kind` pairing
(`common.schema.json` `inline_curve`).

## Levels of detail

`resources.geometry.lod` lists the mesh at each level, finest first: level 0
is the main `.skin`, later levels the `_lod01`..`_lod03` skins the `SFID`
chunk names. To draw level n read `levels[min(n, max_level)]`. Every level
shares the one skeleton and the one clip list; LOD changes geometry only.

## `export-world`

`export-world`'s model pass runs with `--db2-dir none` and the default
`--anim auto`. Clips, external `.anim` sequences included, arrive as in a
single `export`. Client clip names don't (a 12.x client has none);
`--anim-names`, given to `export-world`, `export-corpus` or `snapshot`, is
passed through, so community names do. Fallbacks come from the tree's one
shared `animation_fallbacks.json`, written when `export-world` has
`--db2-dir`/`--dbd-dir` ("Fallbacks").
