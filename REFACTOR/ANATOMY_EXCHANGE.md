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
| `anatomy_result` | solver | What one run solved, per pose: the skin's vertex positions on the fixed topology, its stretch along every edge and face diagonal, optional per-vertex fit error; with the body parameter set, the solved poses and the solver's identity. |

Schemas: `schema/anatomy.schema.json`, `schema/anatomy_run.schema.json`,
`schema/anatomy_result.schema.json`. They are the definition; this file
explains them and records the decisions.

**Status: provisional** (Luna, 2026-11-25: "still kinda flexible until all
of the parts come together"). The shape changes in place, without a
version bump, until the first round trip (authoring side → mantlecore MS7 →
bake) has run on it; no document is kept across such a change.

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
4. **The skin topology is fixed, and quad-based.** The anatomy has exactly
   one `skin` tissue, Multires level 0 of the authoring side's mesh
   (pipeline 2.1), in the authoring tool's own vertex, face and edge order.
   Its faces have 4 or more corners and never 3: Multires splits an n-gon
   into quads cleanly but has to tessellate a triangle, and the GPU side
   draws quads natively (gfx906's quad primitives, quad-domain patches).
   A result's per-vertex, per-edge and per-diagonal arrays are over that
   tissue, in its order; `validate-bundle` checks the counts against the
   referenced anatomy. Other tissues may have any polygons of 3 corners or
   more: the solver triangulates them for tet meshing anyway.
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
  polygon surface at rest: `positions` f32×3, `face_sizes` u8 per face,
  `face_vertices` u32 per corner (faces in order, each face's corners
  counter-clockwise from outside), and on the skin `edges` u32×2 in the
  authoring tool's edge order (faces can't reproduce it, and per-edge
  results must land on its edges by index). A muscle also has the
  base-model muscle it realizes (`base_muscle`) and its fibre direction per
  surface vertex (f32×3, unit, rest frame). Tets are meshed by the solver
  from these surfaces, so none are stored. Faces are a list, not strips:
  strip order can't keep the authoring tool's face order, and a GPU strip
  layout is built from the list on the consumer's side.
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
tissue's id and its vertex, edge and diagonal counts; `selection.body` the
values solved with; `solver` (name, version, method, settings); `poses[]`:
`id`, `origin` (`requested` from the run, `sampled` by the solver),
`values`, convergence, `skin_positions` (f32×3 per skin vertex, posed,
metres), `edge_stretch` (f32 per skin edge), `diagonal_stretch` (f32 per
face diagonal), optional `fit_error` (f32 per skin vertex, metres, against
the reference the result names).

### Strain: stretch along edges and diagonals (Luna, 2026-11-25)

A stretch is posed length / rest length (1 = rest, < 1 compression),
measured along every skin edge and every face diagonal. A face of n
corners has n (n − 3) / 2 diagonals, listed per face in corner-pair order:
corners i < j that aren't adjacent, i ascending, then j (a quad: 0–2,
then 1–3). Edges are stored once, in the edge table's order, since two
faces share each.

Why measured this way: nothing is evaluated at a face centre, and no face
is split, so there is no choice of diagonal for two sides to disagree on
(mantlecore measured the tessellator's own diagonal choice changing results
on curved quads). A triangle's three edges fix its in-plane metric
exactly; a quad's four edges and two diagonals overdetermine it, and the
misfit says how unevenly the quad deformed.

**Rule `edge_metric_strain`**: the principal stretches and direction of
one face, for any consumer that wants them (wrinkle maps, the debug view).
In the face's rest plane (normal by Newell's method over its corners; any
orthonormal basis of the plane, which the result doesn't depend on), take
each of its edges and diagonals as a rest unit direction d_k with stretch
s_k, and fit the symmetric 2×2 tensor C by least squares over
s_k² = d_kᵀ C d_k. The principal stretches are the square roots of C's
eigenvalues, λ1 ≥ λ2; λ1's direction is its eigenvector, given back as a
3D unit vector in the rest frame. The residual is the face's misfit. Rest
frame, not UV: the anatomy carries no UVs, and a UV-space consumer maps the
direction through its own UV Jacobian.

## What `validate-bundle` checks beyond the JSON Schema

- Ids unique in every anatomy-family array; every id pointer names an entry
  (`x-id-in` in the schema).
- `anatomy`: exactly one `skin` tissue, with edges; every face of 3
  corners or more, the skin's 4 or more, and the face sizes summing to the
  corner list (read from the `face_sizes` bytes); a muscle's fibre
  directions have one entry per surface vertex.
- `anatomy_run` / `anatomy_result`: exactly one `anatomy` reference (and at
  most one `anatomy_run` in a result); every pose has one value per
  coordinate; every body value names a parameter of the referenced anatomy.
- `anatomy_result`: `definition.skin` matches the referenced anatomy's skin
  tissue (id, vertex and edge counts, and the diagonal count its face sizes
  give); every pose's slices have those counts.

Not checked, because husk reads no MJCF: that base-model names exist in the
MJCF file; mantlecore checks them at its boundary (it loads the file
through MuJoCo). Not checked either: index values inside `.bin` payloads
(face corners and edge ends < vertex count, attachment vertices), and
whether the edge table matches the faces' edges; the consumer reading the
bytes checks them. `face_sizes` is the one payload husk reads.

## Rejected

- **A skeleton of husk's own** (bones, joints, axes, ranges as JSON):
  rejected by invariant 1.
- **Tets in the anatomy**: the solver meshes them from the closed surfaces
  (mantlecore's ask), so they would be a derived copy.
- **Triangle faces**: the skin is a quad base mesh (invariant 4), and
  triangulating it would bake one diagonal choice into every per-face
  value.
- **Quad strips as the stored order**: a strip reorders faces and needs
  restarts, so per-face results would no longer land on the authoring
  tool's faces by index. Strips are a GPU layout built from the list.
- **Strain as a 2×2 tensor in a per-face tangent frame**: needs a frame
  convention both sides implement identically.
- **Principal stretches at the face centre** (from the bilinear quad's
  derivatives there): a value at one point of a non-planar quad, which
  averages away how unevenly it deformed; edges and diagonals measure it
  instead, and the principal stretches follow from them.
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
