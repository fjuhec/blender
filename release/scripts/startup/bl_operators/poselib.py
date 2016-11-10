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
    """Renders a preview image for each pose in the pose library.

    TODO: the viewport used for OpenGL rendering is more or less random.
    """

    import logging as __logging

    bl_idname = "poselib.render_previews"
    bl_label = "Render pose previews"
    bl_description = "Renders a preview image for each pose in the pose library"

    log = __logging.getLogger('bpy.ops.%s' % bl_idname)

    render_method = EnumProperty(
        items=[
            ('OPENGL', 'OpenGL render', 'Use the OpenGL viewport render'),
            ('FULL', 'Full render', 'Use the same render engine as the scene'),
        ],
        default='OPENGL'
    )

    plib_index = 0
    image_size = 64, 64
    icon_size = 16, 16

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
        import tempfile
        import os.path

        marker = plib.pose_markers[plib_index]
        marker.preview_frame_index = plib_index
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

        if self.render_method == 'OPENGL':
            bpy.ops.render.opengl(view_context=False)
        else:
            bpy.ops.render.render()

        with tempfile.TemporaryDirectory() as tmpdir:
            fname = os.path.join(tmpdir, 'poselib.png')
            bpy.data.images['Render Result'].save_render(fname)

            # Loading and handling the image data should all be done while the tempfile
            # still exists on disk.
            im = bpy.data.images.load(fname)

            im.scale(*self.image_size)
            plib.preview.image_frame_float_set(plib_index, im.pixels)
            self.image_load_time += duration

            if self.icon_size != self.image_size:
                im.scale(*self.icon_size)
            plib.preview.icon_frame_float_set(plib_index, im.pixels)

    def invoke(self, context, event):
        wm = context.window_manager
        wm.modal_handler_add(self)

        self.wm = context.window_manager
        self.timer = self.wm.event_timer_add(0.01, context.window)
        self.plib_index = 0

        plib = context.object.pose_library
        plib.preview.icon_size = self.icon_size
        plib.preview.image_size = self.image_size
        plib.preview.frames_number = len(plib.pose_markers)
        for pmrk in plib.pose_markers:
            pmrk.preview_frame_index = 0

        return {'RUNNING_MODAL'}

    def _finish(self, context):
        self.wm.event_timer_remove(self.timer)
