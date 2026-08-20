# Generic WoW-style Blender materials — findings

Companion exploration to `husk`'s actual export pipeline: instead of
reconstructing materials from real extracted WoW texture data (what `husk
export` does), this asks how far you can get on a **from-scratch custom
mesh with none of that data** — pure procedural Blender shader nodes only.

Session context: this was built by a background subagent in parallel with a
live shader-capture session (`SHADER_SCAN_FINDINGS.md`); the agent's process
was killed by a system crash right before writing this doc, but the actual
material-building/rendering work completed cleanly beforehand — recovered
and written up after the fact.

## What's here

- `build_materials.py` — the generator script (`direnv exec . blender
  --background --python build_materials.py` from this directory, or
  `../../nix` root). Rebuilds everything below from scratch.
- `wow_material_library.blend` — 7 materials as real Blender node groups,
  each with `use_fake_user` set so they survive being opened without being
  assigned to any object.
- `render_*.png` — one test-sphere render per material (Cycles, 640×640).

Materials: `WoW_Metal`, `WoW_Cloth`, `WoW_Crystal`, `WoW_Skin`,
`WoW_Leather`, `WoW_Gem_Glow`, `WoW_Stone` — exceeds the "metal/cloth/
crystal, at least" ask.

## Grounded in real data, not invented numbers

- The blend-mode structure (multiply, mod2x, additive) follows the real M2
  texture-combiner math in `documentation/wowdev-wiki/wikitext/
  Pixel_shader_logic_for_mixing_colors.wiki`.
- `WoW_Skin` is a **node-for-node reproduction** of the real captured
  skin/subsurface-scattering pixel shader found this session
  (`references/wow_shaders/asm/498e864b92fcf1b0.asm`, flagged in
  `SHADER_SCAN_FINDINGS.md`): the exact luma weights (0.299/0.587/0.144),
  the exact wrap-light curve (`min(4·x·(1-x), 1)`), and the exact tint
  constants (~0.325, 0.576, 0.659) from the real disassembly, wired up as
  actual Blender math nodes rather than approximated by eye.

## Honest verdict, per material (renders checked directly, not assumed)

**Convincing as-is:**
- `WoW_Crystal` — faceted Voronoi internal-fracture pattern + Fresnel
  glass/emission mix + volume absorption for real per-ray saturation reads
  as a genuine magic-crystal look, close to WoW's stylized in-game crystals.
- `WoW_Stone` — mottled noise + AO gives a plausible rough stone surface.
- `WoW_Cloth` — coarse dye-lot noise over fine weave noise gives believable
  fabric mottling; reads as dyed cloth, not painted-flat plastic.

**Real problems found, not glossed over:**
- `WoW_Metal` renders too dark and too mirror-like — Metallic=1.0 with the
  scene's near-black world background (`(0.05,0.05,0.07)`) starves the BSDF
  of environment light to reflect, so the intended "flat painted panel"
  look (the Voronoi `base_ramp`) barely shows through a mostly-black sphere
  with a single hard specular hotspot. **Fix for next pass**: a brighter
  environment (an actual HDRI, or at minimum a much brighter/larger world
  background) — this is a scene-lighting problem, not a node-graph problem,
  the underlying `base_ramp`/AO logic is sound.
- `WoW_Skin` renders nearly flat, pale, and washed out — barely reads as
  skin at all. This is the expected, predicted failure mode: the real
  shader's own math (reproduced faithfully here) samples two actual painted
  textures (`tex0` = hand-painted base skin tone, `tex1` = a detail/pore
  mask) that this script fakes with plain noise. **The math is real; the
  inputs aren't**, and skin specifically has no procedural substitute that
  reads convincingly — real painted diffuse+detail maps are not optional
  here, confirming the task's own up-front caveat. Worth noting separately:
  the real shader's peak output (`tint + luma` near luma=0.5) sums past
  1.0, consistent with this being an additive rim/SSS-fill *pass* layered
  on a separate base-diffuse draw call rather than the full skin color by
  itself — `build_materials.py` blends it in at low strength (0.20) over a
  flat base tone rather than reproducing the literal blown-out readout.
- `WoW_Leather`/`WoW_Gem_Glow` not independently render-checked this pass
  (built on the same techniques as cloth/crystal respectively, reasonable
  confidence but not verified against a rendered image the way the other
  five were).

## Overall takeaway

Procedural Blender nodes get convincingly close for materials whose WoW
"look" comes from **structural** properties — blend-mode math, AO-driven
value variation, Fresnel rim lighting (the flat/toon-adjacent read WoW's
engine has partly comes from exactly this rim-light trick, reproduced here
via `add_rim()`), faceted/celled patterns. They fall short wherever WoW's
look actually comes from **painted, non-generic detail** — skin being the
clearest case, but the same logic would apply to any hand-painted emblem,
scar, or armor-plate seam. A real workflow combining this material library
as a base layer with a hand-painted detail/mask texture on top (exactly the
role `tex1` plays in the real skin shader) would likely close most of the
remaining gap without needing full WoW-extracted textures.

## Next step, if picked back up

1. Fix `WoW_Metal`'s lighting setup (HDRI environment) and re-render to
   confirm the panel/rim look actually shows through.
2. Render-check `WoW_Leather`/`WoW_Gem_Glow` the same way the other five
   were checked here.
3. Consider a from-scratch test prop (not just a sphere) to see how the
   materials read on actual geometry with real UV seams/edges.
