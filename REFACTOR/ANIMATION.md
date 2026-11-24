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
- A model that lacks an animation has no clip for it. The client then falls
  back along `AnimationData.Fallback` (e.g. Run to Walk):
  `resources.rig.animation_fallbacks` holds that table as `[animation,
  fallback]` pairs (an unlisted id falls back to 0, Stand); follow it until
  a clip exists. Written only when the export reads AnimationData
  (`--db2-dir`/`--dbd-dir`), so not by `export-world`'s model pass.

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
passed through, so community names do. `animation_fallbacks` is the one
difference: it needs AnimationData, which this pass doesn't read.
