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


import os
import logging
import pathlib

from . import appdirs

log = logging.getLogger(__name__)


def cache_directory(*subdirs) -> pathlib.Path:
    """Returns an OS-specifc cache location, and ensures it exists.

    Should be replaced with a call to bpy.utils.user_resource('CACHE', ...)
    once https://developer.blender.org/T47684 is finished.

    :param subdirs: extra subdirectories inside the cache directory.

    >>> cache_directory()
    '.../blender_cloud/your_username'
    >>> cache_directory('sub1', 'sub2')
    '.../blender_cloud/your_username/sub1/sub2'
    """

    # TODO: use bpy.utils.user_resource('CACHE', ...)
    # once https://developer.blender.org/T47684 is finished.
    user_cache_dir = appdirs.user_cache_dir(appname='Blender', appauthor=False)
    cache_dir = pathlib.Path(user_cache_dir) / 'blender_package_manager' / pathlib.Path(*subdirs)
    cache_dir.mkdir(mode=0o700, parents=True, exist_ok=True)

    return cache_dir
