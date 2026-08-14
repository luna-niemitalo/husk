# TODO: animated texture effects (spinning sigils, decals, spell quads)

**Status: an open punch list, not a historical record.** Fixed items get
removed outright once closed — git history is the record of what was fixed
and when, not this file.

## Background

A large, corpus-wide visual gap (~27.6% of the 130,576-file corpus carries
a genuinely-animated `M2TextureTransform`/`M2Color`/`M2TextureWeight`
curve — `tools/corpus_scan_tasks/animated_texture_effects_task.py`) is now
closed for the two main surfaces: `husk export` resolves and attaches the
real curves as material extras (`texture_transform_animation`/
`tint_animation`/`fade_animation`), `tools/corpus_scan_tasks/render_glb.py`
renders real short animated clips (skeletal and texture-driven alike, see
its own module doc comment), and `tools/husk_blender_geoset_mask.py`'s
`apply_texture_transform_animation`/`apply_tint_fade_animation` play them
back for real in an interactive Blender session (see that script's own
"Fourth, independent job" module docstring section for the design).

## Open: the standalone 3D viewer doesn't play animation back yet

`tools/live_gallery/server.py`'s dedicated three.js GLB viewer
(`static/viewer.html`/`.js`) loads and displays a model's real `.glb` but
does not play back its animations — it detects and counts
`gltf.animations` in the status line but has no `THREE.AnimationMixer`
wired in for real skeletal playback, and husk's own extras-driven texture-
transform/tint/fade curves have no JS-side port at all (the Python/Blender
implementation doesn't translate directly — would need re-implementing
the same curve-eval/looping logic in the viewer's own JS). The main grid
page (`static/page.html`/`.js`) already plays `.webm` clips inline and is
not affected by this gap.
