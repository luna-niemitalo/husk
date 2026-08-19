# TODO: cleanup & follow-ups

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## 1. Comment hygiene: move scope/design-decision prose out of headers into DESIGN.md

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

**Second pass (2026-08-14): audited the 7 files named above, one by one.**
Grepped each for "out of scope"/"not implemented"/"unread"/"skipped"/
"not parsed" and read every hit in context. Verdict: six of seven were
already correctly inline (boundary contracts, gotchas, non-obvious why —
`blp.hpp`'s BLP0/BLP1 scope note, `listfile.hpp`'s malformed-line-skip
rationale, `db2table.hpp`'s offset-map-sections-still-skipped note
[confirmed still true by checking `db2table.cpp` — no offset-map handling
wired in there despite `db2.cpp`'s own separate `decodeOffsetMapRecord`
existing for the lower-level reader], `db2.hpp`'s dropped-`extra*`-fields
note, `dbd.hpp`'s two `nonInline`/uncheckable-field notes, `m2_animation.hpp`'s
Color/TextureWeight doc comment — verbose but a real field-level contract,
not sweep-worthy scope prose). **One real, stale claim found and fixed**:
`m2_header.hpp`'s `physFileId` doc comment said `.phys` was "a format husk
doesn't parse yet" — false since `--phys`/`src/phys.cpp` shipped (this
project's own flagship physics/collision feature); corrected in place to
describe what's actually true (the *format* is fully parsed, this field is
only the header's own FileDataID pointer to it, still unresolved to a path
by husk itself, which was the real, still-accurate boundary). Same
"stale claim, not a scope decision" shape the first pass already found
twice in `m2_skeleton.hpp`. No `DESIGN.md` ledger entries needed this
pass — nothing found was broad enough scope-decision prose to warrant
migrating; the ledger already covers the real cases from the first pass.
Full suite green, 634/634 (comment-only + one doc-comment fix, no
behavior change).

**Remaining real scope**: the rest of `src/` (this pass covered exactly
the 7 files the first pass's own note named, not a full-codebase sweep) —
still needs a per-file read before this item can close, same caveat as
before: grep finds candidates, only a human/agent read tells which are
genuine scope-decision essays worth migrating.

## 2. A corpus-wide "dangling internal reference" scan — a deliberate counterweight to completeness metrics

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

## 3. `husk export` has no batch/directory mode

Every call exports exactly one `.m2`. Every multi-file job (corpus scans,
`tools/full_render.py`, this session's HD-character-roster export) works
around it externally with its own driver loop instead. Worth a real
`--input-dir`/glob mode in `cmd_export.cpp` if these loops keep getting
reinvented — until then, `tools/export_hd_characters.nu` is the pattern to
copy, not `bash` one-liners.

## 4. `--skin auto` loses its numbered-scan fallback when `--lod` is given explicitly

Found 2026-08-19 exporting the HD character roster: `--skin auto` with no
`--lod` (`resolveSkin`, `export_skin_resolution.cpp`) tries the SFID-named
`<FileDataID>.skin` first, then falls back to a same-basename numbered scan
(`bloodelffemale_hd00.skin`) if that file isn't present locally — real and
needed, since not every local extraction has FileDataID-named skin files.
But passing `--lod` explicitly, even `--lod 0` (the same value 'auto'
already defaults to), routes through `resolveAutoSkinPaths` instead
(`cmd_export.cpp`'s `resolveSkinsToExport`, gated on `lodGiven`) — which
has no such fallback and hard-fails if the SFID-named file is missing, even
though the numbered file sitting right next to it would resolve fine.
Reproduced on `bloodelffemale_hd.m2`: `--lod 0` fails, no `--lod` succeeds,
same directory, same files. Worth teaching `resolveAutoSkinPaths` the same
fallback `resolveSkin` already has, so `--lod`'s behavior doesn't depend on
whether it was passed explicitly.
