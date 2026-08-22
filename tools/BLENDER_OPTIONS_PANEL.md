# Blender options panel: design notes

`tools/husk_blender_options_panel.py` is a live, persistent Blender N-panel
for browsing and editing a character's real customization-choice menu once
a husk `.glb` export is imported. This doc is the human-readable "why" and
"how it was validated" — the script's own docstring covers the "what",
`tools/test_husk_blender_options_panel.py` covers "does it actually work".

## Why a separate file from `husk_blender_geoset_mask.py`

`husk_blender_geoset_mask.py` is **post-import wiring**: run it once right
after importing a `.glb`, it builds the geoset-switch / texture-switch
Geometry/Shader node graphs, and its job is done.

This panel is a different kind of thing: a UI surface meant to *travel with
the `.blend` file itself*, editable at any later point — reopen the file
next week and the panel should still be there, without re-running any
import script. That's a genuinely different lifecycle, so it's a genuinely
separate file, not a mode of the first one.

## Reference investigated: a real professional rig file

Before designing this, we opened (read-only — it's Luna's own asset, not
touched or modified) `~/Documents/Blender/pharah v5.3.45/Base.blend`, a
large, professional character rig with exactly this kind of options panel,
to see how it's actually built in the wild rather than guessing.

Found via a headless Blender inspection (`bpy.data.texts`, node groups,
custom properties — not by unzipping/parsing the `.blend` by hand): the
whole panel is one embedded `Text` datablock (`PBGv5.py`, ~5000 lines)
with Blender's own "Register" checkbox enabled in the Text Editor — no
external addon install, it just self-registers every time the file opens.
That's exactly the "travels with the .blend" property we wanted, confirmed
working in a real, shipped file rather than assumed.

**The actual architecture, read straight from that source:**

1. One hidden settings object holds a single `PointerProperty` to a
   `PropertyGroup` with ~150 flat fields (one per armor piece / outfit slot
   / hair option / ...), every field carrying an `update=` callback.
2. Every `update_*` callback mirrors its value into a **plain ID-property**
   on the same object (`obj["armor_torso"] = obj.pharah_vars.armor_torso`).
   This is the one non-obvious trick: `PropertyGroup` fields aren't
   reliably driver/constraint-readable and get reset by script-reload
   during dev, but plain custom properties (`obj["key"]`) are stable and
   animatable — the `PropertyGroup` is purely the *UI* layer, the plain
   properties are the actual source of truth everything else reads.
3. **Constraints are re-derivation, not validation.** There's no "grey out
   / disallow this combination" logic living separately from the rest.
   Every `update_*` callback re-runs a `set_*_item_visibility()` function
   that recomputes *every* dependent object's visibility from scratch as a
   plain boolean AND-chain, e.g. `thighs = armor>0 and legs_thighs and
   legs_shins`. "Thighs require shins" isn't enforced by blocking a
   checkbox — it's just a term in thighs' own visibility formula. Toggle
   shins off, thighs go invisible even though its own bool is still True;
   toggle shins back on, thighs reappear, no separate re-validation step.
4. Panels mirror the same relationship in the UI by setting
   `row.enabled = <the depended-on property>` on the dependent widget
   (greyed out, not hidden, so the relationship stays visible).

The one thing this reference *couldn't* answer: Pharah's schema is
hand-authored and fixed (the same ~150 named fields forever). husk's
schema is different per model (a different real `ChrCustomizationOption`
list per `ChrModelID`) — so "one field per option, hardcoded in a Python
class" doesn't work here. That's the part we had to design and test
ourselves — see below.

## Dynamic-schema design: two approaches tested headlessly

Before writing the real script, both plausible designs were built as
throwaway headless-Blender tests (`blender --background --python
<test>.py`, real property registration/get/set/update, not just read
through) to find out which one actually works, not just which one
sounds right.

**Approach A — generate a `PropertyGroup` subclass per model** (`type()` +
`bpy.utils.register_class()`, generalizing Pharah's fixed-class pattern to
a dynamic one). Field values and update-callback closures worked
correctly (no late-binding bug — each generated field's callback closed
over the right option id). **Real bug found**: re-registering a class
under a name that's already registered (the "user imports the same model
twice in one Blender session" case) does **not** raise — it silently
breaks `bpy.types.<ClassName>` lookup for *both* the old and new class.
Data already bound to already-imported objects keeps working (Blender
resolves through the bound RNA reference, not the `bpy.types` namespace),
but anything that looks the class up by name afterward silently fails
with no error at the point of collision. Avoidable, but only by
uniquifying every generated class name per model and never reusing one —
extra bookkeeping this whole approach exists only to avoid.

**Approach B — one fixed, statically-registered `CollectionProperty` of a
generic row struct**, with per-row *data* (not per-row *class*) driving a
dynamic `EnumProperty` `items` callback. Confirmed working: variable
option/choice counts per model, independent per-row dynamic enum item
counts, and a real cross-option constraint recompute (Pharah's
AND-formula pattern, but keyed by `option_id` instead of a hardcoded
field name) all passed — including two models with genuinely different
schemas reusing the exact same two registered classes. Zero
class-registration collisions are possible by construction, since no
per-model class registration ever happens.

**Chosen: Approach B.** It sidesteps Approach A's whole bug class for
free, and the UI becomes a straightforward `UIList` loop instead of
hand-written per-field rows — the better fit for a schema that varies per
model anyway.

One caveat flagged but not fully exercised by that first round of
testing: Blender has a documented crash risk with dynamic `EnumProperty`
`items` callbacks that build a fresh Python list of fresh strings on
every call — the C side can hold pointers past their Python GC lifetime
under redraw pressure. The real script (`husk_blender_options_panel.py`)
builds in the standard mitigation from the start: each row's returned
items list is cached in a module-level dict keyed by a stable per-row id,
so a live Python reference stays alive for as long as Blender might still
hold the C-side pointer.

## What the real script actually does (phase 1)

- Reads the real per-model `chr_customization_options` skin extras (via a
  **standalone copy** of `husk_blender_geoset_mask.py`'s own
  `_root_joint_extras`/`_deep_copy_id_property` — not imported from that
  file, deliberately, since it's under separate active development; if
  the two ever diverge, that file's copy is the more battle-tested one to
  resync from).
- Populates one row per real `ChrCustomizationOption`, each with an
  independent dynamic choice dropdown, real option/choice *names* (never a
  bare numeric id anywhere in the UI). Grouped under a real
  `ChrCustomizationCategory` header per section (e.g. "Face"/"Hair"/
  "Body"/"Accessories"/"Markings" — the same real in-game character-
  creation section headers, husk-side support added this session:
  `src/chrcustomization_db2.hpp`'s new `Category` struct/`loadCategories`,
  joined via each `Option`'s real `ChrCustomizationCategoryID`, attached
  as `category_id`/`category_name`/`category_order_index` on each
  option's own extras — verified against real local data,
  `bloodelffemale_hd`'s 17 options split 9/"Face", 3/"Accessories",
  5/"Markings", matching an independent `sqlite3` join against the same
  source tables done before writing any code). An option with no real
  category (`category_id` 0 — the category table wasn't loaded, or a
  genuine dangling reference) sorts last under a literal
  `"(Uncategorized)"` heading rather than being silently merged into
  whichever real category happens to sort next to it. Replaced an earlier
  flat `UIList` with one `layout.box()` per category — real Blender
  section headers, not a scrollable flat list, matching the reference
  rig's own boxed-section shape (see the investigation above) now that
  there's real grouping data to hang it on.
- Computes real cross-option dependency status from husk's own
  already-resolved data: a `CustomizationChoice`'s `Material` entry can
  carry a nonzero `related_choice_id` naming a choice from a *different*
  option it depends on (husk resolves this via
  `chrcustomization_db2`/`MaterialResolution::relatedChoiceId` — this
  script doesn't invent the relationship, it reads one husk already
  worked out). A row's status is `"unmet"` when its current choice names
  a dependency that isn't the choice currently selected elsewhere,
  `"ok"` otherwise.
- **Phase 2, now implemented**: `apply_selection()` pushes the panel's
  current choice into the real texture-switch node graphs
  `husk_blender_geoset_mask.py`'s `apply_customization_texture_switch`
  builds; `sync_from_node_graph()` reads the current live selection back
  (used on load, so the panel reflects whichever default that file
  already resolved via `chr_enabled_materials`, not always each option's
  first choice). This waited until the peer session's concurrent
  gear-item work landed (commit `0328c5a`), then re-read that file's
  *then-current* node shape fresh rather than trusting the shape recorded
  above — it had genuinely changed in the meantime (see "What changed
  underneath" below). Both directions are found **structurally**, not by
  name: any `ShaderNodeGroup` node on the armature's own mesh materials
  that exposes a `NodeSocketMenu` input matching an option's real name,
  regardless of that file's own internal node/group naming — so this
  script stays decoupled from `husk_blender_geoset_mask.py`'s internal
  layout even though it now drives its output.

- **Geoset switching, also now implemented**: `apply_geoset_switch`'s
  `HuskGeosetSwitch` Geometry Nodes modifier uses a genuinely different
  mechanism from the material case — a `NodeSocketMenu` promoted onto the
  *modifier's own interface* rather than a node instance on a material,
  and its live value is stored as a plain integer IDProperty keyed by an
  internal per-node item id, not a string. Three real API differences
  confirmed empirically before wiring to it, not assumed: (1) writing a
  string to a modifier's Menu input raises `TypeError` — it wants an int;
  (2) that int is each enum item's own internal id, discoverable only via
  the underlying `GeometryNodeMenuSwitch` node's per-choice value-socket
  identifier (`"Item_<n>"` — `bpy.types.NodeEnumItem` itself exposes no
  id/index to Python at all); (3) comparing `NodeLink.from_socket`/
  `.to_node` against a separately-fetched socket/node with `==`/`is` is
  unreliable in this Blender version — silently `False` even for the same
  underlying node — so every correlation in `_geoset_group_sockets`
  matches by `.name`/`.identifier` instead. `apply_geoset_selection`/
  `sync_geoset_from_modifier` mirror the material-switch pair for this
  mechanism; a choice with no `geoset_id` (a pure-texture option) simply
  isn't geoset-backed and only drives the material side.

### What changed underneath, since this doc's own architecture-investigation section was written

The Pharah investigation and the two-approaches test above both predate
the peer session's gear-item work landing. By the time phase 2 was
written, `husk_blender_geoset_mask.py`'s own texture-switch mechanism had
already been rebuilt once more in the meantime (`ba8bf68`, `5c89bb6`): a
real Blender `NodeSocketMenu`/`GeometryNodeMenuSwitch` per option, not
the float-index-plus-`Math(COMPARE)` scheme this doc originally
described reading. Re-verified the new shape empirically before wiring
to it — three headless checks, one per real nesting depth
(`node.inputs["Choice"].type == 'MENU'`; setting `default_value` to a
real enum name vs. a bogus one; the same check two group-levels deep, the
actual production shape) — rather than assuming the earlier read still
applied.

## Real bug found and fixed during testing

`tools/test_husk_blender_options_panel.py` caught this, not manual
review: an **empty nested array** inside bone custom-property data (e.g.
a `CustomizationChoice` with no `materials` element — a real, documented
case, `gltf_skeleton.hpp`'s own comment: "empty when this choice has no
material element") lands as Blender's own `IDPropertyArray` type, not a
plain Python `list`. The original `_deep_copy_id_property` pattern
(copied from `husk_blender_geoset_mask.py`) only special-cases
`IDPropertyGroup` and `isinstance(x, list)` — an `IDPropertyArray` falls
through both, passing through unconverted. Harmless for a caller that
just iterates it, but breaks anything doing `isinstance(x, list)` or
`json.dumps()` on the result (this script does both). Fixed here by also
matching `type(value).__name__ == "IDPropertyArray"` in that branch.
**Not yet ported back** to `husk_blender_geoset_mask.py`'s own copy of
the same helper — flag this if that file ever hits an empty-array extras
field (an item with no overlays, say) and something downstream trips on
it unexpectedly.

## Running the tests

```
blender --background --factory-startup --python tools/test_husk_blender_options_panel.py
```

Builds a synthetic armature with real-shaped `chr_customization_options`
bone extras (nested dict/list custom properties, not a JSON-string
stand-in — the same structure Blender's real glTF import produces), plus
a synthetic skinned mesh with two materials built to the exact real
two-group-level `NodeSocketMenu` shape `apply_customization_texture_switch`
produces (built independently in the test, not by calling that file's
own functions — so this exercises the real socket *shape*, not that
file's current implementation of it), and exercises the actual module
functions — including `apply_selection`/`sync_from_node_graph` against
those real sockets — against it. Exits nonzero on any failed check,
prints an `[OK]`/`[FAIL]` line per assertion (34 checks as of the category-grouping work). The husk-side category resolution itself has its own separate C++ CLI-tier test, `tests/test_cli_chrcustomization.cpp`'s newest `TEST_CASE`, verified against both a synthetic fixture and real local `test_data/db2/chrcustomization{option,category}.db2` data.

## Real workflow

```
blender --python tools/husk_blender_geoset_mask.py -- model.glb   # builds the node graphs
blender --python tools/husk_blender_options_panel.py               # registers the panel, syncs from them
```

or, once both are embedded as registered `Text` datablocks in a saved
`.blend`, neither needs to be run manually again — reopening the file
re-registers both and the panel reflects whatever selection was live
when it was last saved.
