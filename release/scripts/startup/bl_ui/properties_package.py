import bpy
from bpy.types import UIList, PropertyGroup

class RepositoryProperty(PropertyGroup):
    name = bpy.props.StringProperty(name="Name")
    url = bpy.props.StringProperty(name="URL")
    filepath = bpy.props.StringProperty(name="Filepath")
    status = bpy.props.EnumProperty(name="Status", items=[
            ("OK",        "Okay",              "FILE_TICK"),
            ("NOTFOUND",  "Not found",         "ERROR"),
            ("NOCONNECT", "Could not connect", "QUESTION"),
            ])
    enabled = bpy.props.BoolProperty(name="Enabled")

class PACKAGE_UL_repositories(UIList):
    def draw_item(self, context, layout, data, item, icon, active_data, active_propname):
        try:
            layout.label(text=item['name'] + ":", icon='FILE_TICK')
        except KeyError: #name not defined while still downloading
            layout.label(text="", icon='FILE_REFRESH')
        # TODO: for some reason unembossing the following causes blender to become unresponsive when ctrl clicking the url
        layout.prop(item, "url", text="")

classes = (
    RepositoryProperty,
    PACKAGE_UL_repositories,
)
