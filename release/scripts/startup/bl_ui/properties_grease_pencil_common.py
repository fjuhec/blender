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


from bpy.types import Menu, UIList
from bpy.app.translations import pgettext_iface as iface_


def gpencil_stroke_placement_settings(context, layout):
    if context.space_data.type == 'VIEW_3D':
        propname = "gpencil_stroke_placement_view3d"
    elif context.space_data.type == 'SEQUENCE_EDITOR':
        propname = "gpencil_stroke_placement_sequencer_preview"
    elif context.space_data.type == 'IMAGE_EDITOR':
        propname = "gpencil_stroke_placement_image_editor"
    else:
        propname = "gpencil_stroke_placement_view2d"

    ts = context.tool_settings

    col = layout.column(align=True)

    if context.space_data.type != 'VIEW_3D':
        col.label(text="Stroke Placement:")
        row = col.row(align=True)
        row.prop_enum(ts, propname, 'VIEW')
        row.prop_enum(ts, propname, 'CURSOR', text="Cursor")


def gpencil_active_brush_settings_simple(context, layout):
    brush = context.active_gpencil_brush
    if brush is None:
        layout.label("No Active Brush")
        return

    col = layout.column()
    col.label("Active Brush:      ")

    row = col.row(align=True)
    row.operator_context = 'EXEC_REGION_WIN'
    row.operator_menu_enum("gpencil.brush_change", "brush", text="", icon='BRUSH_DATA')
    row.prop(brush, "name", text="")

    col.prop(brush, "line_width", slider=True)
    row = col.row(align=True)
    row.prop(brush, "use_random_pressure", text="", icon='RNDCURVE')
    row.prop(brush, "pen_sensitivity_factor", slider=True)
    row.prop(brush, "use_pressure", text="", icon='STYLUS_PRESSURE')
    row = col.row(align=True)
    row.prop(brush, "use_random_strength", text="", icon='RNDCURVE')
    row.prop(brush, "strength", slider=True)
    row.prop(brush, "use_strength_pressure", text="", icon='STYLUS_PRESSURE')
    row = col.row(align=True)
    row.prop(brush, "jitter", slider=True)
    row.prop(brush, "use_jitter_pressure", text="", icon='STYLUS_PRESSURE')
    row = col.row()
    row.prop(brush, "angle", slider=True)
    row.prop(brush, "angle_factor", text="Factor", slider=True)


class GreasePencilDrawingToolsPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = " "
    bl_category = "Create"
    bl_region_type = 'TOOLS'

    @staticmethod
    def draw_header(self, context):
        layout = self.layout
        if context.space_data.type == 'VIEW_3D':
            if context.active_object and context.active_object.mode == 'GPENCIL_PAINT':
                layout.label(text="Shapes")
            else:
                layout.label(text="Measurement")
        else:
            layout.label(text="Grease Pencil")

    @staticmethod
    def draw(self, context):
        layout = self.layout

        is_3d_view = context.space_data.type == 'VIEW_3D'

        col = layout.column(align=True)

        # TODO: Shapes tools maybe need a panel
        if is_3d_view:
            col.operator("gpencil.primitive", text="Rectangle", icon='UV_FACESEL').type = 'BOX'
            col.operator("gpencil.primitive", text="Circle", icon='ANTIALIASED').type = 'CIRCLE'

        if not is_3d_view:
            col.label(text="Draw:")
            row = col.row(align=True)
            row.operator("gpencil.draw", icon='GREASEPENCIL', text="Draw").mode = 'DRAW'
            row.operator("gpencil.draw", icon='FORCE_CURVE', text="Erase").mode = 'ERASER'

            row = col.row(align=True)
            row.operator("gpencil.draw", icon='LINE_DATA', text="Line").mode = 'DRAW_STRAIGHT'
            row.operator("gpencil.draw", icon='MESH_DATA', text="Poly").mode = 'DRAW_POLY'

            col.separator()

            sub = col.column(align=True)
            sub.prop(context.tool_settings, "use_gpencil_continuous_drawing", text="Continuous Drawing")

            col.separator()

        if context.space_data.type in {'CLIP_EDITOR'}:
            col.separator()
            col.label("Data Source:")
            row = col.row(align=True)
            row.prop(context.space_data, "grease_pencil_source", expand=True)

        gpd = context.gpencil_data
        if gpd and not is_3d_view:
            col.separator()
            col.separator()

            gpencil_stroke_placement_settings(context, col)

        if gpd and not is_3d_view:
            layout.separator()
            layout.separator()

            col = layout.column(align=True)
            col.prop(gpd, "use_stroke_edit_mode", text="Enable Editing", icon='EDIT', toggle=True)

        if is_3d_view:
            is_gpmode = context.active_object and \
                        context.active_object.mode in ('GPENCIL_EDIT', 'GPENCIL_PAINT', 'GPENCIL_SCULPT')
            if context.active_object is None or is_gpmode is False:
                col.label(text="Tools:")
                col.operator("view3d.ruler")


class GreasePencilStrokeEditPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Edit Strokes"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        gpd = context.gpencil_data
        return bool(context.editable_gpencil_strokes) and bool(gpd.use_stroke_edit_mode)

    @staticmethod
    def draw(self, context):
        layout = self.layout

        is_3d_view = context.space_data.type == 'VIEW_3D'

        if not is_3d_view:
            layout.label(text="Select:")
            col = layout.column(align=True)
            col.operator("gpencil.select_all", text="Select All")
            col.operator("gpencil.select_border")
            col.operator("gpencil.select_circle")

            layout.separator()

            col = layout.column(align=True)
            col.operator("gpencil.select_linked")
            col.operator("gpencil.select_more")
            col.operator("gpencil.select_less")
            col.operator("gpencil.select_alternate")
            col.operator("gpencil.palettecolor_select")

        layout.label(text="Edit:")
        row = layout.row(align=True)
        row.operator("gpencil.copy", text="Copy")
        row.operator("gpencil.paste", text="Paste").type = 'COPY'
        row.operator("gpencil.paste", text="Paste & Merge").type = 'MERGE'

        col = layout.column(align=True)
        col.operator("gpencil.delete")
        col.operator("gpencil.duplicate_move", text="Duplicate")
        if is_3d_view:
            col.operator("gpencil.stroke_cyclical_set", text="Toggle Cyclic").type = 'TOGGLE'

        layout.separator()

        if not is_3d_view:
            col = layout.column(align=True)
            col.operator("transform.translate")                # icon='MAN_TRANS'
            col.operator("transform.rotate")                   # icon='MAN_ROT'
            col.operator("transform.resize", text="Scale")     # icon='MAN_SCALE'

            layout.separator()

        col = layout.column(align=True)
        col.operator("transform.bend", text="Bend")
        col.operator("transform.mirror", text="Mirror")
        col.operator("transform.shear", text="Shear")
        col.operator("transform.tosphere", text="To Sphere")

        layout.separator()
        col = layout.column(align=True)
        col.operator_menu_enum("gpencil.stroke_arrange", text="Arrange Strokes...", property="direction")
        col.operator("gpencil.stroke_change_color", text="Move to Color")

        if is_3d_view:
            layout.separator()


        layout.separator()
        col = layout.column(align=True)
        col.operator("gpencil.stroke_subdivide", text="Subdivide")
        col.operator("gpencil.stroke_simplify", text="Simplify")
        col.operator("gpencil.stroke_join", text="Join").type = 'JOIN'
        col.operator("gpencil.stroke_join", text="Join & Copy").type = 'JOINCOPY'
        col.operator("gpencil.stroke_flip", text="Flip Direction")

        if is_3d_view:
            layout.separator()
            layout.operator_menu_enum("gpencil.reproject", text="Reproject Strokes...", property="type")


class GreasePencilAnimationPanel:
    bl_space_type = 'VIEW_3D'
    bl_label = "Animation"
    bl_category = "Animation"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False
        elif context.space_data.type != 'VIEW_3D':
            return False
        elif context.object.mode == 'OBJECT':
            return False

        return True

    @staticmethod
    def draw(self, context):
        layout = self.layout

        col = layout.column(align=True)
        col.operator("gpencil.blank_frame_add", icon='NEW')
        col.operator("gpencil.active_frames_delete_all", icon='X', text="Delete Frame(s)")

        col.separator()
        col.prop(context.tool_settings, "use_gpencil_additive_drawing", text="Additive Drawing")


class GreasePencilInterpolatePanel:
    bl_space_type = 'VIEW_3D'
    bl_label = "Interpolate Strokes"
    bl_category = "Animation"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False
        elif context.space_data.type != 'VIEW_3D':
            return False

        gpd = context.gpencil_data

        return bool(context.editable_gpencil_strokes) and bool(gpd.use_stroke_edit_mode)

    @staticmethod
    def draw(self, context):
        layout = self.layout
        settings = context.tool_settings.gpencil_interpolate

        col = layout.column(align=True)
        col.label("Interpolate Strokes")
        col.operator("gpencil.interpolate", text="Interpolate")
        col.operator("gpencil.interpolate_sequence", text="Sequence")
        col.operator("gpencil.interpolate_reverse", text="Remove Breakdowns")

        col = layout.column(align=True)
        col.label(text="Options:")
        col.prop(settings, "interpolate_all_layers")
        col.prop(settings, "interpolate_selected_only")

        col = layout.column(align=True)
        col.label(text="Sequence Options:")
        col.prop(settings, "type")
        if settings.type == 'CUSTOM':
            box = layout.box()
            # TODO: Options for loading/saving curve presets?
            box.template_curve_mapping(settings, "interpolation_curve", brush=True)
        elif settings.type != 'LINEAR':
            col.prop(settings, "easing")

            if settings.type == 'BACK':
                layout.prop(settings, "back")
            elif setting.type == 'ELASTIC':
                sub = layout.column(align=True)
                sub.prop(settings, "amplitude")
                sub.prop(settings, "period")


class GreasePencilBrushPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Drawing Brushes"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        is_3d_view = context.space_data.type == 'VIEW_3D'
        if is_3d_view:
            if context.gpencil_data is None:
                return False

            gpd = context.gpencil_data
            return bool(gpd.is_stroke_paint_mode)
        else:
            return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        row = layout.row()
        col = row.column()

        ts = context.scene.tool_settings
        col.template_icon_view(ts, "gpencil_brushes_enum", show_labels=True)

        col = row.column()
        sub = col.column(align=True)
        sub.operator("gpencil.brush_add", icon='ZOOMIN', text="")
        sub.operator("gpencil.brush_remove", icon='ZOOMOUT', text="")
        sub.menu("GPENCIL_MT_brush_specials", icon='DOWNARROW_HLT', text="")
        brush = context.active_gpencil_brush
        if brush:
            if len(ts.gpencil_brushes) > 1:
                col.separator()
                sub = col.column(align=True)
                sub.operator("gpencil.brush_move", icon='TRIA_UP', text="").type = 'UP'
                sub.operator("gpencil.brush_move", icon='TRIA_DOWN', text="").type = 'DOWN'

        # Brush details
        if brush is not None:
            row = layout.row()
            row.prop(brush, "name", text="")
            row = layout.row(align=True)
            row.prop(brush, "use_random_pressure", text="", icon='RNDCURVE')
            row.prop(brush, "line_width", text="Radius")
            row.prop(brush, "use_pressure", text="", icon='STYLUS_PRESSURE')
            row = layout.row(align=True)
            row.prop(brush, "use_random_strength", text="", icon='RNDCURVE')
            row.prop(brush, "strength", slider=True)
            row.prop(brush, "use_strength_pressure", text="", icon='STYLUS_PRESSURE')

            row = layout.row(align=False)
            row.prop(context.tool_settings, "use_gpencil_draw_onback", text="Draw on Back")

        row = layout.row()
        row.operator("gpencil.fill", icon='BRUSH_DATA', text="Fill")


class GreasePencilBrushOptionsPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Strokes"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        is_3d_view = context.space_data.type == 'VIEW_3D'
        if is_3d_view:
            if context.gpencil_data is None:
                return False

            gpd = context.gpencil_data
            return bool(gpd.is_stroke_paint_mode)
        else:
            return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        brush = context.active_gpencil_brush
        if brush is not None:

            row = layout.row(align=True)
            col = row.column(align=True)
            col.label(text="Stroke Quality:")
            col.prop(brush, "pen_smooth_factor")
            col.prop(brush, "pen_smooth_steps")

            row = layout.row(align=True)
            col = row.column(align=True)
            col.prop(brush, "pen_thick_smooth_factor")
            col.prop(brush, "pen_thick_smooth_steps")

            col.separator()
            row = col.row(align=False)
            row.prop(brush, "pen_subdivision_steps")
            row.prop(brush, "random_subdiv", text="Randomness", slider=True)

            col = layout.column(align=True)
            col.label(text="Settings:")
            col.prop(brush, "random_press", slider=True)

            row = layout.row(align=True)
            row.prop(brush, "jitter", slider=True)
            row.prop(brush, "use_jitter_pressure", text="", icon='STYLUS_PRESSURE')
            row = layout.row()
            row.prop(brush, "angle", slider=True)
            row.prop(brush, "angle_factor", text="Factor", slider=True)


class GreasePencilStrokeSculptPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Sculpt Strokes"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        gpd = context.gpencil_data
        if context.editable_gpencil_strokes:
            is_3d_view = context.space_data.type == 'VIEW_3D'
            if not is_3d_view:
                return bool(gpd.use_stroke_edit_mode)
            else:
                return bool(gpd.is_stroke_sculpt_mode)

        return False

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpd = context.gpencil_data
        settings = context.tool_settings.gpencil_sculpt
        tool = settings.tool
        brush = settings.brush

        layout.template_icon_view(settings, "tool", show_labels=True)

        col = layout.column()
        col.prop(brush, "size", slider=True)
        row = col.row(align=True)
        row.prop(brush, "strength", slider=True)
        row.prop(brush, "use_pressure_strength", text="")
        col.prop(brush, "use_falloff")

        if tool in {'SMOOTH', 'RANDOMIZE'}:
            row = layout.row(align=True)
            row.prop(settings, "affect_position", text="Position", icon='MESH_DATA', toggle=True)
            row.prop(settings, "affect_strength", text="Strength", icon='COLOR', toggle=True)
            row.prop(settings, "affect_thickness", text="Thickness", icon='LINE_DATA', toggle=True)

        layout.separator()

        if tool == 'THICKNESS':
            layout.row().prop(brush, "direction", expand=True)
        elif tool == 'PINCH':
            row = layout.row(align=True)
            row.prop_enum(brush, "direction", 'ADD', text="Pinch")
            row.prop_enum(brush, "direction", 'SUBTRACT', text="Inflate")
        elif settings.tool == 'TWIST':
            row = layout.row(align=True)
            row.prop_enum(brush, "direction", 'SUBTRACT', text="CW")
            row.prop_enum(brush, "direction", 'ADD', text="CCW")

        row = layout.row(align=True)
        row.prop(settings, "use_select_mask")
        row = layout.row(align=True)
        row.prop(settings, "selection_alpha", slider=True)

        if tool == 'SMOOTH':
            layout.prop(brush, "affect_pressure")


class GreasePencilWeightPaintPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Weight Paint"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        gpd = context.gpencil_data
        if context.editable_gpencil_strokes:
            is_3d_view = context.space_data.type == 'VIEW_3D'
            if not is_3d_view:
                return bool(gpd.use_stroke_edit_mode)
            else:
                return bool(gpd.is_stroke_weight_mode)

        return False

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpd = context.gpencil_data
        settings = context.tool_settings.gpencil_sculpt
        tool = settings.tool
        brush = settings.brush

        layout.template_icon_view(settings, "weight_tool", show_labels=True)

        col = layout.column()
        col.prop(brush, "size", slider=True)
        row = col.row(align=True)
        row.prop(brush, "strength", slider=True)
        row.prop(brush, "use_pressure_strength", text="")
        col.prop(brush, "use_falloff")


class GreasePencilMultiFramePanel:
    bl_label = "Multi Frame"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        gpd = context.gpencil_data
        if context.editable_gpencil_strokes:
            is_3d_view = context.space_data.type == 'VIEW_3D'
            if is_3d_view:
                return bool(gpd.is_stroke_sculpt_mode or gpd.is_stroke_weight_mode)

        return False

    @staticmethod
    def draw_header(self, context):
        layout = self.layout

        gpd = context.gpencil_data
        layout.prop(gpd, "use_multiedit", text="")

    @staticmethod
    def draw(self, context):
        gpd = context.gpencil_data
        settings = context.tool_settings.gpencil_sculpt

        layout = self.layout
        layout.enabled = gpd.use_multiedit

        col = layout.column(align=True)
        col.prop(gpd, "show_multiedit_line_only", text="Display only edit lines")
        col.prop(settings, "use_multiframe_falloff")

        # Falloff curve
        box = col.box()
        box.enabled = gpd.use_multiedit and settings.use_multiframe_falloff
        box.template_curve_mapping(settings, "multiframe_falloff_curve", brush=True)


class GreasePencilAppearancePanel:
    bl_label = "Appearance"
    bl_category = "Options"
    bl_region_type = 'TOOLS'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        is_gpmode = context.active_object and \
                    context.active_object.mode in {'GPENCIL_EDIT', 'GPENCIL_PAINT', 'GPENCIL_SCULPT'}
        return is_gpmode

    @staticmethod
    def draw(self, context):
        layout = self.layout
        settings = context.tool_settings.gpencil_sculpt
        brush = settings.brush

        col = layout.column()
        if context.active_object.mode == 'GPENCIL_PAINT':
            drawingbrush = context.active_gpencil_brush
            col.prop(drawingbrush, "use_cursor", text="Show Brush")
            row = col.row(align=True)
            row.prop(drawingbrush, "cursor_color", text="Color")

        if context.active_object.mode in ('GPENCIL_SCULPT', 'GPENCIL_WEIGHT'):
            col.prop(brush, "use_cursor", text="Show Brush")
            row = col.row(align=True)
            row.prop(brush, "cursor_color_add", text="Add")
            row = col.row(align=True)
            row.prop(brush, "cursor_color_sub", text="Subtract")


class GreasePencilEraserPanel:
    bl_label = "Eraser"
    bl_category = "Options"
    bl_region_type = 'TOOLS'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        if context.gpencil_data is None:
            return False

        return context.active_object and context.active_object.mode == 'GPENCIL_PAINT'

    @staticmethod
    def draw(self, context):
        layout = self.layout
        col = layout.column(align=True)
        col.prop(context.user_preferences.edit, "grease_pencil_eraser_radius", text="Radius")


class GreasePencilBrushCurvesPanel:
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    bl_label = "Brush Curves"
    bl_category = "Tools"
    bl_region_type = 'TOOLS'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        if context.active_gpencil_brush is None:
            return False

        is_3d_view = context.space_data.type == 'VIEW_3D'
        brush = context.active_gpencil_brush
        if is_3d_view:
            if context.gpencil_data is None:
                return False

            gpd = context.gpencil_data
            return bool(gpd.is_stroke_paint_mode)
        else:
            return bool(brush)

    @staticmethod
    def draw(self, context):
        layout = self.layout
        brush = context.active_gpencil_brush
        # Brush
        layout.label("Sensitivity")
        box = layout.box()
        box.template_curve_mapping(brush, "curve_sensitivity", brush=True)

        layout.label("Strength")
        box = layout.box()
        box.template_curve_mapping(brush, "curve_strength", brush=True)

        layout.label("Jitter")
        box = layout.box()
        box.template_curve_mapping(brush, "curve_jitter", brush=True)


###############################

class GPENCIL_MT_pie_tool_palette(Menu):
    """A pie menu for quick access to Grease Pencil tools"""
    bl_label = "Grease Pencil Tools"

    def draw(self, context):
        layout = self.layout

        pie = layout.menu_pie()
        gpd = context.gpencil_data

        # W - Drawing Types
        col = pie.column()
        col.operator("gpencil.draw", text="Draw", icon='GREASEPENCIL').mode = 'DRAW'
        col.operator("gpencil.draw", text="Straight Lines", icon='LINE_DATA').mode = 'DRAW_STRAIGHT'
        col.operator("gpencil.draw", text="Poly", icon='MESH_DATA').mode = 'DRAW_POLY'

        # E - Eraser
        # XXX: needs a dedicated icon...
        col = pie.column()
        col.operator("gpencil.draw", text="Eraser", icon='FORCE_CURVE').mode = 'ERASER'

        # E - "Settings" Palette is included here too, since it needs to be in a stable position...
        if gpd and gpd.layers.active:
            col.separator()
            col.operator("wm.call_menu_pie", text="Settings...", icon='SCRIPTWIN').name = "GPENCIL_MT_pie_settings_palette"

        # Editing tools
        if gpd:
            if gpd.use_stroke_edit_mode and context.editable_gpencil_strokes:
                # S - Exit Edit Mode
                pie.operator("gpencil.editmode_toggle", text="Exit Edit Mode", icon='EDIT')

                # N - Transforms
                col = pie.column()
                row = col.row(align=True)
                row.operator("transform.translate", icon='MAN_TRANS')
                row.operator("transform.rotate", icon='MAN_ROT')
                row.operator("transform.resize", text="Scale", icon='MAN_SCALE')
                row = col.row(align=True)
                row.label("Proportional Edit:")
                row.prop(context.tool_settings, "proportional_edit", text="", icon_only=True)
                row.prop(context.tool_settings, "proportional_edit_falloff", text="", icon_only=True)

                # NW - Select (Non-Modal)
                col = pie.column()
                col.operator("gpencil.select_all", text="Select All", icon='PARTICLE_POINT')
                col.operator("gpencil.select_all", text="Select Inverse", icon='BLANK1')
                col.operator("gpencil.select_linked", text="Select Linked", icon='LINKED')
                col.operator("gpencil.palettecolor_select", text="Select Color", icon='COLOR')

                # NE - Select (Modal)
                col = pie.column()
                col.operator("gpencil.select_border", text="Border Select", icon='BORDER_RECT')
                col.operator("gpencil.select_circle", text="Circle Select", icon='META_EMPTY')
                col.operator("gpencil.select_lasso", text="Lasso Select", icon='BORDER_LASSO')
                col.operator("gpencil.select_alternate", text="Alternate Select", icon='BORDER_LASSO')

                # SW - Edit Tools
                col = pie.column()
                col.operator("gpencil.duplicate_move", icon='PARTICLE_PATH', text="Duplicate")
                col.operator("gpencil.delete", icon='X', text="Delete...")

                # SE - More Tools
                pie.operator("wm.call_menu_pie", text="More...").name = "GPENCIL_MT_pie_tools_more"
            else:
                # Toggle Edit Mode
                pie.operator("gpencil.editmode_toggle", text="Enable Stroke Editing", icon='EDIT')


class GPENCIL_MT_pie_settings_palette(Menu):
    """A pie menu for quick access to Grease Pencil settings"""
    bl_label = "Grease Pencil Settings"

    @classmethod
    def poll(cls, context):
        return bool(context.gpencil_data and context.active_gpencil_layer)

    def draw(self, context):
        layout = self.layout

        pie = layout.menu_pie()
        gpd = context.gpencil_data
        gpl = context.active_gpencil_layer
        palcolor = context.active_palettecolor
        brush = context.active_gpencil_brush

        is_editmode = bool(gpd and gpd.use_stroke_edit_mode and context.editable_gpencil_strokes)

        # W - Stroke draw settings
        col = pie.column(align=True)
        if palcolor is not None:
            col.enabled = not palcolor.lock
            col.label(text="Stroke")
            col.prop(palcolor, "color", text="")
            col.prop(palcolor, "alpha", text="", slider=True)

        # E - Fill draw settings
        col = pie.column(align=True)
        if palcolor is not None:
            col.enabled = not palcolor.lock
            col.label(text="Fill")
            col.prop(palcolor, "fill_color", text="")
            col.prop(palcolor, "fill_alpha", text="", slider=True)

        # S Brush settings
        gpencil_active_brush_settings_simple(context, pie)

        # N - Active Layer
        col = pie.column()
        col.label("Active Layer:      ")

        row = col.row()
        row.operator_context = 'EXEC_REGION_WIN'
        row.operator_menu_enum("gpencil.layer_change", "layer", text="", icon='GREASEPENCIL')
        row.prop(gpl, "info", text="")
        row.operator("gpencil.layer_remove", text="", icon='X')

        row = col.row()
        row.prop(gpl, "lock")
        row.prop(gpl, "hide")
        col.prop(gpl, "use_onion_skinning")

        # NW/NE/SW/SE - These operators are only available in editmode
        # as they require strokes to be selected to work
        if is_editmode:
            # NW - Move stroke Down
            col = pie.column(align=True)
            col.label("Arrange Strokes")
            col.operator("gpencil.stroke_arrange", text="Send to Back").direction = 'BOTTOM'
            col.operator("gpencil.stroke_arrange", text="Send Backward").direction = 'DOWN'

            # NE - Move stroke Up
            col = pie.column(align=True)
            col.label("Arrange Strokes")
            col.operator("gpencil.stroke_arrange", text="Bring to Front").direction = 'TOP'
            col.operator("gpencil.stroke_arrange", text="Bring Forward").direction = 'UP'

            # SW - Move stroke to color
            col = pie.column(align=True)
            col.operator("gpencil.stroke_change_color", text="Move to Color")

            # SE - Join strokes
            col = pie.column(align=True)
            col.label("Join Strokes")
            row = col.row()
            row.operator("gpencil.stroke_join", text="Join").type = 'JOIN'
            row.operator("gpencil.stroke_join", text="Join & Copy").type = 'JOINCOPY'
            col.operator("gpencil.stroke_flip", text="Flip Direction")

            col.prop(gpd, "show_stroke_direction", text="Show Drawing Direction")


class GPENCIL_MT_pie_tools_more(Menu):
    """A pie menu for accessing more Grease Pencil tools"""
    bl_label = "More Grease Pencil Tools"

    @classmethod
    def poll(cls, context):
        gpd = context.gpencil_data
        return bool(gpd and gpd.use_stroke_edit_mode and context.editable_gpencil_strokes)

    def draw(self, context):
        layout = self.layout

        pie = layout.menu_pie()
        # gpd = context.gpencil_data

        col = pie.column(align=True)
        col.operator("gpencil.copy", icon='COPYDOWN', text="Copy")
        col.operator("gpencil.paste", icon='PASTEDOWN', text="Paste")

        col = pie.column(align=True)
        col.operator("gpencil.select_more", icon='ZOOMIN')
        col.operator("gpencil.select_less", icon='ZOOMOUT')

        pie.operator("transform.mirror", icon='MOD_MIRROR')
        pie.operator("transform.bend", icon='MOD_SIMPLEDEFORM')
        pie.operator("transform.shear", icon='MOD_TRIANGULATE')
        pie.operator("transform.tosphere", icon='MOD_MULTIRES')

        pie.operator("gpencil.convert", icon='OUTLINER_OB_CURVE', text="Convert...")
        pie.operator("wm.call_menu_pie", text="Back to Main Palette...").name = "GPENCIL_MT_pie_tool_palette"


class GPENCIL_MT_pie_sculpt(Menu):
    """A pie menu for accessing Grease Pencil stroke sculpting settings"""
    bl_label = "Grease Pencil Sculpt"

    @classmethod
    def poll(cls, context):
        gpd = context.gpencil_data
        return bool(gpd and gpd.use_stroke_edit_mode and context.editable_gpencil_strokes)

    def draw(self, context):
        layout = self.layout

        pie = layout.menu_pie()

        settings = context.tool_settings.gpencil_sculpt
        brush = settings.brush

        # W - Launch Sculpt Mode
        col = pie.column()
        # col.label("Tool:")
        col.prop(settings, "tool", text="")
        col.operator("gpencil.brush_paint", text="Sculpt", icon='SCULPTMODE_HLT')

        # E - Common Settings
        col = pie.column(align=True)
        col.prop(brush, "size", slider=True)
        row = col.row(align=True)
        row.prop(brush, "strength", slider=True)
        # row.prop(brush, "use_pressure_strength", text="", icon_only=True)
        col.prop(brush, "use_falloff")
        if settings.tool in {'SMOOTH', 'RANDOMIZE'}:
            row = col.row(align=True)
            row.prop(settings, "affect_position", text="Position", icon='MESH_DATA', toggle=True)
            row.prop(settings, "affect_strength", text="Strength", icon='COLOR', toggle=True)
            row.prop(settings, "affect_thickness", text="Thickness", icon='LINE_DATA', toggle=True)

        # S - Change Brush Type Shortcuts
        row = pie.row()
        row.prop_enum(settings, "tool", value='GRAB')
        row.prop_enum(settings, "tool", value='PUSH')
        row.prop_enum(settings, "tool", value='CLONE')

        # N - Change Brush Type Shortcuts
        row = pie.row()
        row.prop_enum(settings, "tool", value='SMOOTH')
        row.prop_enum(settings, "tool", value='THICKNESS')
        row.prop_enum(settings, "tool", value='STRENGTH')
        row.prop_enum(settings, "tool", value='RANDOMIZE')


###############################


class GPENCIL_MT_snap(Menu):
    bl_label = "Snap"

    def draw(self, context):
        layout = self.layout

        layout.operator("gpencil.snap_to_grid", text="Selection to Grid")
        layout.operator("gpencil.snap_to_cursor", text="Selection to Cursor").use_offset = False
        layout.operator("gpencil.snap_to_cursor", text="Selection to Cursor (Offset)").use_offset = True

        layout.separator()

        layout.operator("gpencil.snap_cursor_to_selected", text="Cursor to Selected")
        layout.operator("view3d.snap_cursor_to_center", text="Cursor to Center")
        layout.operator("view3d.snap_cursor_to_grid", text="Cursor to Grid")


class GPENCIL_MT_gpencil_edit_specials(Menu):
    bl_label = "GPencil Specials"

    def draw(self, context):
        layout = self.layout
        is_3d_view = context.space_data.type == 'VIEW_3D'

        layout.operator_context = 'INVOKE_REGION_WIN'

        layout.operator("gpencil.stroke_subdivide", text="Subdivide")
        layout.operator("gpencil.stroke_simplify", text="Simplify")

        layout.separator()

        layout.operator("gpencil.stroke_join", text="Join").type = 'JOIN'
        layout.operator("gpencil.stroke_join", text="Join & Copy").type = 'JOINCOPY'
        layout.operator("gpencil.stroke_flip", text="Flip Direction")

        if is_3d_view:
            layout.separator()
            layout.operator("gpencil.reproject")


class GPENCIL_MT_gpencil_draw_specials(Menu):
    bl_label = "GPencil Draw Specials"

    def draw(self, context):
        layout = self.layout
        is_3d_view = context.space_data.type == 'VIEW_3D'

        layout.operator_context = 'INVOKE_REGION_WIN'

        layout.operator("gpencil.active_frames_delete_all", text="Delete Frame")


class GPENCIL_MT_gpencil_vertex_group(Menu):
    bl_label = "GP Vertex Groups"

    def draw(self, context):
        layout = self.layout

        layout.operator_context = 'EXEC_AREA'
        layout.operator("object.vertex_group_add")

        ob = context.active_object
        if ob.vertex_groups.active:
            layout.separator()

            layout.operator("gpencil.vertex_group_assign", text="Assign to Active Group")
            layout.operator("gpencil.vertex_group_remove_from", text="Remove from Active Group")

            layout.separator()
            layout.operator_menu_enum("object.vertex_group_set_active", "group", text="Set Active Group")
            layout.operator("object.vertex_group_remove", text="Remove Active Group").all = False
            layout.operator("object.vertex_group_remove", text="Remove All Groups").all = True

            layout.separator()
            layout.operator("gpencil.vertex_group_select", text="Select Points")
            layout.operator("gpencil.vertex_group_deselect", text="Deselect Points")


class GPENCIL_UL_brush(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.GPencilBrush)
        brush = item

        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            row = layout.row(align=True)
            row.prop(brush, "name", text="", emboss=False, icon='BRUSH_DATA')
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class GPENCIL_UL_palettecolor(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.PaletteColor)
        palcolor = item

        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            if palcolor.lock:
                layout.active = False

            split = layout.split(percentage=0.25)
            row = split.row(align=True)
            row.enabled = not palcolor.lock
            row.prop(palcolor, "color_rgba", text="", emboss=palcolor.is_stroke_visible)
            row.prop(palcolor, "fill_rgba", text="", emboss=palcolor.is_fill_visible)
            split.prop(palcolor, "name", text="", emboss=False)

            row = layout.row(align=True)
            row.prop(palcolor, "lock", text="", emboss=False)
            row.prop(palcolor, "hide", text="", emboss=False)
            if palcolor.ghost is True:
                icon = 'GHOST_DISABLED'
            else:
                icon = 'GHOST_ENABLED'
            row.prop(palcolor, "ghost", text="", icon=icon, emboss=False)

        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class GPENCIL_UL_layer(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.GPencilLayer)
        gpl = item
        gpd = context.gpencil_data

        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            if gpl.lock:
                layout.active = False

            row = layout.row(align=True)
            if gpl.is_parented:
                icon = 'BONE_DATA'
            else:
                icon = 'BLANK1'

            row.label(text="", icon=icon)
            row.prop(gpl, "info", text="", emboss=False)

            row = layout.row(align=True)
            row.prop(gpl, "lock", text="", emboss=False)
            row.prop(gpl, "hide", text="", emboss=False)
            row.prop(gpl, "unlock_color", text="", emboss=False)
            if gpl.use_onion_skinning is False:
                icon = 'GHOST_DISABLED'
            else:
                icon = 'GHOST_ENABLED'
            subrow = row.row(align=True)
            subrow.prop(gpl, "use_onion_skinning", text="", icon=icon, emboss=False)
            subrow.active = gpd.use_onion_skinning
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class GPENCIL_MT_layer_specials(Menu):
    bl_label = "Layer"

    def draw(self, context):
        layout = self.layout

        layout.operator("gpencil.layer_duplicate", icon='COPY_ID')  # XXX: needs a dedicated icon

        layout.separator()

        layout.operator("gpencil.reveal", icon='RESTRICT_VIEW_OFF', text="Show All")
        layout.operator("gpencil.hide", icon='RESTRICT_VIEW_ON', text="Hide Others").unselected = True

        layout.separator()

        layout.operator("gpencil.lock_all", icon='LOCKED', text="Lock All")
        layout.operator("gpencil.unlock_all", icon='UNLOCKED', text="UnLock All")

        layout.separator()

        layout.operator("gpencil.layer_merge", icon='NLA', text="Merge Down")


class GPENCIL_MT_brush_specials(Menu):
    bl_label = "Layer"

    def draw(self, context):
        layout = self.layout
        layout.operator("gpencil.brush_copy", icon='PASTEDOWN', text="Copy Current Drawing Brush")
        layout.operator("gpencil.brush_presets_create", icon='HELP', text="Create a Set of Predefined Brushes")


class GPENCIL_MT_palettecolor_duplicate(Menu):
    bl_label = "Layer"

    def draw(self, context):
        layout = self.layout

        layout.operator_enum("palette.palettecolor_duplicate", "type")

class GPENCIL_MT_palettecolor_specials(Menu):
    bl_label = "Layer"

    def draw(self, context):
        layout = self.layout

        layout.operator("palette.palettecolor_reveal", icon='RESTRICT_VIEW_OFF', text="Show All")
        layout.operator("palette.palettecolor_hide", icon='RESTRICT_VIEW_ON', text="Hide Others").unselected = True

        layout.separator()

        layout.operator("palette.palettecolor_lock_all", icon='LOCKED', text="Lock All")
        layout.operator("palette.palettecolor_unlock_all", icon='UNLOCKED', text="UnLock All")

        layout.separator()
        layout.menu("GPENCIL_MT_palettecolor_duplicate", icon='PASTEDOWN', text="Duplicate")

        layout.separator()

        layout.operator("palette.palettecolor_select", icon='COLOR', text="Select Strokes")
        layout.operator("gpencil.stroke_change_color", icon='MAN_TRANS', text="Move to Color")

        layout.separator()

        layout.operator("gpencil.stroke_lock_color", icon='BORDER_RECT', text="Lock Unselected")
        layout.operator("palette.lock_layer", icon='COLOR', text="Lock Unused")


class GreasePencilDataPanel:
    bl_label = "Grease Pencil Layers"
    bl_region_type = 'UI'

    @classmethod
    def poll(cls, context):

        if context.gpencil_data is None:
            return False

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            if context.space_data.context == 'DATA':
                if context.object.type != 'GPENCIL':
                    return False

        return True

    @staticmethod
    def draw_header(self, context):
        if context.space_data.type != 'PROPERTIES':
            self.layout.prop(context.space_data, "show_grease_pencil", text="")

    @staticmethod
    def draw(self, context):
        layout = self.layout

        # owner of Grease Pencil data
        gpd_owner = context.gpencil_data_owner
        gpd = context.gpencil_data

        # Owner Selector
        if context.space_data.type == 'CLIP_EDITOR':
            layout.row().prop(context.space_data, "grease_pencil_source", expand=True)
        # Grease Pencil data selector
        if context.space_data.type != 'PROPERTIES':
            layout.template_ID(gpd_owner, "grease_pencil", new="gpencil.data_add", unlink="gpencil.data_unlink")

        # Grease Pencil data...
        if (gpd is None) or (not gpd.layers):
            layout.operator("gpencil.layer_add", text="New Layer")
        else:
            self.draw_layers(context, layout, gpd)

    def draw_layers(self, context, layout, gpd):
        row = layout.row()

        col = row.column()
        if len(gpd.layers) >= 2:
            layer_rows = 5
        else:
            layer_rows = 2
        col.template_list("GPENCIL_UL_layer", "", gpd, "layers", gpd.layers, "active_index", rows=layer_rows)

        col = row.column()

        sub = col.column(align=True)
        sub.operator("gpencil.layer_add", icon='ZOOMIN', text="")
        sub.operator("gpencil.layer_remove", icon='ZOOMOUT', text="")

        gpl = context.active_gpencil_layer
        if gpl:
            sub.menu("GPENCIL_MT_layer_specials", icon='DOWNARROW_HLT', text="")

            if len(gpd.layers) > 1:
                col.separator()

                sub = col.column(align=True)
                sub.operator("gpencil.layer_move", icon='TRIA_UP', text="").type = 'UP'
                sub.operator("gpencil.layer_move", icon='TRIA_DOWN', text="").type = 'DOWN'

                col.separator()

                sub = col.column(align=True)
                sub.operator("gpencil.layer_isolate", icon='LOCKED', text="").affect_visibility = False
                sub.operator("gpencil.layer_isolate", icon='RESTRICT_VIEW_OFF', text="").affect_visibility = True

        row = layout.row(align=True)
        if gpl:
            row.prop(gpl, "opacity", text="Opacity", slider=True)

        layout.separator()

        # Full-Row - Frame Locking (and Delete Frame)
        row = layout.row(align=True)
        row.active = not gpl.lock

        if gpl.active_frame:
            lock_status = iface_("Locked") if gpl.lock_frame else iface_("Unlocked")
            lock_label = iface_("Frame: %d (%s)") % (gpl.active_frame.frame_number, lock_status)
        else:
            lock_label = iface_("Lock Frame")
        row.prop(gpl, "lock_frame", text=lock_label, icon='UNLOCKED')
        row.operator("gpencil.active_frame_delete", text="", icon='X')


class GreasePencilLayerOptionPanel:
    bl_label = "Options"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        ts = context.scene.tool_settings

        if context.gpencil_data is None:
            return False

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            if context.space_data.context == 'DATA':
                if context.object.type != 'GPENCIL':
                    return False

        gpl = context.active_gpencil_layer
        if gpl is None:
            return False;

        return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpl = context.active_gpencil_layer
        ts = context.scene.tool_settings

        # Layer options
        if context.space_data.type not in ('VIEW_3D', 'PROPERTIES'):
            split = layout.split(percentage=0.5)
            split.active = not gpl.lock
            split.prop(gpl, "show_points")

        split = layout.split(percentage=0.5)
        split.active = not gpl.lock

        # Offsets - Color Tint
        col = split.column()
        subcol = col.column(align=True)
        subcol.enabled = not gpl.lock
        subcol.prop(gpl, "tint_color", text="")
        subcol.prop(gpl, "tint_factor", text="Factor", slider=True)

        # Offsets - Thickness
        col = split.column(align=True)
        row = col.row(align=True)
        row.prop(gpl, "line_change", text="Thickness Change")
        row.operator("gpencil.stroke_apply_thickness", icon='STYLUS_PRESSURE', text="")

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            row = layout.row(align=True)
            row.prop(gpl, "use_stroke_location", text="Draw on Stroke Location")


class GreasePencilOnionPanel:
    bl_label = "Onion Skinning"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        ts = context.scene.tool_settings

        if context.gpencil_data is None:
            return False

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            if context.space_data.context == 'DATA':
                if context.object.type != 'GPENCIL':
                    return False

        gpl = context.active_gpencil_layer
        if gpl is None:
            return False;

        return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpd = context.gpencil_data
        gpl = context.active_gpencil_layer

        col = layout.column(align=True)

        row = col.row()
        row.prop(gpd, "use_onion_skinning")
        sub = row.row(align=True)
        icon = 'RESTRICT_RENDER_OFF' if gpd.use_ghosts_always else 'RESTRICT_RENDER_ON'
        sub.prop(gpd, "use_ghosts_always", text="", icon=icon)
        sub.prop(gpd, "use_ghost_custom_colors", text="", icon='COLOR')

        row = layout.row(align=True)
        row.active = gpd.use_onion_skinning
        row.prop(gpd, "onion_mode", expand=True)

        split = layout.split(percentage=0.5)
        split.active = gpd.use_onion_skinning

        # - Before Frames
        sub = split.column(align=True)
        row = sub.row(align=True)
        row.active = gpd.use_ghost_custom_colors
        row.prop(gpd, "before_color", text="")

        row = sub.row(align=True)
        row.active = gpd.onion_mode in ('ABSOLUTE', 'RELATIVE')
        row.prop(gpd, "ghost_before_range", text="Before")

        # - After Frames
        sub = split.column(align=True)
        row = sub.row(align=True)
        row.active = gpd.use_ghost_custom_colors
        row.prop(gpd, "after_color", text="")

        row = sub.row(align=True)
        row.active = gpd.onion_mode in ('ABSOLUTE', 'RELATIVE')
        row.prop(gpd, "ghost_after_range", text="After")

        # - fade
        split = layout.split(percentage=0.5)
        split.active = gpd.use_onion_skinning
        sub = split.column(align=True)
        sub.prop(gpd, "use_onion_fade", text="Fade")

        sub = split.column(align=True)
        sub.prop(gpd, "onion_factor", text="Opacity", slider=True)

        # -----------------
        # layer override
        # -----------------
        ovr = gpd.use_onion_skinning and gpl.override_onion
        layout.separator()
        box = layout.box()
        col = box.column(align=True)
        col.active = gpd.use_onion_skinning

        row = col.row()
        row.prop(gpl, "override_onion", text="Layer Override")
        if gpl.override_onion:
            sub = row.row(align=True)
            icon = 'RESTRICT_RENDER_OFF' if gpd.use_ghosts_always else 'RESTRICT_RENDER_ON'
            sub.prop(gpl, "use_ghosts_always", text="", icon=icon)
            sub.prop(gpl, "use_ghost_custom_colors", text="", icon='COLOR')

            row = box.row(align=True)
            row.active = ovr
            row.prop(gpl, "onion_mode", expand=True)

            split = box.split(percentage=0.5)
            split.active = ovr

            # - Before Frames
            sub = split.column(align=True)
            row = sub.row(align=True)
            row.active = gpl.use_ghost_custom_colors
            
            row.prop(gpl, "before_color", text="")
            row = sub.row(align=True)
            row.active = gpl.onion_mode in ('ABSOLUTE', 'RELATIVE')
            row.prop(gpl, "ghost_before_range", text="Before")

            # - After Frames
            sub = split.column(align=True)
            row = sub.row(align=True)
            row.active = gpl.use_ghost_custom_colors
            row.prop(gpl, "after_color", text="")
            
            row = sub.row(align=True)
            row.active = gpl.onion_mode in ('ABSOLUTE', 'RELATIVE')
            row.prop(gpl, "ghost_after_range", text="After")

            # - Fade
            split = box.split(percentage=0.5)
            split.active = ovr
            sub = split.column(align=True)
            sub.prop(gpl, "use_onion_fade", text="Fade")

            sub = split.column(align=True)
            sub.prop(gpl, "onion_factor", text="Opacity", slider=True)


class GreasePencilParentLayerPanel:
    bl_label = "Parent Layer"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        ts = context.scene.tool_settings

        if context.gpencil_data is None:
            return False

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            if context.space_data.context == 'DATA':
                if context.object.type != 'GPENCIL':
                    return False

        gpl = context.active_gpencil_layer
        if gpl is None:
            return False;

        return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpl = context.active_gpencil_layer
        row = layout.row()

        col = row.column(align=True)
        col.active = not gpl.lock
        col.label(text="Parent:")
        col.prop(gpl, "parent", text="")

        sub = col.column()
        sub.prop(gpl, "parent_type", text="")
        parent = gpl.parent
        if parent and gpl.parent_type == 'BONE' and parent.type == 'ARMATURE':
            sub.prop_search(gpl, "parent_bone", parent.data, "bones", text="")


class GPENCIL_UL_vgroups(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        vgroup = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            layout.prop(vgroup, "name", text="", emboss=False, icon_value=icon)
            # icon = 'LOCKED' if vgroup.lock_weight else 'UNLOCKED'
            # layout.prop(vgroup, "lock_weight", text="", icon=icon, emboss=False)
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class GreasePencilVertexGroupPanel:
    bl_label = "Vertex Groups"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        obj = context.object
        return obj and obj.type == 'GPENCIL'

    def draw(self, context):
        layout = self.layout

        ob = context.object
        group = ob.vertex_groups.active

        rows = 2
        if group:
            rows = 4

        row = layout.row()
        row.template_list("GPENCIL_UL_vgroups", "", ob, "vertex_groups", ob.vertex_groups, "active_index", rows=rows)

        col = row.column(align=True)
        col.operator("object.vertex_group_add", icon='ZOOMIN', text="")
        col.operator("object.vertex_group_remove", icon='ZOOMOUT', text="").all = False

        if ob.vertex_groups:
            row = layout.row()

            sub = row.row(align=True)
            sub.operator("gpencil.vertex_group_assign", text="Assign")
            sub.operator("gpencil.vertex_group_remove_from", text="Remove")

            sub = row.row(align=True)
            sub.operator("gpencil.vertex_group_select", text="Select")
            sub.operator("gpencil.vertex_group_deselect", text="Deselect")

            layout.prop(context.tool_settings, "vertex_group_weight", text="Weight")


class GreasePencilInfoPanel:
    bl_label = "Information"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        ts = context.scene.tool_settings

        if context.gpencil_data is None:
            return False

        if context.space_data.type in ('VIEW_3D', 'PROPERTIES'):
            if context.space_data.context == 'DATA':
                if context.object.type != 'GPENCIL':
                    return False

        return True

    @staticmethod
    def draw(self, context):
        layout = self.layout
        gpd = context.gpencil_data

        split = layout.split(percentage=0.5)

        col = split.column(align=True)
        col.label("Layers:", icon="LAYER_ACTIVE")
        col.label("Frames:", icon="LAYER_ACTIVE")
        col.label("Strokes:", icon="LAYER_ACTIVE")
        col.label("Points:", icon="LAYER_ACTIVE")
        col.label("Palettes:", icon="LAYER_ACTIVE")

        col = split.column(align=True)
        col.label(str(gpd.info_total_layers))
        col.label(str(gpd.info_total_frames))
        col.label(str(gpd.info_total_strokes))
        col.label(str(gpd.info_total_points))
        col.label(str(gpd.info_total_palettes))


###############################

# TODO: Placeholder - Annotation views shouldn't use this anymore...
class GreasePencilPaletteColorPanel:
    bl_label = "Grease Pencil Colors"
    bl_region_type = 'TOOLS'
    
    @staticmethod
    def draw(self, context):
        layout = self.layout
        
        layout.label("Placeholder")

###############################

class GreasePencilToolsPanel:
    # For use in "2D" Editors without their own toolbar
    # subclass must set
    # bl_space_type = 'IMAGE_EDITOR'
    # bl_options = {'DEFAULT_CLOSED'}
    bl_label = "Grease Pencil Settings"
    bl_region_type = 'UI'
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return (context.gpencil_data is not None)

    @staticmethod
    def draw(self, context):
        layout = self.layout

        # gpd_owner = context.gpencil_data_owner
        gpd = context.gpencil_data

        layout.prop(gpd, "use_stroke_edit_mode", text="Enable Editing", icon='EDIT', toggle=True)

        layout.separator()

        layout.label("Proportional Edit:")
        row = layout.row()
        row.prop(context.tool_settings, "proportional_edit", text="")
        row.prop(context.tool_settings, "proportional_edit_falloff", text="")

        layout.separator()
        layout.separator()

        gpencil_active_brush_settings_simple(context, layout)

        layout.separator()

        gpencil_stroke_placement_settings(context, layout)

###############################

classes = (
    GPENCIL_MT_pie_tool_palette,
    GPENCIL_MT_pie_settings_palette,
    GPENCIL_MT_pie_tools_more,
    GPENCIL_MT_pie_sculpt,
    GPENCIL_MT_snap,
    GPENCIL_MT_gpencil_edit_specials,
    GPENCIL_MT_gpencil_draw_specials,
    GPENCIL_MT_gpencil_vertex_group,
    GPENCIL_UL_brush,
    GPENCIL_UL_palettecolor,
    GPENCIL_UL_layer,
    GPENCIL_MT_layer_specials,
    GPENCIL_MT_brush_specials,
    GPENCIL_MT_palettecolor_specials,
    GPENCIL_MT_palettecolor_duplicate,
    GPENCIL_UL_vgroups
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
