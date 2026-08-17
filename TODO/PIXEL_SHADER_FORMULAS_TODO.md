# TODO: fill wowdev.wiki's undocumented pixel-shader combiner formulas

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

`TODO/MULTI_TEXTURE_LAYER_TODO.md` found that `documentation/wowdev-wiki/
wikitext/Pixel_shader_logic_for_mixing_colors.wiki` — the page giving the
real GLSL-equivalent formula for every named `Combiners_*` fragment
shader — has **17 of the 36 real `s_modelPixelShaders[36]` table entries
with no formula documented at all** (an empty `===Name===` section, no
`finalColor` math underneath):

```
Combiners_Opaque_Mod2xNA_Alpha_Add
Combiners_Opaque_Mod2xNA_Alpha_3s
Combiners_Opaque_AddAlpha_Wgt
Combiners_Mod_Add_Alpha
Combiners_Opaque_ModNA_Alpha
Combiners_Mod_AddAlpha_Wgt
Combiners_Opaque_Mod_Add_Wgt
Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha
Combiners_Mod_Dual_Crossfade
Combiners_Opaque_Mod2xNA_Alpha_Alpha
Combiners_Mod_Masked_Dual_Crossfade
Combiners_Opaque_Alpha
Guild
Guild_NoBorder
Guild_Opaque
Combiners_Mod_Depth
Illum
```

`husk`'s own `src/m2_shader_names.cpp` (`TODO/MULTI_TEXTURE_LAYER_TODO.md`'s
implementation) can already resolve real corpus batches to these exact
names via `M2Batch::shaderId` — so which real files exercise which of
these formulas is now a directly answerable question, not a guess (see
Concrete next steps).

## A real, promising, previously-unexploited lead: `reference/wow.export`'s own fragment shader

`reference/wow.export/src/shaders/m2.fragment.shader` (354 lines, real
GLSL ES 3.0, actively used by that project's own WebGL renderer — not a
stub, not the older/simpler `wowser` shader this project already found
and correctly distrusted for `TODO/MULTI_TEXTURE_LAYER_TODO.md`) has a
`switch (u_pixel_shader)` with **a real case for every single one of the
36 table entries, all 17 undocumented ones included** — cases 15, 19–34
below map exactly onto the wiki's own empty-section list above:

```glsl
case 15: // Combiners_Opaque_Mod2xNA_Alpha_Add
    mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a));
    specular = tex3.rgb * tex3.a * u_tex_sample_alpha.b;

case 19: // Combiners_Opaque_Mod2xNA_Alpha_3s
    mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a));

case 20: // Combiners_Opaque_AddAlpha_Wgt
    mat_diffuse = mesh_color * tex1.rgb;
    specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g;

case 21: // Combiners_Mod_Add_Alpha
    mat_diffuse = mesh_color * tex1.rgb;
    discard_alpha = tex1.a + tex2.a;  // can_discard = true
    specular = tex2.rgb * (1.0 - tex1.a);

case 22: // Combiners_Opaque_ModNA_Alpha
    mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb, tex1.rgb, vec3(tex1.a));

case 23: // Combiners_Mod_AddAlpha_Wgt
    mat_diffuse = mesh_color * tex1.rgb;
    discard_alpha = tex1.a;  // can_discard = true
    specular = tex2.rgb * tex2.a * u_tex_sample_alpha.g;

case 24: // Combiners_Opaque_Mod_Add_Wgt
    mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a));
    specular = tex1.rgb * tex1.a * u_tex_sample_alpha.r;

case 25: // Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha
    glow_opacity = clamp(tex3.a * u_tex_sample_alpha.b, 0.0, 1.0);
    mat_diffuse = mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb, vec3(tex1.a)) * (1.0 - glow_opacity);
    specular = tex3.rgb * glow_opacity;

case 26: // Combiners_Mod_Dual_Crossfade  (uv2/uv3 forced to uv1, see below)
    mixed = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0)));
    mat_diffuse = mesh_color * mixed.rgb;
    discard_alpha = mixed.a;  // can_discard = true

case 27: // Combiners_Opaque_Mod2xNA_Alpha_Alpha  (uv2/uv3 forced to uv1)
    mat_diffuse = mesh_color * mix(mix(tex1.rgb * tex2.rgb * 2.0, tex3.rgb, vec3(tex3.a)), tex1.rgb, vec3(tex1.a));

case 28: // Combiners_Mod_Masked_Dual_Crossfade  (uv2/uv3 forced to uv1)
    mixed = mix(mix(tex1, tex2, vec4(clamp(u_tex_sample_alpha.g, 0.0, 1.0))), tex3, vec4(clamp(u_tex_sample_alpha.b, 0.0, 1.0)));
    mat_diffuse = mesh_color * mixed.rgb;
    discard_alpha = mixed.a * tex4.a;  // can_discard = true, 4th texture sampled at uv2

case 29: // Combiners_Opaque_Alpha
    mat_diffuse = mesh_color * mix(tex1.rgb, tex2.rgb, vec3(tex2.a));

case 30: // Guild  (generic0/1/2 are real per-material tint colors this
         // shader hardcodes to vec3(1.0) -- see "What's missing" below)
    mat_diffuse = mesh_color * mix(tex1.rgb * mix(generic0, tex2.rgb * generic1, vec3(tex2.a)), tex3.rgb * generic2, vec3(tex3.a));
    discard_alpha = tex1.a;  // can_discard = true

case 31: // Guild_NoBorder
    mat_diffuse = mesh_color * tex1.rgb * mix(generic0, tex2.rgb * generic1, vec3(tex2.a));
    discard_alpha = tex1.a;  // can_discard = true

case 32: // Guild_Opaque
    mat_diffuse = mesh_color * mix(tex1.rgb * mix(generic0, tex2.rgb * generic1, vec3(tex2.a)), tex3.rgb * generic2, vec3(tex3.a));

case 33: // Combiners_Mod_Depth
    mat_diffuse = mesh_color * tex1.rgb;
    discard_alpha = tex1.a;  // can_discard = true

case 34: // Illum
    discard_alpha = tex1.a;  // can_discard = true; mat_diffuse stays 0 -- see "What's missing" below
```

(`mix(a, b, t)` is GLSL's built-in `a*(1-t) + b*t`, same function the wiki
page's own header already defines for its documented formulas —
`u_tex_sample_alpha` is that shader's name for `M2TextureWeight`
values, the same "transparency lookup" concept `WIKI_FINDINGS/M2/skin.md`
already documents for `textureWeightComboIndex`.)

## Real, concrete reason not to trust this wholesale: a documented case doesn't match

Cross-checked wow.export's formula for a case the wiki *does* already
document — `Combiners_Opaque_Mod2xNA_Alpha` (case 12) — against the
wiki's own text, as a sanity check before leaning on the 17 undocumented
ones:

- **Wiki**: `finalColor.rgb = meshResColor.rgb * ((tex1.rgb - tex1.rgb *
  tex2.rgb) * tex1.a + tex1.rgb * tex2.rgb)`, which expands to
  `meshResColor * (tex1·tex1.a + tex1·tex2·(1 - tex1.a))`.
- **wow.export**: `mesh_color * mix(tex1.rgb * tex2.rgb * 2.0, tex1.rgb,
  vec3(tex1.a))`, which expands to `mesh_color * (tex1·tex1.a +
  tex1·tex2·2·(1 - tex1.a))`.

**A real factor-of-2 discrepancy on the `tex2` cross term** — these are
not the same formula. Not investigated further this session (found late,
flagged rather than chased) — plausible explanations, none confirmed:
wow.export's shader has a real bug/simplification here; the wiki's own
documented formula is itself imprecise; real client behavior differs by
version/build in a way neither source captures exactly. **This means the
17 undocumented formulas above are a real, promising lead — a genuine,
independently-implemented reference, not guessed — but must be verified
against real data before being trusted, exactly the same "strong
hypothesis, not ground truth" tier `DESIGN.md`'s standing discipline
already gives every `reference/` source in this project (`wow.export`,
`wowser`, `WoWDBDefs`).**

## What's missing even if the formulas check out

- `u_tex_sample_alpha` (this shader's `M2TextureWeight`/transparency-
  lookup stand-in) is a `vec3` here, but the real M2 data can carry more
  than 3 weight values per batch — not investigated whether this shader
  genuinely caps at 3 or whether wow.export's own UI/pipeline only ever
  feeds it 3.
- `Guild`/`Guild_NoBorder`/`Guild_Opaque` (cases 30–32) reference
  `generic0`/`generic1`/`generic2` — hardcoded to `vec3(1.0)` in this
  shader, i.e. **wow.export doesn't actually implement the real guild-
  emblem tint-color inputs**, just stubs them to white/no-op. The real
  formula *shape* (the mix/blend structure) is likely still useful, but
  the real per-material tint values these represent are a separate,
  unresolved question — plausibly `M2Material`-adjacent data or a
  DB2-driven guild-emblem color, not investigated.
- `Illum` (case 34) never sets `mat_diffuse` at all (stays black,
  `vec3(0.0)`) — either a genuine "no diffuse contribution, alpha-test
  only" shader (plausible for some effect/overlay use), or wow.export
  itself doesn't have a real answer for this one either. Needs checking
  against what `Illum`-shaded batches actually look like in-game before
  trusting "always black" as the real formula.
- Real `reference/wow.export` also has a whole separate legacy renderer
  (`src/js/3D/renderers/M2LegacyRendererGL.js`, `M2RendererGL.js`) and a
  `ShaderMapper.js` with its own (WotLK-era, `wowser`-shaped) shader-name
  resolution table — not cross-checked against `m2.fragment.shader`
  above for internal consistency. If the two disagree on which shaderID
  maps to which case, that's worth knowing before trusting either.

## Concrete next steps (in rough order of expected payoff)

1. **Found real corpus files that actually exercise one of the 17
   undocumented shaders — done 2026-08-14**, kept here because later steps
   depend on its data:
   `tools/corpus_scan_tasks/shader_names_task.py` (new, transcribes
   `husk::m2::resolveShaderNames` into Python, same pattern
   `shader_id_task.py` already established) run against the full local
   corpus (287,005 `.skin` files). Real, concrete results — **14 of the 17
   undocumented shaders have at least one real corpus repro**, several
   with thousands:

   | Shader | Files | Example |
   |---|---|---|
   | `Combiners_Opaque_Mod2xNA_Alpha_Add` | 15,775 | `character/companionnetherwingdrake/companionnetherwingdrake00.skin` |
   | `Combiners_Opaque_Alpha` | 1,643 | `character/mechagnome/male/mechagnomemale00.skin` |
   | `Combiners_Opaque_AddAlpha_Wgt` | 1,617 | `character/darkirondwarf/female/darkirondwarffemale00.skin` |
   | `Combiners_Mod_Depth` | 1,400 | `creature/airspiritsmall/airspiritsmall00.skin` |
   | `Combiners_Opaque_ModNA_Alpha` | 1,175 | `creature/amanitrollcastermale/amanitrollcastermale00.skin` |
   | `Combiners_Mod_Add_Alpha` | 906 | `creature/arakkoagolem/arakkoagolem00.skin` |
   | `Guild_Opaque` | 629 | `character/dwarf/female/dwarffemale_hd_sdr_lod01.skin` |
   | `Combiners_Mod_AddAlpha_Wgt` | 515 | `creature/airspiritsmall/airspiritsmall00.skin` |
   | `Combiners_Mod_Dual_Crossfade` | 149 | `creature/faeriedragon/faeriedragon02.skin` |
   | `Combiners_Opaque_Mod2xNA_Alpha_UnshAlpha` | 106 | `creature/darkirondwarfcorehound/darkirondwarfcorehound00.skin` |
   | `Combiners_Mod_Masked_Dual_Crossfade` | 98 | `environments/stars/10gsl_sky0100.skin` |
   | `Combiners_Opaque_Mod2xNA_Alpha_3s` | 42 | `creature/felorcboss/felorcwarriorboss00.skin` |
   | `Guild` | 23 | `item/objectcomponents/weapon/misc_1h_guildflag_alliance_a_0100.skin` |
   | `Combiners_Opaque_Mod_Add_Wgt` | 1 | `world/expansion03/doodads/uldum/mirrors/uldum_mirror_sun_0100.skin` |

   **3 of the 17 never resolved in this corpus**: `Combiners_Opaque_
   Mod2xNA_Alpha_Alpha`, `Guild_NoBorder`, `Illum` — either genuinely rare/
   version-gated content not present in this local extraction, or (for
   `Guild`/`Guild_NoBorder`/`Guild_Opaque` specifically) plausibly gated on
   a live guild-tabard customization the base corpus export wouldn't
   capture (`Guild_Opaque` did resolve, so the table-lookup path itself is
   reachable — `Guild_NoBorder` just didn't happen to appear). Paths are
   relative to `/media/luna/data/wow_export`; full per-file data in
   `shader_names_corpus.csv` (gitignored, regenerate with the command
   below). Overall: 47,058 / 1,075,970 batches (4.37%) resolve to one of
   the 17 undocumented shaders — a real, non-trivial slice of the corpus,
   not a rounding error.
2. **Cross-check the factor-of-2 discrepancy above concretely**: find (or
   build) a real, minimal test case for `Combiners_Opaque_Mod2xNA_Alpha`
   specifically (a real file that resolves to it, per step 1's method)
   and compare an actual in-game/WMV/wow.export-rendered screenshot
   against both formulas' predicted output for a few sample texel values
   — resolves which formula (if either) is actually correct, and how much
   to trust wow.export's shader for the undocumented cases.
3. **If the formulas hold up**, transcribe them into
   `Pixel_shader_logic_for_mixing_colors.wiki`'s own `===Name===`/
   `<syntaxhighlight>` format (matching the file's existing style exactly)
   — filling wowdev.wiki's own gap locally, the same "verified-then-
   documented" treatment every other real formula in that file already
   got. Consider whether to also contribute this back to the actual
   wowdev.wiki (a real, external, generous option — not required for
   husk's own use, but a nice thing to do given how much this project
   already leans on that wiki).
4. **Once trusted**, this directly feeds `TODO/MULTI_TEXTURE_LAYER_TODO.md`'s
   own step 4 (Blender-side node recipes) — 17 more real formulas means
   17 more `Combiners_*` cases `render_glb.py`'s post-import material
   rebuild can actually reconstruct, instead of falling back to whatever
   default Blender's stock glTF import gives an unhandled combiner.
