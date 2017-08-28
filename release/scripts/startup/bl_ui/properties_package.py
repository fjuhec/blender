import bpy
from bpy.types import UIList, PropertyGroup

class RepositoryProperty(PropertyGroup):
    name = bpy.props.StringProperty(name="Name")
    url = bpy.props.StringProperty(name="URL")
    status = bpy.props.EnumProperty(name="Status", items=[
            ("OK",        "Okay",              "FILE_TICK"),
            ("NOTFOUND",  "Not found",         "ERROR"),
            ("NOCONNECT", "Could not connect", "QUESTION"),
            ])
    enabled = bpy.props.BoolProperty(name="Enabled")

class PACKAGE_UL_repositories(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
        layout.prop(item, "url", text="", emboss=False)

classes = (
    RepositoryProperty,
    PACKAGE_UL_repositories,
)
