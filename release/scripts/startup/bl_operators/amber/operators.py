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
        Operator,
        )
#~ from bpy.props import (
        #~ BoolProperty,
        #~ CollectionProperty,
        #~ EnumProperty,
        #~ IntProperty,
        #~ IntVectorProperty,
        #~ PointerProperty,
        #~ StringProperty,
        #~ )

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
        if space and space.type == 'FILE_BROWSER':
            return (space.active_operator is None) and AmberOps.poll(context)
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


class AmberOpsAssetAdd(Operator, AmberOpsEditing):
    """Add an Amber asset to the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_add"
    bl_label = "Add Asset"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        asset = ae.repository_pg.assets.add()

        repository = getattr(ae, "repository", None)
        if repository is None:
            repository = ae.repository = AmberDataRepository()
        repository.from_pg(ae.repository_pg)

        repository.wrt_repo(os.path.join(ae.repository.path, utils.AMBER_DB_NAME), ae.repository.to_dict())

        bpy.ops.file.refresh()

        return {'FINISHED'}


class AmberOpsAssetDelete(Operator, AmberOpsEditing):
    """Delete active Amber asset from the repository (WARNING! No undo!)"""
    bl_idname = "amber.asset_delete"
    bl_label = "Delete Asset"
    bl_options = set()

    def execute(self, context):
        ae = context.space_data.asset_engine
        ae.repository_pg.assets.remove(ae.repository_pg.asset_index_active)

        repository = getattr(ae, "repository", None)
        if repository is None:
            repository = ae.repository = AmberDataRepository()
        repository.from_pg(ae.repository_pg)

        repository.wrt_repo(os.path.join(repository.path, utils.AMBER_DB_NAME), repository.to_dict())

        bpy.ops.file.refresh()

        return {'FINISHED'}


classes = (
    AmberOpsRepositoryAdd,
    AmberOpsAssetDelete,
    )
