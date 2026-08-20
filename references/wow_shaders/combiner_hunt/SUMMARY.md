# Combiner hunt: results summary and prune-vs-read verdict

Full corpus: 1048 captured shaders, 19,876 decomposed blocks
(`tools/shader_pattern_search/decompose.py`). Two independent testers ran
against every block: `invariants.py` (generic "N-layer alpha composite"
shape battery, 1178 blocks / 192 shaders survive) and `equivalence.py`
(full numeric equivalence against the actual candidate algebra for all 17
undocumented formulas *and* the already-documented wiki formulas, per
Luna's push to treat the wiki itself as unverified).

## Three real bugs found and fixed before trusting any of this

The first full run reported implausibly large, heavily-overlapping
survivor sets (e.g. one block matching 12 different formula names at
once). Traced by hand instead of trusted: the block's `fitted_scale` was
`[0,0,0]` -- its real output is constant-zero (confirmed in the raw
instructions: a register explicitly zeroed two lines before the final
`mul`), and `0 * anything == 0` trivially "matches" every formula's
predicted shape at once. A second, related bug: `fitted_scale = [nan, nan,
nan]` from a block whose evaluation went outside a real instruction's
domain under this tester's domain-agnostic random sampling (e.g. `sqrt`/
`log` of a negative intermediate) -- NaN comparisons are `False` against
everything in Python, so a NaN scale silently passed every subsequent
check instead of failing it. Both fixed with explicit near-zero-scale and
`math.isfinite` guards in `equivalence.py` (and the same NaN-comparison
flaw fixed in `invariants.py`, though it turned out to have negligible
effect there: 1178/192 vs. 1190/191 before). **Real impact of the fix**:
`Guild / Guild_Opaque`'s entire earlier "25 shaders" result was 100%
degenerate artifacts -- every one of the 63 pre-fix matches had
`fitted_scale = [0,0,0]`. Post-fix, automated equivalence testing finds
**zero** genuine Guild/Guild_Opaque matches in this corpus; the only real
one remains the original hand-verified `47c35a45740c769d` capture, which
the strict tester still can't catch for an unrelated, already-documented
reason (see "known false negative" below). Several other formulas
(`Combiners_Opaque_Alpha_Alpha`, `Combiners_Opaque_Mod2xNA_Alpha (wiki)`,
`Combiners_Opaque_ModNA_Alpha`, `Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha`)
also dropped from real-looking counts to genuine zero. Every number below
is post-fix.

## Verified fills for the 17 undocumented formulas

Every survivor below was cross-checked by hand -- traced the actual
instruction algebra, not just trusted the tool's verdict (`fitted_scale`
now sits at `[1,1,1]` for every real survivor, meaning an *exact* match,
no unmodeled tint needed):

| Formula | Shaders | Verdict |
|---|---|---|
| `Combiners_Opaque_Mod2xNA_Alpha_3s` | 9 | **Verified.** 3 distinct textures, blend target is `tex3` (not `tex1`) -- structurally distinct from the 2-texture formulas below, unambiguous. |
| `Combiners_Opaque_Mod2xNA_Alpha_Alpha` | 9 | **Verified.** Nested double-mix, `mix(mix(2·tex1·tex2, tex3, tex3.a), tex1, tex1.a)` -- exact algebraic match. |
| `Guild_NoBorder` | 4 | **Verified.** All 4 matches exact (`scale=[1,1,1]`); one of the 4 is `47c35a45740c769d` itself -- the inner base×border sub-blend of the full hand-derived Guild composite. Confirms `Guild_NoBorder` really is "Guild without the emblem layer," structurally. |
| `Combiners_Opaque_Mod2xNA_Alpha_Add` | 42 | **Plausible, not disambiguated.** Diffuse math traced and exact, but every matched block only has 2 textures present -- indistinguishable from plain `Mod2xNA_Alpha (wow.export)` at the diffuse level, exactly the ambiguity flagged when the formula was transcribed (its extra specular term needs a 3rd texture this tool can't confirm from color alone). |
| `Combiners_Opaque_Alpha` | 7 | **Plausible, not disambiguated.** Same block, same formula as `Mod_Add_Wgt` below -- turns out `wow.export`'s own source has textually identical diffuse math for both cases (24 and 29), differing only in a weighted specular term neither tool models. Real ambiguity in the reference source, not a transcription error. |
| `Combiners_Opaque_Mod_Add_Wgt` | 7 | Same as `Combiners_Opaque_Alpha` above. |
| `Guild` / `Guild_Opaque` | 1 (hand-verified only) | **Verified by hand, not by the automated tester.** The original `47c35a45740c769d` find remains the only real evidence; a real compiled-swizzle artifact in its final instruction (duplicate/dropped output component) defeats the strict equivalence tester specifically, confirmed root-caused, not chased further. |

**Not found in this corpus (genuine negative results, not tool gaps):**
`Combiners_Opaque_ModNA_Alpha` -- zero real matches post-fix.
`Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha` -- zero real matches post-fix.

**Not implemented, not attempted:** `Combiners_Mod_Dual_Crossfade` /
`_Masked_Dual_Crossfade` (need an external weight scalar as the blend
factor itself, not a texture alpha -- this tester's role-search doesn't
cover that). `Illum` (real formula is "always black," a constant-output
property, not something formula-equivalence testing is shaped to check).

**Structurally unresolvable by color math alone (Tier 1, 5 formulas):**
`Combiners_Opaque_AddAlpha_Wgt`, `Combiners_Mod_Add_Alpha`,
`Combiners_Mod_AddAlpha_Wgt`, `Combiners_Mod_Depth` are all diffuse-
identical to `Combiners_Mod` itself (~2700+ blocks/~352 shaders each,
same numbers because it's the same math). Real, abundant evidence that
*some* single-texture identity-shaped combiner exists everywhere in this
corpus -- equally consistent with any of these 5 names. No further code
can separate them; they only differ in alpha/discard/specular behavior
this tool doesn't model.

## Wiki-verification side-quest (Luna's ask: don't just trust the "known" formulas either)

Of the wiki's ~19 documented formulas, 14 turned out untestable by this
method (`meshResColor` appears non-trivially -- additively, or blended via
its own alpha, not just as a trailing uniform scale the tester's scale-fit
can stand in for). Of the 5 that were testable:

- `Combiners_Mod` / `Combiners_Opaque` / `Combiners_Mod2x` (indistinguishable
  from each other, same reasoning as Tier 1 above): **confirmed real**, 352
  shaders. Combiners_Mod itself was the original hand-verified calibration case.
- `Combiners_Mod_Mod` + 8 more wiki names (also mutually indistinguishable):
  **confirmed real**, 117 shaders.
- `Combiners_Opaque_Alpha_Alpha`: **zero real matches** post-fix -- a
  documented formula that doesn't appear (in this shape) anywhere in this
  particular capture. Inconclusive (could be genuinely rare content, not
  necessarily a wrong formula), but a real, honest negative result worth
  recording.
- `Combiners_Opaque_Mod2xNA_Alpha`, the disputed factor-of-2 case
  (`TODO/PIXEL_SHADER_FORMULAS_TODO.md` step 2): **the wiki version now
  finds zero real matches; the wow.export (doubled) version finds 42
  real shaders.** This is the first actual corpus evidence either way on
  this dispute -- doesn't fully settle it (a real screenshot comparison,
  the TODO's original plan, is still the gold standard), but real data now
  leans toward `wow.export`'s doubled formula being the one that actually
  occurs, not the wiki's.

## 12 wiki table entries are mutually indistinguishable by construction

Independent of any corpus data: `Combiners_Opaque`, `Combiners_Mod2x`,
`Combiners_Mod` (3 names) and `Combiners_Opaque_Opaque`, `_Mod2x`,
`_Mod2xNA`, `_Mod`, `Mod_Opaque`, `Mod_Mod2x`, `Mod_Mod2xNA`, `Mod2x_Mod2x`,
`Mod_Mod` (9 names) reduce to the same two shapes ("tex1 × scale" /
"tex1×tex2 × scale") once alpha is set aside -- a real fact about the
wiki's own table, not a gap in this tool.

## Recommendation

`TODO/PIXEL_SHADER_FORMULAS_TODO.md` can mark 3 formulas solidly filled
(`_3s`, `_Alpha_Alpha`, `Guild_NoBorder`) and 3 more as plausible-but-
ambiguous (`_Add`, `Opaque_Alpha`, `Mod_Add_Wgt` -- real matches, but
genuinely indistinguishable from a sibling formula without weight/specular
modeling this tool doesn't do). 2 formulas got genuine negative results
worth recording (`ModNA_Alpha`, `UnshAlpha` -- not found, not a bug). 2
were never attempted (`Dual_Crossfade` family, `Illum` -- need different
methodology entirely). `Guild`/`Guild_Opaque` stays at its original
1-example hand-verified state. That's real, honest progress on 8 of the
17 -- not "solved," but no longer purely a documentation gap either.
