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


##########
# Helpers.

# Notes about UUIDs:
#    * UUID of an asset/variant/revision is computed once at its creation! Later changes to data do not affect it.
#    * Collision, for unlikely it is, may happen across different repositories...
#      Doubt this will be practical issue though.
#    * We keep eight first bytes of 'clear' identifier, to (try to) keep some readable uuid.

def _uuid_gen_single(used_uuids, uuid_root, h, str_arg):
    h.update(str_arg.encode())
    uuid = uuid_root + h.digest()
    uuid = uuid[:23].replace(b'\0', b'\1')  # No null chars, RNA 'bytes' use them as in regular strings... :/
    if uuid not in used_uuids:  # *Very* likely, but...
        used_uuids.add(uuid)
        return uuid
    return None


def _uuid_gen(used_uuids, uuid_root, bytes_seed, *str_args):
    h = hashlib.md5(bytes_seed)
    for arg in str_args:
        uuid = _uuid_gen_single(used_uuids, uuid_root, h, arg)
        if uuid is not None:
            return uuid
    # This is a fallback in case we'd get a collision... Should never be needed in real life!
    for i in range(100000):
        uuid = _uuid_gen_single(used_uuids, uuid_root, h, i.to_bytes(4, 'little'))
        if uuid is not None:
            return uuid
    return None  # If this happens...


def uuid_asset_gen(used_uuids, path_db, name, tags):
    uuid_root = name.encode()[:8] + b'|'
    return _uuid_gen_single(used_uuids, uuid_root, path_db.encode(), name, *tags)


def uuid_variant_gen(used_uuids, asset_uuid, name):
    uuid_root = name.encode()[:8] + b'|'
    return _uuid_gen_single(used_uuids, uuid_root, asset_uuid, name)


def uuid_revision_gen(used_uuids, variant_uuid, number, size, time):
    uuid_root = str(number).encode() + b'|'
    return _uuid_gen_single(used_uuids, uuid_root, variant_uuid, str(number), str(size), str(timestamp))


def uuid_unpack_bytes(uuid_bytes):
    return struct.unpack("!iiii", uuid_bytes.rjust(16, b'\0'))


def uuid_unpack(uuid_hexstr):
    return struct.unpack("!iiii", binascii.unhexlify(uuid_hexstr).rjust(16, b'\0'))


def uuid_pack(uuid_iv4):
    return binascii.hexlify(struct.pack("!iiii", *uuid_iv4))


# XXX Hack, once this becomes a real addon we'll just use addons' config system, for now store that in some own config.
amber_repos_path = os.path.join(bpy.utils.user_resource('CONFIG', create=True), "amber_repos.json")
amber_repos = None
if not os.path.exists(amber_repos_path):
    with open(amber_repos_path, 'w') as ar_f:
        json.dump({}, ar_f)
with open(amber_repos_path, 'r') as ar_f:
    amber_repos = {uuid_unpack(uuid): path for uuid, path in json.load(ar_f).items()}
assert(amber_repos != None)


def save_amber_repos():
    ar = {uuid_pack(uuid).decode(): path for uuid, path in amber_repos.items()}
    with open(amber_repos_path, 'w') as ar_f:
        json.dump(ar, ar_f)
