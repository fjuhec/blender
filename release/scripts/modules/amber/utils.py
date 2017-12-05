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

import bpy

import binascii
import hashlib
import json
import os
import struct


AMBER_DB_NAME = "__amber_db.json"
AMBER_DBK_VERSION = "version"

AMBER_LIST_FILENAME = "amber_repos.json"

AMBER_LOCAL_STORAGE = "LOCAL_STORAGE"
AMBER_PREVIEW_STORAGE = "PREVIEWS"


BLENDER_TYPES_TO_PATH = {
    bpy.types.Object: "Object",
    bpy.types.Group: "Group",
    bpy.types.Material: "Material",
    bpy.types.Texture: "Texture",
    # TODO complete this!
}

BLENDER_TYPES_TO_ENUMVAL = {
    bpy.types.Object: 'OBJECT',
    bpy.types.Group: 'GROUP',
    bpy.types.Material: 'MATERIAL',
    bpy.types.Texture: 'TEXTURE',
    # TODO complete this!
}

##########
# Helpers.

# Notes about UUIDs:
#    * UUID of an asset/variant/revision is computed once at its creation! Later changes to data do not affect it.
#    * Collision, for unlikely it is, may happen across different repositories...
#      Doubt this will be practical issue though.
#    * We keep eight first bytes of 'clear' identifier, to (try to) keep some readable uuid.

def _uuid_gen_single(used_uuids, pattern, uuid_root, h, str_arg):
    h.update(str_arg.encode())
    hd = h.digest()
    hd = hd[:32 - len(uuid_root)]
    uuid_bytes = uuid_root + hd
    assert(len(uuid_bytes) == 16)
    uuid = uuid_unpack_bytes(uuid_bytes)
    if pattern is not (0, 1, 2, 3):
        uuid = tuple(0 if idx is ... else uuid[idx] for idx in pattern)
    if uuid not in used_uuids:  # *Very* likely, but...
        used_uuids.add(uuid)
        return uuid
    return None


def _uuid_gen(used_uuids, pattern=(0, 1, 2, 3), uuid_root=b"", bytes_seed=b"", *str_args):
    h = hashlib.md5(bytes_seed)
    for arg in str_args:
        uuid = _uuid_gen_single(used_uuids, pattern, uuid_root, h, arg)
        if uuid is not None:
            return uuid
    # This is a fallback in case we'd get a collision... Should never be needed in real life!
    for i in range(100000):
        uuid = _uuid_gen_single(used_uuids, pattern, uuid_root, h, str(i))
        if uuid is not None:
            return uuid
    return None  # If this happens...


def uuid_repo_gen(used_uuids, path, name):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", name.encode(), path)


def uuid_asset_gen(used_uuids, repo_uuid, path, name, tags):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", name.encode(), path, *tags)


def uuid_variant_gen(used_uuids, asset_uuid, name):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", str(asset_uuid).encode(), name)


def uuid_revision_gen(used_uuids, variant_uuid, number, time):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", str(variant_uuid).encode(), str(number), str(time))


def uuid_view_gen(used_uuids, revision_uuid, name, size, time):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", str(revision_uuid).encode(), name, str(size), str(time))


def uuid_unpack_bytes(uuid_bytes):
    return struct.unpack("!iiii", uuid_bytes.rjust(16, b'\0'))


def uuid_unpack(uuid_hexstr):
    return struct.unpack("!iiii", binascii.unhexlify(uuid_hexstr).rjust(16, b'\0'))


def uuid_pack(uuid_iv4):
    return binascii.hexlify(struct.pack("!iiii", *uuid_iv4)).decode()


# Colors & previews (probably only temp solution until something better is found...
# Note: ideally should use directly .blend previews, and be able to read usual image files... later!
def color_4f_to_i32(colors):
    """Converts rgba floats (in flat array) to single 32bits integers, returns an iterator of those."""
    return (int(r * 255.0) << 24 + int(g * 255.0) << 16 + int(b * 255.0) << 8 + int(a * 255.0) for r, g, b, a in zip((iter(colors),) * 4))


def preview_write_dat(path, w, h, col_i32):
    """Dummy format, 8 bytes of header, 4 bytes for width, 4 bytes for height, and raw data dump of 32bits integers."""
    with open(path, 'wb') as f:
        f.write(b"AMBERPRV" + struct.pack("!ii", w, h))
        f.write(struct.pack("!" + "i" * len(col_i32), *col_i32))


def preview_read_dat(path):
    """Dummy format, 8 bytes of header, 4 bytes for width, 4 bytes for height, and raw data dump of 32bits integers."""
    with open(path, 'rb') as f:
        if f.read(8) != b"AMBERPRV":
            return 0, 0, []
        w, h = struct.unpack("!ii", f.read(8))
        dat = struct.unpack("!" + "i" * (w * h), f.read(w * h * 4))
        return w, h, dat
