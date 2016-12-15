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
    bl_options = {'REGISTER', 'UNDO'}

    log = __logging.getLogger('bpy.ops.%s' % bl_idname)

    render_method = EnumProperty(
        items=[
            ('OPENGL', 'OpenGL render', 'Use the OpenGL viewport render'),
            ('FULL', 'Full render', 'Use the same render engine as the scene'),
        ],
        default='OPENGL'
    )

    render_pose_index = IntProperty(name='Pose index',
                                    default=-1,
                                    description='Index of the pose to render, -1 renders all poses')

    plib_index = 0
    image_size = 64, 64
    icon_size = 16, 16

    @classmethod
    def poll(cls, context):
        """Running only makes sense if there are any poses in the library."""
        plib = context.object and context.object.pose_library
        return bool(plib and plib.pose_markers)

    def execute(self, context):
        if self.render_pose_index < 0:
            self.report({'WARNING'}, "Rendering multiple previews is only available when INVOKE'd.")
            return {'PASS_THROUGH'}

        plib = context.object.pose_library
        self.setup_plib(plib)
        self.render_pose(context, plib, self.render_pose_index)
        return {'FINISHED'}

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

        context.window_manager.progress_update(self.plib_index)
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

        im = bpy.data.images['Render Result'].copy_from_render(scene=context.scene)

        # Scale to image and then icon size. Assumption: icon size is smaller than image size.
        im.scale(*self.image_size)
        plib.preview.image_frame_img_set(plib_index, im)

        if self.icon_size != self.image_size:
            im.scale(*self.icon_size)
        plib.preview.icon_frame_img_set(plib_index, im)

        bpy.data.images.remove(im)

    def invoke(self, context, event):
        if self.render_pose_index >= 0:
            self.execute(context)
            return {'FINISHED'}

        plib = context.object.pose_library

        wm = context.window_manager
        wm.modal_handler_add(self)
        wm.progress_begin(0, len(plib.pose_markers))

        self.wm = context.window_manager
        self.timer = self.wm.event_timer_add(0.01, context.window)
        self.plib_index = 0
        self.setup_plib(plib)

        return {'RUNNING_MODAL'}

    def setup_plib(self, plib):
        plib.preview.icon_size = self.icon_size
        plib.preview.image_size = self.image_size
        plib.preview.frames_number = len(plib.pose_markers)

    def _finish(self, context):
        self.wm.event_timer_remove(self.timer)
        context.window_manager.progress_end()
