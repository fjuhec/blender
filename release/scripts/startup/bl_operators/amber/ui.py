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

# Note: This will be a simple addon later, but until it gets to master, it's simpler to have it
#       as a startup module!

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


class AMBER_PT_tags(Panel, AmberPanel):
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
        elif self.layout_type == 'GRID':
            layout.alignment = 'CENTER'
            layout.label(text="", icon_value=icon)


class AMBER_PT_datablocks(Panel, AmberPanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Available Datablocks"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        ae = space.asset_engine

        row = layout.row()


class AMBER_PT_assets(Panel, AmberPanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Existing Assets"

    def draw(self, context):
        ae = context.space_data.asset_engine

        row = self.layout.row()
        row.template_list("AMBER_UL_assets", "", ae.repository_pg, "assets", ae.repository_pg, "asset_index_active",
                          rows=3)

        col = row.column()
        col.operator("AMBER_OT_asset_add", text="", icon='OBJECT_DATA').active_type = 'OBJECT'
        col.operator("AMBER_OT_asset_add", text="", icon='MATERIAL_DATA').active_type = 'MATERIAL'
        col.operator("AMBER_OT_asset_delete", text="", icon='ZOOMOUT')


classes = (
    AMBER_PT_repositories,

    AMBER_UL_tags_filter,
    AMBER_PT_options,
    AMBER_PT_tags,

    AMBER_UL_assets,
    AMBER_PT_assets,
)
