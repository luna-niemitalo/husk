# CANON_DESIGN.md — design records for `canon::` types and the writers over them

Why the `canon::` value types, their assemblers, and the two writers built
directly on `canon::Model` are shaped the way they are. Moved out of
`DESIGN.md` on 2026-11-09 (it was written there as each piece landed,
2026-09-06/07). The types themselves are defined by their headers
(`src/canon_*.hpp`, `src/writers/*.hpp`); this file keeps only the
reasoning. Stage status: `README.md`; open gaps: `AUDIT.md`. Invariants
I1–I8 are `README.md`'s.

Where the code has moved on since a decision was recorded, the entry says
so and names the file that now holds the answer.

## `canon::` bone naming — three tiers, chunk-based structural stems

Built: `src/canon_bone_naming.*`, `src/canon_bone_stem.*`,
`canon::Joint::structuralLabel` (`src/canon_skeleton.hpp`). Replaces the
retired glTF pipeline's fallback `"bone_<index>"`, unique but unreadable.

**Three tiers, all stored, preference in this order:**

1. **Hand-authored** — a `m2::keyBoneName(Bone::keyBoneId)` hit
   (`NameSource::M2Embedded`).
2. **Consistent** — a structural label computed for *every* bone, named or
   not, deterministic for a model's bone order.
3. **Guaranteed-unique identity** — `Ref::id` as `RecordIndex{boneIndex}`.

`canon::Ref` keeps one identity and one preferred name (hand-authored beats
synthesized). `canon::Joint` carries the consistent label in its own field
unconditionally: Blizzard's name and husk's derivation are different facts,
and one slot would lose one of them. Consumers pick: index for joins, label
for tooling, `ref` name for display.

**Format**: `<stem>_<side>_<index>.<subindex>`, e.g. a left leg
`a1a_L_0.0 -> a1a_L_1.0 -> a1a_L_2.0`.

- `index` walks position along a chain. `subindex` is a counter, not a
  fraction (`1.32` is position 1's 32nd auxiliary slot), for short branches
  (≤ 2 bones) off one position, e.g. an attachment bone on one leg:
  `a1a_L_1.1`.
- A branch longer than 2 bones becomes its own chain nested under its
  parent position: `a1a_L_3_sub_qwq_0 -> a1a_L_3_sub_qwq_1`; a second long
  branch at the same position gets its own stem (`a1a_L_3_sub_owo_0`).
  Nests recursively.
- A mirrored limb's sub-chains reuse the opposite side's child stem; only
  `_L`/`_R` differs.

**Symmetry is geometric**: two bones whose bind-pose pivots mirror across
the model's sagittal plane, with structurally mirrored parent chains too
(not position alone), are an `_L`/`_R` pair. Rejected: names-only (covers only bones `keyBoneName`
already names, i.e. none of the ones this exists for) and a hybrid.

**Stem ranking follows chunking theory, not repeat-avoidance.** A patterned
sequence collapses into fewer memory chunks than an arbitrary one (Miller,
and later chunking/compression work); a first draft scored the opposite way
until Luna corrected it.

- Repeats need not be contiguous: `a1a`/`aqa` ("bookend + one middle") cost
  two chunks, same as `aa1`. Candidates are scored by **position template**:
  `XXX` best, then `XYX`/`XXY`/`YXX`, worst `XYZ`.
- Across chains in one skeleton, **vary template shape before the filler
  character**: `a1a`/`1aa`/`aa1` read as distinct; `aa1`/`aa2`/`aaq` differ
  in one position and are the confusable family.
- Legibility filter, separate from scoring: `l`/`1`, `O`/`0`, `Z`/`2`, `1`/`7`
  cause over half of alphanumeric misreadings (Bell Labs study, via DTIC
  AD0647371 and Pharmacy Times, "Misidentification of Alphanumeric Symbols
  in Electronic Communications"). Digits stay; the letters `i`/`l`, `o`,
  `z` are dropped. `1`/`7` remains a residual risk (dropping a digit would
  recreate the problem).
- Stems are assigned to chains in parse order, so the same model always
  produces the same labels. The exact ordering is otherwise unspecified
  beyond "deterministic" (`canon_bone_stem.cpp` documents its choice).
- The vary-shape-before-filler rule is a reasoned synthesis of Luna's
  correction and general chunking theory, not a result from one paper.

**Applies more broadly: the precedent for every "index but no trustworthy
name" case.** Next
candidate: sequences, since `AnimationData.db2` has no `Name` column in
current clients (`DESIGN.md` "Human-readable names"). Not scoped or
started; how "chain" and "symmetry" map onto sequences is open.

## `canon::` material design

Types: `src/canon_material.hpp` (the definition). Assembly:
`src/m2_material_input.*` (M2), `wmo_canon_input.*` (WMO). Material
*documents* (composites, function vocabulary) are a later design:
`MATERIALS.md`.

**Framing.** Designed for N consumers and one real, often incomplete
producer (M2), not as a rename of `gltf::Material`. The legacy shape is
read only as evidence of what M2 can express.

**Real facts checked before designing anything:**

- Legion+ M2 has **no PBR map slots** (no normal/roughness/metalness). The
  only non-diffuse texturing it asserts: one unresolved specular-ish
  `TextureType` role (`itemappearance_db2.hpp`) and an env-map UV mode
  (`textureCoordComboIndex == -1`, a runtime reflection vector).
- Layering is a **per-batch ordered texture-unit stack**
  (`M2Batch.textureCount`), combined by one of ~20 named `Combiners_*`
  formulas keyed by `shaderId`. husk resolves the formula *name*
  (`m2::resolveShaderNames`); the formulas' maths is open
  (`TODO/PIXEL_SHADER_FORMULAS_TODO.md`).
- Tint/fade/UV-transform are **per-batch** with no join ambiguity
  (`skin::Batch::colorIndex`/`textureWeightComboIndex`/
  `textureTransformComboIndex`), unifiable via `canon::Curve`.
- **Character-customization compositing is a separate mechanism.**
  `CharComponentTextureLayouts`/`Sections`/`TextureLayer` reference no mesh,
  vertex or UV: image-space data in Blizzard's own model.
- UV *values* are per-vertex mesh data; UV *selection* is per-batch.
- Producer unreliability is real: a slot can have several same-basename
  candidates with no in-file tiebreak (legacy `AlternateTextureCandidate`).

**Decisions:**

- **The ordered layer stack is the truth; named slots (`diffuseLayer`,
  `specularLayer`, ...) are references into it.** Most layers have no role
  the data asserts (a two-diffuse combiner has no PBR mapping); a
  named-slots-only model would invent facts. A naive consumer ignores
  role-less layers; a PBR consumer takes the typed path. No
  `normalLayer`/`roughnessLayer` until a producer populates one.
- **`TextureRef` has three states**: `Resolved`, `KnownUnresolved` (slot
  asserted, no bytes; carries a reason), `Ambiguous` (all candidates kept,
  not collapsed to a guess).
- **`UvRef` is a typed selector, not a compatibility matcher.** The client
  never matches: `textureCoordComboIndex` is fixed at authoring time.
  `EnvironmentMapped` is a tagged case instead of a `-1` sentinel, and a
  mesh isn't capped at two UV fields.
- **`LayerRole` is `KnownRole` or an open string**, so a non-M2 producer
  isn't forced into M2's vocabulary.
- **Layer identity is a `Ref`, never a vector index** — same reasoning as
  bones and geosets.
- **A layer carries its own tint, alpha fade and UV animation curves**;
  `uvAnimation`'s shape was left TBD at implementation time and became
  `TextureTransformCurves` (one curve per `M2TextureTransform` field).
- **`BlendOp` is the real `EGxTexOp` vocabulary** (wowdev.wiki Rendering
  "Texture Blending"), shared by `MaterialLayer::blendIntoPrevious` and
  `Function::Stage` so there is one combiner vocabulary.
- **Atlas compositing is a mesh-independent resource.** Deferred: checking
  that the mesh's UVs actually sample the right atlas regions needs both.
- **Geoset filtering is not a material concern**: geosets are mesh
  topology (Selection), compositing is Resources.
- **Materials are independently exportable**: a material is a `Ref` like
  an equipped item, so it can live in its own manifest
  (`BUNDLE_FORMAT.md` "Embed or reference — one rule").
- **Two "material preview" capabilities, kept apart:**
  1. A wireframe of where each patch sits, from `Section` rects normalized
     by the atlas `width`/`height` — buildable with no mesh; a true UV
     wireframe needs one. Assumes rects and mesh UVs share `[0,1]²`;
     **unverified** against a real character's UV bounds vs. Section
     alignment — check before relying on it.
  2. A rendered preview of the composited stack. Canon stays
     **resolve-only**: it makes the stack, formula identity and curves
     complete enough for a separate preview tool; it does not implement
     `Combiners_*` maths ("husk resolves, never applies").

**Review flags recorded at assembly (2026-09-07):**

- A batch can animate `M2Color::alpha` *and* `M2TextureWeight::weight`
  (multiplied together); `MaterialLayer` has one `alphaFade`, and the
  assembler takes colour's. Still open (`m2_material_input.cpp`).
- `blendMode -> BlendOp` approximated a framebuffer blend onto a texture-
  combiner op. **Resolved**: `canon::Material::framebufferBlend`
  (`FramebufferBlend`) is now its own axis.
- One sequence's curves per material: see "`canon::Model`" below.

## Shading-function descriptions: a scaffold, not a one-shot transcription

Goal: a material manifest reconstructable with no husk binary, so the
formula name (`Combiners_X`) must travel with what it *means*. Types exist
(`ShadingFunction`, `BaseFunction`, `Function`, `Confidence` in
`canon_material.hpp`); **nothing populates them**. The transcribed
formulas live instead in technique documents (`techniques/`,
`schema/technique.schema.json`; `BUNDLE_FORMAT.md` "Techniques"), read
from the client's compiled shaders (`WIKI_FINDINGS/M2/rendering.md`),
which materials reference by name.

**The formulas are settled; the "four base functions" grouping is not.**
Every combiner's formula is read from the client's own shader bytecode
(`WIKI_FINDINGS/M2/rendering.md` "Formulas", carried as data in
`techniques/m2_pixel_shader/`); the wiki's formulas are only a cross-check
there. Still unchecked is the hypothesis that most `Combiners_*` reduce to
**alpha clip, alpha blend, additive overlay, multiplicative overlay** plus
parameters, the multiplicative tint source varying (white, black, an
environment sample). That table is where to test it.

- Two tiers if it holds (`BaseFunction` + per-formula `ShadingFunction`);
  degrades to one `ShadingFunction` per formula with no shared base if not.
- `ShadingFunction` = identity (`Synthesized` name, e.g.
  `Combiners_Mod_Add`), base, confidence, and three views of decreasing
  readability: `function` (machine-readable, ground truth; AST vs. DSL
  undecided), `math` (derivable from `function`), `description` (human
  prose, when one exists).
- `tintSource`'s shape is the open transcription work; not specified.
- **`Confidence`** — `Verified`, `Assumed`, `Ambiguous`, `GenuinelyUnknown`
  — is a different axis from `TextureRef::State`, so a different
  vocabulary. A `GenuinelyUnknown` formula still renders: it falls back to
  a documented default base function, chosen from real corpus frequency by
  whoever transcribes (the `AlphaClip` default in the header is a stated
  placeholder).

## `canon::` definition assembly

`canon::Definition`/`Option`/`Choice`/`Category` (`canon_definition.hpp`):
I5's "what exists and is valid," scoped to one `ChrModelID`.
`canon::assembleDefinition` (`canon_definition_builder.*`) groups
`chrcustomization::namedChoicesForModel`'s flat list into that shape,
reusing its DB2 join instead of re-deriving it.

- **Sorted**: options by `optionOrderIndex` (tie: `optionId`), choices by
  `choiceOrderIndex` (tie: `choiceId`) — deterministic and the
  character-creation UI's order, not table load order.
- **Excluded**: `Material`/`fileDataId`/`boneSetId` and anything else that
  realizes a choice — that's Resources; mixing it in is what I5 forbids.
- `NameSource::None` covers both nameless cases met (swatch-only choice,
  `Name_lang == "0"`; unresolved `ChrCustomizationCategoryID`).
- Test: `tests/test_canon_definition_convergence.cpp`, incl. real
  `test_data/db2/chrcustomization{option,choice,category}.db2`.

## `canon::` selection assembly

`canon::CharacterSelection`/`CreatureSelection` (`canon_selection.hpp`):
I5's "what did this instance choose."

- `assembleCreatureSelection` copies `creaturegeoset::resolveDisplay`'s
  `geosetId`s (`(GeosetIndex+1)*100+GeosetValue`) **unsorted and
  undeduplicated**, matching `attachCreatureGeosets`, real duplicate rows
  included. Test is synthetic-only (no real
  `creaturedisplayinfogeosetdata.db2` fixture in `test_data/db2`).
- `assembleCharacterSelection` is thin: each choice id becomes
  `Ref{Db2Row{"ChrCustomizationChoice", id}}`, no name, no sort (the list
  as given). Each `canon::Item`
  reduces to `EquippedItem{identity, slots}`; `components` are dropped
  (Resources data; a consumer re-joins on `identity`). Order preserved.

## `canon::` item assembly

`canon::Item`/`GeometryComponent`/`SectionOverlayComponent`
(`canon_item.hpp`): I7, items by identity, not slot.
`canon::assembleEquippedItems` (`canon_item_builder.*`) builds them from
`itemappearance::Data` (+ optional `modelfiledata`/`texturefiledata`) for
`appearance::GearEntry`s, reusing `itemappearance::resolve`.

- **The I7 payoff**: entries sharing one `itemModifiedAppearanceId`
  (`gear=MAINHAND:15,OFFHAND:15`) collapse into **one** `Item` with several
  `slots`, resolved once. The retired glTF pipeline's `attachGearAppearance`
  emitted one unlinked duplicate per entry.
- Otherwise mirrors that function: a `GeometryComponent` only when
  `ModelResourcesID` is nonzero or materials exist (zero means "no
  standalone geometry", not unresolved); a missing optional table leaves
  that FileDataID unresolved, never fails; an entry with no real
  `ItemDisplayInfoID` is skipped, not fabricated.
- Test: `tests/test_canon_item_convergence.cpp`, synthetic only, with a
  `doctest::skip`-gated real-data case waiting for a fixture set.
- `export --appearance` builds its gear with it (`cmd_export.cpp`).

## `canon::Mesh`/`canon::Model` — geometry and the composition root

`canon::Mesh` (`canon_mesh.hpp`) and `canon::Model`/`assembleModel`
(`canon_model.*`); M2 input: `m2_mesh_input.*`, `m2_canon_input.*`.

- **One flat model-wide vertex array** (`positions`/`normals`/`uv0`/
  `skinning`), `indices`/`primitives` slicing into it; `primitives` reuses
  `assemblePrimitiveGeosets`.
- **Raw M2 space** (`v.pos`, `v.normal`, `v.texCoords[0..1]`): axis
  conversion is a writer's job, not a fact about the source.
- **Skinning is empty when every vertex weight is zero** — unlike legacy
  `buildSkinning`, which runs whenever the model has bones. M2 has no
  "unskinned" header bit (checked `m2_header.hpp`/`m2::Model`).
- **`uv1` is `nullopt` when every vertex's second UV is the origin.** Usage
  is per-shader (wowdev.wiki `tex_coords[2]`), no header flag exists, and
  legacy `buildBaseMesh` fabricates it unconditionally. A canon-level
  policy, not a mirrored fact; flagged for review.
- **`assembleModel` is wiring only**: every fact comes from the
  separately convergence-tested assemblers.
- **Materials vs. primitives**: designed as `materials[i]` ↔
  `primitives[i]` (legacy `buildMaterialsAndPrimitives`' per-batch order).
  **Superseded**: materials are deduplicated and
  `Model::primitiveMaterials` maps primitive → material (`canon_model.hpp`).
- **Alias sequences**: originally excluded (inline `0x20` only).
  **Superseded**: `m2_canon_input.cpp` resolves `aliasNext` chains.
- **Open — one sequence's material curves**: `MaterialLayer` holds one
  curve per property, so material assembly takes a single `sequenceIndex`
  (the first inline sequence, else 0). A multi-sequence model's other
  sequences' tint/fade/UV animation is not represented. Needs a
  per-sequence curve list or a multi-material shape.

## `husk::writers::writer_common` — shared bind-translation/curve maths

`writers::localBindTranslation`, `writers::composeJointCurves`
(`src/writers/writer_common.*`), used by every writer over `canon::Model`.

- **Namespace `husk::writers`, directory `src/writers/`** — the first
  module to claim its target subdirectory; `husk::canon` kept flat
  `canon_*.cpp` names. `CMakeLists.txt` lists sources explicitly.
- **Raw M2 space (I1)**: no axis conversion; each writer converts after.
- `localBindTranslation`: root → `globalPosition`; else `globalPosition`
  minus parent's. **Re-validates** joint and parent indices and throws:
  `assembleSkeleton` guarantees them, but `canon::Skeleton` is a plain
  struct a test or bundle reader can build directly.
- `composeJointCurves`: `nullopt` when the clip has no curves for the
  joint; translation = bind translation + raw delta; rotation and scale =
  raw deltas (bind pose has no rotation; bind scale is `(1,1,1)`, legacy
  `toGltfScale` is a pure axis swap).
- **No hemisphere-continuity fix here.** Raw `M2CompQuat` keyframes are
  already continuous in M2 space; the discontinuity legacy
  `enforceHemisphereContinuity` fixes comes from `rotationZUpToYUp`'s
  quat→mat→quat round trip. Each writer that converts axes owns its own
  fix. Flagged as the one place a subtle bug could hide.

## `husk::writers::gltf_lean` — the lean, extras-free glTF projection

`src/writers/gltf_lean.*`; written by `export --glb <path>`. husk's only
glTF writer, over `canon::Model`. Reuses only generic pieces:
`writers/gltf_math.hpp` (axis conversion, `enforceHemisphereContinuity`),
`writers/gltf_buffer_utils.hpp` (`appendBufferView`/`padTo4`),
`writer_common`.

- **Never emits `extras`**, by construction. Data without a core glTF field
  is in the bundle instead. Measured on `bloodelffemale.m2`: the retired
  legacy `.glb` had 374 `"extras"`, lean 0.
- **Validator: 0 errors** (`tests/test_writers_gltf_lean.cpp`,
  `HUSK_GLTF_VALIDATOR`-gated). `TEXCOORD_n` "may be unused" warnings were
  expected while no material referenced a real texture.
- **Blender structural convergence**: headless import
  (`tests/blender_import_check.py`) gives 1 armature with
  `skeleton.joints.size()` bones and 1 mesh with `mesh.positions.size()`
  vertices.
- **Multi-root skeletons** get one non-joint parent node named `skeleton`
  (`DESIGN.md` "Lean glTF projection").
- **`alphaMode`**: from `Material::framebufferBlend` when set (Opaque →
  `OPAQUE`, AlphaKey → `MASK`, else `BLEND`); otherwise a reasoned fallback
  from the last layer's `blendIntoPrevious` (Replace/Modulate/Modulate2x →
  `OPAQUE`, Decal → `MASK`, Add/Fade → `BLEND`).
- **Inverse bind matrices** from `canon::Joint::globalPosition` directly
  (`-worldPos` translation).
- **One shared index buffer**, each primitive an accessor `byteOffset` into
  it — `canon::Mesh`'s own shape.
- **Animation names** synthesized from `(kind, index)`: `sequence_<n>`/
  `global_sequence_<n>` (`canon::SequenceRef` carries no name).
