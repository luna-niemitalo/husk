# TODO: animated texture effects (spinning sigils, decals, spell quads)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file. Full narrative: `CLAUDE_HISTORY.md`.

## Open

`tools/live_gallery`'s standalone three.js GLB viewer
(`static/viewer.html`/`.js`) now plays back both real skeletal animation
(`THREE.AnimationMixer`, clip dropdown, play/pause/loop) and husk's own
extras-driven texture-transform/tint/fade curves (a real JS port of
`tools/husk_blender_geoset_mask.py`'s curve-eval logic — closes the gap
this file used to describe), plus mesh/material picking (click to inspect
`blend_mode`/`pixel_shader`/`vertex_shader`/etc.) and lighting-intensity/
exposure sliders. Verified structurally (element-ID wiring, extras JSON
shape cross-checked against `gltf_mesh.cpp`'s actual output, served
correctly through a real local server) — **not yet visually confirmed in
an actual browser** (no headless browser available in this environment);
worth a real look before treating the curve-playback math as trusted the
same way the Blender-side port already is.

Not attempted this round, real "overlay shenanigans" stretch scope if
there's appetite for more: JS-side parity for the Blender interactive
script's geoset-switch dropdown and texture-layout overlay — the viewer
can inspect a mesh's material but can't yet toggle geoset variants or
preview the character-texture-layout compositing rectangles the way
`husk_blender_geoset_mask.py` does.
