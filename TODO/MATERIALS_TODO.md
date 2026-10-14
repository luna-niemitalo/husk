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
4. **WMO shader inputs**: 23's tint (`cb2[5]`) is unidentified, and what
   the client samples for a missing height texture (`runTimeData` 0) is
   unknown. The lighting after the shader (vertex light, the `r8.x`
   flag) is not traced.
5. **Mesh colour alpha**: a batch whose colour alpha and texture weight
   both animate keeps only the colour's in `alpha_fade`; the shaders
   multiply by both.
6. **`canon::BlendOp`**: no longer written; only the legacy comparison
   (`canon_diff`) and the glTF writer read it. Remove with the glTF path.
7. **UV animation coverage**: a layer's transform is resolved for
   sequence 0 only, so transforms animated in other sequences or on a
   global sequence never reach `uv_maps` (the spell fixture
   `7037014.m2` has one transform and exports none).
8. **Environment mapping source**: the fox's vertex shader is
   `Diffuse_T1_Env` while its texture-coordinate combo gives set 0 for
   unit 1. Which one the client honours is inferred only
   (`WIKI_FINDINGS/M2/rendering.md`, `cb0[5].y`).
9. **Effect styles as techniques**: the classic effect styles (particle
   and ribbon draws, fog) still carry their parameters inline; whether
   they move onto technique documents is open.
10. **NPC bakes and composites**: a bake replaces the whole skin canvas
   (a slot option); nothing says which composite a preset uses vs its
   bake beyond the preset's slot choice.
11. **Choice textures unplaced**: reported by export (`not placed`) when
   no layout layer draws the target, the material has no texture file,
   or the related choice isn't the model's.
