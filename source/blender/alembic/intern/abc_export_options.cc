/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Esteban Tovagliari, Cedric Paille, Kevin Dietrich
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "abc_export_options.h"

#include <string>

#include "abc_util.h"

extern "C" {
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_idprop.h"
}

ExportSettings::ExportSettings()
    : selected_only(false)
	, visible_layers_only(false)
	, renderable_only(false)
	, startframe(1)
    , endframe(1)
	, xform_frame_step(1)
	, shape_frame_step(1)
	, shutter_open(0.0)
	, shutter_close(1.0)
	, global_scale(1.0f)
	, flatten_hierarchy(false)
	, export_normals(false)
	, export_uvs(false)
	, export_vcols(false)
	, export_face_sets(false)
	, export_mat_indices(false)
	, export_vweigths(false)
	, export_subsurfs_as_meshes(false)
	, use_subdiv_schema(false)
	, export_child_hairs(true)
	, export_ogawa(true)
	, pack_uv(false)
	, do_convert_axis(false)
	, scene(NULL)
{}

bool ExportSettings::exportObject(Object *obj) const
{
	if (selected_only && !parent_selected(obj)) {
		return false;
	}

	if (visible_layers_only && !(scene->lay & obj->lay)) {
		return false;
	}

	if (renderable_only && (obj->restrictflag & 4)) {
		return false;
	}

	return true;
}
