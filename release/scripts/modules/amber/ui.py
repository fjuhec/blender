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
from bpy.types import (
        Panel,
        UIList,
        )


class AmberPanel():
    """Base Amber asset engine panels class."""
    @classmethod
    def poll(cls, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineAmber":
                return True
        return False


class AmberPanelEditing(AmberPanel):
    """Base Amber asset engine panels class for editing repositories."""
    @classmethod
    def poll(cls, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            return (space.active_operator is None) and AmberPanel.poll(context)
        return False


##############
# Engine stuff

class AMBER_PT_repositories(Panel, AmberPanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Bookmarks"
    bl_label = "Amber Repositories"

    def draw(self, context):
        ae = context.space_data.asset_engine

        row = self.layout.row()
        row.template_list("FILEBROWSER_UL_dir", "amber_repositories",
                          ae.repositories_pg, "repositories", ae.repositories_pg, "repository_index_active",
                          item_dyntip_propname="path", rows=1)

        col = row.column()
        col.operator("AMBER_OT_repository_add", text="", icon='ZOOMIN')
        col.operator("AMBER_OT_repository_remove", text="", icon='ZOOMOUT')
        col.operator("AMBER_OT_repository_remove", text="", icon='PANEL_CLOSE').do_erase = True

        repo = ae.repositories_pg.repositories[ae.repositories_pg.repository_index_active]
        row = self.layout.row()
        row.prop(repo, "path", text="")


class AMBER_UL_tags_filter(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.AmberDataTagPG))
        ae_amber_repo = data
        tag = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            split = layout.split(0.66, False)
            split.prop(tag, "name", text="", emboss=False, icon_value=icon)
            row = split.row(align=True)
            sub = row.row(align=True)
            sub.active = tag.use_include
            sub.prop(tag, "use_include", emboss=False, text="", icon='ZOOMIN')
            sub = row.row(align=True)
            sub.active = tag.use_exclude
            sub.prop(tag, "use_exclude", emboss=False, text="", icon='ZOOMOUT')
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class AMBER_PT_options(Panel, AmberPanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Amber Options"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        ae = space.asset_engine

        row = layout.row()
        row.prop(context.window_manager, "amber_enable_editing", toggle=True)


class AMBER_PT_tags_filter(Panel, AmberPanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Filter"
    bl_label = "Tags"

    def draw(self, context):
        ae = context.space_data.asset_engine

        self.layout.template_list("AMBER_UL_tags_filter", "", ae.repository_pg, "tags", ae.repository_pg, "tag_index_active")


#######################
# Adding/editing assets

class AMBER_UL_datablocks(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.AmberDataTagPG))
        ae_amber_repo = data
        tag = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            split = layout.split(0.66, False)
            split.prop(tag, "name", text="", emboss=False, icon_value=icon)
            row = split.row(align=True)
            sub = row.row(align=True)
            sub.active = tag.use_include
            sub.prop(tag, "use_include", emboss=False, text="", icon='ZOOMIN')
            sub = row.row(align=True)
            sub.active = tag.use_exclude
            sub.prop(tag, "use_exclude", emboss=False, text="", icon='ZOOMOUT')
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class AMBER_UL_assets(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.AmberDataAssetPG))
        ae_amber_repo = data
        asset = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            split = layout.split(0.66, False)
            split.prop(asset, "name", text="", emboss=False, icon_value=icon)
            split.prop(asset, "is_selected", text="")
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class AMBER_UL_asset_tags(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.AmberDataTagPG))
        tag = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            split = layout.split(0.66, False)
            split.prop(tag, "name", text="", emboss=False, icon_value=icon)
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class AMBER_UL_tags(UIList):
    use_filter_name_reverse = bpy.props.BoolProperty(name="Reverse Name", default=False, options=set(),
                                                     description="Reverse name filtering")

    def _gen_order_update(name1, name2):
        def _u(self, ctxt):
            if (getattr(self, name1)):
                setattr(self, name2, False)
        return _u
    use_order_name = bpy.props.BoolProperty(name="Name", default=False, options=set(),
                                            description="Sort tags by their name (case-insensitive)",
                                            update=_gen_order_update("use_order_name", "use_order_importance"))
    use_order_importance = bpy.props.BoolProperty(name="Importance", default=True, options=set(),
                                                  description="Sort tags by their weight",
                                                  update=_gen_order_update("use_order_importance", "use_order_name"))

    use_filter_orderby_invert= bpy.props.BoolProperty(name="Invert Ordering", default=False, options=set(),
                                                      description="Reverse tag ordering")

    def draw_item(self, context, layout, data, item, icon, active_data, active_propname, index):
        # assert(isinstance(item, bpy.types.AmberDataTagPG))
        tag = item
        if self.layout_type in {'DEFAULT', 'COMPACT'}:
            split = layout.split(0.5, False)
            split.prop(tag, "name", text="", emboss=False, icon_value=icon)
            split.prop(tag, "priority", text="", emboss=False)
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)

    def draw_filter(self, context, layout):
        # Nothing much to say here, it's usual UI code...
        row = layout.row()

        subrow = row.row(align=True)
        subrow.prop(self, "filter_name", text="")
        icon = 'ZOOM_OUT' if self.use_filter_name_reverse else 'ZOOM_IN'
        subrow.prop(self, "use_filter_name_reverse", text="", icon=icon)

        row = layout.row(align=True)
        row.label("Order by:")
        row.prop(self, "use_order_name", toggle=True)
        row.prop(self, "use_order_importance", toggle=True)
        icon = 'TRIA_UP' if self.use_filter_orderby_invert else 'TRIA_DOWN'
        row.prop(self, "use_filter_orderby_invert", text="", icon=icon)

    def filter_items(self, context, data, propname):
        tags = getattr(data, propname)
        helper_funcs = bpy.types.UI_UL_list

        # Default return values.
        flt_flags = []
        flt_neworder = []

        # Filtering by name
        if self.filter_name:
            flt_flags = helper_funcs.filter_items_by_name(self.filter_name, self.bitflag_filter_item, tags, "name",
                                                          reverse=self.use_filter_name_reverse)
        if not flt_flags:
            flt_flags = [self.bitflag_filter_item] * len(tags)

        # Reorder by name or weight.
        if self.use_order_name:
            flt_neworder = helper_funcs.sort_items_by_name(tags, "name")
        elif self.use_order_importance:
            _sort = [(idx, -tag.priority if self.use_filter_orderby_invert else tag.priority) for idx, tag in enumerate(tags)]
            flt_neworder = helper_funcs.sort_items_helper(_sort, lambda e: e[1], True)

        return flt_flags, flt_neworder


class AMBER_PT_datablocks(Panel, AmberPanelEditing):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Available Datablocks"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        ae = space.asset_engine

        row = layout.row()


class AMBER_PT_assets(Panel, AmberPanelEditing):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Existing Assets"

    def draw(self, context):
        ae = context.space_data.asset_engine

        row = self.layout.row()
        row.template_list("AMBER_UL_assets", "", ae.repository_pg, "assets", ae.repository_pg, "asset_index_active",
                          rows=4)

        col = row.column()
        col.operator_context = 'EXEC_DEFAULT'
        col.operator("AMBER_OT_asset_add", text="", icon='OBJECT_DATA').active_type = 'OBJECT'
        col.operator("AMBER_OT_asset_add", text="", icon='MATERIAL_DATA').active_type = 'MATERIAL'
        col.operator_context = 'INVOKE_DEFAULT'
        col.operator_menu_enum("AMBER_OT_asset_add", "datablock_name", text="", icon='ZOOMIN')
        col.operator("AMBER_OT_asset_delete", text="", icon='ZOOMOUT')

        row = self.layout.row()
        if 0 <= ae.repository_pg.asset_index_active < len(ae.repository_pg.assets):
            asset = ae.repository_pg.assets[ae.repository_pg.asset_index_active]
            row.template_list("AMBER_UL_asset_tags", "", asset, "tags", asset, "tag_index_active", rows=2)
            col = row.column()
            col.operator_menu_enum("AMBER_OT_asset_tag_add", "tag", text="", icon='ZOOMIN')
            col.operator("AMBER_OT_asset_tag_remove", text="", icon='ZOOMOUT')


class AMBER_PT_tags(Panel, AmberPanelEditing):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Existing Tags"

    def draw(self, context):
        ae = context.space_data.asset_engine

        row = self.layout.row()
        row.template_list("AMBER_UL_tags", "", ae.repository_pg, "tags", ae.repository_pg, "tag_index_active", rows=3)

        col = row.column()
        col.operator("AMBER_OT_tag_add", text="", icon='ZOOMIN')
        col.operator("AMBER_OT_tag_delete", text="", icon='ZOOMOUT')


classes = (
    # Browsing
    AMBER_PT_repositories,

    AMBER_UL_tags_filter,
    AMBER_PT_options,
    AMBER_PT_tags_filter,

    # Editing
    AMBER_UL_assets,
    AMBER_UL_asset_tags,
    AMBER_PT_assets,

    AMBER_UL_tags,
    AMBER_PT_tags,
)
