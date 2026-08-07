# wowdev.wiki findings — `M2/.anim`

Current, correct facts only. Full evidence trail: `../../WIKI_FINDINGS_HISTORY.md`
§2.

---

## Chunked `.anim`'s real per-bone data — verified — history §2

The dedicated `M2/.anim` wiki page has no content at all; the only spec
lives in the main `M2` page's prose ("identical to the flat format"). That
description only holds for **inline-boned** models. For a model whose bones
live in an external `.skel` file, a chunked `.anim` file's real per-bone
keyframe data does **not** live in `AFM2` — it lives in an `AFSB` chunk
instead:

- `AFSB` alone (no `AFM2`) — a real, observed shape.
- A small `AFM2` stub (16–1,344 bytes, always a multiple of 16, near-zero
  content — not real flat-format track data) followed by a much larger
  `AFSB` chunk — the more common real shape.

## `AFSB`'s byte layout — verified, cracked — history §2 ("Follow-up" section)

**`AFSB` is not a new format** — it's the exact same per-bone `M2Track` data
`.skel`'s `SKB1` chunk already describes (see `M2/skel.md`), just stored in
a different blob than husk originally looked in. The `(count, offset)` tuple
a `.skel`-sourced bone's `M2Track` outer array already resolves is **not**
always empty for external sequences (a prior assumption, now known wrong) —
211 of 245 real bones on `bloodelffemale_hd.skel` have real, non-zero
entries, and `offset` points directly into the owning sequence's own
`.anim` file's `AFSB` payload:

- `AFSB`'s first bytes are a clean, monotonic run of millisecond keyframe
  timestamps, `0` up to the sequence's own `duration`.
- The value region immediately after (byte length padded to the next
  multiple of 16) is a raw 12-byte `C3Vector` (translation) or the existing
  8-byte `M2CompQuat` decoder (rotation) — the exact same decode already
  used for `.skel`'s inline case.

No new parsing code was needed — `resolveVec3TrackSequence`/
`resolveQuatTrackSequence`'s existing `externalDataBlob` parameter just
needed to be fed `AFSB`'s payload instead of skipped. Implemented in
`buildAnimations` (`src/cmd_export.cpp`). `--anim`'s own file-resolution
needed a separate fix (`findAnimFileByBasename`): it only ever looked for
`<FileDataID>.anim`, not the real `wow.export`-style
`<model-basename><animId>-<subId>.anim` naming these fixtures use — fixed,
verified against the 2 real external clips this repo's fixture set has
(`anim_69_0`/`anim_69_1`).

Verified three independent ways: husk's own decode (336 real clips across
104 real `.anim` files), the Khronos `gltf_validator` (zero new errors), and
Blender's own glTF importer run headlessly (336 actions, matching exactly
from a completely separate implementation). No published `AFSB` byte layout
was found anywhere reachable (wowdev.wiki's own indexed summary gives only
the semantic split, `AFSA`=attachment/`AFSB`=bone, no structure; no
open-source WoW tooling has it either).
