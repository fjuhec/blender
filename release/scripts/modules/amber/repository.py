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
    def tag_data_update(self, context):
        sd = context.space_data
        if not (sd and sd.type == 'FILE_BROWSER' and sd.asset_engine):
            return
        ae = sd.asset_engine
        name_prev = self.name_prev or self.name

        if ae.repository_pg.tag_lock_updates:
            return
        ae.repository_pg.tag_lock_updates = True

        tag = ae.repository_pg.tags.get(name_prev, None)
        if tag and tag != self:
            tag.name_prev = tag.name = self.name
            tag.priority = self.priority

        for asset in ae.repository_pg.assets:
            tag = asset.tags.get(name_prev, None)
            if tag and tag != self:
                tag.name_prev = tag.name = self.name
                tag.priority = self.priority

        self.name_prev = self.name

        ae.repository_pg.tag_lock_updates = False

        AmberDataRepository.update_from_asset_engine(ae)

    name = StringProperty(name="Name", description="Tag name", update=tag_data_update)
    priority = IntProperty(name="Priority", default=0, update=tag_data_update,
                           description="Tag priority (used to order tags, highest priority go first)")

    name_prev = StringProperty(options={'HIDDEN'})

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
                tag_pg = pg.add()
                tag_pg.name_prev = tag_pg.name = tag_name
                tag_pg.priority = tag_priority
        else:
            removed_tags = set(t.name for t in pg) - set(subset if subset is not None else tags)
            added_tags = set(subset if subset is not None else tags) - set(t.name for t in pg)
            for tag_name in removed_tags:
                pg.remove(pg.find(tag_name))
            for tag_pg in pg:
                tag_pg.priority = tags[tag_pg.name]
            for tag_name in added_tags:
                tag_pg = pg.add()
                tag_pg.name_prev = tag_pg.name = tag_name
                tag_pg.priority = tags[tag_name]


class AmberDataAssetViewPG(PropertyGroup):
    uuid = IntVectorProperty(name="UUID", description="View unique identifier", size=4)
    name = StringProperty(name="Name", description="Asset/Variant/Revision view name")
    description = StringProperty(name="Description", description="Asset/Variant/Revision view description")

    size = IntProperty(name="Size")
    timestamp = IntProperty(name="Timestamp")

    path = StringProperty(name="Path", description="File path of this item", subtype='FILE_PATH')


class AmberDataAssetView():
    def __init__(self):
        self.uuid = (0, 0, 0, 0)
        self.name = ""
        self.description = ""
        self.size = 0
        self.timestamp = 0
        self.path = ""

    @staticmethod
    def from_dict(views, views_dict):
        # For now, fully override revisions.
        views.clear()
        for uuid_hexstr, vw in views_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)
            view = views[uuid] = AmberDataAssetView()

            view.uuid = uuid
            view.name = vw["name"]
            view.description = vw["description"]

            view.size = vw["size"]
            view.timestamp = vw["timestamp"]
            view.path = vw["path"]

    @staticmethod
    def to_dict(views):
        views_dict = {
            utils.uuid_pack(vw.uuid): {
                "name": vw.name,
                "description": vw.description,
                "size": vw.size,
                "timestamp": vw.timestamp,
                "path": vw.path,
            }
            for vw in views.values()
        }

        return views_dict

    @staticmethod
    def from_pg(views, pg):
        # For now, fully override variants.
        views.clear()
        for vw in pg:
            uuid = vw.uuid[:]
            view = views[uuid] = AmberDataAssetView()

            view.uuid = uuid
            view.name = vw.name
            view.description = vw.description

            view.size = vw.size
            view.timestamp = vw.timestamp
            view.path = vw.path

    @staticmethod
    def to_pg(pg, views):
        for idx, view in enumerate(views.values()):
            if idx == len(pg):
                pg.add()
            view_pg = pg[idx]
            view_pg.uuid = view.uuid

            view_pg.name = view.name
            view_pg.description = view.description

            view_pg.size = view.size
            view_pg.timestamp = view.timestamp
            view_pg.path = view.path
        for idx in range(len(pg), len(views), -1):
            pg.remove(idx - 1)


class AmberDataAssetRevisionPG(PropertyGroup):
    comment = StringProperty(name="Comment", description="Asset/Variant revision comment")
    uuid = IntVectorProperty(name="UUID", description="Revision unique identifier", size=4)

    timestamp = IntProperty(name="Timestamp")

    views = CollectionProperty(name="Views", type=AmberDataAssetViewPG, description="Views of the revision")
    view_index_active = IntProperty(name="Active View", options={'HIDDEN'})

    view_default = IntVectorProperty(name="Default view", size=4,
                                     description="Default view of the revision, to be used when nothing explicitly chosen")
    def views_itemf(self, context):
        if not hasattr(self, "views_items"):
            self.views_items = [(utils.uuid_pack(v.uuid),) * 2 + (v.name[0], idx) for idx, v in enumerate(self.views)]
            self.views_itemidx_to_uuid_map = [v.uuid for v in self.views]
            self.views_uuid_to_itemidx_map = {v.uuid: idx for idx, v in enumerate(self.views)}
        return self.views_items
    def view_default_ui_get(self):
        return self.views_uuid_to_itemidx_map[self.view_default]
    def view_default_ui_set(self, val):
        self.view_default = self.views_itemidx_to_uuid_map[val]
    view_default_ui = EnumProperty(items=views_itemf, get=view_default_ui_get, set=view_default_ui_set, name="Default View",
                                   description="Default view of the variant, to be used when nothing explicitly chosen")


class AmberDataAssetRevision():
    def __init__(self):
        self.comment = ""
        self.uuid = (0, 0, 0, 0)
        self.timestamp = 0
        self.views = {}
        self.view_default = None

    @staticmethod
    def from_dict(revisions, revisions_dict):
        # For now, fully override revisions.
        revisions.clear()
        for uuid_hexstr, rev in revisions_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)
            revision = revisions[uuid] = AmberDataAssetRevision()

            revision.comment = rev["comment"]
            revision.uuid = uuid

            revision.timestamp = rev["timestamp"]

            AmberDataAssetView.from_dict(revision.views, rev["views"])
            revision.view_default = revision.views[utils.uuid_unpack(rev["view_default"])]

    @staticmethod
    def to_dict(revisions):
        revisions_dict = {
            utils.uuid_pack(rev.uuid): {
                "comment": rev.comment,
                "timestamp": rev.timestamp,
                "views": AmberDataAssetView.to_dict(rev.views),
                "view_default": utils.uuid_pack(rev.view_default.uuid),
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

            revision.timestamp = rev.timestamp

            AmberDataAssetView.from_pg(revision.views, rev.views)
            revision.view_default = revision.views[rev.view_default[:]]

    @staticmethod
    def to_pg(pg, revisions):
        for idx, revision in enumerate(revisions.values()):
            if idx == len(pg):
                pg.add()
            revision_pg = pg[idx]
            revision_pg.uuid = revision.uuid

            revision_pg.comment = revision.comment

            revision_pg.timestamp = revision.timestamp

            AmberDataAssetView.to_pg(revision_pg.views, revision.views)
            revision_pg.view_default = revision.view_default.uuid
        for idx in range(len(pg), len(revisions), -1):
            pg.remove(idx - 1)


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
        for idx in range(len(pg), len(variants), -1):
            pg.remove(idx - 1)


class AmberDataAssetPG(PropertyGroup):
    def asset_data_update(self, context):
        sd = context.space_data
        if not (sd and sd.type == 'FILE_BROWSER' and sd.asset_engine):
            return
        ae = sd.asset_engine

        AmberDataRepository.update_from_asset_engine(ae)
        bpy.ops.file.refresh()

    name = StringProperty(name="Name", description="Asset name", update=asset_data_update)
    description = StringProperty(name="Description", description="Asset description", update=asset_data_update)
    uuid = IntVectorProperty(name="UUID", description="Asset unique identifier", size=4, update=asset_data_update)

    file_type = EnumProperty(items=[(e.identifier, e.name, e.description, e.value) for e in AssetEntry.bl_rna.properties["type"].enum_items],
                             name="File Type", description="Type of file storing the asset", update=asset_data_update)
    blender_type = EnumProperty(items=[(e.identifier, e.name, e.description, e.value) for e in AssetEntry.bl_rna.properties["blender_type"].enum_items],
                                name="Blender Type", description="Blender data-block type of the asset", update=asset_data_update)

    preview_path = StringProperty(name="Preview Path", description="File path of the preview of this item", subtype='FILE_PATH')
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

    is_selected = BoolProperty(name="Selected", default=False, description="Whether this item is selected")


class AmberDataAsset():
    def __init__(self):
        self.name = ""
        self.description = ""
        self.uuid = (0, 0, 0, 0)
        self.file_type = ''
        self.blender_type = ''
        self.preview_path = ""
        self.tags = {}
        self.variants = {}
        self.variant_default = None
        self.is_selected = False

    @staticmethod
    def from_dict(assets, entries_dict, repo_uuid):
        # For now, fully override entries.
        assets.clear()
        for uuid_hexstr, ent in entries_dict.items():
            uuid = utils.uuid_unpack(uuid_hexstr)

            asset = assets[uuid] = AmberDataAsset()

            asset.name = ent["name"]
            asset.description = ent["description"]
            asset.uuid = uuid

            asset.file_type = ent["file_type"]
            asset.blender_type = ent["blen_type"]

            asset.preview_path = ent["preview_path"]
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
                "preview_path": asset.preview_path,
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

            asset.preview_path = asset_pg.preview_path
            asset.tags = set(t.name for t in asset_pg.tags)

            AmberDataAssetVariant.from_pg(asset.variants, asset_pg.variants)
            asset.variant_default = asset.variants[asset_pg.variant_default[:]]

            asset.is_selected = asset_pg.is_selected

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

            asset_pg.preview_path = asset.preview_path
            AmberDataTagPG.to_pg(asset_pg.tags, tags, subset=asset.tags, do_clear=False)

            AmberDataAssetVariant.to_pg(asset_pg.variants, asset.variants)
            asset_pg.variant_default = asset.variant_default.uuid

            asset_pg.is_selected = asset.is_selected

        for idx in range(len(pg), len(assets), -1):
            pg.remove(idx - 1)


class AmberDataRepositoryPG(PropertyGroup):
    VERSION = "1.0.1"

    path = StringProperty(name="Path", description="Path to Amber repository", subtype='FILE_PATH')

    version = StringProperty(name="Version", description="Repository version")
    name = StringProperty(name="Name", description="Repository name")
    description = StringProperty(name="Description", description="Repository description")
    uuid = IntVectorProperty(name="UUID", description="Repository unique identifier", size=4)

    tags = CollectionProperty(name="Tags", type=AmberDataTagPG, description="Filtering tags")
    tag_index_active = IntProperty(name="Active Tag", options={'HIDDEN'})
    tag_lock_updates = BoolProperty(options={'HIDDEN'})

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

    def clear(self, repository_pg=None):
        self.path = ""
        self.version = "1.0.1"
        self.name = ""
        self.description = ""
        self.uuid = (0, 0, 0, 0)
        self.tags.clear()
        self.assets.clear()

        if repository_pg is not None:
            self.to_pg(repository_pg)

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
        temp_path = os.path.join(os.path.dirname(db_path), utils.AMBER_DB_NAME + ".tmp")
        with open(temp_path, 'w') as db_f:
            json.dump(repo_dict, db_f, indent=4)
        os.replace(temp_path, db_path)

    @staticmethod
    def update_from_asset_engine(ae):
        """Generic helper wrapping repository JSON file update when editing within Blender."""
        repository = getattr(ae, "repository", None)
        if repository is None:
            repository = ae.repository = AmberDataRepository()
        repository.from_pg(ae.repository_pg)

        repository.wrt_repo(os.path.join(repository.path, utils.AMBER_DB_NAME), repository.to_dict())


    def from_dict(self, repo_dict, root_path):
        self.clear()

        self.path = root_path
        self.version = repo_dict["version"]

        if self.version == self.VERSION:
            self.name = repo_dict["name"]
            self.description = repo_dict["description"]
            self.uuid = utils.uuid_unpack(repo_dict["uuid"])

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


class AmberDataRepositoryListItemPG(PropertyGroup):
    def repository_update(self, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineAmber":
                repos = AmberDataRepositoryList()
                repos.from_pg(ae.repositories_pg)
                repos.save()

    uuid = IntVectorProperty(name="UUID", description="Repository unique identifier", size=4)
    name = StringProperty(name="Name", update=repository_update)
    path = StringProperty(name="Path", description="Path to this Amber repository", subtype='DIR_PATH')
    is_valid = BoolProperty(name="Is Valid")


class AmberDataRepositoryListPG(PropertyGroup):
    def repositories_update(self, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineAmber":
                space.params.directory = self.repositories[self.repository_index_active].path

    repositories = CollectionProperty(name="Repositories", type=AmberDataRepositoryListItemPG)
    repository_index_active = IntProperty(name="Active Repository", options={'HIDDEN'}, update=repositories_update, default=-1)


class AmberDataRepositoryList:
    """
    List of Amber repositories (singleton).
    """
    singleton = None

    def __new__(cls, path=...):
        if cls.singleton is None:
            return super().__new__(cls)
        return cls.singleton

    def __init__(self, path=...):
        if self != self.__class__.singleton:
            if path is ...:
                path = os.path.join(bpy.utils.user_resource('CONFIG', create=True), utils.AMBER_LIST_FILENAME)
            self._path = ""
            self.repositories = {}
            self.path = path
            self.__class__.singleton = self
        elif path is not ...:
            self._path = ""
            self.repositories = {}
            self.path = path

    def load(self):
        if not os.path.exists(self.path):
            with open(self.path, 'w') as ar_f:
                json.dump({}, ar_f)
        with open(self.path, 'r') as ar_f:
            self.repositories = {utils.uuid_unpack(uuid): name_path for uuid, name_path in json.load(ar_f).items()}

    def save(self):
        ar = {utils.uuid_pack(uuid): name_path for uuid, name_path in self.repositories.items()}
        with open(self.path, 'w') as ar_f:
            json.dump(ar, ar_f)

    def path_get(self):
        return self._path
    def path_set(self, path):
        if self._path != path:
            self._path = path
            self.load()
    path = property(path_get, path_set)

    def to_pg(self, pg):
        for idx, (uuid, (name, path)) in enumerate(self.repositories.items()):
            print("to_pg", name, path)
            if idx == len(pg.repositories):
                pg.repositories.add()
            repo_pg = pg.repositories[idx]
            repo_pg.uuid = uuid
            repo_pg.name = name
            repo_pg.path = path
            repo_pg.is_valid = os.path.exists(path)
        for idx in range(len(pg.repositories), len(self.repositories), -1):
            pg.repositories.remove(idx - 1)

    def from_pg(self, pg):
        print("from_pg", [(pg_item.name, pg_item.path) for pg_item in pg.repositories])
        self.repositories = {pg_item.uuid[:]: (pg_item.name, pg_item.path) for pg_item in pg.repositories}


classes = (
    AmberDataTagPG,

    AmberDataAssetViewPG,
    AmberDataAssetRevisionPG,
    AmberDataAssetVariantPG,
    AmberDataAssetPG,
    AmberDataRepositoryPG,

    AmberDataRepositoryListItemPG,
    AmberDataRepositoryListPG,
)
