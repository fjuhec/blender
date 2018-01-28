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


class GROOM_UL_bundles(bpy.types.UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index, flt_flag):
        groom = data
        bundle = item

        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row = layout.row(align=True)
            if not bundle.is_bound:
                row.label(icon='ERROR')
            if groom.scalp_object:
                row.prop_search(bundle, "scalp_facemap", groom.scalp_object, "face_maps", text="")
            else:
                row.prop(bundle, "scalp_facemap", text="")

        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


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

        layout.template_list("GROOM_UL_bundles", "bundles",
                             groom, "bundles",
                             groom.bundles, "active_index")

        split = layout.split()

        col = split.column()
        col.label("Scalp Object:")
        col.prop(groom, "scalp_object", "")

        col = split.column()
        col.label("Curves:")
        col.prop(groom, "curve_resolution", "Resolution")


class DATA_PT_groom_hair(DataButtonsPanel, Panel):
    bl_label = "Hair"

    def draw(self, context):
        layout = self.layout
        groom = context.groom

        layout.operator("groom.hair_distribute")


class DATA_PT_groom_draw_settings(DataButtonsPanel, Panel):
    bl_label = "Draw Settings"

    def draw(self, context):
        layout = self.layout
        groom = context.groom
        ds = groom.hair_draw_settings

        split = layout.split()
        col = split.column()
        col.label("Follicles:")
        col.prop(ds, "follicle_mode", expand=True)


class DATA_PT_custom_props_groom(DataButtonsPanel, PropertyPanel, Panel):
    _context_path = "object.data"
    _property_type = bpy.types.Groom


classes = (
    GROOM_UL_bundles,
    DATA_PT_context_groom,
    DATA_PT_groom,
    DATA_PT_groom_hair,
    DATA_PT_groom_draw_settings,
    DATA_PT_custom_props_groom,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
