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
import bpy
from bpy.types import Menu, Panel
from rna_prop_ui import PropertyPanel


class DataButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"

    @classmethod
    def poll(cls, context):
        return context.groom


class DATA_PT_context_groom(DataButtonsPanel, Panel):
    bl_label = ""
    bl_options = {'HIDE_HEADER'}

    def draw(self, context):
        layout = self.layout

        ob = context.object
        groom = context.groom
        space = context.space_data

        split = layout.split(percentage=0.65)

        if ob:
            split.template_ID(ob, "data")
        elif groom:
            split.template_ID(space, "pin_id")


class DATA_PT_groom(DataButtonsPanel, Panel):
    bl_label = "Groom"

    def draw(self, context):
        layout = self.layout

        groom = context.groom

        split = layout.split()

        col = split.column()
        col.prop(groom, "curve_resolution")


class DATA_PT_custom_props_groom(DataButtonsPanel, PropertyPanel, Panel):
    _context_path = "object.data"
    _property_type = bpy.types.Groom


classes = (
    DATA_PT_context_groom,
    DATA_PT_groom,
    DATA_PT_custom_props_groom,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
