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

#ifndef __ABC_ALEMBIC_H__
#define __ABC_ALEMBIC_H__

#ifdef __cplusplus
extern "C" {
#endif

struct bContext;
struct DerivedMesh;
struct Object;
struct Scene;

typedef struct AbcArchiveHandle AbcArchiveHandle;

enum {
	ABC_ARCHIVE_OGAWA = 0,
	ABC_ARCHIVE_HDF5  = 1,
};

int ABC_get_version(void);

void ABC_export(
        struct Scene *scene,
        struct bContext *C,
        const char *filepath,
        const double start,
        const double end,
        const double xformstep,
        const double geomstep,
        const double shutter_open,
        const double shutter_close,
        const bool selected_only,
        const bool uvs,
        const bool normals,
        const bool vcolors,
        const bool apply_subdiv,
        const bool flatten_hierarchy,
        const bool vislayers,
        const bool renderable,
        const bool facesets,
        const bool use_subdiv_schema,
        const bool compression,
        const bool packuv,
        const float global_scale);

void ABC_import(struct bContext *C,
                const char *filepath,
                float scale,
                bool is_sequence,
                bool set_frame_range,
                int sequence_len,
                int offset);

AbcArchiveHandle *ABC_create_handle(const char *filename);

void ABC_free_handle(AbcArchiveHandle *handle);

void ABC_get_transform(AbcArchiveHandle *handle,
                       struct Object *ob,
                       const char *object_path,
                       float r_mat[4][4],
                       float time,
                       float scale);

struct DerivedMesh *ABC_read_mesh(AbcArchiveHandle *handle,
                                  struct Object *ob,
                                  struct DerivedMesh *dm,
                                  const char *object_path,
                                  const float time);

bool ABC_has_velocity_cache(AbcArchiveHandle *handle, const char *object_path, const float time);
void ABC_get_velocity_cache(AbcArchiveHandle *handle, const char *object_path, float *values, float time);

#ifdef __cplusplus
}
#endif

#endif  /* __ABC_ALEMBIC_H__ */
