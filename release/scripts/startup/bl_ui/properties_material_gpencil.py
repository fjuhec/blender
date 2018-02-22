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
from bpy.types import Panel, UIList


class GPENCIL_UL_paletteslots(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        slot = item
        palette = slot.palette
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            if palette:
                layout.prop(palette, "name", text="", emboss=False, icon_value=icon)
            else:
                layout.label(text="", icon_value=icon)
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class MATERIAL_PT_gpencil_palette_slots(Panel):
    bl_label = "Palette Slots"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"
    bl_options = {'HIDE_HEADER'}

    @classmethod
    def poll(cls, context):
        return context.object and context.object.type == 'GPENCIL'

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpd = context.gpencil_data

        row = layout.row()
        col = row.column()
        if len(gpd.palette_slots) >= 2:
            slot_rows = 5
        else:
            slot_rows = 2
        col.template_list("GPENCIL_UL_paletteslots", "", gpd, "palette_slots", gpd, "active_palette_index",
                          rows=slot_rows)

        col = row.column()

        sub = col.column(align=True)
        sub.operator("gpencil.palette_slot_add", icon='ZOOMIN', text="")
        sub.operator("gpencil.palette_slot_remove", icon='ZOOMOUT', text="")

        sub = col.column(align=True)
        sub.operator_menu_enum("gpencil.stroke_change_palette", text="", icon='ARROW_LEFTRIGHT', property="type")


class MATERIAL_PT_gpencil_palette_colors(Panel):
    bl_label = "Active Palette"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"

    @classmethod
    def poll(cls, context):
        return context.object and context.object.type == 'GPENCIL'
    
    @staticmethod
    def draw(self, context):
        layout = self.layout
        
        gpd = context.gpencil_data
        slot = gpd.active_palette_slot
        palette = context.active_gpencil_palette

        row = layout.row()
        row.template_ID(slot, "palette", new="palette.new_gpencil")

        if palette:
            row = layout.row()
            col = row.column()
            if len(palette.colors) >= 2:
                color_rows = 6
            else:
                color_rows = 2
            col.template_list("GPENCIL_UL_palettecolor", "", palette, "colors", palette, "active_index",
                              rows=color_rows)

            col = row.column()

            sub = col.column(align=True)
            sub.operator("palette.color_add", icon='ZOOMIN', text="")
            sub.operator("palette.color_delete", icon='ZOOMOUT', text="")

            palcol = context.active_gpencil_palettecolor
            if palcol:
                sub.menu("GPENCIL_MT_palettecolor_specials", icon='DOWNARROW_HLT', text="")

            if len(palette.colors) > 1:
                col.separator()

                sub = col.column(align=True)
                sub.operator("palette.palettecolor_move", icon='TRIA_UP', text="").direction = 'UP'
                sub.operator("palette.palettecolor_move", icon='TRIA_DOWN', text="").direction = 'DOWN'

                col.separator()

                sub = col.column(align=True)
                sub.operator("palette.palettecolor_isolate", icon='LOCKED', text="").affect_visibility = False
                sub.operator("palette.palettecolor_isolate", icon='RESTRICT_VIEW_OFF', text="").affect_visibility = True


class MATERIAL_PT_gpencil_palette_strokecolor(Panel):
    bl_label = "Stroke"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return context.object and context.object.type == 'GPENCIL' and context.active_gpencil_palettecolor

    @staticmethod
    def draw(self, context):
        layout = self.layout
        palette = context.active_gpencil_palette
        pcolor = palette.colors.active

        split = layout.split(percentage=1.0)
        split.active = not pcolor.lock

        col = split.column(align=True)
        col.enabled = not pcolor.lock
        col.prop(pcolor, "stroke_style", text="")

        if pcolor.stroke_style == 'TEXTURE':
            row = layout.row()
            row.enabled = not pcolor.lock
            col = row.column(align=True)
            col.template_ID(pcolor, "stroke_image", open="image.open")
            col.prop(pcolor, "use_pattern", text="Use as Pattern")

        if pcolor.stroke_style == 'SOLID' or pcolor.use_pattern is True:
            row = layout.row()
            col = row.column(align=True)
            col.prop(pcolor, "color", text="")
            col.prop(pcolor, "alpha", slider=True)

        row = layout.row(align=True)
        row.enabled = not pcolor.lock
        row.prop(pcolor, "use_dot", text="Dots")

        # Options
        row = layout.row()
        row.active = not pcolor.lock
        col = row.column(align=True)
        col.prop(pcolor, "pass_index")
        if pcolor.stroke_style == 'TEXTURE':
            col.prop(pcolor, "pixel_size", text="UV Factor")


class MATERIAL_PT_gpencil_palette_fillcolor(Panel):
    bl_label = "Fill"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "material"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return context.object and context.object.type == 'GPENCIL' and context.active_gpencil_palettecolor
 
    @staticmethod
    def draw(self, context):
        layout = self.layout
        palette = context.active_gpencil_palette
        pcolor = palette.colors.active

        # color settings
        split = layout.split(percentage=1.0)
        split.active = not pcolor.lock

        row = layout.row()
        col = row.column(align=True)
        col.enabled = not pcolor.lock
        col.prop(pcolor, "fill_style", text="")

        row = layout.row()
        col = row.column(align=True)

        if pcolor.fill_style != 'TEXTURE':
            col.prop(pcolor, "fill_color", text="")
            col.prop(pcolor, "fill_alpha", text="Opacity", slider=True)
            col.separator()
            if pcolor.texture_mix is True or pcolor.fill_style in ('GRADIENT', 'RADIAL'):
                col.prop(pcolor, "mix_factor", text="Mix", slider=True)

        if pcolor.fill_style in ('GRADIENT', 'RADIAL', 'CHESSBOARD'):
            if pcolor.texture_mix is False or pcolor.fill_style == 'CHESSBOARD':
                col.prop(pcolor, "mix_color", text="")
            split = col.split(percentage=0.5)
            subcol = split.column(align=True)
            subcol.prop(pcolor, "pattern_shift", text="Location")
            subrow = subcol.row(align=True)
            if pcolor.fill_style == 'RADIAL':
                subrow.enabled = False
            subrow.prop(pcolor, "pattern_angle", text="Angle")
            subcol.prop(pcolor, "flip", text="Flip")

            subcol = split.column(align=True)
            subcol.prop(pcolor, "pattern_scale", text="Scale")
            subrow = subcol.row(align=True)
            if pcolor.fill_style != 'RADIAL':
                subrow.enabled = False
            subrow.prop(pcolor, "pattern_radius", text="Radius")
            subrow = subcol.row(align=True)
            if pcolor.fill_style != 'CHESSBOARD':
                subrow.enabled = False
            subrow.prop(pcolor, "pattern_boxsize", text="Box")

        col.separator()
        col.label("Texture")
        if pcolor.fill_style not in ('TEXTURE', 'PATTERN'):
            col.prop(pcolor, "texture_mix", text="Mix Texture")
        if pcolor.fill_style in ('TEXTURE', 'PATTERN') or pcolor.texture_mix is True:
            col.template_ID(pcolor, "fill_image", open="image.open")
            split = col.split(percentage=0.5)
            subcol = split.column(align=True)
            subcol.prop(pcolor, "texture_offset", text="Offset")
            subcol.prop(pcolor, "texture_angle")
            subcol.prop(pcolor, "texture_clamp", text="Clip Image")
            subcol = split.column(align=True)
            subcol.prop(pcolor, "texture_scale", text="Scale")
            subcol.prop(pcolor, "texture_opacity")


classes = (
    GPENCIL_UL_paletteslots,
    MATERIAL_PT_gpencil_palette_slots,
    MATERIAL_PT_gpencil_palette_colors,
    MATERIAL_PT_gpencil_palette_strokecolor,
    MATERIAL_PT_gpencil_palette_fillcolor,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
