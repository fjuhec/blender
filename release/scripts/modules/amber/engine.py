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
        AssetEngine,
        PropertyGroup,
        )
from bpy.props import (
        StringProperty,
        BoolProperty,
        IntProperty,
        CollectionProperty,
        PointerProperty,
        )

import concurrent.futures as futures
import os
import stat
import time
import random

from . import (repository, utils)

from .repository import (
        AmberDataRepository,
        AmberDataRepositoryPG,

        AmberDataRepositoryList,
        AmberDataRepositoryListPG,
        )


#############
# Amber Jobs.
class AmberJob:
    def __init__(self, executor, job_id):
        self.executor = executor
        self.job_id = job_id
        self.status = {'VALID'}
        self.progress = 0.0


class AmberJobList(AmberJob):
    @staticmethod
    def ls(path):
        repo = None
        ret = [".."]
        tmp = os.listdir(path)
        if utils.AMBER_DB_NAME in tmp:
            # That dir is an Amber repo, we only list content define by our amber 'db'.
            repo = AmberDataRepository.ls_repo(os.path.join(path, utils.AMBER_DB_NAME))
        if repo is None:
            ret += tmp
        #~ time.sleep(0.1)  # 100% Artificial Lag (c)
        return ret, repo

    @staticmethod
    def stat(root, path):
        st = os.lstat(root + path)
        #~ time.sleep(0.1)  # 100% Artificial Lag (c)
        return path, (stat.S_ISDIR(st.st_mode), st.st_size, st.st_mtime)

    def start(self):
        self.nbr = 0
        self.tot = 0
        self.ls_task = self.executor.submit(self.ls, self.root)
        self.status = {'VALID', 'RUNNING'}

    def update(self, repository, dirs):
        self.status = {'VALID', 'RUNNING'}
        if self.ls_task is not None:
            if not self.ls_task.done():
                return
            paths, repo = self.ls_task.result()
            self.ls_task = None
            self.tot = len(paths)
            repository.clear()
            dirs.clear()
            if repo is not None:
                repository.update(repo)
            for p in paths:
                self.stat_tasks.add(self.executor.submit(self.stat, self.root, p))

        done = set()
        for tsk in self.stat_tasks:
            if tsk.done():
                path, (is_dir, size, timestamp) = tsk.result()
                self.nbr += 1
                if is_dir:
                    # We only list dirs from real file system.
                    uuid = utils.uuid_unpack_bytes((path.encode()[:8] + b"|" + self.nbr.to_bytes(4, 'little')))
                    dirs.append((path, size, timestamp, uuid))
                done.add(tsk)
        self.stat_tasks -= done

        self.progress = self.nbr / self.tot
        if not self.stat_tasks and self.ls_task is None:
            self.status = {'VALID'}

    def __init__(self, executor, job_id, root):
        super().__init__(executor, job_id)
        self.root = root

        self.ls_task = None
        self.stat_tasks = set()

        self.start()

    def __del__(self):
        # Avoid useless work!
        if self.ls_task is not None:
            self.ls_task.cancel()
        for tsk in self.stat_tasks:
            tsk.cancel()


class AmberJobPreviews(AmberJob):
    @staticmethod
    def preview(uuid):
        repo_uuid = uuid[0]
        repo_path = AmberDataRepositoryList().repositories.get(repo_uuid, (None, None))[1]
        if repo_path is None:
            return [0, 0, []]

        repo = AmberDataRepository()
        repo.from_dict(repo.ls_repo(os.path.join(repo_path, utils.AMBER_DB_NAME)), repo_path)

        preview_path = os.path.join(repo_path, repo.assets[uuid[1]].preview_path)

        if preview_path and preview_path.endswith(".dat"):
            w, h, pixels = utils.preview_read_dat(preview_path)
            return [w, h, list(pixels)]

        #~ time.sleep(0.1)  # 100% Artificial Lag (c)
        w = random.randint(8, 32)
        h = random.randint(8, 32)
        return [w, h, [random.randint(-2147483647, 2147483647) for i in range(w * h)]]

    def start(self, uuids):
        self.nbr = 0
        self.preview_tasks = {uuid.uuid_asset[:]: self.executor.submit(self.preview, (uuid.uuid_repository[:], uuid.uuid_asset[:])) for uuid in uuids.uuids}
        self.tot = len(self.preview_tasks)
        self.status = {'VALID', 'RUNNING'}

    def update(self, uuids):
        self.status = {'VALID', 'RUNNING'}

        uuids = {uuid.uuid_asset[:]: uuid for uuid in uuids.uuids}

        new_uuids = set(uuids)
        old_uuids = set(self.preview_tasks)
        del_uuids = old_uuids - new_uuids
        new_uuids -= old_uuids

        for uuid_asset in del_uuids:
            self.preview_tasks[uuid_asset].cancel()
            del self.preview_tasks[uuid_asset]

        for uuid_asset in new_uuids:
            uuid = uuids[uuid_asset]
            self.preview_tasks[uuid_asset] = self.executor.submit(self.preview, (uuid.uuid_repository[:], uuid_asset))

        self.tot = len(self.preview_tasks)
        self.nbr = 0

        done_uuids = set()
        for uuid_asset, tsk in self.preview_tasks.items():
            if tsk.done():
                w, h, pixels = tsk.result()
                uuids[uuid_asset].preview_size = (w, h)
                uuids[uuid_asset].preview_pixels = pixels
                self.nbr += 1
                done_uuids.add(uuid_asset)

        for uuid_asset in done_uuids:
            del self.preview_tasks[uuid_asset]

        self.progress = self.nbr / self.tot
        if not self.preview_tasks:
            self.status = {'VALID'}

    def __init__(self, executor, job_id, uuids):
        super().__init__(executor, job_id)
        self.preview_tasks = {}

        self.start(uuids)

    def __del__(self):
        # Avoid useless work!
        for tsk in self.preview_tasks.values():
            tsk.cancel()


###########################
# Main Asset Engine class.
class AssetEngineAmber(AssetEngine):
    bl_label = "Amber"
    bl_version = (0 << 16) + (0 << 8) + 4  # Usual maj.min.rev version scheme...

    repository_pg = PointerProperty(name="Repository", type=AmberDataRepositoryPG, description="Current Amber repository")
    repositories_pg = PointerProperty(name="Repositories", type=AmberDataRepositoryListPG, description="Known Amber repositories")

    def __init__(self):
        self.executor = futures.ThreadPoolExecutor(8)  # Using threads for now, if issues arise we'll switch to process.
        self.jobs = {}
        self.repos = {}
        self.repository = AmberDataRepository()

        self.repositories = AmberDataRepositoryList()
        self.repositories.to_pg(self.repositories_pg)

        self.reset()

        self.job_uuid = 1

    def __del__(self):
        # XXX This errors, saying self has no executor attribute... Suspect some py/RNA funky game. :/
        #     Even though it does not seem to be an issue, this is not nice and shall be fixed somehow.
        # XXX This is still erroring... Looks like we should rather have a 'remove' callback or so. :|
        #~ executor = getattr(self, "executor", None)
        #~ if executor is not None:
            #~ executor.shutdown(wait=False)
        pass

    ########## Various helpers ##########
    def reset(self):
        print("Amber Reset!")
        self.root = ""
        self.repo = {}
        self.dirs = []

        self.repository.clear(self.repository_pg)

        self.sortedfiltered = []

    def entry_from_uuid(self, entries, euuid, vuuid, ruuid, wuuid):
        act_view = None

        def views_gen(revision, e, v, r, wuuid, is_default):
            nonlocal act_view
            if wuuid == (0, 0, 0, 0):
                wact = r.view_default
                ws = r.views.values()
            else:
                wact = r.views[wuuid]
                ws = (wact,)
            for w in ws:
                view = revision.views.add()
                view.uuid = w.uuid
                view.name = w.name
                view.description = w.description
                view.size = w.size
                view.timestamp = w.timestamp
                if w == wact:
                    revision.views.active = view
                    if is_default:
                        act_view = w

        def revisions_gen(variant, e, v, ruuid, wuuid, is_default):
            if ruuid == (0, 0, 0, 0):
                ract = v.revision_default
                rvs = v.revisions.values()
                wuuid = ruuid
            else:
                ract = v.revisions[ruuid]
                rvs = (ract,)
            for r in rvs:
                revision = variant.revisions.add()
                revision.uuid = r.uuid
                revision.comment = r.comment
                revision.timestamp = r.timestamp
                if r == ract:
                    variant.revisions.active = revision
                views_gen(revision, e, v, r, wuuid, is_default and (r == ract))

        def variants_gen(entry, e, vuuid, ruuid, wuuid):
            if vuuid == (0, 0, 0, 0):
                vact = e.variant_default
                vrs = e.variants.values()
                ruuid = vuuid
            else:
                vact = e.variants[vuuid]
                vrs = (vact,)
            for v in vrs:
                variant = entry.variants.add()
                variant.uuid = v.uuid
                variant.name = v.name
                variant.description = v.description
                if v == vact:
                    entry.variants.active = variant
                revisions_gen(variant, e, v, ruuid, wuuid, v == vact)
            
        e = self.repository.assets[euuid]
        entry = entries.entries.add()
        entry.uuid = e.uuid
        entry.uuid_repository = self.repository.uuid
        entry.name = e.name
        entry.description = e.description
        entry.type = {e.file_type}
        entry.blender_type = e.blender_type

        variants_gen(entry, e, vuuid, ruuid, wuuid)

        if act_view:
            entry.relpath = os.path.relpath(os.path.join(self.repository.path, act_view.path), self.repository.path)
#        print("added entry for", entry.relpath)

    def pretty_version(self, v=None):
        if v is None:
            v = self.bl_version
        return "%d.%d.%d" % ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)

    ########## PY-API only ##########
    # UI header
    def draw_header(self, layout, context):
        st = context.space_data
        params = st.params

        # can be None when save/reload with a file selector open
        if params:
            is_lib_browser = params.use_library_browsing

            layout.prop(params, "display_type", expand=True, text="")
            layout.prop(params, "display_size", text="")
            layout.prop(params, "sort_method", expand=True, text="")

            layout.prop(params, "show_hidden", text="", icon='FILE_HIDDEN')
            layout.prop(params, "use_filter", text="", icon='FILTER')

            row = layout.row(align=True)
            row.active = params.use_filter

            if params.filter_glob:
                #if st.active_operator and hasattr(st.active_operator, "filter_glob"):
                #    row.prop(params, "filter_glob", text="")
                row.label(params.filter_glob)
            else:
                row.prop(params, "use_filter_blender", text="")
                row.prop(params, "use_filter_backup", text="")
                row.prop(params, "use_filter_image", text="")
                row.prop(params, "use_filter_movie", text="")
                row.prop(params, "use_filter_script", text="")
                row.prop(params, "use_filter_font", text="")
                row.prop(params, "use_filter_sound", text="")
                row.prop(params, "use_filter_text", text="")

            if is_lib_browser:
                row.prop(params, "use_filter_blendid", text="")
                if (params.use_filter_blendid) :
                    row.separator()
                    row.prop(params, "filter_id_category", text="")

            row.separator()
            row.prop(params, "filter_search", text="", icon='VIEWZOOM')

    ########## C (RNA) API ##########
    def status(self, job_id):
        if job_id:
            job = self.jobs.get(job_id, None)
            return job.status if job is not None else set()
        return {'VALID'}

    def progress(self, job_id):
        if job_id:
            job = self.jobs.get(job_id, None)
            return job.progress if job is not None else 0.0
        progress = 0.0
        nbr_jobs = 0
        for job in self.jobs.values():
            if 'RUNNING' in job.status:
                nbr_jobs += 1
                progress += job.progress
        return progress / nbr_jobs if nbr_jobs else 0.0

    def kill(self, job_id):
        if job_id:
            self.jobs.pop(job_id, None)
            return
        self.jobs.clear()

    def list_dir(self, job_id, entries):
        job = self.jobs.get(job_id, None)
        #~ print(entries.root_path, job_id, job)
        if job is not None and isinstance(job, AmberJobList):
            if job.root != entries.root_path:
                self.reset()
                self.jobs[job_id] = AmberJobList(self.executor, job_id, entries.root_path)
                self.root = entries.root_path
            else:
                job.update(self.repo, self.dirs)
        elif self.root != entries.root_path or entries.nbr_entries == 0:
            self.reset()
            job_id = self.job_uuid
            self.job_uuid += 1
            self.jobs[job_id] = AmberJobList(self.executor, job_id, entries.root_path)
            self.root = entries.root_path
        if self.repo:
            self.repository.from_dict(self.repo, self.root)
            self.repository.to_pg(self.repository_pg)
            uuid_repo = tuple(self.repository.uuid)
            if self.repositories.repositories.get(uuid_repo, (None, None))[1] != self.root:
                self.repositories.repositories[uuid_repo] = (self.repository.name, self.root)  # XXX Not resistant to uuids collisions (use a set instead)...
                self.repositories.save()
                self.repositories.to_pg(self.repositories_pg)
            self.repos[uuid_repo] = self.repo
            entries.nbr_entries = len(self.repository.assets) + 1  # Don't forget the 'up' entry!
        else:
            entries.nbr_entries = len(self.dirs)
            self.repository.clear(self.repository_pg)
        return job_id

    def update_check(self, job_id, uuids):
        # do nothing for now, no need to use actual job...
        if uuids.asset_engine_version != self.bl_version:
            print("Updating asset uuids from Amber v.%s to amber v.%s" %
                  (self.pretty_version(uuids.asset_engine_version), self.pretty_version()))
        for uuid in uuids.uuids:
            repo_uuid = uuid.uuid_repository[:]
            if (repo_uuid not in self.repositories.repositories or
                not os.path.exists(os.path.join(self.repositories.repositories[repo_uuid][1], utils.AMBER_DB_NAME))):
                uuid.is_asset_missing = True
                continue
            # Here in theory we'd reload given repo (async process) and check for asset's status...
            uuid.use_asset_reload = True
        return self.job_id_invalid

    def previews_get(self, job_id, uuids):
        job = self.jobs.get(job_id, None)
        #~ print(entries.root_path, job_id, job)
        if job is not None and isinstance(job, AmberJobPreviews):
            job.update(uuids)
        else:
            job_id = self.job_uuid
            self.job_uuid += 1
            self.jobs[job_id] = AmberJobPreviews(self.executor, job_id, uuids)
        return job_id

    def load_pre(self, uuids, entries):
        # Not quite sure this engine will need it in the end, but for sake of testing...
        if uuids.asset_engine_version != self.bl_version:
            print("Updating asset uuids from Amber v.%s to amber v.%s" %
                  (self.pretty_version(uuids.asset_engine_version), self.pretty_version()))
#            print(entries.entries[:])
        for uuid in uuids.uuids:
            repo_uuid = uuid.uuid_repository[:]
            assert(repo_uuid in self.repositories.repositories)
            repo = self.repos.get(repo_uuid, None)
            if repo is None:
                repo = self.repos[repo_uuid] = AmberDataRepository.ls_repo(os.path.join(self.repositories.repositories[repo_uuid][1], utils.AMBER_DB_NAME))
            self.repository.from_dict(repo, self.repositories.repositories[repo_uuid][1])
            euuid = uuid.uuid_asset[:]
            vuuid = uuid.uuid_variant[:]
            ruuid = uuid.uuid_revision[:]
            wuuid = uuid.uuid_view[:]
            e = self.repository.assets[euuid]
            v = e.variants[vuuid]
            r = v.revisions[ruuid]
            w = r.views[wuuid]

            entry = entries.entries.add()
            entry.type = {e.file_type}
            entry.blender_type = e.blender_type
            # archive part not yet implemented!
            entry.relpath = os.path.join(self.repositories.repositories[repo_uuid][1], w.path)
#                print("added entry for", entry.relpath)
            entry.uuid_repository = repo_uuid
            entry.uuid = e.uuid
            var = entry.variants.add()
            var.uuid = v.uuid
            rev = var.revisions.add()
            rev.uuid = r.uuid
            view = rev.views.add()
            view.uuid = w.uuid
            rev.views.active = view
            var.revisions.active = rev
            entry.variants.active = var
        entries.root_path = ""
        return True

    def load_post(self, context, uuids):
        print("load_post: active object:", context.object.name if context.object else "<NONE>")
        print("load_post: for", ", ".join(uuid.id.name for uuid in uuids.uuids))

    def check_dir(self, entries, do_change):
        while do_change and not os.path.exists(entries.root_path):
            entries.root_path = os.path.normpath(os.path.join(entries.root_path, ".."))
        return os.path.exists(entries.root_path)

    def sort_filter(self, use_sort, use_filter, params, entries):
#        print(use_sort, use_filter)
        if use_filter:
            filter_search = params.filter_search
            self.sortedfiltered.clear()
            if self.repo:
                if params.use_filter:
                    file_type = set()
                    blen_type = set()
                    tags_incl = {t.name for t in self.repository_pg.tags if t.use_include}
                    tags_excl = {t.name for t in self.repository_pg.tags if t.use_exclude}
                    if params.use_filter_image:
                        file_type.add('IMAGE')
                    if params.use_filter_blender:
                        file_type.add('BLENDER')
                    if params.use_filter_backup:
                        file_type.add('BACKUP')
                    if params.use_filter_movie:
                        file_type.add('MOVIE')
                    if params.use_filter_script:
                        file_type.add('SCRIPT')
                    if params.use_filter_font:
                        file_type.add('FONT')
                    if params.use_filter_sound:
                        file_type.add('SOUND')
                    if params.use_filter_text:
                        file_type.add('TEXT')
                    if params.use_filter_blendid and params.use_library_browsing:
                        file_type.add('BLENLIB')
                        blen_type = params.filter_id

                for asset in self.repository.assets.values():
                    if filter_search and filter_search not in (asset.name + asset.description):
                        continue
                    if params.use_filter:
                        if asset.file_type not in file_type:
                            continue
                        if params.use_library_browsing and asset.blender_type not in blen_type:
                            continue
                        if tags_incl or tags_excl:
                            tags = set(asset.tags)
                            if tags_incl and ((tags_incl & tags) != tags_incl):
                                continue
                            if tags_excl and (tags_excl & tags):
                                continue
                    self.sortedfiltered.append((asset.uuid[:], asset))

            elif self.dirs:
                for path, size, timestamp, uuid in self.dirs:
                    if filter_search and filter_search not in path:
                        continue
                    if not params.show_hidden and path.startswith(".") and not path.startswith(".."):
                        continue
                    self.sortedfiltered.append((path, size, timestamp, uuid))
            use_sort = True
        entries.nbr_entries_filtered = len(self.sortedfiltered) + (1 if self.repo else 0)

        if use_sort:
            if self.repo:
                if params.sort_method == 'FILE_SORT_TIME':
                    self.sortedfiltered.sort(key=lambda e: e[1].variant_default.revision_default.timestamp)
                elif params.sort_method == 'FILE_SORT_SIZE':
                    self.sortedfiltered.sort(key=lambda e: e[1].variant_default.revision_default.size)
                elif params.sort_method == 'FILE_SORT_EXTENSION':
                    self.sortedfiltered.sort(key=lambda e: e[1].blender_type)
                else:
                    self.sortedfiltered.sort(key=lambda e: e[1].name.lower())
            else:
                if params.sort_method == 'FILE_SORT_TIME':
                    self.sortedfiltered.sort(key=lambda e: e[2])
                elif params.sort_method == 'FILE_SORT_SIZE':
                    self.sortedfiltered.sort(key=lambda e: e[1])
                else:
                    self.sortedfiltered.sort(key=lambda e: e[0].lower())
            return True
        return False

    def entries_block_get(self, start_index, end_index, entries):
#        print(entries.entries[:])
        if self.repo:
            if start_index == 0:
                entry = entries.entries.add()
                entry.type = {'DIR'}
                entry.relpath = '..'
                variant = entry.variants.add()
                entry.variants.active = variant
                rev = variant.revisions.add()
                variant.revisions.active = rev
                view = rev.views.add()
                rev.views.active = view
            else:
                start_index -= 1
            end_index -= 1
            #~ print("self repo", len(self.sortedfiltered), start_index, end_index)
            for euuid, e in self.sortedfiltered[start_index:end_index]:
                self.entry_from_uuid(entries, euuid, (0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0))
        else:
            #~ print("self dirs", len(self.sortedfiltered), start_index, end_index)
            for path, size, timestamp, uuid in self.sortedfiltered[start_index:end_index]:
                entry = entries.entries.add()
                entry.type = {'DIR'}
                entry.relpath = path
#                print("added entry for", entry.relpath)
                entry.uuid = uuid
                variant = entry.variants.add()
                entry.variants.active = variant
                rev = variant.revisions.add()
                rev.timestamp = timestamp
                variant.revisions.active = rev
                view = rev.views.add()
                view.size = size
                view.timestamp = timestamp
                rev.views.active = view
        return True

    def entries_uuid_get(self, uuids, entries):
#        print(entries.entries[:])
        if self.repo:
            for uuid in uuids.uuids:
                self.entry_from_uuid(entries, uuid.uuid_asset[:], uuid.uuid_variant[:], uuid.uuid_revision[:], uuid.uuid_view[:])
            return True
        return False


classes = (
    AssetEngineAmber,
)
