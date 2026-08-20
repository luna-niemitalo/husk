# combiner_hunt

Results + runnable tooling for hunting the 17 undocumented `Combiners_*`/
`Guild*` pixel-shader formulas (`TODO/PIXEL_SHADER_FORMULAS_TODO.md`) in
the captured `wow_shader_dump` corpus. Full narrative: `../SHADER_SCAN_FINDINGS.md`.

## What's here

- `invariant_survivors.json` — output of `tools/shader_pattern_search/invariants.py`:
  every decomposed block passing the generic "N-layer tinted alpha
  composite" shape battery (affine-in-alpha, then annihilation). Doesn't
  know which specific formula it is, just that it's the right *shape*.
- `equivalence_results.json` — output of `tools/shader_pattern_search/equivalence.py`:
  every decomposed block passing full numeric equivalence against one of
  the 17 formulas' actual candidate algebra (transcribed from `wow.export`,
  see `tools/shader_pattern_search/formula_specs.py`), keyed by formula
  name. This is the sharper of the two tools -- see the summary below for
  why it still isn't precise enough to skip manual reading.

## Runnable tests

The actual test code lives with the rest of the shader-pattern-search tool,
not duplicated here (`tools/` is where runnable code goes in this project;
`references/` is where findings/data go -- see `../SHADER_SCAN_FINDINGS.md`'s
own convention). To reproduce or extend:

```
tools/venv/bin/python3 tools/shader_pattern_search/decompose.py     # regenerate references/wow_shaders/decomposed/
tools/venv/bin/python3 tools/shader_pattern_search/invariants.py    --output references/wow_shaders/combiner_hunt/invariant_survivors.json
tools/venv/bin/python3 tools/shader_pattern_search/equivalence.py   --output references/wow_shaders/combiner_hunt/equivalence_results.json
```

`tools/shader_pattern_search/formula_specs.py` is where a new/corrected
formula gets added -- one `Formula(name, n_tex, n_const, fn, ...)` entry,
picked up automatically by `equivalence.py`.

See `../SHADER_SCAN_FINDINGS.md`'s "invariant-based matching" / "equivalence
testing" sections for the full precision/recall discussion and the
prune-via-code vs. just-read-N-files recommendation.
