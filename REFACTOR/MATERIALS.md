# MATERIALS.md — material documents (proposal, 2026-10-11)

**Status: slice 1 built (2026-10-11): character skin composites.** A
character model's texture layout becomes one `material` document per
canvas (`materials/<n>.json`, referenced from the model), a slot names
the one that composes it (`composite`), and each customization choice
binds its textures to the composite's inputs (`textures`, with
`only_with` for textures that depend on another choice). Blend
operations are the client compositor's (`texturecompositing.bls`,
`WIKI_FINDINGS/M2/rendering.md`); the source mode → operation mapping is
marked `blend_mapping: unverified`. Open: `TODO/MATERIALS_TODO.md`. Today a material lives inline in its
model's `resources.materials` as texture layers with a combiner op each
(`canon::Material`); the classic shader formula (`Combiners_*`, WMO shader
ids) is only named, and a character's composited skin is not represented
at all. This file proposes a fifth document kind so a renderer can draw
any material on its own — a flat texture, a lit preview — from the
document alone, and so texture overlays (a face choice on the skin atlas)
can be checked for being in the right place.

## Goal

A `material` document describes how a surface looks as a stack of layers
over texture inputs, each combined into the previous with a named,
fully specified function, plus how the result meets the framebuffer. A
consumer that implements the function vocabulary below draws every
husk material without knowing any source format.

Two things go beyond today's inline materials:

1. **Composite textures.** A texture can be a canvas built from other
   textures placed at rectangles with a blend mode each (a character's
   skin atlas: base skin, face, markings, underwear, each in its section).
   The canvas is an input of the material like any texture; a consumer
   renders it flat, or applies it.
2. **The math travels with the data.** Every combine and composite blend
   is a name from a closed vocabulary whose formula this file (and the
   schema's descriptions) states. The source's own shader identity rides
   along as provenance with a confidence, the way effects keep their
   classic `style`.

## Shape

```jsonc
{ "kind": "material", …contract header…,
  "definition": {
    "inputs": {                         // like an effect's: what a placer may supply
      "skin_atlas": { "kind": "image" },
      "tint":       { "kind": "color_alpha", "default": [1,1,1,1] } } },
  "layers": [                           // bottom first
    { "role": "base",                   // base | emission | specular | reflection | mask | detail
      "texture": { "input": "skin_atlas" },   // or a resources.textures index
      "uv": "uv_set_0",                 // uv_set_<n> | sphere_environment | cube_reflection | screen
      "uv_transform": { … static or curve … },
      "color": { … static or curve … }, "alpha": { … },
      "combine": "replace" },           // into the stack so far, see vocabulary
    { "role": "reflection", "texture": 3, "uv": "sphere_environment",
      "combine": "add_scaled_by_alpha_of_previous" } ],
  "output": { "framebuffer_blend": "alpha_key", "alpha_cutoff": 0.5,
              "lit": true, "fogged": true, "two_sided": false },
  "source": { "shader": {ref, "Combiners_Opaque_Mod2xNA_Alpha"}, "confidence": "candidate" },
  "resources": { "textures": [ …texture entries… ] } }
```

A composite is a texture-table entry of its own kind:

```jsonc
{ "composite": { "width": 1024, "height": 512,
    "layers": [ { "texture": 7, "rect": [0, 0, 512, 512], "blend": "blit" },
                { "texture": { "input": "face_lower" }, "rect": [512, 320, 256, 128], "blend": "alpha" } ] } }
```

A model's `resources.materials` becomes references to material
documents (inline or files, like placement sets), and a preset (or a
customization choice) binds the material's inputs: a face choice
supplies `face_lower`, the NPC's bake supplies `skin_atlas` whole.

## Function vocabulary

Each name is a formula over the stack so far `P` (rgb, a) and the layer
`L` (texture × color × alpha):

| combine | result |
|---|---|
| `replace` | L |
| `modulate` | P × L |
| `modulate_2x` | P × L × 2 |
| `add` | P + L |
| `alpha_blend` | mix(P, L, L.a) |
| `add_scaled_by_alpha_of_previous` | P + L × P.a |
| … | each classic formula decomposed into these, or added with its math |

Composite blends (character atlas, `ChrModelTextureLayer.BlendMode`
0..16 as wow.export names them): `none`/`blit`, `blit_alpha_mask`, `add`,
`multiply`, `mod2x`, `overlay`, `screen`, `hard_light`, `alpha_straight`,
`blend_black`, `mask_greyscale`, `mask_greyscale_color_alpha`,
`generate_greyscale`, `colorize`, `infer_alpha`, with each one's math
stated. Values wow.export calls unused stay as the source number until
a formula is known.

## Sources and tiers

Same evidence ladder as effects (`REFACTOR/EFFECTS.md`):

- **M2 batches**: the 36 `Combiners_*` pixel shaders. wow.export's
  `m2.fragment.shader` has a formula for every one (pseudo-verified;
  `TODO/PIXEL_SHADER_FORMULAS_TODO.md` lists which are corpus-verified).
  Each becomes a layer stack in the vocabulary, with `confidence`
  verified / candidate per that table.
- **WMO materials**: the WMO shader ids, same treatment.
- **Character skin**: `CharComponentTextureLayout` (canvas size),
  `CharComponentTextureSections` (rectangles), `ChrModelTextureLayer`
  (order, blend mode, section mask, target), customization choices'
  `ChrCustomizationMaterial` (texture per target, conditional on a
  related choice): one composite per texture type, its layers' textures
  bound to choice inputs.
- **Terrain splats** could later be the same kind (alpha-masked layers).

## Validation use

husk describes materials; it is not a renderer. A consumer (MantleCore,
or a small checking script) renders a material document flat: for a
character, the skin composite for a preset or a chosen set of options.
Comparing that against the client, or against wow.export's own bake, is
a direct check that overlays land in the right section with the right
blend, independent of any 3D renderer. A husk-side flat render is
optional tooling, not part of the format work.

## Decided (Luna, 2026-10-11)

1. Materials are their own document kind, `material`.
2. Combines and composite blends are named functions from a closed
   vocabulary whose formulas the schema states.
3. First slice: character skin composites. Rendering them is the
   consumer's job (Luna: husk is not a render engine).
