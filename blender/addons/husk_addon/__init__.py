"""husk world authoring: edit a husk world in Blender, bake it back to husk documents.

Design: REFACTOR/BLENDER_WORLD_AUTHORING.md. Reads and writes husk documents
only; never calls husk.
"""
VERSION = "0.1.0"

bl_info = {
    "name": "husk world authoring",
    "version": (0, 1, 0),
    "blender": (5, 1, 0),
    "location": "View3D > Sidebar > husk",
    "category": "Import-Export",
}


def register():
    from . import ops
    ops.register()


def unregister():
    from . import ops
    ops.unregister()
