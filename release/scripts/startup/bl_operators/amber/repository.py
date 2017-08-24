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
        AssetEntry,
        PropertyGroup,
        )
from bpy.props import (
        BoolProperty,
        CollectionProperty,
        EnumProperty,
        IntProperty,
        IntVectorProperty,
        PointerProperty,
        StringProperty,
        )

import json
import os
import stat
import time

from . import utils


###########################
# Asset Engine data classes.
class AmberDataTagPG(PropertyGroup):
    name = StringProperty(name="Name", description="Tag name")
    priority = IntProperty(name="Priority", default=0, description="Tag priority (used to order tags, highest priority go first)")

    def include_update(self, context):
        if self.use_include:
            self.use_exclude = False
        sd = context.space_data
        if sd and sd.type == 'FILE_BROWSER' and sd.asset_engine:
            sd.asset_engine.is_dirty_filtering = True
    use_include = BoolProperty(name="Include", default=False, description="This tag must exist in filtered items",
                               update=include_update)

    def exclude_update(self, context):
        if self.use_exclude:
            self.use_include = False
        sd = context.space_data
        if sd and sd.type == 'FILE_BROWSER' and sd.asset_engine:
            sd.asset_engine.is_dirty_filtering = True
    use_exclude = BoolProperty(name="Exclude", default=False, description="This tag must not exist in filtered items",
                               update=exclude_update)

    @staticmethod
    def from_dict(tags, tags_dict):
        tags.clear()
        tags.update(tags_dict)

    @staticmethod
    def to_dict(tags):
        return tags.copy()

    @staticmethod
    def from_pg(tags, pg):
        tags.clear()
        tags.update({t.name: t.priority for t in pg})

    @staticmethod
    def to_pg(pg, tags, subset=None, do_clear=False):
        if do_clear:
            pg.clear()
            for tag_name, tag_priority in tags.items():
                if subset is not None and tag_name not in subset:
                    continue
                pg_tag = pg.add()
                pg.name = tag_name
                pg.priority = tag_priority
        else:
            removed_tags = set(t.name for t in pg) - set(subset or tags)
            added_tags = set(subset or tags) - set(t.name for t in pg)
            for tag_name in removed_tags:
                pg.remove(pg.find(tag_name))
            for tag_pg in pg:
                tag_pg.priority = tags[tag_pg.name]
            for tag_name in added_tags:
                tag_pg = pg.add()
                tag_pg.name = tag_name
                tag_pg.priority = tags[tag_name]


class AmberDataAssetRevisionPG(PropertyGroup):
    comment = StringProperty(name="Comment", description="Asset/Variant revision comment")
    uuid = IntVectorProperty(name="UUID", description="Revision unique identifier", size=4)

    size = IntProperty(name="Size")
    timestamp = IntProperty(name="Timestamp")

    path = StringProperty(name="Path", description="File path of this item", subtype='FILE_PATH')


class AmberDataAssetRevision():
    def __init__(self):
        self.comment = ""
        self.uuid = (0, 0, 0, 0)
        self.size = 0
        self.timestamp = 0
        self.path = ''

    @staticmethod
    def from_dict(revisions, revisions_dict):
        # For now, fully override revisions.
        revisions.clear()
        for uuid_hexstr, rev in revisions_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)
            revision = revisions[uuid] = AmberDataAssetRevision()

            revision.comment = rev["comment"]
            revision.uuid = uuid

            revision.size = rev["size"]
            revision.timestamp = rev["timestamp"]
            revision.path = rev["path"]

    @staticmethod
    def to_dict(revisions):
        revisions_dict = {
            utils.uuid_pack(rev.uuid): {
                "comment": rev.comment,
                "size": rev.size,
                "timestamp": rev.timestamp,
                "path": rev.path,
            }
            for rev in revisions.values()
        }

        return revisions_dict

    @staticmethod
    def from_pg(revisions, pg):
        # For now, fully override variants.
        revisions.clear()
        for rev in pg:
            uuid = rev.uuid[:]
            revision = revisions[uuid] = AmberDataAssetRevision()

            revision.comment = rev.comment
            revision.uuid = uuid

            revision.size = rev.size
            revision.timestamp = rev.timestamp
            revision.path = rev.path

    @staticmethod
    def to_pg(pg, revisions):
        for idx, revision in enumerate(revisions.values()):
            if idx == len(pg):
                pg.add()
            revision_pg = pg[idx]
            revision_pg.uuid = revision.uuid

            revision_pg.comment = revision.comment

            revision_pg.size = revision.size
            revision_pg.timestamp = revision.timestamp
            revision_pg.path = revision.path


class AmberDataAssetVariantPG(PropertyGroup):
    name = StringProperty(name="Name", description="Asset name")

    description = StringProperty(name="Description", description="Asset description")
    uuid = IntVectorProperty(name="UUID", description="Variant unique identifier", size=4)

    revisions = CollectionProperty(name="Revisions", type=AmberDataAssetRevisionPG, description="Revisions of the variant")
    revision_index_active = IntProperty(name="Active Revision", options={'HIDDEN'})

    revision_default = IntVectorProperty(name="Default Revision", size=4,
                                              description="Default revision of the variant, to be used when nothing explicitly chosen")
    def revisions_itemf(self, context):
        if not hasattr(self, "revisions_items"):
            self.revisions_items = [(utils.uuid_pack(r.uuid),) * 2 + (r.comment.split('\n')[0], idx) for idx, r in enumerate(self.revisions)]
            self.revisions_itemidx_to_uuid_map = [r.uuid for r in self.revisions]
            self.revisions_uuid_to_itemidx_map = {r.uuid: idx for idx, r in enumerate(self.revisions)}
        return self.revisions_items
    def revision_default_ui_get(self):
        return self.revisions_uuid_to_itemidx_map[self.revision_default]
    def revision_default_ui_set(self, val):
        self.revision_default = self.revisions_itemidx_to_uuid_map[val]
    revision_default_ui = EnumProperty(items=revisions_itemf, get=revision_default_ui_get, set=revision_default_ui_set,
                                       name="Default Revision",
                                       description="Default revision of the variant, to be used when nothing explicitly chosen")


class AmberDataAssetVariant():
    def __init__(self):
        self.name = ""
        self.description = ""
        self.uuid = (0, 0, 0, 0)
        self.revisions = {}
        self.revision_default = None

    @staticmethod
    def from_dict(variants, variants_dict):
        # For now, fully override variants.
        variants.clear()
        for uuid_hexstr, var in variants_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)
            variant = variants[uuid] = AmberDataAssetVariant()

            variant.name = var["name"]
            variant.description = var["description"]
            variant.uuid = uuid

            AmberDataAssetRevision.from_dict(variant.revisions, var["revisions"])
            variant.revision_default = variant.revisions[utils.uuid_unpack(var["revision_default"])]

    @staticmethod
    def to_dict(variants):
        variants_dict = {
            utils.uuid_pack(var.uuid): {
                "name": var.name,
                "description": var.description,
                "revisions": AmberDataAssetRevision.to_dict(var.revisions),
                "revision_default": utils.uuid_pack(var.revision_default.uuid),
            }
            for var in variants.values()
        }

        return variants_dict

    @staticmethod
    def from_pg(variants, pg):
        # For now, fully override variants.
        variants.clear()
        for var in pg:
            uuid = var.uuid[:]
            variant = variants[uuid] = AmberDataAssetVariant()

            variant.name = var.name
            variant.description = var.description
            variant.uuid = uuid

            AmberDataAssetRevision.from_pg(variant.revisions, var.revisions)
            variant.revision_default = variant.revisions[var.revision_default[:]]

    @staticmethod
    def to_pg(pg, variants):
        for idx, variant in enumerate(variants.values()):
            if idx == len(pg):
                pg.add()
            variant_pg = pg[idx]
            variant_pg.uuid = variant.uuid

            variant_pg.name = variant.name
            variant_pg.description = variant.description

            AmberDataAssetRevision.to_pg(variant_pg.revisions, variant.revisions)
            variant_pg.revision_default = variant.revision_default.uuid


class AmberDataAssetPG(PropertyGroup):
    name = StringProperty(name="Name", description="Asset name")
    description = StringProperty(name="Description", description="Asset description")
    uuid = IntVectorProperty(name="UUID", description="Asset unique identifier", size=4)

    file_type = EnumProperty(items=[(e.identifier, e.name, e.description, e.value) for e in AssetEntry.bl_rna.properties["type"].enum_items],
                             name="File Type", description="Type of file storing the asset")
    blender_type = EnumProperty(items=[(e.identifier, e.name, e.description, e.value) for e in AssetEntry.bl_rna.properties["blender_type"].enum_items],
                                name="Blender Type", description="Blender data-block type of the asset")

    tags = CollectionProperty(name="Tags", type=AmberDataTagPG, description="Tags of the asset")
    tag_index_active = IntProperty(name="Active Tag", options={'HIDDEN'})

    variants = CollectionProperty(name="Variants", type=AmberDataAssetVariantPG, description="Variants of the asset")
    variant_index_active = IntProperty(name="Active Variant", options={'HIDDEN'})

    variant_default = IntVectorProperty(name="Default Variant", size=4,
                                        description="Default variant of the asset, to be used when nothing explicitly chosen")
    def variants_itemf(self, context):
        if not hasattr(self, "variants_items"):
            self.variants_items = [(utils.uuid_pack(v.uuid),) * 2 + (v.description.split('\n')[0], idx) for idx, v in enumerate(self.variants)]
            self.variants_itemidx_to_uuid_map = [v.uuid for v in self.variants]
            self.variants_uuid_to_itemidx_map = {v.uuid: idx for idx, v in enumerate(self.variants)}
        return self.variants_items
    def variant_default_ui_get(self):
        return self.variants_uuid_to_itemidx_map[self.variant_default]
    def variant_default_ui_set(self, val):
        self.variant_default = self.variants_itemidx_to_uuid_map[val]
    variant_default_ui = EnumProperty(items=variants_itemf, get=variant_default_ui_get, set=variant_default_ui_set,
                                      name="Default Variant",
                                      description="Default variant of the asset, to be used when nothing explicitly chosen")


class AmberDataAsset():
    def __init__(self):
        self.name = ""
        self.description = ""
        self.uuid = (0, 0, 0, 0)
        self.file_type = ''
        self.blender_type = ''
        self.tags = {}
        self.variants = {}
        self.variant_default = None

    @staticmethod
    def from_dict(assets, entries_dict, repo_uuid):
        # For now, fully override entries.
        assets.clear()
        for uuid_hexstr, ent in entries_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)
            assert(uuid[:2] == repo_uuid[:2])

            asset = assets[uuid] = AmberDataAsset()

            asset.name = ent["name"]
            asset.description = ent["description"]
            asset.uuid = uuid

            asset.file_type = ent["file_type"]
            asset.blender_type = ent["blen_type"]

            asset.tags = set(ent["tags"])

            AmberDataAssetVariant.from_dict(asset.variants, ent["variants"])
            asset.variant_default = asset.variants[utils.uuid_unpack(ent["variant_default"])]

    @staticmethod
    def to_dict(assets):
        entries_dict = {
            utils.uuid_pack(asset.uuid): {
                "name": asset.name,
                "description": asset.description,
                "file_type": asset.file_type,
                "blen_type": asset.blender_type,
                "tags": list(asset.tags),
                "variants": AmberDataAssetVariant.to_dict(asset.variants),
                "variant_default": utils.uuid_pack(asset.variant_default.uuid),
            }
            for asset in assets.values()
        }

        return entries_dict

    @staticmethod
    def from_pg(assets, pg):
        # For now, fully override entries.
        assets.clear()
        for asset_pg in pg:
            uuid = asset_pg.uuid[:]
            asset = assets[uuid] = AmberDataAsset()

            asset.name = asset_pg.name
            asset.description = asset_pg.description
            asset.uuid = uuid

            asset.file_type = asset_pg.file_type
            asset.blender_type = asset_pg.blender_type

            asset.tags = set(t.name for t in asset_pg.tags)

            AmberDataAssetVariant.from_pg(asset.variants, asset_pg.variants)
            asset.variant_default = asset.variants[asset_pg.variant_default[:]]

    @staticmethod
    def to_pg(pg, assets, tags):
        for idx, asset in enumerate(assets.values()):
            if idx == len(pg):
                pg.add()
            asset_pg = pg[idx]
            asset_pg.uuid = asset.uuid

            asset_pg.name = asset.name
            asset_pg.description = asset.description
            asset_pg.uuid = asset.uuid

            asset_pg.file_type = asset.file_type
            asset_pg.blender_type = asset.blender_type

            AmberDataTagPG.to_pg(asset_pg.tags, tags, subset=asset.tags, do_clear=False)

            AmberDataAssetVariant.to_pg(asset_pg.variants, asset.variants)
            asset_pg.variant_default = asset.variant_default.uuid


class AmberDataRepositoryPG(PropertyGroup):
    VERSION = "1.0.1"

    path = StringProperty(name="Path", description="Path to Amber repository", subtype='FILE_PATH')

    version = StringProperty(name="Version", description="Repository version")
    name = StringProperty(name="Name", description="Repository name")
    description = StringProperty(name="Description", description="Repository description")
    uuid = IntVectorProperty(name="UUID", description="Repository unique identifier", size=4)

    tags = CollectionProperty(name="Tags", type=AmberDataTagPG, description="Filtering tags")
    tag_index_active = IntProperty(name="Active Tag", options={'HIDDEN'})

    assets = CollectionProperty(name="Assets", type=AmberDataAssetPG)
    asset_index_active = IntProperty(name="Active Asset", options={'HIDDEN'})


class AmberDataRepository:
    """
    Amber repository main class.

    Note: Remember that in Amber, first 8 bytes of asset's UUID are same as first 8 bytes of repository UUID.
          Repository UUID's last 8 bytes shall always be NULL.
          This allows us to store repository identifier into all assets, and ensure we have uniqueness of
          Amber assets UUIDs (which is mandatory from Blender point of view).
    """
    VERSION = "1.0.1"

    def __init__(self):
        self.tags = {}
        self.assets = {}

        self.clear()

    def clear(self):
        self.path = ""
        self.version = "1.0.1"
        self.name = ""
        self.description = ""
        self.uuid = (0, 0, 0, 0)
        self.tags.clear()
        self.assets.clear()

    @classmethod
    def ls_repo(cls, db_path):
        repo_dict = None
        with open(db_path, 'r') as db_f:
            repo_dict = json.load(db_f)
        if isinstance(repo_dict, dict):
            repo_ver = repo_dict.get(utils.AMBER_DBK_VERSION, "")
            if repo_ver != cls.VERSION:
                # Unsupported...
                print("WARNING: unsupported Amber repository version '%s'." % repo_ver)
                repo_dict = None
        else:
            repo_dict = None
        return repo_dict

    @classmethod
    def wrt_repo(cls, db_path, repo_dict):
        with open(db_path, 'w') as db_f:
            json.dump(repo_dict, db_f, indent=4)

    def from_dict(self, repo_dict, root_path):
        self.clear()

        self.path = root_path
        self.version = repo_dict["version"]

        if self.version == self.VERSION:
            self.name = repo_dict["name"]
            self.description = repo_dict["description"]
            self.uuid = utils.uuid_unpack(repo_dict["uuid"])
            assert(self.uuid[2:] == (0, 0))

            # We update tags instead of overriding them completely...
            AmberDataTagPG.from_dict(self.tags, repo_dict["tags"])

            # For now, fully override entries.
            AmberDataAsset.from_dict(self.assets, repo_dict["entries"], self.uuid)
        else:
            print("Unsupported repository version: ", self.version)
            self.clear(self.storage)

    def to_dict(self):
        repo_dict = {}

        repo_dict["version"] = self.VERSION
        repo_dict["name"] = self.name
        repo_dict["description"] = self.description

        assert(self.uuid[2:] == (0, 0))
        repo_dict["uuid"] = utils.uuid_pack(self.uuid)

        repo_dict["tags"] = AmberDataTagPG.to_dict(self.tags)

        repo_dict["entries"] = AmberDataAsset.to_dict(self.assets)

        return repo_dict

    def from_pg(self, pg):
        self.clear()

        self.path = pg.path
        self.version = pg.version

        self.name = pg.name
        self.description = pg.description
        self.uuid = pg.uuid[:]

        AmberDataTagPG.from_pg(self.tags, pg.tags)

        # For now, fully override entries.
        AmberDataAsset.from_pg(self.assets, pg.assets)

    def to_pg(self, pg):
        pg.path = self.path
        pg.version = self.version
        pg.name = self.name
        pg.description = self.description
        pg.uuid = self.uuid

        # We update PG tags instead of overriding them completely...
        AmberDataTagPG.to_pg(pg.tags, self.tags, do_clear=False)

        AmberDataAsset.to_pg(pg.assets, self.assets, self.tags)


class AmberDataRepositoryListPG(PropertyGroup):
    def repositories_itemf(self, context):
        if not hasattr(self, "repositories_items"):
            self.repositories_items = [(utils.uuid_pack(uuid), name, path, idx) for idx, (uuid, (name, path)) in enumerate(utils.amber_repos.items())]
        return self.repositories_items
    def repositories_update(self, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineAmber":
                uuid = utils.uuid_unpack(self.repositories)
                space.params.directory = utils.amber_repos[uuid][1]
    repositories = EnumProperty(items=repositories_itemf, update=repositories_update,
                                name="Current Repository", description="Active Amber asset repository")


class AmberDataRepositoryList:
    """
    Amber repository main class.

    Note: Remember that in Amber, first 8 bytes of asset's UUID are same as first 8 bytes of repository UUID.
          Repository UUID's last 8 bytes shall always be NULL.
          This allows us to store repository identifier into all assets, and ensure we have uniqueness of
          Amber assets UUIDs (which is mandatory from Blender point of view).
    """
    pass


classes = (
    AmberDataTagPG,
    AmberDataAssetRevisionPG,
    AmberDataAssetVariantPG,
    AmberDataAssetPG,
    AmberDataRepositoryPG,
    AmberDataRepositoryListPG,
)
