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

# Blender Cloud Asset Engine, using Pillar and based on the Cloud addon.
# "Claude" (French first name pronounced in a similar way as “cloud”) stands for Cloud Light Asset Under Development Engine.
# Note: This will be a simple addon later, but until it gets to master, it's simpler to have it as a startup module!

import bpy
from bpy.types import (
        AssetEngine,
        Panel,
        PropertyGroup,
        )
from bpy.props import (
        StringProperty,
        BoolProperty,
        IntProperty,
        FloatProperty,
        EnumProperty,
        CollectionProperty,
        )

# Support reloading
if 'pillar' in locals():
    import importlib

    wheels = importlib.reload(wheels)
    wheels.load_wheels()

    pillar = importlib.reload(pillar)
    cache = importlib.reload(cache)
else:
    from . import wheels

    wheels.load_wheels()

    from . import pillar, cache

import concurrent.futures as futures
import asyncio
import threading

import binascii
import hashlib
import json
import os
import pathlib
import stat
import struct
import time
import random

from collections import OrderedDict

import pillarsdk

REQUIRED_ROLES_FOR_CLAUDE = {'subscriber', 'demo'}


##########
# Helpers.
class SpecialFolderNode(pillarsdk.Node):
    pass


class UpNode(SpecialFolderNode):
    def __init__(self):
        super().__init__()
        self['_id'] = 'UP'
        self['node_type'] = 'UP'
        self['name'] = ".."


class ProjectNode(SpecialFolderNode):
    def __init__(self, project):
        super().__init__()

        assert isinstance(project, pillarsdk.Project), "wrong type for project: %r" % type(project)

        self.merge(project.to_dict())
        self['node_type'] = 'PROJECT'


class ClaudeRepository:
    def __init__(self):
        self.path_map = {}  # Mapping names->uuid of Cloud nodes (so that user gets friendly 'named' paths in browser).
        self.curr_path = pillar.CloudPath("/")  # Using node names.
        self.curr_path_real = pillar.CloudPath("/")  # Using node uuid's.
        self.pending_path = []  # Defined when we get a path from browser (e.g. user input) that we don't know of...

        self.nodes = OrderedDict()  # uuid's to nodes mapping, for currently browsed area...
        self.is_ready = False

    def check_dir_do(self, root_path, do_change, do_update_intern):
        if self.curr_path == root_path:
            return True, root_path
        parts = root_path.parts
        if do_update_intern:
            self.pending_path = []
            self.is_ready = False
            self.nodes.clear()
        if parts and parts[0] == "/":
            nids = [parts[0]]
            paths = self.path_map
            for i, p in enumerate(parts[1:]):
                nid, paths = paths.get(p, (None, None))
                if nid is None:
                    if do_update_intern:
                        self.pending_path = parts[i + 1:]
                        self.curr_path = pillar.CloudPath("/".join(parts[:i + 1]))
                        self.curr_path_real = pillar.CloudPath("/".join(nids))
                    break
                else:
                    nids.append(nid)
            return True, root_path
        elif do_change:
            if do_update_intern:
                self.curr_path = pillar.CloudPath("/")
                self.curr_path_real = pillar.CloudPath("/")
            return True, pillar.CloudPath("/")
        else:
            return False, root_path


#############
# Claude Jobs.
class ClaudeJob:
    @staticmethod
    def async_looper(func):
        def wrapper(self, *args, **kwargs):
            loop = self.loop
            assert(not loop.is_running())
            print("proceed....")
            ret = func(self, *args, **kwargs)
            if not self.evt_cancel.is_set():
                print("kickstep")
                # This forces loop to only do 'one step', and return (hopefully!) very soon.
                #~ loop.stop()
                loop.call_soon(loop.stop)
                loop.run_forever()
            else:
                print("canceled")
            return ret
        return wrapper

    def add_task(self, coro):
        task = asyncio.ensure_future(coro)
        self._tasks.append(task)
        return task

    def remove_task(self, task):
        self._tasks.remove(task)
        return None

    def cancel(self):
        print("Cancelling ", self)
        self.evt_cancel.set()
        for task in self._tasks:
            task.cancel()
        self._tasks[:] = []
        self.status = {'VALID'}
        assert(not self.loop.is_running())
        self.loop.call_soon(self.loop.stop)
        self.loop.run_forever()

    def __init__(self, job_id):
        self.job_id = job_id
        self.status = {'VALID'}
        self.progress = 0.0

        self._tasks = []

        self.evt_cancel = threading.Event()
        self.loop = asyncio.get_event_loop()

    def __del__(self):
        print("deleting ", self)
        self.cancel()

class ClaudeJobCheckCredentials(ClaudeJob):
    @staticmethod
    async def check():
        """
        Check credentials with Pillar, and if ok returns the user ID.
        Returns None if the user cannot be found, or if the user is not a Cloud subscriber.
        """
        try:
            print("Awaiting pillar.check_pillar_credentials...")
            user_id = await pillar.check_pillar_credentials(REQUIRED_ROLES_FOR_CLAUDE)
            print("Done pillar.check_pillar_credentials...")
        except pillar.NotSubscribedToCloudError:
            print('Not subsribed.')
            return None
        except pillar.CredentialsNotSyncedError:
            print('Credentials not synced, re-syncing automatically.')
            #~ self.log.info('Credentials not synced, re-syncing automatically.')
        else:
            print('Credentials okay.')
            #~ self.log.info('Credentials okay.')
            return user_id

        try:
            print("awaiting pillar.refresh_pillar_credentials...")
            user_id = await pillar.refresh_pillar_credentials(required_roles)
            print("Done pillar.refresh_pillar_credentials...")
        except pillar.NotSubscribedToCloudError:
            print('Not subsribed.')
            return None
        except pillar.UserNotLoggedInError:
            print('User not logged in on Blender ID.')
            #~ self.log.error('User not logged in on Blender ID.')
        else:
            print('Credentials refreshed and ok.')
            #~ self.log.info('Credentials refreshed and ok.')
            return user_id

        return None

    @ClaudeJob.async_looper
    def start(self):
        self.check_task = self.add_task(self.check())
        self.progress = 0.0
        self.status = {'VALID', 'RUNNING'}

    @ClaudeJob.async_looper
    def update(self):
        self.status = {'VALID', 'RUNNING'}
        user_id = ...

        self.progress += 0.05
        if (self.progress > 1.0):
            self.progress = 0.0

        print("Updating credential status, ", self.check_task, " | Done: ", self.check_task.done() if self.check_task else "")
        if self.check_task is not None:
            if not self.check_task.done():
                return ...
            print("Cred check finished, we should have cloud access...")
            user_id = self.check_task.result()
            self.check_task = self.remove_task(self.check_task)
            self.progress = 1.0

        if self.check_task is None:
            self.progress = 1.0
            self.status = {'VALID'}
        return user_id

    def __init__(self, job_id):
        super().__init__(job_id)

        self.check_task = None

        self.start()


class ClaudeJobList(ClaudeJob):
    @staticmethod
    async def ls(repo):
        print("we should be listing Cloud content from %s..." % repo.curr_path_real)

        project_uuid = repo.curr_path_real.project_uuid
        node_uuid = repo.curr_path_real.node_uuid

        if node_uuid:
            # Query for sub-nodes of this node.
            print("Getting subnodes for parent node %r" % node_uuid)
            children = await pillar.get_nodes(parent_node_uuid=node_uuid, node_type='group_texture')
            print(children)
        elif project_uuid:
            # Query for top-level nodes.
            print("Getting subnodes for project node %r" % project_uuid)
            children = await pillar.get_nodes(project_uuid=project_uuid, parent_node_uuid='', node_type='group_texture')
            print(children)
        else:
            # Query for projects
            print("No node UUID and no project UUID, listing available projects")
            children = await pillar.get_texture_projects()
            print(children)

        repo.pending_path = []
        repo.is_ready = True

    @ClaudeJob.async_looper
    def start(self, user_id):
        self.ls_task = self.add_task(self.ls(self.repo))
        self.status = {'VALID', 'RUNNING'}

    @ClaudeJob.async_looper
    def update(self, user_id):
        self.status = {'VALID', 'RUNNING'}

        if user_id is None:
            print("Invalid user ID, cannot proceed...")
            self.cancel()
            return

        if user_id is ...:
            print("Awaiting a valid user ID...")
            return

        self.progress += 0.05
        if (self.progress > 1.0):
            self.progress = 0.0

        if self.ls_task is not None:
            if self.ls_task.done():
                print("ls finished, we should have our children nodes now!")
                self.ls_task = self.remove_task(self.ls_task)

        if self.ls_task is None:
            self.progress = 1.0
            self.status = {'VALID'}

    def cancel(self):
        if self.ls_task:
            self.ls_task.cancel()
        super().cancel()

    def __init__(self, job_id, user_id, repo):
        super().__init__(job_id)
        self.repo = repo
        self.curr_path = repo.curr_path_real

        self.ls_task = None

        self.start(user_id)


"""
class AmberJobPreviews(AmberJob):
    @staticmethod
    def preview(uuid):
        time.sleep(0.1)  # 100% Artificial Lag (c)
        w = random.randint(2, 8)
        h = random.randint(2, 8)
        return [w, h, [random.getrandbits(32) for i in range(w * h)]]

    def start(self, uuids):
        self.nbr = 0
        self.preview_tasks = {uuid.uuid_asset[:]: self.executor.submit(self.preview, uuid.uuid_asset[:]) for uuid in uuids.uuids}
        self.tot = len(self.preview_tasks)
        self.status = {'VALID', 'RUNNING'}

    def update(self, uuids):
        self.status = {'VALID', 'RUNNING'}

        uuids = {uuid.uuid_asset[:]: uuid for uuid in uuids.uuids}

        new_uuids = set(uuids)
        old_uuids = set(self.preview_tasks)
        del_uuids = old_uuids - new_uuids
        new_uuids -= old_uuids

        for uuid in del_uuids:
            self.preview_tasks[uuid].cancel()
            del self.preview_tasks[uuid]

        for uuid in new_uuids:
            self.preview_tasks[uuid] = self.executor.submit(self.preview, uuid)

        self.tot = len(self.preview_tasks)
        self.nbr = 0

        done_uuids = set()
        for uuid, tsk in self.preview_tasks.items():
            if tsk.done():
                w, h, pixels = tsk.result()
                uuids[uuid].preview_size = (w, h)
                uuids[uuid].preview_pixels = pixels
                self.nbr += 1
                done_uuids.add(uuid)

        for uuid in done_uuids:
            del self.preview_tasks[uuid]

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
"""


###########################
# Main Asset Engine class.
class AssetEngineClaude(AssetEngine):
    bl_label = "Claude"
    bl_version = (0 << 16) + (0 << 8) + 1  # Usual maj.min.rev version scheme...

    def __init__(self):
        self.jobs = {}

        self.job_uuid = 1
        self.user_id = ...
        self.job_id_check_credentials = ...
        self.check_credentials()

        self.repo = ClaudeRepository()

    def __del__(self):
        pass

    ########## Various helpers ##########
    def check_credentials(self):
        if self.job_id_check_credentials is None:
            return self.user_id
        if self.job_id_check_credentials is ...:
            self.job_id_check_credentials = job_id = self.job_uuid
            self.job_uuid += 1
            job = self.jobs[job_id] = ClaudeJobCheckCredentials(job_id)
        else:
            job = self.jobs[self.job_id_check_credentials]
        print("main check_credentials: ", job)
        if job is not None:
            self.user_id = job.update()
            if self.user_id is not ...:
                self.kill(self.job_id_check_credentials)
                self.job_id_check_credentials = None
        return self.user_id

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
        return (progress / nbr_jobs) if nbr_jobs else 0.0

    def kill(self, job_id):
        if job_id:
            self.jobs.pop(job_id, None)
            return
        self.jobs.clear()

    def list_dir(self, job_id, entries):
        user_id = self.check_credentials()

        curr_path = pillar.CloudPath(entries.root_path)
        if curr_path != self.repo.curr_path:
            self.repo.check_dir_do(curr_path, True, True)

        if not self.repo.is_ready:
            curr_path = self.repo.curr_path_real
            pending_path = self.repo.pending_path
            print("listing %s%s[%s]..." % (str(curr_path), "" if str(curr_path).endswith('/') else "/", "/".join(pending_path)))

            job = self.jobs.get(job_id, None)
            if job is None or not isinstance(job, ClaudeJobList):
                job_id = self.job_uuid
                self.job_uuid += 1
                job = self.jobs[job_id] = ClaudeJobList(job_id, user_id, self.repo)
            if job is not None:
                job.update(user_id)
                print(job.status)
                if 'RUNNING' not in job.status:
                    entries.root_path = str(self.repo.curr_path)

        print(list(self.repo.nodes.values()))
        entries.nbr_entries = 1 #len(self.repo.nodes)
        return job_id

    def sort_filter(self, use_sort, use_filter, params, entries):
        entries.nbr_entries_filtered = 1 #len(self.repo.nodes)
        return False

    def update_check(self, job_id, uuids):
        # do nothing for now, no need to use actual job...
        return self.job_id_invalid

    """
    def previews_get(self, job_id, uuids):
        pass
        job = self.jobs.get(job_id, None)
        #~ print(entries.root_path, job_id, job)
        if job is not None and isinstance(job, AmberJobPreviews):
            job.update(uuids)
        else:
            job_id = self.job_uuid
            self.job_uuid += 1
            self.jobs[job_id] = AmberJobPreviews(job_id, uuids)
        return job_id
    """

    def check_dir(self, entries, do_change):
        ret, root_path = self.repo.check_dir_do(pillar.CloudPath(entries.root_path), do_change, False)
        entries.root_path = str(root_path)
        return ret

    def entries_block_get(self, start_index, end_index, entries):
        entry = entries.entries.add()
        entry.type = {'DIR'}
        entry.relpath = ".."
        #~ print("added entry for", entry.relpath)
        entry.uuid = (0, 0, 0, 1)
        variant = entry.variants.add()
        entry.variants.active = variant
        rev = variant.revisions.add()
        variant.revisions.active = rev
        """
        print(start_index, end_index)
        for uuid, path in enumerate(self.dirs[start_index:end_index]):
            uuid += start_index
            print(uuid, path)
            entry = entries.entries.add()
            entry.type = {'DIR'}
            entry.relpath = path
            #~ print("added entry for", entry.relpath)
            entry.uuid = (0, 0, 0, uuid)
            variant = entry.variants.add()
            entry.variants.active = variant
            rev = variant.revisions.add()
            variant.revisions.active = rev
        """
        return True

    def entries_uuid_get(self, uuids, entries):
        return False


##########
# UI stuff

class ClaudePanel():
    @classmethod
    def poll(cls, context):
        space = context.space_data
        if space and space.type == 'FILE_BROWSER':
            ae = space.asset_engine
            if ae and space.asset_engine_type == "AssetEngineClaude":
                return True
        return False


class CLAUDE_PT_messages(Panel, ClaudePanel):
    bl_space_type = 'FILE_BROWSER'
    bl_region_type = 'TOOLS'
    bl_category = "Asset Engine"
    bl_label = "Claude Messages"

    def draw(self, context):
        layout = self.layout
        space = context.space_data
        ae = space.asset_engine

        layout.label("Some stupid test info...", icon='INFO')


if __name__ == "__main__":  # only for live edit.
    bpy.utils.register_module(__name__)
