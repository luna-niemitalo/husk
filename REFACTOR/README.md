# REFACTOR/ — target pipeline and cleanup punch list

**This directory describes a target, not the current tree.** `DESIGN.md`
remains the authority on why today's code is shaped the way it is; this is
where the shape it's moving toward lives, kept separate on purpose
(`READABILITY.md` §3.10 — current and target never blurred).

**Status (2026-09-15): Stages 1-2 are landed, Stage 3 is deep in progress but
not feature-complete, Stage 4 has two working writers with nothing wired to
call them yet, Stages 5-6 are not started.** `src/sources/` is real —
`Catalog`/`Resolved<T>`/`TextureCatalog`/`ListfileCatalog` (`catalog.{hpp,cpp}`
+ siblings, ~2200 lines) collapse the texture-resolution tiers `AUDIT.md §1.1`
catalogued, and `husk resolve` (`src/cmd_resolve.cpp`) exposes it as a
machine-readable ledger that corpus-scan tasks now consume instead of
re-implementing resolution. `TEXTURE_POOL_RECALL_TODO.md` (steps 1/2/3/5) and
a listfile-perf detour (`listfile_mmap_index`/`listfile_cache`) landed in the
same window. `src/m2_model.hpp`'s `m2::Model` is the landed Stage 2
aggregate. Stage 3's `canon::` types (`Skeleton`, `Mesh`/`Geoset`, `Material`,
`Curve`, `Physics`, `Definition`, `Selection`, `Item`, and the `Model`
composition root) are all implemented as pure value types with real assembly
functions, each convergence-tested against the legacy pipeline on real
fixtures — `canon::` itself is now format-agnostic top to bottom (I1: no
`m2::`/`skin::` type anywhere in `namespace husk::canon`, including its own
`Vec2`/`Vec3`/`Quat` — the M2-consuming assembly logic lives in the sibling
`husk::m2input` module instead), and **every named gap in `AUDIT.md` §7 is
now closed**: material identity + dedup (`canon::Model::materials` matches
legacy's own deduped count exactly, 8/8 on the real `bloodelffemale.m2`
fixture), global-sequence/alias animation resolution, external-`.anim`
resolution (both inline-bones and `.skel`-sourced), and — the last and
biggest blocker — real `.skel`-sourced coverage (`m2input::
ExternalSkeletonSource`, closing what had been zero real `--compare-canon`
coverage for the majority-case player-character models). Texture resolution
is verified end to end against real, resolved-to-real-bytes CASC data (not
just structurally): `compareMaterialBlendModes` now checks resolved texture
identity against legacy's own `baseColorTextureFileDataId` too, not just
blend mode, and comes back clean. `canon::Model` is not yet formally declared
a like-for-like replacement — no full structural pre/post diff has been run
across the whole real fixture set the way `AUDIT.md §7`'s own preamble
frames as Stage 3's actual gate — but every concretely-named coverage gap
found while building `--compare-canon` is closed and verified clean
(`bloodelffemale.m2` inline-bones and `bloodelffemale_hd.m2` `.skel`-sourced,
both 0 deviations across mesh/skeleton/animations/materials). The broader
whole-corpus structural gate hasn't formally run yet, but the real visual
(rendered) check now has automated tooling behind it (2026-09-15,
`tools/render_lean_glb.py` + `tools/compare_canon_render.py`: renders a
`--compare-canon` pair's legacy `.glb` and `.canon.glb` through the same
extras-blind headless-Blender importer at a forced literal rest pose
— `pose_position = 'REST'`, robust against NLA-track pose leakage, not just
clearing `.action` — and pixel-diffs the two stills) — see `AUDIT.md §7.4`
for what it found on first real use. Stage 4 has two real, independently
tested writers (`src/writers/bundle_writer.*`, `src/writers/gltf_lean.*`),
but nothing in `cmd_export.cpp` calls either of them — `husk export
--compare-canon` (opt-in, off by default) runs them as a diagnostic sidecar
alongside the still-shipping legacy pipeline, not a replacement for it.
Texture payload embedding — found missing by the render-diff tool's first
real run, then closed the same day (`AUDIT.md §7.4`): `canon::TextureRef`
gained an optional `payload` field (bytes + encoding, populated only when
a producer chooses to fetch and embed rather than merely identify a
resolved texture — `BUNDLE_FORMAT.md`'s "Embed or reference" decision,
made at resolution time, same place as every other pre-resolved-input
pattern here) and both writers now use it: `gltf_lean` embeds a real
`images`/`textures` entry and sets `baseColorTexture`; `bundle_writer`
writes a real `textures/<name>.png` file with a real manifest `uri`,
matching `BUNDLE_FORMAT.md`'s own shape sketch for the first time. Only
`Png` is reachable through the catalog today (intake still decodes BLP
before canon:: sees the bytes) — but the real block-preserving decoder
`BUNDLE_FORMAT.md`'s settled DDS target needs turned out to already exist
(`src/blp.hpp`/`.cpp`, independent of the separate Python `blp/` tool):
`blp::extractRawPayload`/`blp::encodeDds` (`AUDIT.md §7.4`, closed the
same day) return real mip0 bytes verbatim and wrap them in a real,
standard DDS container, verified against a real 512×512 fixture and a
real independent public reader (Pillow). Not yet wired into
`sources::Catalog`/the export pipeline itself — that's the remaining,
separate step.
`src/formats/`, `src/canon/`, and `src/writers/` as their own *subdirectories*
(as opposed to the flat `canon_*.cpp`/`writers/` files that exist today) never
materialized — a cosmetic deviation from this document's original sketch, not
a blocker.

Same conventions as `TODO/`: each file is an open punch list, closed items get
removed outright, git history is the record.

**Citation shorthand** used throughout this directory, since both design
documents number their sections from 1 and would otherwise collide:

- *Canonical Data Model §N* → `POTENTIAL_PLAN/WoW RE — Canonical Data Model and Implementation Boundary.md`
- *Eventual Goals* → `POTENTIAL_PLAN/Eventual Potential Goals for WoW RE Project.md`.
  Note this file concatenates three documents that each restart their own
  section numbering, so it is cited by section *title*, never by number.

## Why

husk grew outward from `cmd_export.cpp` rather than down through named stages.
The result, in Luna's own framing: *"different parts search different files and
different methods from different directions, and then we have situations where 3
different pieces resolve the same data in 3 slightly different incorrect ways."*

That is literally measurable — texture-slot resolution alone has four
independent implementations that already disagree, and one of them silently
dropped a resolution tier for weeks without any test noticing. `AUDIT.md` is the
full inventory with file:line evidence.

The goal is not more features. It is that each question has exactly one place
that answers it, and each consumer has exactly one place to look.

## The target pipeline

Four stages, in Luna's own words: *input data parsing → adjacent data resolution
→ internal representation → converting the data to target format*.

```
   .m2 .skin .skel .bone .anim .phys .blp .db2 .dbd  listfile
                          │
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 1. FORMATS   src/formats/                    │  bytes → structs
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 2. SOURCES   src/sources/                    │  where do the bytes live
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 3. CANON     src/canon/                      │  what does it mean
   └──────────────────────┬───────────────────────┘
                          ▼
   ┌──────────────────────────────────────────────┐
   │ 4. WRITERS   src/writers/                    │  how is it delivered
   └──────┬──────────────────────┬────────────────┘
          ▼                      ▼
     husk bundle            glTF (.glb)
     (primary)              (optional projection)
          ▼
     Blender addon
```

| Stage | Home | Owns | Must not contain |
|---|---|---|---|
| 1. input parsing | `src/formats/` | bytes → structs: m2, skin, skel, bone, anim, phys, blp, db2, dbd | filesystem search, defaults, policy |
| 2. adjacent resolution | `src/sources/` | *where* bytes live: the catalog, listfile, sidecar conventions, DB2 table cache | semantic interpretation |
| 3. internal representation | `src/canon/` | meaning: definition / selection / semantic resources | paths, tinygltf, bpy, GPU types |
| 4. target conversion | `src/writers/` | bundle (primary), glTF (projection), JSON (info/dump) | re-resolving anything |

## Input/output modules — a real design decision, deliberately deferred

Luna's own framing (2026-09-15): stages 1+2 above (parsing + resolution) are
an **input module**, stage 3 (`canon::`) is the **core**, stage 4 (`writers::`)
is a set of **output modules** — and each input/output module should be able
to be added or removed without the others caring, the same way `m2` isn't
the only input format canon:: should ever accept (a future `gltf`-as-input
parser is a different input module's concern, not canon::'s — canon:: is the
representation, an input module is "the solver who finds the connections
and/or logs why a connection could not be found"). Concretely, a CLI shape
like:

```
husk --input example.m2 --input-module m2 --output example.glb --output-module glb
husk --input example.m2                   --output example.glb              # both auto-derived from extension
husk --input example.m2                   --output example/ --output-module bundle  # ambiguous by path alone, needs the flag
```

**Decided, not yet built: no module registry or dispatch mechanism yet.**
A real registry (`InputModule`/`OutputModule` structs, a name→module lookup
table, `--input-module`/`--output-module` CLI flags with extension-based
auto-detection) is real, scoped work — but building it now would be
designing an interface against a single real implementation of each side
(`m2` in, `gltf`/`bundle` out), which is exactly the "interface designed
before a second real case exists to prove it against" trap. Deferred until
a second real input format (e.g. glTF-as-input) or a second real output
target actually exists to validate the interface shape against, per this
project's own established `feedback_dont_start_big_refactors_mid_design`
discipline.

**What already satisfies the *separation* half, without a registry**: the
current tree keeps the three layers structurally independent, just wired by
direct calls instead of a lookup table. This was NOT true when this
paragraph was first written the same day (2026-09-15) — `canon::assembleModel`
still took `m2::Model`/`skin::Batch`/`skin::Submesh` directly at that point,
a real, found-and-fixed drift (`AUDIT.md` §7.3 has the full account). Now
genuinely true: `m2::loadModel`/`skin::` parsing plus `sources::Catalog`
resolution, orchestrated by the new M2 input module (`husk::m2input::
buildCanonModel`, `m2_canon_input.hpp` — a sibling of canon::, not part of
it) hand fully pre-resolved, plain canon:: values to `canon::assembleModel`
(core: `Skeleton`, `Mesh`, a deduped `std::vector<Material>`, and
`primitiveMaterials` — no m2::/skin:: type anywhere in its signature),
whose `canon::Model` output is consumed by `writers::writeLeanGlb`/
`writers::writeBundle` (output-module-shaped code) with no knowledge of
`m2::`/`skin::`/`sources::` at all. A second output module or a second
input module can be added today by writing a new function with the same
shape and wiring it into `cmd_export.cpp` directly — the only thing missing
is the registry/CLI-selection layer that would let that wiring happen by
name/extension instead of by editing `cmd_export.cpp`'s own call sites.
That's the concrete, scoped follow-up this section exists to name, not to
build yet. Still open, per `AUDIT.md` §7.3: `assembleMesh`/
`assembleSkeleton`/`assembleMaterial`/`assembleBoneAnimation` are textually
declared inside `namespace husk::canon` while still taking `m2::`/`skin::`
types directly — the input-module/core namespace boundary isn't fully
honest yet, only the composition root (`assembleModel`) itself is.

## Invariants

Stated once here. Every other file in this directory references them by number
rather than restating them.

**I1 — no paths in `canon::`.** Stage 2 resolves; canon holds semantic resource
references; the *writer* is what mints relative URIs. If a `std::string` holding
a filesystem path appears in a `canon::` struct, the boundary has leaked.

**I2 — one implementation per resolution question.** Two callers needing the
same answer both call the catalog. No mirrors, no "second opinion"
reimplementations inside the product. (Corpus *excavation* tooling is a
deliberate, scoped exception — see `RESOURCE_CATALOG.md`.)

**I3 — a consumer opens what the manifest names, and nothing it does not.** No
directory searching, no "extra data dir" parameter, no shelling out to husk.
Following a reference the manifest itself declares — including one pointing at
another bundle's manifest, see `BUNDLE_FORMAT.md` — is not an exception to this;
it is the rule working. The distinction is between being *told* where something
is and going *looking* for it. `GearItem::auxGlbPath`
(`src/cmd_export.cpp:695-705`) is the existing good precedent: husk resolves
once and bakes a relative path in. Generalize it; don't reinvent it per feature.

**I4 — resolution is self-announcing.** Which tier fired, from which directory,
via which fallback, is a *read operation* (`CLI.md` §2.4/§2.11), not something
inferred from an export summary after the fact. This is also what makes the
migration gates below affordable to run.

**I5 — definition ≠ selection.** Canonical Data Model §4: "NightElf.face[12]" (what
option 12 *is*) and "this character chose 12" are different kinds of fact. Today
`chr_customization_options` (definition) and `enabled_geosets` (selection) are
sibling extras keys with no structural distinction between them.

**I6 — identity is an ID; a name is decoration with a stated source.** Most
human-readable naming here comes from the community listfile (unreliable at
best) or is husk-synthesized. Every reference carries its real identity *and*
where its name came from, so a downstream consumer can tell a listfile guess
from a DB2 fact from a string embedded in the M2 itself.

**I7 — item identity is not slot identity.** One appearance item may occupy more
than one equipment slot and contribute more than one mesh/geoset region — a
tunic whose hem is a leg-slot piece is one garment, not a chest thing and an
unrelated legs thing. Canon models `item → {slots[], components[]}` so a
consumer can eventually load *the tunic*, not "the chest half of the tunic".
**Preserving this possibility is required; implementing outfit-level loading is
not.** The constraint is only that nothing in the model may make it impossible
later.

**I8 — husk preserves the payload, never the proprietary container; readability
is a transform it owes, not a conversion it bakes in.** Luna's rule, stated for
the whole project, in three parts:

- **Nothing proprietary is stored.** A proprietary input — an M2, a BLP, a DB2 —
  is rehoused into a format a publicly available tool can open. husk is *the*
  tool with built-in support for this data; it is never the *mandatory* one. A
  headerless dump of extracted bytes fails this too: "not proprietary" is not the
  same as "openable", and the bar is the second one.
- **Stored data is human-readable, or trivially transformable to it.** The escape
  clause is what lets a compressed payload stay compressed — and the obligation
  it creates is real: **a binary payload is only permitted where husk has a verb
  that emits its human-readable equivalent.** Manifests, indexes and anything
  describing structure are human-readable unconditionally (JSON).
- **Convert on output, never on intake.** This is the half that is easy to get
  backwards. Converting at ingest looks like it satisfies the rule above, but it
  spends something irreversible to buy convenience that was one command away. A
  canonical store that cannot reproduce its own input is not canonical.

The three combine into one move: keep the *payload* bit-exact, swap the
*container* for an open one, and generate readable projections on demand. See
`BUNDLE_FORMAT.md`'s "Texture encoding" for the case that forced this to be
written down — BLP's DXT blocks preserved verbatim inside a standard DDS
container, with PNG emitted on request.

## No gate is "output is unchanged"

Current exports have never been visually correct. Byte-identity would therefore
enshrine the existing wrongness as the acceptance criterion for a refactor whose
whole point is giving that wrongness fewer places to hide.

Every gate below is instead **"every difference is explained and attributed"**.
A difference that turns out to be a fix is a result to record, not a regression
to revert. Part of the hope for this work is precisely that consolidating four
drifting implementations into one exposes *why* things look wrong today.

## The documents

| File | Scope | Gate |
|---|---|---|
| `../CANONICAL_FORMAT.md` (repo root) | The canonical conceptual explainer of `canon::`/the bundle format, written for a reader outside this project | Reference doc, not a stage |
| `AUDIT.md` | Evidence inventory: every duplicated, divergent, or counter-intuitive path, with file:line | Independent |
| `RESOURCE_CATALOG.md` | Stage 2 — the one resolution boundary object, and the excavation escape hatch | Independent; carries a reopened design question (persisted cross-patch reference-integrity index) — needs a decision, see Blockers below |
| `CANONICAL_MODEL.md` | Stage 3 — the semantic model, built pure | Independent to design; the migration itself is the expensive stage |
| `BUNDLE_FORMAT.md` | The native husk bundle + manifest schema; glTF's demoted role | Independent; carries a reopened design question (physical storage shape at corpus scale) — needs a decision, see Blockers below |
| `BLENDER_ADDON.md` | Stage 4 consumer — packaging, and deleting every discovery mechanism I3 forbids | Independent to build; final visual pass is Luna's |
| `CLI_AND_TOOLING.md` | Flag surface, structured output, corpus-tooling cleanup | Independent |

Out of scope, deliberately: everything in `POTENTIAL_PLAN/` about engines, GPU
backends, and GFX906. The canonical model is designed so those stay *possible*
(Canonical Data Model §10's architectural test), not so they happen next. Current
trajectory is `M2 → canonical → Blender`.

## Blockers, scoped to this refactor

**As of 2026-09-03, nothing design-level blocks starting Stage 2
(`src/formats/`, `m2::Model`) or Stage 3 (`canon::`, per `CANONICAL_MODEL.md`,
which reads as essentially fully designed and settled already).** What's left
is implementation effort, not an open decision — the two design questions this
directory had open are now resolved or explicitly deferred:

- **`BUNDLE_FORMAT.md`'s physical storage shape — settled.** Loose files, one
  directory per bundle, unchanged from the original design. ZFS (this
  project's real filesystem) already dedups shared payload bytes at the block
  level, which was the concrete cost in question; a content-addressed backing
  store would buy the same thing at the cost of a real extra layer of
  indirection, not worth taking on pre-emptively. Revisit only if the
  file-count/metadata cost (not the byte-dedup cost, which ZFS already
  handles) is ever actually measured and found to bite.
- **`RESOURCE_CATALOG.md`'s persisted cross-patch reference-integrity
  index — still open, but non-blocking.** Whether husk should maintain a
  patch-versioned edge log (what points at what, first/last seen) separate
  from `dangling_references_task.py`'s ephemeral scan output and
  `knowledge.sqlite`'s resolution-answer cache. Doesn't gate Stage 1-4; safe
  to leave open and revisit later.

The actual next bottleneck is Stage 3 itself: `CANONICAL_MODEL.md`'s own
words, "the migration itself is the expensive stage" — a large, invasive
rewrite (`gltf_*.cpp` rebuilt as a writer over `canon::`) that per this
project's own feedback pattern (wide refactors started mid-design get thrown
away) should be driven with you steering it live, not delegated wholesale.

## Migration order

Each stage lands independently. Ordered so the highest-drift-risk duplication
dies first, and nothing waits on the bundle format.

**1. Catalog** (`src/sources/`) — collapse the four texture-resolution and four
FileDataID→path implementations onto one object; add `describe()`. **Landed**
(Aug 29-30 2026, see Status above) — `husk resolve` is the ledger; whether its
gate (the attributed before/after diff on the four real fixtures) was actually
run and recorded is not confirmed by this file and should be checked before
treating the stage as closed, not just built.

**2. `m2::Model` aggregate** — the three commands stop each assembling their own
partial view.
*Gate*: `husk info` / `dump-chunks` output diffed on real fixtures, every
difference attributed. The three commands parse genuinely different subsets
today, so unifying them can legitimately surface fields one of them was silently
omitting.

**3. `canon::`, built pure** — the three-layer model with `Ref`-typed
references, first-class geosets, unified curves, version-collapsed physics,
declarative population. `gltf_*.cpp` is then rewritten as a writer over it. The
expensive stage, and the one that must not be shortcut.
*Gate*: the 39 parse-tier test files must need no edits at all — if they do,
stages 1 and 3 have leaked into each other. Full suite green. Structural (not
byte-level) pre/post glTF comparison on the fixture set: same primitive / joint
/ clip / material inventory, every difference attributed.

**4. Bundle writer + manifest schema v1.**
*Gate*: new round-trip tests; the existing glTF conformance suite still green
(the projection must not regress).

**5. Blender addon** reading the manifest; delete every discovery mechanism I3
forbids.
*Gate*: a headless import test (`tests/blender_import_check.py`'s pattern)
proving zero arguments and zero husk dependency. The real interactive pass is
Luna's own — the same human gate `TODO/CHAR_TEXTURE_BLENDER_SWITCH_TODO.md`
already carries.

**6. CLI regroup + structured output + corpus-task conversion.**
*Gate*: completions regenerated via `--print-completion` (never hand-edited);
converted tasks reproduce their last known corpus numbers
(`corpus_reports/corpus_scan_22_08/`).
