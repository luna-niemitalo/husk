# TODO: open investigations, gathered from across TODO/

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Purpose

A meta-list, not a duplicate. Every item below already has a home file with
its own full detail — this file exists only to answer "what's currently an
open investigation, anywhere in the project" in one place, and to track this
session's own pass through them. When an item closes, close it in its real
home file (per that file's own convention) and remove the row here — don't
let this file drift into a second source of truth.

**Investigation** here means: the next step is *finding something out*
(corpus scan, byte/data analysis, cross-referencing an existing source,
reading undocumented behavior out of real files) rather than writing a
feature, applying a known fix, or waiting on Luna's own eyes/judgment.
Items already tagged **human-gated** in their home file are listed for
completeness but are explicitly out of scope for unattended work here.

## Independent investigations

| # | Item | Home file | What's actually open |
|---|---|---|---|
| 1 | ~~`field2`/`field3` full per-subdirectory audit~~ | `DPIV_TODO.md` step 4 | **Done 2026-08-21** — real negative result, no directory-level correlation for either the zero-point placeholder pattern (61.6% of all records, uniformly high across every subdirectory) or `field3`'s distribution |
| 2 | ~~Equipped-gear `TextureType` enum ground-truth~~ | `EQUIPPED_GEAR_RENDER_TODO.md` step 1 | **Done 2026-08-21** — falsified: real values are `{2,3,4,5,24}`, not the pre-8.0 0-8 body-section range; `reference/wow.export` never even reads this field. Opened a real follow-up (`ChrModelTextureTargetID` as the next candidate section key) |
| 3 | ~~`blendTimeOperation` heuristic~~ | `ENGINE_TODO.md` #3 | **Done 2026-08-21** — checked both vendored reference implementations; neither blends transitions at all (hard cut). Real baseline found, not a ready heuristic |
| 4 | ~~Sound linking (`M2Event` → `SoundKit`)~~ | `ENGINE_TODO.md` #4 | **Done 2026-08-21** — split result: the general `ModelSound*` chain is 100% TACT-encrypted locally (genuinely blocked); a narrower creature-only path via `CreatureSoundData`/`--creature-display-id` is real and open, confirmed by direct name correlation against husk's own `M2Event` table |
| 5 | Remaining ~62 "unexplained" blank renders | `RENDER_QUALITY_TODO.md` §1 | 6/68 spot-checked, 0 real bugs (5 correctly-blank-by-design, 1 a `--listfile` framing issue) — the rest haven't been looked at; likely more of the same already-understood categories |
| 6 | Black-additive-texture exclude-list promotion | `RENDER_QUALITY_TODO.md` §1 | 79 zero-particle candidates found (`black_additive_task.py`); not yet spot-checked before folding into an exclude list the way the 36-file particle class already was |
| 7 | Non-character texture-candidate tiebreak | `RENDER_QUALITY_TODO.md` §2 | `orderCandidatesForDefault` only has real tiebreak logic for skin/skin_extra/char_jewelry categories; other categories fall through to alphabetical — could produce wrong color-variant picks, not confirmed how often |
| 8 | Black silhouette root cause | `RENDER_QUALITY_TODO.md` §5 | `demolishercannonball` renders too dark to read despite correct lit shading and a non-black (if dark) texture — root cause (render lighting intensity vs. genuinely low-albedo materials generally, vs. something file-specific) not identified; worth running `categorize_flagged_renders.py` over the current flagged set to size the bucket first |
| 9 | Disco/flicker animation repro | `RENDER_QUALITY_TODO.md` §6 | `druidtreeharanir` reported flicker/strobe — no investigation done yet, several plausible mechanisms listed but unconfirmed |
| 10 | White silhouette repro | `RENDER_QUALITY_TODO.md` §6 | `earthspiritsmalllesser` — plausible combination of total texture-resolution miss + unlit-import quirk, not confirmed, just hypothesized |
| 11 | Image-dedup-collision frequency | `MULTI_TEXTURE_LAYER_TODO.md` Open follow-up | How often a 2nd texture layer is byte-identical to another texture elsewhere in the same file (the fixed collision bug's real trigger condition) is unquantified across the corpus |
| 12 | `SwatchColor` DB2 availability | `CHAR_TEXTURE_BLENDER_SWITCH_TODO.md` Still open | Whether flat-color (non-textured) customization choices are recoverable from any DB2 table husk already reads, instead of silently dropping out of the texture switch |
| 13 | `live_gallery` curve playback, real-browser confirmation | `TODO_correctness.md` #3 | Verified structurally only — no headless browser was available in-session before; worth checking whether one is available now rather than assuming it still isn't |

## Explicitly human-gated (listed for completeness, not worked here)

- Billboard ground-truth pass (`RENDER_QUALITY_TODO.md` §4) — needs a real client comparison.
- `.bone` correction application semantics (`BONE_CORRECTION_APPLICATION_TODO.md`) — needs a real client screenshot reference.
- Pixel-shader formula final verification (`PIXEL_SHADER_FORMULAS_TODO.md` step 2) — needs a real in-game/WMV screenshot to resolve the factor-of-2 discrepancy.
- Full-corpus knowledge-base render + visual spot-check (`KNOWLEDGE_BASE_DESIGN.md`) — explicitly gated per a prior incident, needs Luna's go-ahead and her own eyes.
- Tint/fade animation ground-truth (`RENDER_QUALITY_TODO.md` §3) — structurally sound, unverified against real per-frame alpha values; not urgent, no fresh repro flagged.

## This session's pass

Items 1-4 done 2026-08-21 (2/3/4 explicitly requested; 1 revisited
`DPIV_TODO.md` since a prior session had just touched that file but its
own item 4 was never actually done). Results landed in each item's own
home file — struck through above rather than deleted outright, so the
row stays as a pointer to where the finding actually lives; delete the
rows on a future pass once they're no longer useful as a map. Remaining
9 items (5-13) are untouched, available for a future pass.
