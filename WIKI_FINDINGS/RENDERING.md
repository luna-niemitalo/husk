# wowdev.wiki findings — sky, light data, screen effects

Current, correct facts only, read from the 12.1.0 client's compiled
shaders (container format: `BLS.md`). Full evidence trail:
`../WIKI_FINDINGS_HISTORY.md` §19. husk exports none of this yet; it is
recorded for the world renderer (`WORLD_COMPLETENESS.md`).

## Confirmed — verified

- **Sky cone** (`Day_night_cycle.md`): the sky is a mesh coloured by
  per-vertex colours, as the wiki says. `dnsky` takes its colour from the
  vertex colour, plus a sun glow and dither.
- **Colour-grading LUT** (`DB/LightData.md`): the wiki's 1024×32 strip
  lookup with a lerp between slices is exactly what `ffxcolorgrading`
  and the M2 combiners' colour-grading permutation compute.
- **FFXGlow** (`Rendering/ScreenEffects.md`): `ffxglow` is exactly
  `mix(screen, blur, blurAmount.z) + blur²·blurAmount.w`.

M2 combiner, particle and ribbon formulas: `M2/rendering.md`. Terrain
blend: `ADT.md`. WMO materials: `WORLD.md`.
