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
from bpy.types import Panel
from bl_ui.properties_grease_pencil_common import (
        GreasePencilDataPanel,
        GreasePencilLayerOptionPanel,
        GreasePencilOnionPanel,
        GreasePencilParentLayerPanel,
        GreasePencilVertexGroupPanel,
        )


class DataButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"

    @classmethod
    def poll(cls, context):
        return context.object and context.object.type == 'GPENCIL'


class DATA_PT_gpencil(DataButtonsPanel, Panel):
    bl_label = ""
    bl_options = {'HIDE_HEADER'}

    def draw(self, context):
        layout = self.layout

        # Grease Pencil data selector
        gpd_owner = context.gpencil_data_owner
        gpd = context.gpencil_data

        layout.template_ID(gpd_owner, "grease_pencil", new="gpencil.data_add", unlink="gpencil.data_unlink")


class DATA_PT_gpencil_datapanel(GreasePencilDataPanel, Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Layers"

    # NOTE: this is just a wrapper around the generic GP Panel


class DATA_PT_gpencil_layeroptionpanel(GreasePencilLayerOptionPanel, Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Options"

    # NOTE: this is just a wrapper around the generic GP Panel


class DATA_PT_gpencil_onionpanel(GreasePencilOnionPanel, Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Onion Skinning"

    # NOTE: this is just a wrapper around the generic GP Panel


class DATA_PT_gpencilparentpanel(GreasePencilParentLayerPanel, Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Layer Relations"

    # NOTE: this is just a wrapper around the generic GP Panel

class DATA_PT_gpencilvertexpanel(GreasePencilVertexGroupPanel, Panel):
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    bl_label = "Vertex Groups"

class DATA_PT_gpencil_display(DataButtonsPanel, Panel):
    bl_label = "Display"

    def draw(self, context):
        layout = self.layout
        ob = context.object
        layout.prop(ob, "empty_draw_size", text="Size")

        gpd = context.gpencil_data
        row = layout.row()
        row.prop(gpd, "xray_mode", text="Draw Mode")
        row = layout.row()
        row.prop(gpd, "keep_stroke_thickness")

        row = layout.row()
        row.prop(gpd, "show_stroke_direction", text="Show Stroke Directions")

        gpl = context.active_gpencil_layer
        if gpl:
            row = layout.row()
            row.prop(gpl, "show_points")


classes = (
    DATA_PT_gpencil,
    DATA_PT_gpencil_datapanel,
    DATA_PT_gpencil_onionpanel,
    DATA_PT_gpencil_layeroptionpanel,
    DATA_PT_gpencilvertexpanel,
    DATA_PT_gpencilparentpanel,
    DATA_PT_gpencil_display,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)