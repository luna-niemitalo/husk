"""husk_blender_options_panel.py -- a live, persistent Blender N-panel for
browsing/editing a character's real customization-choice menu, once a
husk `.glb` export is already imported.

Deliberately a **separate file** from `tools/husk_blender_geoset_mask.py`,
per Luna's own direct steer: that script's job is post-import wiring (it
runs once, builds the geoset-switch/texture-switch node graphs, then its
job is done); this script's job is a UI surface meant to *travel with the
.blend* afterward -- register it as an embedded `Text` datablock with
Blender's own "Register" checkbox enabled (Text Editor > Text menu), and
it self-installs on every future open of that .blend with zero setup,
the same trick a real professional rig file
(`~/Documents/Blender/pharah v5.3.45/Base.blend`, its own embedded
`PBGv5.py`) was found to use when investigated for this exact purpose --
see the architecture notes in `tools/BLENDER_OPTIONS_PANEL.md` for the
full writeup of what that investigation found and why this script copies
its shape.

Phase 1 scope: surface the real per-model customization menu
(`chr_customization_options` skin extras) as a live, editable list --
one row per `ChrCustomizationOption`, a dropdown of its real
`ChrCustomizationChoice`s -- plus real cross-option dependency status
computed from husk's own already-resolved `relatedChoiceId` data (a
material variant that only applies when some *other* option's choice is
also selected).

Phase 2 (this file, now): drives the real texture-switch node graphs
`husk_blender_geoset_mask.py`'s `apply_customization_texture_switch`
builds. That file's own node shape was re-read fresh right before this
was written (per its own "grab a fresh read close to when you actually
need it" advice, since it was under active concurrent edit when this
script's phase 1 landed) -- confirmed empirically, not assumed, that
each relevant material now carries exactly one combined
`ShaderNodeGroup` with one real `NodeSocketMenu` input per option,
named by the option's own real name, and that setting
`socket.default_value = "<choice name>"` on that socket (even nested two
group-levels deep, the exact real shape) drives the live shader. This
script never builds or owns that node graph -- it only finds the
already-built sockets by their type (`MENU`) and name, and reads/writes
`default_value`, so it stays decoupled from that file's own internal
node layout. `apply_selection` pushes a chosen row into every matching
socket across every material on the armature's own mesh objects;
`sync_from_node_graph` does the reverse (used on load, so the panel
reflects whatever `apply_customization_texture_switch` already set as
the default, instead of always showing each option's first choice).

Also drives `apply_geoset_switch`'s own `HuskGeosetSwitch` Geometry Nodes
modifier, a genuinely different mechanism (a Menu socket promoted onto a
*modifier's* interface, stored as a plain integer IDProperty keyed by an
internal per-node item id, not a string on a node instance) --
`apply_geoset_selection`/`sync_geoset_from_modifier` mirror the material
pair above for that mechanism specifically. A row whose current choice
carries a real `geoset_id` drives both a material and a geoset switch
when applicable; one with none only drives the material switch (not
every option is geoset-backed).

Run inside Blender the same way as `husk_blender_geoset_mask.py`:
  blender --python tools/husk_blender_options_panel.py -- model.glb
or with no trailing args, against whatever's already in the scene, or
embed as a registered Text datablock for it to persist in the .blend.
"""

import json
import re
import sys

import bpy

# Vertex-group naming convention husk_blender_geoset_mask.py's own
# `GROUP_PREFIX`/`VARIANT_PREFIX` constants use (`"group_<n>,variant_<n>"`)
# -- duplicated here rather than imported, same "separate file, resync
# from there if they ever diverge" policy as `_deep_copy_id_property`
# below. `group*100+variant` is husk's own real convention for a
# `geoset_id` (`src/chrcustomization_db2.cpp`'s `resolveChoice`,
# confirmed via that file's own `enabled_geosets_to_default_overrides`
# comment), so no separate lookup table is needed to go either direction.
_GEOSET_GROUP_PREFIX = "group_"
_GEOSET_VARIANT_PREFIX = "variant_"
# Matches the exact socket-name suffix `_build_group_hidden_term` always
# appends to a geoset group's own promoted Menu socket
# (`f"{group_label} (group {group})"` / `f"Geoset group {group}"`) --
# read directly from that function's own source, not guessed.
_GEOSET_GROUP_SOCKET_RE = re.compile(r" \(group (\d+)\)$")


# -----------------------------------------------------------------------
# Root-joint extras reading. Deliberately a small, self-contained COPY of
# husk_blender_geoset_mask.py's own `_root_joint_extras`/
# `_deep_copy_id_property` (same real bug they fix: a bone's own
# IDPropertyGroup/list values are live views into Blender's storage, not
# independent copies, and can segfault Blender if read again after an
# unrelated bone deletion -- see that file's own doc comment for the full
# crash repro) -- NOT imported from that file, since it's under active
# concurrent edit in a sibling session and this script is meant to stand
# alone. If the two ever diverge, `husk_blender_geoset_mask.py`'s copy is
# the more battle-tested one; resync from there.
# -----------------------------------------------------------------------

def _deep_copy_id_property(value):
    if type(value).__name__ == "IDPropertyGroup":
        return {key: _deep_copy_id_property(value[key]) for key in value.keys()}
    # An empty (or numeric-homogeneous) nested array lands as Blender's own
    # `IDPropertyArray`, not a plain `list` -- confirmed directly: a choice
    # with `"materials": []` (a real, documented case -- gltf_skeleton.hpp's
    # own doc comment: "empty when this choice has no material element")
    # comes back as `IDPropertyArray`, which json.dumps can't serialize and
    # `isinstance(x, list)` doesn't recognize, unlike a populated nested
    # array of dicts (which lands as a real `list` of `IDPropertyGroup`).
    if isinstance(value, list) or type(value).__name__ == "IDPropertyArray":
        return [_deep_copy_id_property(v) for v in value]
    return value


def _root_joint_extras(armature_obj):
    if armature_obj is None:
        return {}
    for bone in armature_obj.data.bones:
        if "joint_names" in bone.keys():
            return {key: _deep_copy_id_property(bone[key]) for key in bone.keys()}
    return {}


def read_chr_customization_options(armature_obj):
    return _root_joint_extras(armature_obj).get("chr_customization_options")


def find_armature():
    arm_objs = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    if not arm_objs:
        raise RuntimeError("no armature object found in the current scene -- import a husk "
                            "export first")
    return arm_objs[0]


# -----------------------------------------------------------------------
# Data model (headless-verified design: see the conversation this file
# was built from -- a dynamic per-model bpy.props class generated with
# type() was tried first and works, but silently breaks bpy.types name
# lookup if the same model is ever imported twice in one session without
# manual unregister bookkeeping; a single, statically-registered
# CollectionProperty of a fixed row struct sidesteps that whole bug class
# by construction, since no per-model class registration ever happens.
# Confirmed working for models with genuinely different option/choice
# counts sharing the same registered classes.
# -----------------------------------------------------------------------

# Per-row dynamic EnumProperty `items` callbacks must return a list
# Blender's C side can hold a reference into past the callback's own
# Python-side return -- building a fresh list of fresh strings on every
# call risks a real, documented Blender crash under redraw/GC pressure.
# Caching the last-returned list per row (keyed by a stable id baked in
# at population time) keeps a live Python reference alive for exactly as
# long as Blender might still hold the C-side pointer.
_enum_items_cache = {}


def _update_choice_index(self, context):
    self.choice_index = int(self.choice_enum)
    armature_obj = context.object
    if armature_obj and armature_obj.type == 'ARMATURE':
        recompute_dependency_status(armature_obj)
        apply_selection(armature_obj)
        apply_geoset_selection(armature_obj)


class HUSK_PG_ChoiceItem(bpy.types.PropertyGroup):
    option_id: bpy.props.IntProperty()
    option_name: bpy.props.StringProperty()
    option_order_index: bpy.props.IntProperty()
    # Real ChrCustomizationCategory grouping (husk's own category_id/
    # category_name/category_order_index extras -- see
    # gltf::Skeleton::CustomizationOption::categoryId's own doc comment).
    # category_name is the empty string for an option with no real
    # category (categoryId 0 -- chrcustomizationcategory.db2 wasn't loaded
    # at export time, or a genuine dangling reference) -- grouped under a
    # literal "(Uncategorized)" heading in the UI rather than silently
    # merged into whichever real category happens to sort adjacent to it.
    category_id: bpy.props.IntProperty()
    category_name: bpy.props.StringProperty()
    category_order_index: bpy.props.IntProperty()
    row_key: bpy.props.StringProperty()  # stable cache key, set at population time
    choice_index: bpy.props.IntProperty(name="Choice")
    dependency_status: bpy.props.StringProperty(default="")  # "", "ok", "unmet"

    def _choices_data(self):
        raw = self.get("_choices_json")
        return json.loads(raw) if raw else []

    def choice_enum_items(self, context):
        choices = self._choices_data()
        items = [(str(i), c["choice_name"], "") for i, c in enumerate(choices)] or [("0", "(none)", "")]
        _enum_items_cache[self.row_key] = items  # keep alive past this call, see note above
        return _enum_items_cache[self.row_key]

    choice_enum: bpy.props.EnumProperty(items=choice_enum_items, name="Choice", update=_update_choice_index)

    def current_choice(self):
        choices = self._choices_data()
        if not choices:
            return None
        idx = min(max(self.choice_index, 0), len(choices) - 1)
        return choices[idx]


class HUSK_PG_ChoiceSchema(bpy.types.PropertyGroup):
    items: bpy.props.CollectionProperty(type=HUSK_PG_ChoiceItem)


def populate_choice_schema(armature_obj, chr_customization_options):
    """Rebuilds `armature_obj.husk_choices.items` from real
    `chr_customization_options` extras (`read_chr_customization_options`'s
    own return shape). Safe to call again on the same armature (e.g. after
    a re-export/re-import) -- clears and repopulates rather than assuming
    a first-time-only call. Sorted by (real category order, then real
    option order within it) rather than a flat option_order_index -- a
    category id of 0 (no real category resolved) sorts last, under a
    literal "(Uncategorized)" heading, never silently merged into an
    adjacent real category.
    """
    schema = armature_obj.husk_choices

    def sort_key(option):
        category_id = option.get("category_id", 0)
        return (category_id == 0, option.get("category_order_index", 0), option["option_order_index"])

    schema.items.clear()
    for option in sorted(chr_customization_options or [], key=sort_key):
        row = schema.items.add()
        row.option_id = option["option_id"]
        row.option_name = option["option_name"]
        row.option_order_index = option["option_order_index"]
        row.category_id = option.get("category_id", 0)
        row.category_name = option.get("category_name") or "(Uncategorized)"
        row.category_order_index = option.get("category_order_index", 0)
        row.row_key = f"{armature_obj.name}::{option['option_id']}"
        choices = sorted(option.get("choices", []), key=lambda c: c["choice_order_index"])
        row["_choices_json"] = json.dumps(choices)
        row.choice_index = 0


def recompute_dependency_status(armature_obj):
    """Mirrors the reference rig's own `set_*_item_visibility()` pattern
    (see `tools/BLENDER_OPTIONS_PANEL.md`): re-derive every row's status
    from scratch on every call, never as an incremental patch. Unlike
    that reference (which hardcodes its own AND-formulas by hand, since
    its schema is fixed at author time), this reads husk's own real,
    already-resolved `related_choice_id` data -- a material variant on
    the CURRENTLY selected choice of one option can name a choice id from
    a DIFFERENT option it depends on; `dependency_status` becomes "unmet"
    when that named choice isn't the one currently selected there, "ok"
    otherwise (including when the current choice has no such dependency
    at all -- no dependency is a satisfied dependency, not an unknown
    one).
    """
    schema = armature_obj.husk_choices
    selected_choice_id_by_option = {}
    for row in schema.items:
        choice = row.current_choice()
        if choice is not None:
            selected_choice_id_by_option[row.option_id] = choice["choice_id"]

    for row in schema.items:
        choice = row.current_choice()
        if choice is None:
            row.dependency_status = ""
            continue
        related_ids = {
            m["related_choice_id"]
            for m in choice.get("materials", [])
            if m.get("related_choice_id", 0) != 0
        }
        if not related_ids:
            row.dependency_status = "ok"
            continue
        selected_elsewhere = set(selected_choice_id_by_option.values())
        row.dependency_status = "ok" if related_ids & selected_elsewhere else "unmet"


def _mesh_objects_for_armature(armature_obj):
    """Every mesh object skinned to `armature_obj` via a real Armature
    modifier -- Blender's own glTF importer wires this up automatically
    for every skinned mesh, so this is the same real relationship the
    scene itself already encodes, not a naming-convention guess.
    """
    return [o for o in bpy.data.objects
            if o.type == 'MESH'
            and any(m.type == 'ARMATURE' and m.object == armature_obj for m in o.modifiers)]


def _customization_group_nodes(armature_obj):
    """Yields `(material, node)` for every `ShaderNodeGroup` node, across
    every distinct material on every mesh skinned to `armature_obj`, that
    exposes at least one `NodeSocketMenu`-typed input -- structurally
    identifying `apply_customization_texture_switch`'s own per-material
    combined group node (see this module's docstring) without needing to
    know its exact name (`f"{short_type_name} customization"`, an
    implementation detail of that file this script doesn't hardcode).
    """
    seen_materials = set()
    for mesh_obj in _mesh_objects_for_armature(armature_obj):
        for slot in mesh_obj.material_slots:
            mat = slot.material
            if mat is None or mat.node_tree is None or mat.name in seen_materials:
                continue
            seen_materials.add(mat.name)
            for node in mat.node_tree.nodes:
                if node.type == 'GROUP' and node.node_tree is not None:
                    if any(inp.type == 'MENU' for inp in node.inputs):
                        yield mat, node


def apply_selection(armature_obj):
    """Pushes every row's current choice into the matching `NodeSocketMenu`
    input (by option name) on every real customization-switch group node
    found on the armature's own mesh materials (see
    `_customization_group_nodes`). A socket whose choice name isn't a
    valid enum item on that particular material (e.g. this option doesn't
    apply to that material's texture_type, or the node graph predates a
    since-changed choice list) is skipped, not silently ignored -- see the
    returned/reported skip count. Returns the list of
    `(material_name, option_name, choice_name)` actually applied.
    """
    schema = armature_obj.husk_choices
    applied = []
    skipped = []
    for mat, node in _customization_group_nodes(armature_obj):
        for row in schema.items:
            socket = node.inputs.get(row.option_name)
            if socket is None or socket.type != 'MENU':
                continue
            choice = row.current_choice()
            if choice is None:
                continue
            try:
                socket.default_value = choice["choice_name"]
                applied.append((mat.name, row.option_name, choice["choice_name"]))
            except TypeError:
                skipped.append((mat.name, row.option_name, choice["choice_name"]))
    if skipped:
        print(f"husk_blender_options_panel: {len(skipped)} option/material pair(s) skipped -- "
              "chosen choice name isn't a valid enum item on that material's own switch node "
              "(stale/mismatched node graph), not applied silently")
    return applied


def sync_from_node_graph(armature_obj):
    """The reverse of `apply_selection`: reads each row's CURRENT live
    choice back from whichever real customization-switch socket already
    matches its option name (the default `apply_customization_texture_switch`
    itself set at import time -- `chr_enabled_materials`-resolved, not
    necessarily each option's first choice), and sets `choice_index` to
    match. Called on load so the panel reflects what's actually showing in
    the viewport instead of always defaulting to index 0. A row with no
    matching live socket (no relevant material on this armature, or the
    node graph was never built -- e.g. `apply_customization_texture_switch`
    wasn't run) keeps whatever `populate_choice_schema` already gave it.
    Returns the set of option names actually resolved, so
    `sync_geoset_from_modifier` can skip re-deciding them.
    """
    schema = armature_obj.husk_choices
    live_choice_name_by_option = {}
    for _mat, node in _customization_group_nodes(armature_obj):
        for row in schema.items:
            socket = node.inputs.get(row.option_name)
            if socket is not None and socket.type == 'MENU' and row.option_name not in live_choice_name_by_option:
                live_choice_name_by_option[row.option_name] = socket.default_value

    resolved = set()
    for row in schema.items:
        live_name = live_choice_name_by_option.get(row.option_name)
        if live_name is None:
            continue
        for idx, choice in enumerate(row._choices_data()):
            if choice["choice_name"] == live_name:
                row.choice_index = idx
                resolved.add(row.option_name)
                break
    return resolved


# -----------------------------------------------------------------------
# Geoset-switch integration. A genuinely different mechanism from the
# material customization-switch above -- husk_blender_geoset_mask.py's
# `apply_geoset_switch` promotes each geoset group's own Menu dropdown
# onto a Geometry Nodes MODIFIER's interface (`mesh_obj.modifiers[
# "HuskGeosetSwitch"]`), not a node instance sitting on a material. Two
# real, confirmed-not-assumed API differences from the material case:
# (1) a modifier's own Menu-typed input is stored as a plain **integer**
# IDProperty (`modifier[socket.identifier]`), not a string -- setting a
# string on it raises `TypeError`; (2) that integer isn't the enum item's
# list-index, it's the item's own internal id, only discoverable via the
# underlying `GeometryNodeMenuSwitch` node's per-choice VALUE socket
# identifier (`"Item_<n>"` -- `bpy.types.NodeEnumItem` itself exposes no
# id/index attribute to Python at all, confirmed directly). Also
# confirmed directly: comparing `NodeLink.from_socket`/`.to_node` against
# a separately-fetched socket/node object with `==`/`is` is unreliable in
# this Blender version (silently False even for the same underlying
# node) -- every correlation below matches by `.name`/`.identifier`
# instead, never object identity.
# -----------------------------------------------------------------------

def _geoset_modifiers_for_armature(armature_obj):
    """`(mesh_obj, modifier)` for every real `HuskGeosetSwitch` Geometry
    Nodes modifier on a mesh skinned to `armature_obj` -- the exact name
    `apply_geoset_switch` gives it, read from that function's own source.
    """
    for mesh_obj in _mesh_objects_for_armature(armature_obj):
        mod = mesh_obj.modifiers.get("HuskGeosetSwitch")
        if mod is not None and mod.type == 'NODES' and mod.node_group is not None:
            yield mesh_obj, mod


def _geoset_group_sockets(node_tree):
    """Yields `(interface_socket, selector_node, group)` for every real
    per-geoset-group dropdown `_build_group_hidden_term` builds: a
    `NodeSocketMenu` interface input whose name matches that function's
    own `" (group <N>)"` suffix convention, paired with the
    `GeometryNodeMenuSwitch` node it feeds (found via the real link from
    `NodeGroupInput`, not a naming guess -- `_build_group_hidden_term`
    itself only names the *node*, not in a way this could match
    directly, e.g. `"Geoset group 3 (selector)"` vs. the socket's own
    `"Geoset group 3"` -- structurally safer to follow the actual link).
    """
    group_input = next((n for n in node_tree.nodes if n.bl_idname == 'NodeGroupInput'), None)
    if group_input is None:
        return
    for item in node_tree.interface.items_tree:
        if getattr(item, "item_type", None) != 'SOCKET' or item.in_out != 'INPUT':
            continue
        if getattr(item, "socket_type", None) != 'NodeSocketMenu':
            continue
        m = _GEOSET_GROUP_SOCKET_RE.search(item.name)
        if not m:
            continue
        group = int(m.group(1))
        selector = None
        for link in node_tree.links:
            if (link.from_node.name == group_input.name
                    and link.from_socket.identifier == item.identifier
                    and link.to_socket.identifier == 'Menu'):
                selector = link.to_node
                break
        if selector is not None:
            yield item, selector, group


def apply_geoset_selection(armature_obj):
    """Pushes each row's current choice's own `geoset_id` (when it has
    one -- not every option is geoset-driven, e.g. a pure skin-tone
    option has none) into the matching `HuskGeosetSwitch` modifier input,
    on every mesh skinned to this armature. A choice with no `geoset_id`
    is simply skipped for this function's purposes -- it may still drive
    a material switch via `apply_selection`, the two aren't mutually
    exclusive. Returns the list of `(mesh_name, option_name, choice_name)`
    actually applied.
    """
    schema = armature_obj.husk_choices
    applied = []
    for mesh_obj, mod in _geoset_modifiers_for_armature(armature_obj):
        for item, selector, group in _geoset_group_sockets(mod.node_group):
            for row in schema.items:
                choice = row.current_choice()
                if choice is None or choice.get("geoset_id") is None:
                    continue
                geoset_id = choice["geoset_id"]
                if geoset_id // 100 != group:
                    continue
                target_vg = f"{_GEOSET_GROUP_PREFIX}{geoset_id // 100},{_GEOSET_VARIANT_PREFIX}{geoset_id % 100}"
                match_input = next(
                    (inp for inp in selector.inputs
                     if inp.identifier not in ('Menu', '__extend__')
                     and getattr(inp, 'default_value', None) == target_vg), None)
                if match_input is None:
                    continue
                mod[item.identifier] = int(match_input.identifier.split('_', 1)[1])
                applied.append((mesh_obj.name, row.option_name, choice["choice_name"]))
    return applied


def sync_geoset_from_modifier(armature_obj, already_resolved=None):
    """The reverse of `apply_geoset_selection`: reads each geoset group's
    CURRENT live modifier selection back and sets the matching row's
    `choice_index` to whichever choice's own `geoset_id` produces that
    exact vertex-group name -- same "reflect reality on load" role
    `sync_from_node_graph` plays for materials. Skips any option name
    already in `already_resolved` (`sync_from_node_graph`'s own return
    value) -- a row with both a texture and a geoset default should agree
    in the common case, since both ultimately trace back to the same husk
    export's own resolved choice, but texture state wins if they ever
    disagree, since it's the caller's job to check that one first.
    """
    schema = armature_obj.husk_choices
    resolved_options = set(already_resolved or ())
    for mesh_obj, mod in _geoset_modifiers_for_armature(armature_obj):
        for item, selector, group in _geoset_group_sockets(mod.node_group):
            current_int = mod[item.identifier]
            value_input = next(
                (inp for inp in selector.inputs
                 if inp.identifier not in ('Menu', '__extend__')
                 and inp.identifier == f"Item_{current_int}"), None)
            if value_input is None:
                continue
            vg_name = getattr(value_input, 'default_value', None)
            if not vg_name or ',' not in vg_name:
                continue
            group_part, variant_part = vg_name.split(',', 1)
            if not group_part.startswith(_GEOSET_GROUP_PREFIX) or not variant_part.startswith(_GEOSET_VARIANT_PREFIX):
                continue
            try:
                geoset_id = int(group_part[len(_GEOSET_GROUP_PREFIX):]) * 100 + \
                    int(variant_part[len(_GEOSET_VARIANT_PREFIX):])
            except ValueError:
                continue
            for row in schema.items:
                if row.option_name in resolved_options:
                    continue
                for idx, choice in enumerate(row._choices_data()):
                    if choice.get("geoset_id") == geoset_id:
                        row.choice_index = idx
                        resolved_options.add(row.option_name)
                        break


# -----------------------------------------------------------------------
# UI
# -----------------------------------------------------------------------

class HUSK_PT_options_panel(bpy.types.Panel):
    bl_idname = "HUSK_PT_options_panel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "husk"
    bl_label = "Customization Options"

    @classmethod
    def poll(cls, context):
        return context.object is not None and context.object.type == 'ARMATURE'

    def draw(self, context):
        layout = self.layout
        armature_obj = context.object
        schema = armature_obj.husk_choices

        if len(schema.items) == 0:
            layout.operator("husk.load_customization_options", icon="FILE_REFRESH")
            return

        # One box per real ChrCustomizationCategory (schema.items is
        # already sorted category-then-option by populate_choice_schema),
        # a real section header per category instead of one flat list --
        # the same shape the reference rig this panel's design was based
        # on uses (see tools/BLENDER_OPTIONS_PANEL.md), just derived from
        # real husk-resolved category data instead of hand-authored.
        current_category = None
        box = None
        for item in schema.items:
            if item.category_name != current_category:
                current_category = item.category_name
                box = layout.box()
                box.label(text=current_category, icon='OUTLINER_COLLECTION')
            row = box.row(align=True)
            row.prop(item, "choice_enum", text=item.option_name)
            if item.dependency_status == "unmet":
                row.label(text="", icon="ERROR")

        layout.operator("husk.load_customization_options", text="Reload from extras", icon="FILE_REFRESH")


class HUSK_OT_load_customization_options(bpy.types.Operator):
    bl_idname = "husk.load_customization_options"
    bl_label = "Load Customization Options"
    bl_description = "(Re)reads chr_customization_options from the armature's own husk extras"

    def execute(self, context):
        armature_obj = context.object
        options = read_chr_customization_options(armature_obj)
        if not options:
            self.report({'WARNING'}, "No chr_customization_options extras on this armature "
                                      "(re-export with --db2-dir/--dbd-dir, or a derivable "
                                      "--chr-model-id, to get one)")
            return {'CANCELLED'}
        populate_choice_schema(armature_obj, options)
        resolved = sync_from_node_graph(armature_obj)
        sync_geoset_from_modifier(armature_obj, resolved)
        recompute_dependency_status(armature_obj)
        return {'FINISHED'}


# -----------------------------------------------------------------------
# Registration
# -----------------------------------------------------------------------

_classes = (
    HUSK_PG_ChoiceItem,
    HUSK_PG_ChoiceSchema,
    HUSK_PT_options_panel,
    HUSK_OT_load_customization_options,
)


def register():
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.Object.husk_choices = bpy.props.PointerProperty(type=HUSK_PG_ChoiceSchema)


def unregister():
    del bpy.types.Object.husk_choices
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)


def main():
    argv = sys.argv
    if "--" in argv:
        extra_args = argv[argv.index("--") + 1:]
        if extra_args and not extra_args[0].startswith("--"):
            bpy.ops.import_scene.gltf(filepath=extra_args[0])

    register()

    try:
        armature_obj = find_armature()
    except RuntimeError as e:
        print(f"husk_blender_options_panel: {e}")
        return

    options = read_chr_customization_options(armature_obj)
    if options:
        populate_choice_schema(armature_obj, options)
        resolved = sync_from_node_graph(armature_obj)
        sync_geoset_from_modifier(armature_obj, resolved)
        recompute_dependency_status(armature_obj)
        print(f"husk_blender_options_panel: loaded {len(options)} customization options "
              f"for '{armature_obj.name}'")
    else:
        print("husk_blender_options_panel: no chr_customization_options extras on "
              f"'{armature_obj.name}' -- panel registered, but empty until a re-export "
              "provides one")


if __name__ == "__main__":
    main()
