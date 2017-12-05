# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

bl_info = {
    "name": "Amber Asset Engine",
    "author": "Bastien Montagne",
    "version": (0, 0, 1),
    "blender": (2, 80, 2),
    "location": "File browser...",
    "description": "Create and use asset repositories on local file system.",
    "category": "Workflow",
    "support": 'OFFICIAL',
}

if "bpy" in locals():
    import importlib
    importlib.reload(repository)
    importlib.reload(engine)
    importlib.reload(operators)
    importlib.reload(ui)
else:
    from . import (
        repository,
        engine,
        operators,
        ui
        )


import bpy
from bpy.props import (
        BoolProperty,
        )


classes = repository.classes + engine.classes + operators.classes + ui.classes


def register():
    bpy.types.WindowManager.amber_enable_editing = BoolProperty(
                                name="Enable Amber Editing",
                                description="Enable editing of items in Amber asset engine repositories")
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in classes:
        bpy.utils.unregister_class(cls)
    del bpy.types.WindowManager.amber_enable_editing


if __name__ == "__main__":
    register()
