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
import stat
import struct
import time
import random

import pillarsdk
from . import pillar, cache

REQUIRED_ROLES_FOR_TEXTURE_BROWSER = {'subscriber', 'demo'}


##########
# Helpers.


#############
# Claude Jobs.
class ClaudeJob:
    @staticmethod
    async def check_loop(loop, evt_cancel):
        """Async function merely endlessly checking 'cancel' flag is not set, and stopping the loop in case it is..."""
        while True:
            if evt_cancel.is_set():
                print("CAAANNNNNNCCCEEEEEEEELLLLLEDDDDDDDD!")
                loop.stop()
                return
            await asyncio.sleep(1e-6)

    @classmethod
    def run_loop(cls, loop, evt_cancel):
        """Function called in worker thread, sets the loop for current thread, and runs it."""
        asyncio.set_event_loop(loop)
        asyncio.ensure_future(cls.check_loop(loop, evt_cancel))
        loop.run_forever()
        loop.close()

    def cancel(self):
        self.evt_cancel.set()
        self.running_loop.cancel()
        futures.wait((self.running_loop,))

    def __init__(self, executor, job_id):
        self.executor = executor

        self.job_id = job_id
        self.status = {'VALID'}
        self.progress = 0.0

        self.loop = asyncio.new_event_loop()
        #~ self.loop.set_default_executor(executor)
        self.evt_cancel = threading.Event()
        self.running_loop = self.executor.submit(self.run_loop, self.loop, self.evt_cancel)

    def __del__(self):
        self.cancel()


class ClaudeJobList(ClaudeJob):
    @staticmethod
    async def ls(evt_cancel, node):
        if evt_cancel.is_set():
            return None

        print("we should be listing Cloud content from Node ", node, "...")
        await asyncio.sleep(1)

        return ["..", "a", "b"]

    def start(self):
        self.nbr = 0
        self.tot = 0
        self.ls_task = asyncio.run_coroutine_threadsafe(self.ls(self.evt_cancel, self.curr_node), self.loop)
        self.status = {'VALID', 'RUNNING'}

    def update(self, dirs):
        if self.evt_cancel.is_set():
            self.cancel()
            return

        self.status = {'VALID', 'RUNNING'}
        if self.ls_task is not None:
            if not self.ls_task.done():
                dirs[:] = [".."]
                return
            print("ls finished, we should have our children nodes now!")
            dirs[:] = self.ls_task.result()
            print(dirs)
            self.ls_task = None

        self.progress = self.nbr / self.tot if self.tot else 0.0
        if self.ls_task is None:
            self.status = {'VALID'}

    def cancel(self):
        print("CANCELLING...")
        super().cancel()
        if self.ls_task is not None and not self.ls_task.done():
            self.ls_task.cancel()
        self.status = {'VALID'}

    def __init__(self, executor, job_id, curr_node):
        super().__init__(executor, job_id)
        self.curr_node = curr_node

        self.ls_task = None

        self.start()


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
        self.executor = futures.ThreadPoolExecutor(8)  # Using threads for now, if issues arise we'll switch to process.

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
        print("Claude Reset!")
        self.jobs = {}
        self.root = ""
        self.dirs = []

        self.sortedfiltered = []

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
        print(entries.root_path, job_id, job)
        if job is not None and isinstance(job, ClaudeJobList):
            if job.curr_node != entries.root_path:
                self.reset()
                self.jobs[job_id] = ClaudeJobList(self.executor, job_id, entries.root_path)
                self.root = entries.root_path
        elif self.root != entries.root_path:
            self.reset()
            job_id = self.job_uuid
            self.job_uuid += 1
            job = self.jobs[job_id] = ClaudeJobList(self.executor, job_id, entries.root_path)
            self.root = entries.root_path
        if job is not None:
            job.update(self.dirs)
        print(self.dirs)
        entries.nbr_entries = len(self.dirs)
        return job_id

    def sort_filter(self, use_sort, use_filter, params, entries):
        entries.nbr_entries_filtered = len(self.dirs)
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
            self.jobs[job_id] = AmberJobPreviews(self.executor, job_id, uuids)
        return job_id
    """

    def check_dir(self, entries, do_change):
        #~ entries.root_path = "NODE/"
        return True

    def entries_block_get(self, start_index, end_index, entries):
        print(start_index, end_index)
        for uuid, path in enumerate(self.dirs[start_index:end_index]):
            uuid += start_index
            print(uuid, path)
            entry = entries.entries.add()
            entry.type = {'DIR'}
            entry.relpath = path
#            print("added entry for", entry.relpath)
            entry.uuid = (0, 0, 0, uuid)
            variant = entry.variants.add()
            entry.variants.active = variant
            rev = variant.revisions.add()
            variant.revisions.active = rev
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

        for icon, message in ae.messages:
            layout.label(message, icon=icon)


if __name__ == "__main__":  # only for live edit.
    bpy.utils.register_module(__name__)
