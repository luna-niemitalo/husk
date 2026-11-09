# TODO: M2 pixel-shader combiner formulas — open items

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

**Scope: investigative.** The formulas themselves are settled: every
combiner ID's diffuse/emissive/alpha is read from the 12.1.0 client's
compiled shaders, including the 17 wowdev.wiki leaves empty and the
`Combiners_Opaque_Mod2xNA_Alpha` factor of 2 (the client doubles;
`WIKI_FINDINGS/M2/rendering.md` "Formulas", correction (c)). husk carries
them as technique documents (`techniques/m2_pixel_shader/`).

1. **Inferred bindings** (`WIKI_FINDINGS/M2/rendering.md` Notes): that
   `*_Wgt`'s `cb0[22]` is the batch's texture weight, that Guild's
   `cb0[10..12]` are texture types 15–17, and that `cb0[5].y` comes from
   the shader ID's env bits. Each needs a source that ties the constant to
   its M2 field.
2. **No comparison against the running client**: the formulas come from
   shader bytecode only. A rendered side-by-side is human-gated.
3. **Legacy renderer** (`tools/corpus_scan_tasks/render_glb.py`'s
   `fix_multi_texture_layers`) still implements only the wiki-sourced
   subset; its exclusion list predates the bytecode formulas. Legacy-only,
   low priority.
