# Apache License, Version 2.0
import bpy
from bpy.types import Operator
from bpy.props import StringProperty
from bpy_extras.io_utils import ExportHelper

def mesh_triangulate(me):
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()


class ExportWidget(Operator, ExportHelper):
    """Export a widget mesh as a C file"""
    bl_idname = "export_scene.widget"
    bl_label = "Export Widget"
    bl_options = {'PRESET', 'UNDO'}

    filename_ext = ".c"
    filter_glob = StringProperty(
            default="*.c;",
            options={'HIDDEN'},
            )
    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (obj and obj.type == 'MESH')

    def execute(self, context):
        ob = context.active_object
        scene = context.scene

        try:
            me = ob.to_mesh(scene, True, 'PREVIEW', calc_tessface=False)
        except RuntimeError:
            me = None

        if me is None:
            return {'CANCELLED'}

        mesh_triangulate(me)

        f = open(self.filepath, 'w')

        f.write("static const float verts[][3] = {\n")
        for v in me.vertices:
            f.write("\t{%.6f, %.6f, %.6f},\n" % v.co[:])
        f.write("};\n\n")
        f.write("static const float normals[][3] = {\n")
        for v in me.vertices:
            f.write("\t{%.6f, %.6f, %.6f},\n" % v.normal[:])
        f.write("};\n\n")
        f.write("static const unsigned short indices[] = {\n")
        for p in me.polygons:
            f.write("\t%d, %d, %d,\n" % p.vertices[:])
        f.write("};\n")

        f.write("\n")

        f.write("ManipulatorGeomInfo wm_manipulator_geom_data_%s = {\n" % ob.name)
        f.write("\t.nverts  = %d,\n" % len(me.vertices))
        f.write("\t.ntris   = %d,\n" % len(me.polygons))
        f.write("\t.verts   = verts,\n")
        f.write("\t.normals = normals,\n")
        f.write("\t.indices = indices,\n")
        f.write("};\n")

        f.close()

        return {'FINISHED'}

def menu_func_export(self, context):
    self.layout.operator(ExportWidget.bl_idname, text="Widget (.c)")


def register():
   bpy.utils.register_module(__name__)
   bpy.types.INFO_MT_file_export.append(menu_func_export)


def unregister():
    bpy.utils.unregister_module(__name__)
    bpy.types.INFO_MT_file_export.remove(menu_func_export)

if __name__ == "__main__":
    register()
