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

# <pep8-80 compliant>

if "bpy" in locals():
    from importlib import reload
    if "anim_utils" in locals():
        reload(anim_utils)
    del reload


import bpy
from bpy.types import Operator
from bpy.props import (
        IntProperty,
        BoolProperty,
        EnumProperty,
        StringProperty,
        )


class POSELIB_OT_render_previews(Operator):
    "Renders a preview image for each pose in the pose library"

    import logging as __logging

    bl_idname = "poselib.render_previews"
    bl_label = "Render pose previews"
    log = __logging.getLogger('bpy.ops.%s' % bl_idname)

    render_method = EnumProperty(
        items=[
            ('OPENGL', 'OpenGL render', 'Use the OpenGL viewport render'),
            ('FULL', 'Full render', 'Use the same render engine as the scene'),
        ],
        default='OPENGL'
    )

    plib_index = 0


    @classmethod
    def poll(cls, context):
        """Running only makes sense if there are any poses in the library."""
        plib = context.object and context.object.pose_library
        return bool(plib and plib.pose_markers)

    def execute(self, context):
        return {'PASS_THROUGH'}

    def modal(self, context, event):
        if event.type == 'ESC':
            self._finish(context)
            self.report({'INFO'}, 'Canceled rendering pose library previews')
            return {'CANCELLED'}

        if event.type != 'TIMER':
            return {'PASS_THROUGH'}

        plib = context.object.pose_library

        pose_count = len(plib.pose_markers)
        if self.plib_index >= pose_count:
            self._finish(context)
            self.report({'INFO'}, 'Done rendering pose library previews')
            return {'FINISHED'}

        self.render_pose(context, plib, self.plib_index)
        self.plib_index += 1

        return {'RUNNING_MODAL'}

    def render_pose(self, context, plib, plib_index):
        import os.path

        marker = plib.pose_markers[plib_index]
        self.log.info('Rendering pose %s at frame %i', marker.name, marker.frame)

        context.scene.frame_set(marker.frame)
        switch_to = None
        if marker.camera:
            switch_to = marker.camera
        else:
            cams = [m.camera for m in context.scene.timeline_markers
                    if m.frame == marker.frame and m.camera]
            if cams:
                switch_to = cams[0]
        if switch_to is not None:
            self.log.info('Switching camera to %s', switch_to)
            context.scene.camera = switch_to

        bpy.ops.poselib.apply_pose(pose_index=plib_index)

        fname = '%s.png' % marker.name
        context.scene.render.filepath = os.path.join(plib.pose_previews_dir, fname)
        bpy.ops.render.opengl(write_still=True)

    def invoke(self, context, event):
        wm = context.window_manager
        wm.modal_handler_add(self)

        self.wm = context.window_manager
        self.timer = self.wm.event_timer_add(0.01, context.window)
        self.plib_index = 0
        self.orig_filepath = context.scene.render.filepath

        return {'RUNNING_MODAL'}

    def _finish(self, context):
        self.wm.event_timer_remove(self.timer)
        context.scene.render.filepath = self.orig_filepath
