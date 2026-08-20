# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## 1. A corpus-wide "dangling internal reference" scan — a deliberate counterweight to completeness metrics

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
