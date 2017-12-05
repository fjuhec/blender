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
        Operator,
        )
from bpy.props import (
        BoolProperty,
        EnumProperty,
        IntProperty,
        StringProperty,
        )

import os

from . import (repository, utils)

from .repository import (
        AmberDataRepository,
        AmberDataRepositoryList,
        )


class AmberOps():
    """Base Amber asset engine operators class."""
    @classmethod
    def poll(cls, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineAmber":
                return True
        return False


class AmberOpsEditing(AmberOps):
    """Base Amber asset engine operators class for editing repositories."""
    @classmethod
    def poll(cls, context):
        space = context.space_data
        wm = context.window_manager
        if space and space.type == 'FILE_BROWSER':
            return (space.active_operator is None) and wm.amber_enable_editing and AmberOps.poll(context)
        return False


class AmberOpsRepositoryAdd(Operator, AmberOpsEditing):
    """Create a new, empty Amber repository in current directory (WARNING! No undo!)"""
    bl_idname = "amber.repository_add"
    bl_label = "Add Repository"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        path = context.space_data.params.directory

        if getattr(ae, "repo", None) is not None:
            self.report({'INFO'}, "Current directory is already an Amber repository, '%s'" % ae.repo.name)
            return {'CANCELLED'}
        if os.path.exists(os.path.join(path, utils.AMBER_DB_NAME)):
            self.report({'INFO'}, "Current directory is already an Amber repository")
            return {'CANCELLED'}

        repository = getattr(ae, "repository", None)
        if repository is None:
            repository = ae.repository = AmberDataRepository()
        repository.clear()

        repository.path = path
        repository.name = "Amber " + os.path.split(repository.path)[0].split(os.path.sep)[-1]
        repository.uuid = utils.uuid_repo_gen(set(AmberDataRepositoryList().repositories), repository.path, repository.name)

        # TODO more default settings, probably default set of basic tags...

        repository.to_pg(ae.repository_pg)
        repository.wrt_repo(os.path.join(repository.path, utils.AMBER_DB_NAME), repository.to_dict())

        bpy.ops.file.refresh()

        return {'FINISHED'}


class AmberOpsRepositoryRemove(Operator, AmberOpsEditing):
    """Remove an Amber repository from this listing, and optionally delete its db and preview files """
    """(*not* the data itself) (WARNING! No undo!)"""
    bl_idname = "amber.repository_remove"
    bl_label = "Remove Repository"
    bl_options = set()

    do_erase = BoolProperty(name="Erase", description="Actually erase active repository")

    def execute(self, context):
        ae = context.space_data.asset_engine

        if self.do_erase:
            repo_pg = ae.repositories_pg.repositories[ae.repositories_pg.repository_index_active]
            repo_path = repo_pg.path
            repo_uuid = repo_pg.uuid[:]
            db_path = os.path.join(repo_path, utils.AMBER_DB_NAME)
            preview_path = os.path.join(repo_path, utils.AMBER_PREVIEW_STORAGE)
            data_path = os.path.join(repo_path, utils.AMBER_LOCAL_STORAGE)

            if not os.path.exists(db_path):
                self.report({'INFO'}, "No Amber repository found at '%s'" % repo_path)
                return {'CANCELLED'}

            repo = AmberDataRepository.ls_repo(db_path)

            if os.path.isdir(preview_path):
                for asset in repo.assets.items:
                    if asset.preview_path:
                        asset_preview_path = os.abspath(os.path.join(repo_path, asset.preview_path))
                        if os.path.isfile(asset_preview_path) and os.path.commonpath((preview_path, asset_preview_path)) == preview_path:
                            os.remove(asset_preview_path)
                    # TODO allow also removing of asset .blend files? would rather not, dangerous option imho...

                if not os.listdir(preview_path):
                    os.rmdir(preview_path)

            if os.path.isdir(data_path):
                if not os.listdir(data_path):
                    os.rmdir(data_path)

        ae.repositories_pg.repositories.remove(ae.repositories_pg.repository_index_active)

        if self.do_erase:
            os.remove(db_path)

        bpy.ops.file.refresh()

        return {'FINISHED'}

    def invoke(self, context, event):
        if self.do_erase:
            return context.window_manager.invoke_confirm(self, event)
        return self.execute(context)


class AmberOpsAssetAdd(Operator, AmberOpsEditing):
    """Add an Amber asset to the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_add"
    bl_label = "Add Asset"
    bl_options = set()

    active_type = EnumProperty(items=(('NONE', "None", "Do not use context to find an active data-block to create asset from"),
                                      ('OBJECT', "Object/Group", "Active Object or Group in Blender"),
                                      ('MATERIAL', "Material", "Active material from active Object"),
                                      # TODO More options?
                                     ),
                               name="Asset Type", description="Type of active datablock to create the asset from")

    copy_local = BoolProperty(name="Copy Local",
                              description="Copy selected datablock and its dependencies into a library .blend file "
                                          "local to the repository (mandatory when current .blend file is not saved)")

    def datablock_name_enum_itemf(self, context):
        ret = [(".".join(("objects", ob.name)), ob.name, "", 'OBJECT_DATA', idx)
                        for idx, ob in enumerate(ob for ob in bpy.data.objects if ob.library is None)]
        off_idx = len(ret)
        ret += [(".".join(("materials", mat.name)), mat.name, "", 'MATERIAL_DATA', off_idx + idx)
                        for idx, mat in enumerate(mat for mat in bpy.data.materials if mat.library is None)]
        off_idx = len(ret)
        ret += [(".".join(("textures", tex.name)), tex.name, "", 'TEXTURE_DATA', off_idx + idx)
                        for idx, tex in enumerate(tex for tex in bpy.data.textures if tex.library is None)]
        return ret
    datablock_name = EnumProperty(items=datablock_name_enum_itemf,
                                  name="ID Name", description="Name of the local datablock to create asset from")

    @staticmethod  # self is not op instance here, but menu generated by window manager...
    def datablocks_select_menu(self, context):
        self.layout.operator_menu_enum("AMBER_OT_asset_add", "datablock_name")

    def execute(self, context):
        import time

        ae = context.space_data.asset_engine

        repository = getattr(ae, "repository", None)
        if repository is None:
            repository = ae.repository = AmberDataRepository()
        repository.from_pg(ae.repository_pg)

        datablock = None
        if self.active_type == 'OBJECT':
            datablock = context.active_object
            if datablock.dupli_type == 'GROUP' and datablock.dupli_group is not None:
                datablock = datablock.dupli_group
        elif self.active_type == 'MATERIAL':
            datablock = context.active_object.material_slots[context.active_object.active_material_index].material

        if datablock is None and self.datablock_name:
            datablock_type, datablock_name = self.datablock_name.split('.', 1)
            datablock = getattr(bpy.data, datablock_type)[datablock_name]

        if datablock is None:
            self.report({'INFO'}, "No suitable active datablock found to create a new asset")
            return {'CANCELLED'}
        elif datablock.library is not None:
            self.report({'INFO'}, "Active datablock is a linked one, this is not supported currently")
            return {'CANCELLED'}

        if bpy.data.is_dirty or not bpy.data.filepath:
            self.report({'WARNING'}, "Current .blend file not saved on disk, enforcing copying data into local repository storage")
            self.copy_local = True

        path_sublib = os.path.join(utils.BLENDER_TYPES_TO_PATH[type(datablock)], datablock.name)
        path_lib = bpy.data.filepath

        asset = ae.repository_pg.assets.add()
        asset.name = datablock.name
        asset.file_type = 'BLENLIB'
        asset.blender_type = utils.BLENDER_TYPES_TO_ENUMVAL[type(datablock)]
        asset.uuid = utils.uuid_asset_gen(set(repository.assets.keys()), ae.repository_pg.uuid, path_sublib, asset.name, [])

        if datablock.preview:
            w, h = datablock.preview.image_size
            if 0 not in {w, h}:
                path_dir = os.path.join(repository.path, utils.AMBER_PREVIEW_STORAGE)
                if not os.path.exists(path_dir):
                    os.mkdir(path_dir)
                path_preview = os.path.join(path_dir, asset.name + "_" + utils.uuid_pack(asset.uuid) + ".dat")
                utils.preview_write_dat(path_preview, w, h, datablock.preview.image_pixels)
                asset.preview_path = os.path.relpath(path_preview, repository.path)

        if self.copy_local:
            path_dir = os.path.join(repository.path, utils.AMBER_LOCAL_STORAGE)
            if not os.path.exists(path_dir):
                os.mkdir(path_dir)
            path_lib = os.path.join(path_dir, asset.name + "_" + utils.uuid_pack(asset.uuid) + ".blend")
            bpy.data.libraries.write(path_lib, {datablock}, relative_remap=True, fake_user=True, compress=True)

        variant = asset.variants.add()
        variant.name = "default"
        variant.uuid = utils.uuid_variant_gen(set(), asset.uuid, variant.name)
        asset.variant_default = variant.uuid

        revision = variant.revisions.add()
        revision.timestamp = int(time.time())
        revision.uuid = utils.uuid_revision_gen(set(), variant.uuid, 0, revision.timestamp)
        variant.revision_default = revision.uuid

        view = revision.views.add()
        view.name = "default"
        view.size = os.stat(path_lib).st_size
        view.timestamp = revision.timestamp
        view.path = os.path.relpath(os.path.join(path_lib, path_sublib), repository.path)
        view.uuid = utils.uuid_view_gen(set(), revision.uuid, view.name, view.size, view.timestamp)
        revision.view_default = view.uuid

        repository.from_pg(ae.repository_pg)
        repository.wrt_repo(os.path.join(ae.repository.path, utils.AMBER_DB_NAME), ae.repository.to_dict())

        bpy.ops.file.refresh()

        return {'FINISHED'}

    def invoke(self, context, event):
        #~ if self.active_type == 'NONE':
            #~ context.window_manager.popup_menu(self.datablocks_select_menu, "Select A Data-block")
            #~ return {'FINISHED'}
        return self.execute(context)


class AmberOpsAssetDelete(Operator, AmberOpsEditing):
    """Delete active Amber asset from the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_delete"
    bl_label = "Delete Asset"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        ae.repository_pg.assets.remove(ae.repository_pg.asset_index_active)

        AmberDataRepository.update_from_asset_engine(ae)

        bpy.ops.file.refresh()

        return {'FINISHED'}


class AmberOpsAssetTagAdd(Operator, AmberOpsEditing):
    """Add a new tag to active Amber asset from the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_tag_add"
    bl_label = "Add Tag"
    bl_options = set()

    def tags_enum_func(self, context):
        if context is None:
            return []
        ae = context.space_data.asset_engine
        tags = ae.repository_pg.tags
        asset = ae.repository_pg.assets[ae.repository_pg.asset_index_active]
        asset_tags = {t.name for t in asset.tags}
        return [(t.name, t.name, t.name) for t in tags if t.name not in asset_tags]
    tag = EnumProperty(items=tags_enum_func, name="Tag", description="New tag to add to active asset")

    def execute(self, context):
        ae = context.space_data.asset_engine
        asset = ae.repository_pg.assets[ae.repository_pg.asset_index_active]
        tags = ae.repository_pg.tags

        tag = [t for t in tags if t.name == self.tag][0]
        if tag:
            asset_tag = asset.tags.add()
            asset_tag.name = tag.name
            asset_tag.priority = tag.priority

        AmberDataRepository.update_from_asset_engine(ae)

        bpy.ops.file.refresh()

        return {'FINISHED'}


class AmberOpsAssetTagRemove(Operator, AmberOpsEditing):
    """Remove active tag of active Amber asset from the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_tag_remove"
    bl_label = "Remove Tag"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        asset = ae.repository_pg.assets[ae.repository_pg.asset_index_active]
        asset.tags.remove(asset.tag_index_active)

        AmberDataRepository.update_from_asset_engine(ae)

        bpy.ops.file.refresh()

        return {'FINISHED'}


class AmberOpsTagAdd(Operator, AmberOpsEditing):
    """Add a new tag to the repository (WARNING! No undo!)"""
    bl_idname = "amber.tag_add"
    bl_label = "Add Tag"
    bl_options = set()

    name = StringProperty(name="Name", description="New tag's name/identifier")
    priority = IntProperty(default=100,
                           name="Priority", description="New tag's priority, how close it will be from start of lists")

    def execute(self, context):
        ae = context.space_data.asset_engine
        tags = ae.repository_pg.tags

        tag_idx = tags.find(self.name)
        if tag_idx != -1:
            ae.repository_pg.tag_index_active = tag_idx
            return {'CANCELLED'}

        tag = tags.add()
        tag.name = self.name
        tag.priority = self.priority
        ae.repository_pg.tag_index_active = len(tags) - 1

        AmberDataRepository.update_from_asset_engine(ae)

        bpy.ops.file.refresh()

        return {'FINISHED'}

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)


class AmberOpsTagDelete(Operator, AmberOpsEditing):
    """Remove active tag from the repository (WARNING! No undo!)"""
    bl_idname = "amber.tag_delete"
    bl_label = "Remove Tag"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        tag = ae.repository_pg.tags[ae.repository_pg.tag_index_active]

        for asset in ae.repository_pg.assets:
            idx = asset.tags.find(tag.name)
            if idx != -1:
                asset.tags.remove(idx)

        ae.repository_pg.tags.remove(ae.repository_pg.tag_index_active)

        AmberDataRepository.update_from_asset_engine(ae)

        bpy.ops.file.refresh()

        return {'FINISHED'}


classes = (
    AmberOpsRepositoryAdd,
    AmberOpsRepositoryRemove,
    AmberOpsAssetAdd,
    AmberOpsAssetDelete,
    AmberOpsAssetTagAdd,
    AmberOpsAssetTagRemove,
    AmberOpsTagAdd,
    AmberOpsTagDelete,
    )
