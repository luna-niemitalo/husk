# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## 1. `src/export_materials.cpp` needs splitting

Flagged directly by Luna while reading through it (2026-08-13). At 1,281
lines it's now the largest file in `src/`, ahead of even `cmd_export.cpp`
(1,239 lines) — and still growing (this session's texture-transform-
animation curve export adds another real chunk to the same file). Same
precedent this project already has twice: `m2.hpp`/`m2.cpp`'s split into
`m2_header.*`/`m2_animation.*`/`m2_primitives.*`/`m2_scene.*`, and
`cmd_export.cpp`'s own Item 1 split that carved `export_materials.cpp`
*out* of it in the first place (see this file's own top-of-file comment:
"split out of cmd_export.cpp per FILE_SPLIT_TODO.md's Item 1" —
`FILE_SPLIT_TODO.md` itself is gone now, fully implemented and deleted per
this project's own TODO lifecycle).

Not investigated yet: where the real seams are. A first-glance read
suggests at least three candidate pieces bundled into one file today —
(1) texture *resolution* (embedded-filename/FileDataID/fuzzy-basename-pool/
listfile matching, `resolveTextureBytes`/`scanFuzzyTexturePool`/
`classifyCandidateCategory`/the ambiguous-candidate ranking functions),
(2) per-batch material *building* (`buildMaterialsAndPrimitives`'s own
body — blend mode, tint/fade, UV transform, multi-texture-layer handling),
(3) the animated-curve resolvers (`resolveAnimatedColorCurve`/
`resolveAnimatedFixed16Curve`/this session's new quat equivalent) — but
this needs a real read of the whole file's call graph before committing to
a specific split, not a guess from a table of contents.

## 2. Comment hygiene: move scope/design-decision prose out of headers into DESIGN.md

Prompted directly, off the back of a real, concrete problem: `M2Batch::
shader_id` sat unparsed for most of this project's life despite this
project's own completeness docs reading as near-complete, because the
scope decision to skip it lived only in `skin.hpp`'s own inline doc
comment ("out of scope for a first metallic-roughness-with-one-texture
pass") — a comment nobody re-reads once written, so it silently aged from
"honest, current scope cut" into "stale, wrong claim" as the project's
ambitions grew well past "first pass." Per `~/nix/claude-rules/
CODE_COMMENTS.md`: "out of scope"/"not implemented"/"stays unread" is a
*decision*, not a fact-at-this-line, and belongs in DESIGN.md (reread at
onboarding, tolerant of slight staleness), not inline (rots silently).

**First pass done this session**: a new "Deliberately unparsed fields"
ledger in `DESIGN.md`'s Key design decisions, covering `M2SkinSection`/
`M2Batch` (`skin.hpp`), `.skel`'s unparsed chunks/fields (`skel.hpp`), and
`M2Event::enabled` (`m2_scene.hpp`) — the inline comments in those four
spots trimmed to why/gotcha-only content, pointing at the new ledger
instead of repeating the scope rationale. Also fixed two comments that
had gone stale in the *other* direction while auditing this (claiming
something unparsed when it was actually fully resolved elsewhere --
`m2_skeleton.hpp`'s `Bone` struct, see git history) — same root cause,
opposite symptom.

**Not exhaustive — real remaining scope**: the same pattern is still
scattered across the rest of the codebase (at minimum `db2.hpp`/`dbd.hpp`/
`db2table.hpp`/`listfile.hpp`/`blp.hpp`/`m2_animation.hpp`/`m2_header.hpp`
all have "skipped"/"out of scope"/"unread" language inline, not audited
this session for which are genuine scope-decision prose worth migrating
vs. legitimate boundary-contract/why content that should stay per
`CODE_COMMENTS.md`'s own rules). A full sweep needs a per-file read, not
a grep-and-batch-edit — some of what grep finds is correctly inline
(boundary contracts, gotchas, non-obvious why), only the "this is a
scope decision spanning the whole struct/module" class should move.

## 3. A corpus-wide "dangling internal reference" scan — a deliberate counterweight to completeness metrics

Prompted directly (2026-08-13), off the back of a real, concrete example
this same session hit while picking a texture-transform-animation test
fixture: `cfx_druid_efflorescence_periodicvar2.m2` has a real, well-formed,
genuinely-animated `M2TextureTransform` record — but its one real `.skin`
batch's `textureTransformComboIndex` resolves, via `textureTransformCombos`,
to the `0xFFFF` "none" sentinel, not to that record. The data exists and
parses cleanly; nothing in the file ever actually points at it. Every
completeness metric this project tracks (`M2_COMPLETENESS.md`, the corpus
scans in `tools/`) only measures *presence* — "does this file have an
animated transform/light/particle/whatever" — and would count this file as
a hit, when the real, rendered-in-game answer is "no, this specific record
is dead."

**The idea**: presence-only metrics have a blind spot in exactly one
direction — they can't distinguish "husk/casc-tool is missing something
real" from "the data legitimately isn't reachable" (stale leftover from an
old asset revision, an unused variant, an authoring mistake that shipped
anyway, ...). A *reachability* scan is the counterweight: for every
internal cross-reference husk already knows how to resolve (batch ->
`textureCombos`/`textureTransformCombos`/`textureWeightCombos` -> texture/
transform/weight record, `boneLookup`/`keyBoneLookup` -> bone,
`sequenceLookup` -> sequence, skin `materialIndex`/`textureComboIndex`
bounds, TXID `textureFileDataIds` entries with no matching local file at
all despite `--listfile` resolution, ...), measure the corpus-wide rate of
"claims a target exists, but it doesn't resolve" (dangling index, sentinel
where a real value was expected, out-of-range index, FileDataID absent
from both the local scan directory *and* the listfile).

**Why the rate itself matters, not just individual hits**: a **low** rate
across the corpus is expected and mostly uninteresting — real Blizzard
authoring mistakes, genuinely unused/stale records, or old-expansion
leftovers that never got cleaned up (this project's own `CORPUS_TODO.md`
history already has confirmed real examples of exactly this class:
mismatched shared batch data, `materialIndex`/`textureComboIndex` pointing
one past the end of the array, 16x-confirmed across real collections/
recolor item variants). A **high or systematic** rate — especially one
concentrated in one specific reference *kind*, or one specific local
extraction area/expansion — is the opposite signal: it would mean the
local CASC extraction itself has a structural blind spot (files fetched
incompletely, a whole reference *category* silently never resolving), which
is casc-tool's own problem to investigate, not something husk can fix by
reading the M2 format more carefully. Same standing rule this project
already applies to DB2 data (never blame "the data isn't there" without
first checking whether it's actually just not been asked for correctly) —
this scan is what would turn "the data isn't there" from an assumption
into a measured, defensible number, worth taking to the casc-tool project
if it comes back large.

Not started — this is a new tool idea, not yet designed as a `ScanTask`
the way `animated_texture_effects_task.py` was this session. Real open
questions before implementation: which reference kinds are cheap enough to
check per-file (some, like `textureTransformCombos` above, need only the
`.m2` itself; others, like `materialIndex`/`textureComboIndex` bounds, need
a matched `.skin` sibling resolved first, same complexity
`find_texture_type_collisions.py` already has); whether to report one
aggregate corpus-wide rate per reference kind or per-file detail (probably
both, same "CSV of hits + a summary log" shape every other `find_*.py`/
`*_task.py` tool in `tools/` already uses); and whether "no local file *and*
no listfile match" for a FileDataID is a fair "dangling" signal on its own,
given `README.md`'s own documented note that a real prior scan already
found 99.9% of "missing" FileDataIDs were actually present under their real
listfile name, not truly absent (the remaining 0.1% -- 79 real
FileDataIDs -- is exactly the kind of number this new scan would want to
reproduce and generalize, not just cite from memory).
