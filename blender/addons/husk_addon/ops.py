"""Operators and the View3D sidebar panel ("husk" tab)."""
import bpy
from bpy.props import EnumProperty, FloatProperty, StringProperty

from . import bake, docs, grid as grid_mod, world_io, zones

PREVIEW_ALL_LIMIT = 64


def _report_errors(op, fn):
    try:
        fn()
    except (ValueError, OSError) as e:
        op.report({"ERROR"}, str(e))
        return {"CANCELLED"}
    return {"FINISHED"}


class HUSK_OT_import_world(bpy.types.Operator):
    """Load cells of a husk world: terrain merged into one mesh, placements as instancing empties"""
    bl_idname = "husk.import_world"
    bl_label = "Import husk world"
    bl_options = {"REGISTER", "UNDO"}

    directory: StringProperty(subtype="DIR_PATH", name="World bundle")
    zone: StringProperty(name="Zone", description="Load this zone and every zone inside it, e.g. Northshire, Badlands",
                         search=lambda self, context, text: _zone_names(self.directory, text))
    min_coverage: FloatProperty(name="Min coverage", default=0.05, min=0.0, max=1.0, subtype="FACTOR",
                                description="Skip cells the zone covers less of than this")
    cells: StringProperty(name="Cells", description="Instead of a zone: " + world_io.CELLS_HELP)

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def draw(self, context):
        layout = self.layout
        layout.label(text="Open the .world.bundle folder, then pick a zone:")
        layout.prop(self, "zone")
        layout.prop(self, "min_coverage")
        layout.separator()
        layout.label(text="Or name cells (i_j, or a rectangle 31:33,48:50):")
        layout.prop(self, "cells")

    def execute(self, context):
        def run():
            if self.zone.strip():
                cells = zones.cells_for(docs.load(self.directory), self.zone, self.min_coverage)
                what = "zone %s" % self.zone
            else:
                cells = world_io.parse_cells(self.cells)
                what = "cells"
            root = world_io.import_world(context, self.directory, cells)
            self.report({"INFO"}, "loaded %d cells of %s" % (len(root["husk_loaded_cells"]), what))
        return _report_errors(self, run)


_ZONE_NAMES = {}


def _zone_names(directory, text):
    """Autocomplete for the zone field; one read of the world document per folder."""
    if directory not in _ZONE_NAMES:
        try:
            _ZONE_NAMES[directory] = zones.names(docs.load(directory))
        except (OSError, ValueError, KeyError):
            return []
    wanted = text.casefold()
    return [n for n in _ZONE_NAMES[directory] if wanted in n.casefold()]


class HUSK_OT_rebuild_cells(bpy.types.Operator):
    """Redraw the cell boxes for the current dicing over the terrain and placements"""
    bl_idname = "husk.rebuild_cells"
    bl_label = "Rebuild cells"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        return _report_errors(self, lambda: world_io.rebuild_cells(world_io.find_root(context)))


class HUSK_OT_scale_world(bpy.types.Operator):
    """Scale the terrain and every placement's position; props keep their own size"""
    bl_idname = "husk.scale_world"
    bl_label = "Scale world"
    bl_options = {"REGISTER", "UNDO"}

    factor: FloatProperty(name="Factor", default=20.0, min=1e-3)
    pivot: EnumProperty(name="Pivot", items=[("ORIGIN", "Scene origin", ""), ("CURSOR", "3D cursor", "")])

    def execute(self, context):
        pivot = context.scene.cursor.location if self.pivot == "CURSOR" else (0.0, 0.0, 0.0)
        return _report_errors(self, lambda: world_io.scale_world(context, world_io.find_root(context), self.factor, pivot))


class HUSK_OT_preview_bake(bpy.types.Operator):
    """Show the heightfield export will write for the selected cells"""
    bl_idname = "husk.preview_bake"
    bl_label = "Preview bake"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        def run():
            root = world_io.find_root(context)
            cells = grid_mod.selected_cells(context)
            if not cells:
                cells = world_io.covered_cells(root, context.scene)
                if len(cells) > PREVIEW_ALL_LIMIT:
                    raise ValueError("%d cells: select the cell boxes to preview" % len(cells))
            grid_obj, terrain_obj = world_io._members(root)
            bake.preview(context, grid_obj, terrain_obj, cells)
        return _report_errors(self, run)


class HUSK_OT_export_world(bpy.types.Operator):
    """Bake every cell and write the world as new husk documents"""
    bl_idname = "husk.export_world"
    bl_label = "Export husk world"

    directory: StringProperty(subtype="DIR_PATH", name="Output root")
    source_tiles: EnumProperty(name="Unloaded source tiles", items=[
        ("AUTO", "Auto", "Keep them while they still fit: source dicing, unscaled world"),
        ("KEEP", "Keep", "Reference every unloaded source tile as it is"),
        ("DROP", "Drop", "Write only what the scene bakes"),
    ])

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        def run():
            keep = {"AUTO": None, "KEEP": True, "DROP": False}[self.source_tiles]
            path = world_io.export_world(context, world_io.find_root(context), self.directory, keep)
            self.report({"INFO"}, "wrote " + path)
        return _report_errors(self, run)


class HUSK_PT_world(bpy.types.Panel):
    bl_label = "husk world"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "husk"

    def draw(self, context):
        layout = self.layout
        layout.operator(HUSK_OT_import_world.bl_idname, icon="IMPORT")
        try:
            root = world_io.find_root(context)
        except ValueError:
            return
        grid_obj = root.objects.get(grid_mod.GRID_NAME)
        box = layout.box()
        box.label(text="Dicing")
        box.prop(grid_obj, '["husk_cell_size"]', text="Cell size")
        box.prop(grid_obj, '["husk_origin"]', text="Origin")
        box.label(text="Change the dicing, then Rebuild cells", icon="INFO")
        box.operator(HUSK_OT_rebuild_cells.bl_idname, icon="MESH_GRID")
        box.label(text="Scale x%g" % root.get("husk_scale", 1.0))
        if world_io.keeps_source_tiles(root):
            box.label(text="Unloaded source tiles kept as they are", icon="CHECKMARK")
        else:
            box.label(text="Re-diced or scaled: every cell is baked", icon="INFO")
        box.label(text="Baked tiles carry no textures, liquids or LOD")
        layout.operator(HUSK_OT_scale_world.bl_idname, icon="FULLSCREEN_ENTER")
        layout.operator(HUSK_OT_preview_bake.bl_idname, icon="HIDE_OFF")
        layout.operator(HUSK_OT_export_world.bl_idname, icon="EXPORT")


CLASSES = (HUSK_OT_import_world, HUSK_OT_rebuild_cells, HUSK_OT_scale_world, HUSK_OT_preview_bake,
           HUSK_OT_export_world, HUSK_PT_world)


def register():
    for cls in CLASSES:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
