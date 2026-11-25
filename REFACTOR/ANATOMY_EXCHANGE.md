# ANATOMY_EXCHANGE.md: the anatomy simulation's exchange documents

The documents that cross between the anatomy authoring side
(`/media/luna/work/tinker/dev/blender_base_model_bs`, its
`docs/anatomy-sim-bake-pipeline.md`) and mantlecore's musculoskeletal
solver (`/home/luna/dev/mantlecore/design/anatomy-sim.md`, "The
interface"). husk owns the format (Luna, 2026-11-25: "if we are building an
exchange format, might as well go with husk canonical, so husk owns the
exchange format"); husk itself writes none of these documents. It defines
them in `schema/` and checks them with `husk validate-bundle`.

## Kinds

| Kind | Written by | What it is |
|---|---|---|
| `anatomy` | authoring side | The rest anatomy: the base model it builds on, the tissue surfaces (muscle, fat, skin), tissue-to-bone attachments, ligament and tendon rods, muscle fibre directions, and which body parameters exist. |
| `anatomy_run` | authoring side | What one solver run is asked for: an anatomy, one body parameter set, a pose list. |
| `anatomy_result` | solver | What one run solved, per pose: the skin's vertex positions on the fixed topology, per-face strain, optional per-vertex fit error; with the body parameter set, the solved poses and the solver's identity. |

Schemas: `schema/anatomy.schema.json`, `schema/anatomy_run.schema.json`,
`schema/anatomy_result.schema.json`. They are the definition; this file
explains them and records the decisions.

## Invariants

1. **The skeleton is the base model's, referenced, never copied.** Bodies,
   joints, joint axes and ranges, muscle and tendon paths come from one
   MuJoCo MJCF model (MS-Human-700 for the human body; a generated test
   anatomy ships a small MJCF of its own). The anatomy document links it as
   a payload and names its bodies, joints and tendons by their MJCF names.
   A second copy in husk's JSON would be a second source of truth for
   kinematics mantlecore already reads through MuJoCo (Luna, 2026-11-25:
   "In + Out, MJCF referenced").
2. **Names are identity.** Every entry of an anatomy-family array has an
   `id` that is a name (pattern `key` in `common.schema.json`), unique in
   its array. Base-model things are named by their MJCF name exactly, so a
   consumer matches them by string. Why not husk's UUID v3 ids: these
   documents have no source identity to hash other than the name, and two
   foreign producers (Python, C++) would each have to reimplement husk's
   namespace hashing for no gain; a unique name already survives filtering
   and re-sorting, which is all the stable-id rule asks
   (`BUNDLE_FORMAT.md` "Stable ids").
3. **Metres, radians, +z up, right-handed** (`common.schema.json`
   `metric_space`). Both sides (Blender, MuJoCo) work in metres; yards are
   WoW's unit and stay in the WoW family.
4. **The skin topology is fixed.** The anatomy has exactly one `skin`
   tissue, Multires level 0 of the authoring side's mesh (pipeline 2.1). A
   result's per-vertex and per-face arrays are over that tissue's vertices
   and triangles, in its order; `validate-bundle` checks the counts against
   the referenced anatomy.
5. **Request and record are separate documents.** A run is what was asked;
   a result is what was solved. A result carries its own pose vectors and
   body parameter values, because the solver adds poses of its own
   (adaptive sampling, mantlecore MS11) that no run lists, and a result must
   say what it is the skin *of* without trusting that the run file is
   unchanged.
6. **Its own version line.** The anatomy family's `schema_version` starts
   at 0.1.0 and moves independently of the WoW family's (0.4.x): their
   producers and consumers differ, and a bump of one would otherwise
   rewrite every document of the other (Luna, 2026-11-25). Within the
   family, the three kinds move together.
7. **Values come from a producer, not a decoded file.** `x-source` names
   `{"producer": "anatomy_authoring"}` or `{"producer": "anatomy_solver"}`
   (`BUNDLE_FORMAT.md` "Where values come from"), and `{"format": true}`
   for structure.

## Data

Large uniform arrays are `buffer_slice`s into `.bin` files next to the
document (`CANONICAL_FORMAT.md` §5), little-endian.

**`anatomy`**
- `definition.base_model`: the MJCF payload (`media_type` is a free string;
  `model/vnd.mujoco.mjcf+xml` by convention), its source revision and licence.
- `definition.tissues[]`: `id`, `layer` (`muscle`, `fat`, `skin`), a closed
  triangle surface at rest (`positions` f32×3, `triangles` u32×3), and for a
  muscle the base-model muscle it realizes (`base_muscle`) and its fibre
  direction per surface vertex (f32×3, unit, rest frame). Tets are meshed by
  the solver from these surfaces, so none are stored.
- `definition.attachments[]`: a tissue's vertices bound to a base-model body
  (`vertices` u32 into the tissue's positions).
- `definition.rods[]`: ligaments and tendons, two ends each, an end on a
  base-model body (`body`, `point` in the body's frame) or on a tissue
  vertex, optional rest-frame via points.
- `definition.parameters[]`: the body parameters that exist for this
  anatomy: `id`, `quantity` (bone length scale, bone width scale, muscle
  volume scale, fat thickness, skin thickness, ligament laxity, skin
  extensibility), what it applies to (base-model bodies, tissues, rods; empty
  = the whole body), its unit and rest value. Region-specific fat or
  per-muscle volume is one parameter per region.

**`anatomy_run`**: references the anatomy;
`definition.coordinates[]` is the order of every pose vector (base-model
joint names, unit radian or metre); `selection.body[]` is a value per
parameter (unlisted parameters keep their rest value); `selection.poses[]`
are `id` and `values`.

**`anatomy_result`**: references the anatomy, and the run when there was
one; `definition.coordinates` as in the run; `definition.skin` the skin
tissue's id and counts; `selection.body` the values solved with;
`solver` (name, version, method, settings); `poses[]`: `id`, `origin`
(`requested` from the run, `sampled` by the solver), `values`, convergence,
`skin_positions` (f32×3 per skin vertex, posed, metres), `strain` (f32×5 per
skin triangle: principal stretches λ1 ≥ λ2, then λ1's direction as a unit
vector in the triangle's rest plane), optional `fit_error` (f32 per skin
vertex, metres, against the reference the result names).

Strain is stated in the rest frame, not a UV frame: the anatomy carries no
UVs, and the consumer that wants UV-space strain (wrinkle maps, pipeline
§2a) maps the rest-plane direction through its own UV Jacobian. Stretches are
ratios (1 = rest, < 1 compression), the eigen-decomposition of the
triangle's in-plane deformation from rest to pose.

## What `validate-bundle` checks beyond the JSON Schema

- Ids unique in every anatomy-family array; every id pointer names an entry
  (`x-id-in` in the schema).
- `anatomy`: exactly one `skin` tissue; a muscle's fibre directions have one
  entry per surface vertex.
- `anatomy_run` / `anatomy_result`: exactly one `anatomy` reference (and at
  most one `anatomy_run` in a result); every pose has one value per
  coordinate; every body value names a parameter of the referenced anatomy.
- `anatomy_result`: `definition.skin` matches the referenced anatomy's skin
  tissue (id, vertex and triangle counts); every pose's slices have those
  counts.

Not checked, because husk reads no MJCF: that base-model names exist in the
MJCF file; mantlecore checks them at its boundary (it loads the file
through MuJoCo). Not checked either: index values inside `.bin` payloads
(triangle indices < vertex count, attachment vertices); the consumer
reading the bytes checks them.

## Rejected

- **A skeleton of husk's own** (bones, joints, axes, ranges as JSON):
  rejected by invariant 1.
- **Tets in the anatomy**: the solver meshes them from the closed surfaces
  (mantlecore's ask), so they would be a derived copy.
- **Strain as a 2×2 tensor in a per-face tangent frame**: needs a frame
  convention both sides implement identically; principal stretches plus a
  rest-plane direction need none.
- **Joint vectors as maps by name per pose**: thousands of poses times ~200
  coordinates; one ordered coordinate list plus arrays says the same once.
- **WoW's `space` (yards)**: invariant 3.

## Not built

- Bone shape overrides (warping the base model's male bone meshes to the
  body, mantlecore "What they don't give"): not in the schema until the
  authoring side produces them.
- Tissue UVs, sculpt maps, the bake's correctives: Blender-side data that
  never crosses to the solver.
- Per-pose dynamics (jiggle): results are quasistatic equilibria.
