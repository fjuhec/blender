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

    bl_idname = "poselib.render_previews"
    bl_label = "Render pose previews"

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
        plib = context.object.pose_library

        if self.plib_index >= len(plib.pose_markers):
            return {'FINISHED'}

        self.render_pose(self.plib_index)
        self.plib_index += 1

        return {'RUNNING_MODAL'}

    def render_pose(self, plib_index):
        bpy.ops.poselib.apply_pose(pose_index=plib_index)

    def invoke(self, context, event):
        wm = context.window_manager
        wm.modal_handler_add(self)
        self.plib_index = 0
        return {'RUNNING_MODAL'}
