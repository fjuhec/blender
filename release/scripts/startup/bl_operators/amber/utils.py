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

# Note: This will be a simple addon later, but until it gets to master, it's simpler to have it
#       as a startup module!

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


BLENDER_TYPES_TO_PATH = {
    bpy.types.Object: "Object",
    bpy.types.Group: "Group",
    bpy.types.Material: "Material",
    # TODO complete this!
}

BLENDER_TYPES_TO_ENUMVAL = {
    bpy.types.Object: 'OBJECT',
    bpy.types.Group: 'GROUP',
    bpy.types.Material: 'MATERIAL',
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
    uuid = _uuid_gen(used_uuids, (0, 1, ..., ...), b"", name.encode(), path)
    assert(uuid is not None)
    return uuid


def uuid_asset_gen(used_uuids, repo_uuid, path, name, tags):
    uuid = _uuid_gen(used_uuids, (..., ..., 0, 1), b"", name.encode(), path, *tags)
    assert(uuid is not None)
    return repo_uuid[:2] + uuid[2:]


def uuid_variant_gen(used_uuids, asset_uuid, name):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", str(asset_uuid).encode(), name)


def uuid_revision_gen(used_uuids, variant_uuid, number, size, time):
    return _uuid_gen(used_uuids, (0, 1, 2, 3), b"", str(variant_uuid).encode(), str(number), str(size), str(time))


def uuid_unpack_bytes(uuid_bytes):
    return struct.unpack("!iiii", uuid_bytes.rjust(16, b'\0'))


def uuid_unpack(uuid_hexstr):
    return struct.unpack("!iiii", binascii.unhexlify(uuid_hexstr).rjust(16, b'\0'))


def uuid_pack(uuid_iv4):
    return binascii.hexlify(struct.pack("!iiii", *uuid_iv4)).decode()
