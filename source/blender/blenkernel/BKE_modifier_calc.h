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
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#ifndef __BKE_MODIFIER_CALC_H__
#define __BKE_MODIFIER_CALC_H__

/** \file BKE_modifier_calc.h
 *  \ingroup bke
 */

#include "DNA_customdata_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_compiler_attrs.h"

#include "BKE_customdata.h"
#include "BKE_bvhutils.h"

struct CCGElem;
struct CCGKey;
struct MVert;
struct MEdge;
struct MFace;
struct MTFace;
struct Object;
struct Scene;
struct Mesh;
struct MLoopNorSpaceArray;
struct BMEditMesh;
struct KeyBlock;
struct ModifierData;
struct MCol;
struct ColorBand;
struct GPUVertexAttribs;
struct GPUDrawObject;
struct PBVH;
struct EvaluationContext;

/* Temporary? A function to give a colorband to derivedmesh for vertexcolor ranges */
void vDM_ColorBand_store(const struct ColorBand *coba, const char alert_color[4]);

/* */
struct DerivedMesh *mesh_get_derived_final(
        struct Scene *scene, struct Object *ob,
        CustomDataMask dataMask);
struct DerivedMesh *mesh_get_derived_deform(
        struct Scene *scene, struct Object *ob,
        CustomDataMask dataMask);

struct DerivedMesh *mesh_create_derived_for_modifier(
        struct Scene *scene, struct Object *ob,
        struct ModifierData *md, int build_shapekey_layers);

struct DerivedMesh *mesh_create_derived_render(
        struct Scene *scene, struct Object *ob,
        CustomDataMask dataMask);

struct DerivedMesh *getEditDerivedBMesh(
        struct BMEditMesh *em, struct Object *ob,
        float (*vertexCos)[3]);

struct DerivedMesh *mesh_create_derived_index_render(
        struct Scene *scene, struct Object *ob,
        CustomDataMask dataMask, int index);

/* same as above but wont use render settings */
struct DerivedMesh *mesh_create_derived(struct Mesh *me, float (*vertCos)[3]);
struct DerivedMesh *mesh_create_derived_view(
        struct Scene *scene, struct Object *ob,
        CustomDataMask dataMask);
struct DerivedMesh *mesh_create_derived_no_deform(
        struct Scene *scene, struct Object *ob,
        float (*vertCos)[3],
        CustomDataMask dataMask);
struct DerivedMesh *mesh_create_derived_no_deform_render(
        struct Scene *scene, struct Object *ob,
        float (*vertCos)[3],
        CustomDataMask dataMask);
/* for gameengine */
struct DerivedMesh *mesh_create_derived_no_virtual(
        struct Scene *scene, struct Object *ob, float (*vertCos)[3],
        CustomDataMask dataMask);
struct DerivedMesh *mesh_create_derived_physics(
        struct Scene *scene, struct Object *ob, float (*vertCos)[3],
        CustomDataMask dataMask);

struct DerivedMesh *editbmesh_get_derived_base(
        struct Object *, struct BMEditMesh *em);
struct DerivedMesh *editbmesh_get_derived_cage(
        struct Scene *scene, struct Object *,
        struct BMEditMesh *em, CustomDataMask dataMask);
struct DerivedMesh *editbmesh_get_derived_cage_and_final(
        struct Scene *scene, struct Object *,
        struct BMEditMesh *em, CustomDataMask dataMask,
        struct DerivedMesh **r_final);

struct DerivedMesh *object_get_derived_final(struct Object *ob, const bool for_render);

float (*editbmesh_get_vertex_cos(struct BMEditMesh *em, int *r_numVerts))[3];
bool editbmesh_modifier_is_enabled(struct Scene *scene, struct ModifierData *md, struct DerivedMesh *dm);
void makeDerivedMesh(
        struct Scene *scene, struct Object *ob, struct BMEditMesh *em,
        CustomDataMask dataMask, const bool build_shapekey_layers);

void BKE_object_eval_mesh(struct EvaluationContext *eval_ctx, struct Scene *scene, struct Object *ob);
void BKE_object_eval_editmesh(struct EvaluationContext *eval_ctx, struct Scene *scene, struct Object *ob);

void weight_to_rgb(float r_rgb[3], const float weight);
/** Update the weight MCOL preview layer.
 * If weights are NULL, use object's active vgroup(s).
 * Else, weights must be an array of weight float values.
 *     If indices is NULL, it must be of numVerts length.
 *     Else, it must be of num length, as indices, which contains vertices' idx to apply weights to.
 *         (other vertices are assumed zero weight).
 */
void DM_update_weight_mcol(
        struct Object *ob, struct DerivedMesh *dm, int const draw_flag,
        float *weights, int num, const int *indices);

#endif  /* __BKE_MODIFIER_CALC_H__ */
