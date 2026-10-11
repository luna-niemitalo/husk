# TODO: material documents — open items

Scope: dual-use (canon bundle). Design: `REFACTOR/MATERIALS.md`; client
compositor facts: `WIKI_FINDINGS/M2/rendering.md` "Character texture
compositing".

1. **Blend mode mapping**: `ChrModelTextureLayer.BlendMode` → compositor
   case is assumed equal and unverified. Mode 15 (most of human male's
   layers) has no case: find where the client maps modes before dispatch
   (the CPU side of `texturecompositing`), then mark verified.
2. **Layer order**: the table's `Layer` vs wow.export's target-id order;
   neither shown from the client.
3. **Texel sizes**: the compositor copies texel for texel, unscaled; how
   a choice texture whose size differs from its section is drawn (a mip,
   a scale) is unknown.
4. **WMO shader formulas**: WMO materials name their shader (`shader`)
   but the schema states no formula for it, as `m2_pixel_shader` does for
   M2. Read them from the client's `mapobj` shaders.
5. **Mesh colour alpha**: a batch whose colour alpha and texture weight
   both animate keeps only the colour's in `alpha_fade`; the shaders
   multiply by both.
6. **`canon::BlendOp`**: no longer written; only the legacy comparison
   (`canon_diff`) and the glTF writer read it. Remove with the glTF path.
7. **M2 materials as documents**: the shader formulas as material
   documents (the combine vocabulary); inline until then.
8. **NPC bakes and composites**: a bake replaces the whole skin canvas
   (a slot option); nothing says which composite a preset uses vs its
   bake beyond the preset's slot choice.
9. **Choice textures unplaced**: reported by export (`not placed`) when
   no layout layer draws the target, the material has no texture file,
   or the related choice isn't the model's.
