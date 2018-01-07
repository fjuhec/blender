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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/mesh_sample.c
 *  \ingroup bke
 *
 * Sample a mesh surface or volume and evaluate samples on deformed meshes.
 */

#include <limits.h>

#include "MEM_guardedalloc.h"

#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_utildefines.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_sort.h"
#include "BLI_task.h"

#include "BKE_bvhutils.h"
#include "BKE_mesh_sample.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"

#include "BLI_strict_flags.h"

#define SAMPLE_INDEX_INVALID 0xFFFFFFFF

#define DEFAULT_TASK_SIZE 1024

/* ==== Utility Functions ==== */

static float calc_mesh_area(DerivedMesh *dm)
{
	int numtris = dm->getNumLoopTri(dm);
	MVert *mverts = dm->getVertArray(dm);
	MLoop *mloops = dm->getLoopArray(dm);
	
	float totarea = 0.0;
	const MLoopTri *tri = dm->getLoopTriArray(dm);
	for (int i = 0; i < numtris; ++i, ++tri) {
		unsigned int index1 = mloops[tri->tri[0]].v;
		unsigned int index2 = mloops[tri->tri[1]].v;
		unsigned int index3 = mloops[tri->tri[2]].v;
		MVert *v1 = &mverts[index1];
		MVert *v2 = &mverts[index2];
		MVert *v3 = &mverts[index3];
		totarea += area_tri_v3(v1->co, v2->co, v3->co);
	}
	
	return totarea;
}

BLI_INLINE float triangle_weight(DerivedMesh *dm, const MLoopTri *tri, const float *vert_weights, float *r_area)
{
	MVert *mverts = dm->getVertArray(dm);
	MLoop *mloops = dm->getLoopArray(dm);
	unsigned int index1 = mloops[tri->tri[0]].v;
	unsigned int index2 = mloops[tri->tri[1]].v;
	unsigned int index3 = mloops[tri->tri[2]].v;
	MVert *v1 = &mverts[index1];
	MVert *v2 = &mverts[index2];
	MVert *v3 = &mverts[index3];
	
	float weight = area_tri_v3(v1->co, v2->co, v3->co);
	if (r_area) {
		*r_area = weight;
	}
	
	if (vert_weights) {
		float w1 = vert_weights[index1];
		float w2 = vert_weights[index2];
		float w3 = vert_weights[index3];
		
		weight *= (w1 + w2 + w3) / 3.0f;
	}
	
	return weight;
}

float* BKE_mesh_sample_calc_triangle_weights(DerivedMesh *dm, MeshSampleVertexWeightFp vertex_weight_cb, void *userdata, float *r_area)
{
	int numverts = dm->getNumVerts(dm);
	int numtris = dm->getNumLoopTri(dm);
	int numweights = numtris;
	
	float *vert_weights = NULL;
	if (vertex_weight_cb) {
		vert_weights = MEM_mallocN(sizeof(float) * (size_t)numverts, "mesh sample vertex weights");
		{
			MVert *mv = dm->getVertArray(dm);
			for (int i = 0; i < numtris; ++i, ++mv) {
				vert_weights[i] = vertex_weight_cb(dm, mv, (unsigned int)i, userdata);
			}
		}
	}
	
	float *tri_weights = MEM_mallocN(sizeof(float) * (size_t)numweights, "mesh sample triangle weights");
	/* accumulate weights */
	float totarea = 0.0;
	float totweight = 0.0f;
	{
		const MLoopTri *mt = dm->getLoopTriArray(dm);
		for (int i = 0; i < numtris; ++i, ++mt) {
			tri_weights[i] = totweight;
			
			float triarea;
			float triweight = triangle_weight(dm, mt, vert_weights, &triarea);
			totarea += triarea;
			totweight += triweight;
		}
	}
	
	if (vert_weights) {
		MEM_freeN(vert_weights);
	}
	
	/* normalize */
	if (totweight > 0.0f) {
		float norm = 1.0f / totweight;
		const MLoopTri *mt = dm->getLoopTriArray(dm);
		for (int i = 0; i < numtris; ++i, ++mt) {
			tri_weights[i] *= norm;
		}
	}
	else {
		/* invalid weights, remove to avoid invalid binary search */
		MEM_freeN(tri_weights);
		tri_weights = NULL;
	}
	
	if (r_area) {
		*r_area = totarea;
	}
	return tri_weights;
}

void BKE_mesh_sample_weights_from_loc(MeshSample *sample, DerivedMesh *dm, int face_index, const float loc[3])
{
	MFace *face = &dm->getTessFaceArray(dm)[face_index];
	unsigned int index[4] = { face->v1, face->v2, face->v3, face->v4 };
	MVert *mverts = dm->getVertArray(dm);
	
	float *v1 = mverts[face->v1].co;
	float *v2 = mverts[face->v2].co;
	float *v3 = mverts[face->v3].co;
	float *v4 = face->v4 ? mverts[face->v4].co : NULL;
	float w[4];
	int tri[3];
	
	interp_weights_quad_v3_index(tri, w, v1, v2, v3, v4, loc);
	
	sample->orig_verts[0] = index[tri[0]];
	sample->orig_verts[1] = index[tri[1]];
	sample->orig_verts[2] = index[tri[2]];
	sample->orig_weights[0] = w[tri[0]];
	sample->orig_weights[1] = w[tri[1]];
	sample->orig_weights[2] = w[tri[2]];
}

/* ==== Evaluate ==== */

bool BKE_mesh_sample_is_valid(const struct MeshSample *sample)
{
	const unsigned int *v = sample->orig_verts;
	
	if (BKE_mesh_sample_is_volume_sample(sample))
	{
		/* volume sample stores position in the weight vector directly */
		return true;
	}
	
	if (v[0] == SAMPLE_INDEX_INVALID || v[1] == SAMPLE_INDEX_INVALID || v[2] == SAMPLE_INDEX_INVALID)
	{
		/* must have 3 valid indices */
		return false;
	}
	
	return true;
}

bool BKE_mesh_sample_is_volume_sample(const MeshSample *sample)
{
	const unsigned int *v = sample->orig_verts;
	return v[0] == SAMPLE_INDEX_INVALID && v[1] == SAMPLE_INDEX_INVALID && v[2] == SAMPLE_INDEX_INVALID;
}

bool BKE_mesh_sample_eval(DerivedMesh *dm, const MeshSample *sample, float loc[3], float nor[3], float tang[3])
{
	MVert *mverts = dm->getVertArray(dm);
	unsigned int totverts = (unsigned int)dm->getNumVerts(dm);
	MVert *v1, *v2, *v3;
	
	zero_v3(loc);
	zero_v3(nor);
	zero_v3(tang);
	
	if (BKE_mesh_sample_is_volume_sample(sample)) {
		/* VOLUME SAMPLE */
		
		if (is_zero_v3(sample->orig_weights))
			return false;
		
		copy_v3_v3(loc, sample->orig_weights);
		return true;
	}
	else {
		/* SURFACE SAMPLE */
		if (sample->orig_verts[0] >= totverts ||
		    sample->orig_verts[1] >= totverts ||
		    sample->orig_verts[2] >= totverts)
			return false;
		
		v1 = &mverts[sample->orig_verts[0]];
		v2 = &mverts[sample->orig_verts[1]];
		v3 = &mverts[sample->orig_verts[2]];
		
		{ /* location */
			madd_v3_v3fl(loc, v1->co, sample->orig_weights[0]);
			madd_v3_v3fl(loc, v2->co, sample->orig_weights[1]);
			madd_v3_v3fl(loc, v3->co, sample->orig_weights[2]);
		}
		
		{ /* normal */
			float vnor[3];
			
			normal_short_to_float_v3(vnor, v1->no);
			madd_v3_v3fl(nor, vnor, sample->orig_weights[0]);
			normal_short_to_float_v3(vnor, v2->no);
			madd_v3_v3fl(nor, vnor, sample->orig_weights[1]);
			normal_short_to_float_v3(vnor, v3->no);
			madd_v3_v3fl(nor, vnor, sample->orig_weights[2]);
			
			normalize_v3(nor);
		}
		
		{ /* tangent */
			float edge[3];
			
			/* XXX simply using the v1-v2 edge as a tangent vector for now ...
			 * Eventually mikktspace generated tangents (CD_TANGENT tessface layer)
			 * should be used for consistency, but requires well-defined tessface
			 * indices for the mesh surface samples.
			 */
			
			sub_v3_v3v3(edge, v2->co, v1->co);
			/* make edge orthogonal to nor */
			madd_v3_v3fl(edge, nor, -dot_v3v3(edge, nor));
			normalize_v3_v3(tang, edge);
		}
		
		return true;
	}
}

bool BKE_mesh_sample_shapekey(Key *key, KeyBlock *kb, const MeshSample *sample, float loc[3])
{
	float *v1, *v2, *v3;

	(void)key;  /* Unused in release builds. */

	BLI_assert(key->elemsize == 3 * sizeof(float));
	BLI_assert(sample->orig_verts[0] < (unsigned int)kb->totelem);
	BLI_assert(sample->orig_verts[1] < (unsigned int)kb->totelem);
	BLI_assert(sample->orig_verts[2] < (unsigned int)kb->totelem);
	
	v1 = (float *)kb->data + sample->orig_verts[0] * 3;
	v2 = (float *)kb->data + sample->orig_verts[1] * 3;
	v3 = (float *)kb->data + sample->orig_verts[2] * 3;
	
	zero_v3(loc);
	madd_v3_v3fl(loc, v1, sample->orig_weights[0]);
	madd_v3_v3fl(loc, v2, sample->orig_weights[1]);
	madd_v3_v3fl(loc, v3, sample->orig_weights[2]);
	
	/* TODO use optional vgroup weights to determine if a shapeky actually affects the sample */
	return true;
}

void BKE_mesh_sample_clear(MeshSample *sample)
{
	memset(sample, 0, sizeof(MeshSample));
}


/* ==== Generator Types ==== */

typedef void (*GeneratorFreeFp)(struct MeshSampleGenerator *gen);

typedef void (*GeneratorBindFp)(struct MeshSampleGenerator *gen);
typedef void (*GeneratorUnbindFp)(struct MeshSampleGenerator *gen);

typedef void* (*GeneratorThreadContextCreateFp)(const struct MeshSampleGenerator *gen, int start);
typedef void (*GeneratorThreadContextFreeFp)(const struct MeshSampleGenerator *gen, void *thread_ctx);
typedef bool (*GeneratorMakeSampleFp)(const struct MeshSampleGenerator *gen, void *thread_ctx, struct MeshSample *sample);
typedef unsigned int (*GeneratorGetMaxSamplesFp)(const struct MeshSampleGenerator *gen);

typedef struct MeshSampleGenerator
{
	GeneratorFreeFp free;
	
	GeneratorBindFp bind;
	GeneratorUnbindFp unbind;
	
	GeneratorThreadContextCreateFp thread_context_create;
	GeneratorThreadContextFreeFp thread_context_free;
	GeneratorMakeSampleFp make_sample;
	GeneratorGetMaxSamplesFp get_max_samples;
	
	/* bind target */
	DerivedMesh *dm;
	
	void *default_ctx;
	int task_size;
} MeshSampleGenerator;

static void sample_generator_init(MeshSampleGenerator *gen,
                                  GeneratorFreeFp free,
                                  GeneratorBindFp bind,
                                  GeneratorUnbindFp unbind,
                                  GeneratorThreadContextCreateFp thread_context_create,
                                  GeneratorThreadContextFreeFp thread_context_free,
                                  GeneratorMakeSampleFp make_sample,
                                  GeneratorGetMaxSamplesFp get_max_samples)
{
	gen->free = free;
	gen->bind = bind;
	gen->unbind = unbind;
	gen->thread_context_create = thread_context_create;
	gen->thread_context_free = thread_context_free;
	gen->make_sample = make_sample;
	gen->get_max_samples = get_max_samples;
	
	gen->dm = NULL;
	
	gen->default_ctx = NULL;
	gen->task_size = DEFAULT_TASK_SIZE;
}

/* ------------------------------------------------------------------------- */

typedef struct MSurfaceSampleGenerator_Vertices {
	MeshSampleGenerator base;
	
	/* bind data */
	int (*vert_loop_map)[3];
} MSurfaceSampleGenerator_Vertices;

static void generator_vertices_free(MSurfaceSampleGenerator_Vertices *gen)
{
	MEM_freeN(gen);
}

static void generator_vertices_bind(MSurfaceSampleGenerator_Vertices *gen)
{
	DerivedMesh *dm = gen->base.dm;
	const int num_verts = dm->getNumVerts(dm);
	
	DM_ensure_normals(dm);
	
	int (*vert_loop_map)[3] = MEM_mallocN(sizeof(int) * 3 * (unsigned int)num_verts, "vertex loop map");
	for (int i = 0; i < num_verts; ++i) {
		vert_loop_map[i][0] = -1;
		vert_loop_map[i][1] = -1;
		vert_loop_map[i][2] = -1;
	}
	
	const int num_polys = dm->getNumPolys(dm);
	const MLoop *mloops = dm->getLoopArray(dm);
	const MPoly *mp = dm->getPolyArray(dm);
	for (int i = 0; i < num_polys; ++i, ++mp) {
		if (mp->totloop < 3) {
			continue;
		}
		
		const MLoop *ml = mloops + mp->loopstart;
		for (int k = 0; k < mp->totloop; ++k, ++ml) {
			int *vmap = vert_loop_map[ml->v];
			if (vmap[0] < 0) {
				vmap[0] = mp->loopstart + k;
				vmap[1] = mp->loopstart + (k + 1) % mp->totloop;
				vmap[2] = mp->loopstart + (k + 2) % mp->totloop;
			}
		}
	}
	
	gen->vert_loop_map = vert_loop_map;
}

static void generator_vertices_unbind(MSurfaceSampleGenerator_Vertices *gen)
{
	if (gen->vert_loop_map) {
		MEM_freeN(gen->vert_loop_map);
	}
}

static void* generator_vertices_thread_context_create(const MSurfaceSampleGenerator_Vertices *UNUSED(gen), int start)
{
	int *cur_vert = MEM_callocN(sizeof(int), "generator_vertices_thread_context");
	*cur_vert = start;
	return cur_vert;
}

static void generator_vertices_thread_context_free(const MSurfaceSampleGenerator_Vertices *UNUSED(gen), void *thread_ctx)
{
	MEM_freeN(thread_ctx);
}

static bool generator_vertices_make_loop_sample(DerivedMesh *dm, const int *loops, MeshSample *sample)
{
	const MLoop *mloops = dm->getLoopArray(dm);
	
	if (loops[0] < 0) {
		return false;
	}
	
	sample->orig_poly = -1;
	
	sample->orig_loops[0] = (unsigned int)loops[0];
	sample->orig_loops[1] = (unsigned int)loops[1];
	sample->orig_loops[2] = (unsigned int)loops[2];
	
	sample->orig_verts[0] = mloops[loops[0]].v;
	sample->orig_verts[1] = mloops[loops[1]].v;
	sample->orig_verts[2] = mloops[loops[2]].v;
	
	sample->orig_weights[0] = 1.0f;
	sample->orig_weights[1] = 0.0f;
	sample->orig_weights[2] = 0.0f;
	
	return true;
}

static bool generator_vertices_make_sample(const MSurfaceSampleGenerator_Vertices *gen, void *thread_ctx, MeshSample *sample)
{
	DerivedMesh *dm = gen->base.dm;
	const int num_verts = dm->getNumVerts(dm);
	
	int cur_vert = *(int *)thread_ctx;
	bool found_vert = false;
	for (; cur_vert < num_verts && !found_vert; ++cur_vert) {
		found_vert |= generator_vertices_make_loop_sample(dm, gen->vert_loop_map[cur_vert], sample);
	}
	
	*(int *)thread_ctx = cur_vert;
	return found_vert;
}

static unsigned int generator_vertices_get_max_samples(const MSurfaceSampleGenerator_Vertices *gen)
{
	return (unsigned int)gen->base.dm->getNumVerts(gen->base.dm);
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_vertices(void)
{
	MSurfaceSampleGenerator_Vertices *gen;
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_Vertices), "MSurfaceSampleGenerator_Vertices");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_vertices_free,
	                      (GeneratorBindFp)generator_vertices_bind,
	                      (GeneratorUnbindFp) generator_vertices_unbind,
	                      (GeneratorThreadContextCreateFp)generator_vertices_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_vertices_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_vertices_make_sample,
	                      (GeneratorGetMaxSamplesFp)generator_vertices_get_max_samples);
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

//#define USE_DEBUG_COUNT

typedef struct MSurfaceSampleGenerator_Random {
	MeshSampleGenerator base;
	
	unsigned int seed;
	bool use_area_weight;
	MeshSampleVertexWeightFp vertex_weight_cb;
	void *userdata;
	
	/* bind data */
	float *tri_weights;
	float *vertex_weights;
	
#ifdef USE_DEBUG_COUNT
	int *debug_count;
#endif
} MSurfaceSampleGenerator_Random;

static void generator_random_free(MSurfaceSampleGenerator_Random *gen)
{
	MEM_freeN(gen);
}

static void generator_random_bind(MSurfaceSampleGenerator_Random *gen)
{
	DerivedMesh *dm = gen->base.dm;
	
	DM_ensure_normals(dm);
	
	if (gen->use_area_weight) {
		gen->tri_weights = BKE_mesh_sample_calc_triangle_weights(dm, gen->vertex_weight_cb, gen->userdata, NULL);
		
#ifdef USE_DEBUG_COUNT
		gen->debug_count = MEM_callocN(sizeof(int) * (size_t)dm->getNumLoopTri(dm), "surface sample debug counts");
#endif
	}
}

static void generator_random_unbind(MSurfaceSampleGenerator_Random *gen)
{
#ifdef USE_DEBUG_COUNT
	if (gen->debug_count) {
		if (gen->tri_weights) {
			int num = gen->dm->getNumLoopTri(gen->dm);
			int i;
			int totsamples = 0;
			
			printf("Surface Sampling (n=%d):\n", num);
			for (i = 0; i < num; ++i)
				totsamples += gen->debug_count[i];
			
			for (i = 0; i < num; ++i) {
				float weight = i > 0 ? gen->tri_weights[i] - gen->tri_weights[i-1] : gen->tri_weights[i];
				int samples = gen->debug_count[i];
				printf("  %d: W = %f, N = %d/%d = %f\n", i, weight, samples, totsamples, (float)samples / (float)totsamples);
			}
		}
		MEM_freeN(gen->debug_count);
	}
#endif
	if (gen->tri_weights)
		MEM_freeN(gen->tri_weights);
	if (gen->vertex_weights)
		MEM_freeN(gen->vertex_weights);
}

static void* generator_random_thread_context_create(const MSurfaceSampleGenerator_Random *gen, int start)
{
	RNG *rng = BLI_rng_new(gen->seed);
	// 3 RNG gets per sample
	BLI_rng_skip(rng, start * 3);
	return rng;
}

static void generator_random_thread_context_free(const MSurfaceSampleGenerator_Random *UNUSED(gen), void *thread_ctx)
{
	BLI_rng_free(thread_ctx);
}

/* Find the index in "sum" array before "value" is crossed. */
BLI_INLINE int weight_array_binary_search(const float *sum, int size, float value)
{
	int mid, low = 0, high = size - 1;
	
	if (value <= 0.0f)
		return 0;
	
	while (low < high) {
		mid = (low + high) >> 1;
		
		if (sum[mid] < value && value <= sum[mid+1])
			return mid;
		
		if (sum[mid] >= value)
			high = mid - 1;
		else if (sum[mid] < value)
			low = mid + 1;
		else
			return mid;
	}
	
	return low;
}

static bool generator_random_make_sample(const MSurfaceSampleGenerator_Random *gen, void *thread_ctx, MeshSample *sample)
{
	DerivedMesh *dm = gen->base.dm;
	RNG *rng = thread_ctx;
	const MLoop *mloops = dm->getLoopArray(dm);
	const MLoopTri *mtris = dm->getLoopTriArray(dm);
	int tottris = dm->getNumLoopTri(dm);
	int totweights = tottris;
	
	int triindex;
	float a, b;
	const MLoopTri *mtri;
	
	if (gen->tri_weights)
		triindex = weight_array_binary_search(gen->tri_weights, totweights, BLI_rng_get_float(rng));
	else
		triindex = BLI_rng_get_int(rng) % totweights;
#ifdef USE_DEBUG_COUNT
	if (gen->debug_count)
		gen->debug_count[triindex] += 1;
#endif
	a = BLI_rng_get_float(rng);
	b = BLI_rng_get_float(rng);
	
	mtri = &mtris[triindex];
	
	sample->orig_verts[0] = mloops[mtri->tri[0]].v;
	sample->orig_verts[1] = mloops[mtri->tri[1]].v;
	sample->orig_verts[2] = mloops[mtri->tri[2]].v;
	
	if (a + b > 1.0f) {
		a = 1.0f - a;
		b = 1.0f - b;
	}
	sample->orig_weights[0] = 1.0f - (a + b);
	sample->orig_weights[1] = a;
	sample->orig_weights[2] = b;
	
	return true;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_random(unsigned int seed, bool use_area_weight,
                                                        MeshSampleVertexWeightFp vertex_weight_cb, void *userdata)
{
	MSurfaceSampleGenerator_Random *gen;
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_Random), "MSurfaceSampleGenerator_Random");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_random_free,
	                      (GeneratorBindFp)generator_random_bind,
	                      (GeneratorUnbindFp) generator_random_unbind,
	                      (GeneratorThreadContextCreateFp)generator_random_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_random_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_random_make_sample,
	                      NULL);
	
	gen->seed = seed;
	gen->use_area_weight = use_area_weight;
	gen->vertex_weight_cb = vertex_weight_cb;
	gen->userdata = userdata;
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

typedef struct MSurfaceSampleGenerator_RayCast {
	MeshSampleGenerator base;
	
	MeshSampleRayFp ray_cb;
	MeshSampleThreadContextCreateFp thread_context_create_cb;
	MeshSampleThreadContextFreeFp thread_context_free_cb;
	void *userdata;
	
	/* bind data */
	BVHTreeFromMesh bvhdata;
} MSurfaceSampleGenerator_RayCast;

static void generator_raycast_free(MSurfaceSampleGenerator_RayCast *gen)
{
	MEM_freeN(gen);
}

static void generator_raycast_bind(MSurfaceSampleGenerator_RayCast *gen)
{
	DerivedMesh *dm = gen->base.dm;
	
	DM_ensure_tessface(dm);
	
	memset(&gen->bvhdata, 0, sizeof(gen->bvhdata));
	
	if (dm->getNumTessFaces(dm) == 0)
		return;
	
	bvhtree_from_mesh_faces(&gen->bvhdata, dm, 0.0f, 4, 6);
}

static void generator_raycast_unbind(MSurfaceSampleGenerator_RayCast *gen)
{
	free_bvhtree_from_mesh(&gen->bvhdata);
}

static void* generator_raycast_thread_context_create(const MSurfaceSampleGenerator_RayCast *gen, int start)
{
	if (gen->thread_context_create_cb) {
		return gen->thread_context_create_cb(gen->userdata, start);
	}
	else {
		return NULL;
	}
}

static void generator_raycast_thread_context_free(const MSurfaceSampleGenerator_RayCast *gen, void *thread_ctx)
{
	if (gen->thread_context_free_cb) {
		return gen->thread_context_free_cb(gen->userdata, thread_ctx);
	}
}

static bool generator_raycast_make_sample(const MSurfaceSampleGenerator_RayCast *gen, void *thread_ctx, MeshSample *sample)
{
	float ray_start[3], ray_end[3], ray_dir[3], dist;
	BVHTreeRayHit hit;
	
	if (!gen->ray_cb(gen->userdata, thread_ctx, ray_start, ray_end))
		return false;
	
	sub_v3_v3v3(ray_dir, ray_end, ray_start);
	dist = normalize_v3(ray_dir);
	
	hit.index = -1;
	hit.dist = dist;

	if (BLI_bvhtree_ray_cast(gen->bvhdata.tree, ray_start, ray_dir, 0.0f,
	                         &hit, gen->bvhdata.raycast_callback, (BVHTreeFromMesh *)(&gen->bvhdata)) >= 0) {
		
		BKE_mesh_sample_weights_from_loc(sample, gen->base.dm, hit.index, hit.co);
		
		return true;
	}
	else
		return false;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_raycast(
        MeshSampleThreadContextCreateFp thread_context_create_cb,
        MeshSampleThreadContextFreeFp thread_context_free_cb,
        MeshSampleRayFp ray_cb,
        void *userdata)
{
	MSurfaceSampleGenerator_RayCast *gen;
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_RayCast), "MSurfaceSampleGenerator_RayCast");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_raycast_free,
	                      (GeneratorBindFp)generator_raycast_bind,
	                      (GeneratorUnbindFp) generator_raycast_unbind,
	                      (GeneratorThreadContextCreateFp)generator_raycast_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_raycast_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_raycast_make_sample,
	                      NULL);
	
	gen->thread_context_create_cb = thread_context_create_cb;
	gen->thread_context_free_cb = thread_context_free_cb;
	gen->ray_cb = ray_cb;
	gen->userdata = userdata;
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

#define MAX_CIRCLE_PACKING 0.906899682
#define SQRT_3 1.732050808

typedef struct IndexedMeshSample {
	unsigned int orig_verts[3];
	float orig_weights[3];
	float co[3];
	int cell_index[3];
} IndexedMeshSample;

typedef struct MSurfaceSampleGenerator_PoissonDisk {
	MeshSampleGenerator base;
	
	MeshSampleGenerator *uniform_gen;
	unsigned int max_samples;
	float mindist_squared;
	/* Size of grid cells is mindist/sqrt(3),
	 * so that each cell contains at most one valid sample.
	 */
	float cellsize;
	/* Transform mesh space to grid space */
	float grid_scale;
	
	/* bind data */

	/* offset and size of the grid */
	int grid_offset[3];
	int grid_size[3];
	
	IndexedMeshSample *uniform_samples;
	unsigned int num_uniform_samples;
	
	struct GHash *cell_table;
} MSurfaceSampleGenerator_PoissonDisk;

typedef struct MeshSampleCell {
	int cell_index[3];
	unsigned int sample_start;
	unsigned int sample;
} MeshSampleCell;

typedef struct MSurfaceSampleGenerator_PoissonDisk_ThreadContext {
	unsigned int trial;
	GHashIterator iter;
} MSurfaceSampleGenerator_PoissonDisk_ThreadContext;

BLI_INLINE void poissondisk_loc_from_grid(const MSurfaceSampleGenerator_PoissonDisk *gen, float loc[3], const int grid[3])
{
	copy_v3_fl3(loc, grid[0] + gen->grid_offset[0], grid[1] + gen->grid_offset[1], grid[2] + gen->grid_offset[2]);
	mul_v3_fl(loc, gen->cellsize);
}

BLI_INLINE void poissondisk_grid_from_loc(const MSurfaceSampleGenerator_PoissonDisk *gen, int grid[3], const float loc[3])
{
	float gridco[3];
	mul_v3_v3fl(gridco, loc, gen->grid_scale);
	grid[0] = (int)floorf(gridco[0]) - gen->grid_offset[0];
	grid[1] = (int)floorf(gridco[1]) - gen->grid_offset[1];
	grid[2] = (int)floorf(gridco[2]) - gen->grid_offset[2];
}

static void generator_poissondisk_free(MSurfaceSampleGenerator_PoissonDisk *gen)
{
	BKE_mesh_sample_free_generator(gen->uniform_gen);
	MEM_freeN(gen);
}

static void generator_poissondisk_uniform_sample_eval(void *userdata, const int iter)
{
	void *(*ptrs)[3] = userdata;
	MSurfaceSampleGenerator_PoissonDisk *gen = (*ptrs)[0];
	const MeshSample *samples = (*ptrs)[1];
	DerivedMesh *dm = (*ptrs)[2];
	
	IndexedMeshSample *isample = &gen->uniform_samples[iter];
	const MeshSample *sample = &samples[iter];
	
	memcpy(isample->orig_verts, sample->orig_verts, sizeof(isample->orig_verts));
	memcpy(isample->orig_weights, sample->orig_weights, sizeof(isample->orig_weights));
	float nor[3], tang[3];
	BKE_mesh_sample_eval(dm, sample, isample->co, nor, tang);
	
	poissondisk_grid_from_loc(gen, isample->cell_index, isample->co);
}

BLI_INLINE void copy_cell_index(int r[3], const int a[3])
{
	r[0] = a[0];
	r[1] = a[1];
	r[2] = a[2];
}

BLI_INLINE int cmp_cell_index(const int a[3], const int b[3])
{
	int d0 = a[0] - b[0];
	int d1 = a[1] - b[1];
	int d2 = a[2] - b[2];
	if (d0 == 0)
	{
		if (d1 == 0)
		{
			if (d2 == 0)
			{
				return 0;
			}
			else
			{
				return d2 > 0 ? 1 : -1;
			}
		}
		else
		{
			return d1 > 0 ? 1 : -1;
		}
	}
	else
	{
		return d0 > 0 ? 1 : -1;
	}
}

static int cmp_indexed_mesh_sample(const void *a, const void *b)
{
	return cmp_cell_index(((const IndexedMeshSample *)a)->cell_index, ((const IndexedMeshSample *)b)->cell_index);
}

BLI_INLINE bool cell_index_eq(const int *a, const int *b)
{
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

/* hash key function */
static unsigned int cell_hash_key(const void *key)
{
	const int *cell_index = (const int *)key;
	unsigned int hash0 = BLI_ghashutil_inthash(cell_index[0]);
	unsigned int hash1 = BLI_ghashutil_inthash(cell_index[1]);
	unsigned int hash2 = BLI_ghashutil_inthash(cell_index[2]);
	return BLI_ghashutil_combine_hash(hash0, BLI_ghashutil_combine_hash(hash1, hash2));
}

/* hash function: return false when equal */
static bool cell_hash_neq(const void *a, const void *b)
{
	return !cell_index_eq((const int *)a, (const int *)b);
}

static unsigned int generator_poissondisk_get_max_samples(const MSurfaceSampleGenerator_PoissonDisk *gen)
{
	static const unsigned int hard_max = UINT_MAX;
	
	const double usable_area = calc_mesh_area(gen->base.dm) * MAX_CIRCLE_PACKING;
	const double circle_area = M_PI * gen->mindist_squared;
	if (circle_area * (double)hard_max < usable_area) {
		return hard_max;
	}
	
	return (unsigned int)(usable_area / circle_area);
}

static void generator_poissondisk_bind(MSurfaceSampleGenerator_PoissonDisk *gen)
{
	DerivedMesh *dm = gen->base.dm;
	static const unsigned int uniform_sample_ratio = 10;
	
	// Determine cell size
	{
		float min[3], max[3];
		INIT_MINMAX(min, max);
		dm->getMinMax(dm, min, max);
		mul_v3_fl(min, gen->grid_scale);
		mul_v3_fl(max, gen->grid_scale);
		/* grid size gets an empty 2 cell margin to simplify neighbor lookups */
		gen->grid_offset[0] = (int)floorf(min[0]) - 2;
		gen->grid_offset[1] = (int)floorf(min[1]) - 2;
		gen->grid_offset[2] = (int)floorf(min[2]) - 2;
		gen->grid_size[0] = (int)floorf(max[0]) - gen->grid_offset[0] + 4;
		gen->grid_size[1] = (int)floorf(max[1]) - gen->grid_offset[1] + 4;
		gen->grid_size[2] = (int)floorf(max[2]) - gen->grid_offset[2] + 4;
	}
	
	// Generate initial uniform random point set
	unsigned int max_pd_samples = generator_poissondisk_get_max_samples(gen);
	gen->num_uniform_samples = MIN2(max_pd_samples * uniform_sample_ratio, gen->max_samples);
	if (gen->num_uniform_samples > 0) {
		BKE_mesh_sample_generator_bind(gen->uniform_gen, dm);
		
		gen->uniform_samples = MEM_mallocN(sizeof(IndexedMeshSample) * gen->num_uniform_samples, "poisson disk uniform samples");
		
		MeshSample *samples = MEM_mallocN(sizeof(MeshSample) * gen->num_uniform_samples, "poisson disk uniform samples");
		BKE_mesh_sample_generate_batch(gen->uniform_gen, samples, (int)gen->num_uniform_samples);
		void *ptrs[3] = { gen, samples, dm };
		BLI_task_parallel_range(0, (int)gen->num_uniform_samples, &ptrs, generator_poissondisk_uniform_sample_eval, true);
		MEM_freeN(samples);
		
		BKE_mesh_sample_generator_unbind(gen->uniform_gen);
	}
	
	// Sort points by cell hash
	{
		qsort(gen->uniform_samples, gen->num_uniform_samples, sizeof(IndexedMeshSample), cmp_indexed_mesh_sample);
	}
	
	// Build a hash table for indexing cells
	{
		gen->cell_table = BLI_ghash_new(cell_hash_key, cell_hash_neq, "MeshSampleCell hash table");
		int cur_cell_index[3] = {-1, -1, -1};
		const IndexedMeshSample *sample = gen->uniform_samples;
		for (unsigned int i = 0; i < gen->num_uniform_samples; ++i, ++sample) {
			BLI_assert(cmp_cell_index(cur_cell_index, sample->cell_index) <= 0);
			if (cmp_cell_index(cur_cell_index, sample->cell_index) < 0) {
				copy_cell_index(cur_cell_index, sample->cell_index);
				
				MeshSampleCell *cell = MEM_mallocN(sizeof(*cell), "MeshSampleCell");
				copy_cell_index(cell->cell_index, cur_cell_index);
				cell->sample_start = (unsigned int)i;
				cell->sample = SAMPLE_INDEX_INVALID;
				BLI_ghash_insert(gen->cell_table, cell->cell_index, cell);
			}
		}
	}
	
#if 0
	for (unsigned int i = 0; i < gen->num_uniform_samples; ++i) {
		const IndexedMeshSample *s = &gen->uniform_samples[i];
		printf("%d: (%.3f, %.3f, %.3f) | %d\n", i, s->co[0], s->co[1], s->co[2], (int)s->cell_hash);
	}
#endif
}

static void generator_poissondisk_unbind(MSurfaceSampleGenerator_PoissonDisk *gen)
{
	if (gen->cell_table) {
		BLI_ghash_free(gen->cell_table, NULL, MEM_freeN);
	}
	
	if (gen->uniform_samples) {
		MEM_freeN(gen->uniform_samples);
	}
}

static void* generator_poissondisk_thread_context_create(const MSurfaceSampleGenerator_PoissonDisk *gen, int UNUSED(start))
{
	MSurfaceSampleGenerator_PoissonDisk_ThreadContext *ctx = MEM_mallocN(sizeof(*ctx), "thread context");
	ctx->trial = 0;
	BLI_ghashIterator_init(&ctx->iter, gen->cell_table);
	return ctx;
}

static void generator_poissondisk_thread_context_free(const MSurfaceSampleGenerator_PoissonDisk *UNUSED(gen), void *thread_ctx)
{
	MEM_freeN(thread_ctx);
}

static bool generator_poissondisk_make_sample(const MSurfaceSampleGenerator_PoissonDisk *gen, void *thread_ctx, MeshSample *sample)
{
	static const unsigned int max_trials = 5;
	
	MSurfaceSampleGenerator_PoissonDisk_ThreadContext *ctx = thread_ctx;
	
	// Offset of cells whose samples can potentially overlap a given cell
	// Four corners are excluded because their samples can never overlap
	const int neighbors[][3] = {
	                  {-1, -2, -2}, { 0, -2, -2}, { 1, -2, -2},
	    {-2, -1, -2}, {-1, -1, -2}, { 0, -1, -2}, { 1, -1, -2}, { 2, -1, -2},
	    {-2,  0, -2}, {-1,  0, -2}, { 0,  0, -2}, { 1,  0, -2}, { 2,  0, -2},
	    {-2,  1, -2}, {-1,  1, -2}, { 0,  1, -2}, { 1, -2, -2}, { 2,  1, -2},
	    {-2,  2, -2}, {-1,  2, -2}, { 0,  2, -2}, { 1, -2, -2}, { 2,  2, -2},

	    {-2, -2, -1}, {-1, -2, -1}, { 0, -2, -1}, { 1, -2, -1}, { 2, -2, -1},
	    {-2, -1, -1}, {-1, -1, -1}, { 0, -1, -1}, { 1, -1, -1}, { 2, -1, -1},
	    {-2,  0, -1}, {-1,  0, -1}, { 0,  0, -1}, { 1,  0, -1}, { 2,  0, -1},
	    {-2,  1, -1}, {-1,  1, -1}, { 0,  1, -1}, { 1, -2, -1}, { 2,  1, -1},
	    {-2,  2, -1}, {-1,  2, -1}, { 0,  2, -1}, { 1, -2, -1}, { 2,  2, -1},

	    {-2, -2,  0}, {-1, -2,  0}, { 0, -2,  0}, { 1, -2,  0}, { 2, -2,  0},
	    {-2, -1,  0}, {-1, -1,  0}, { 0, -1,  0}, { 1, -1,  0}, { 2, -1,  0},
	    {-2,  0,  0}, {-1,  0,  0},               { 1,  0,  0}, { 2,  0,  0},
	    {-2,  1,  0}, {-1,  1,  0}, { 0,  1,  0}, { 1, -2,  0}, { 2,  1,  0},
	    {-2,  2,  0}, {-1,  2,  0}, { 0,  2,  0}, { 1, -2,  0}, { 2,  2,  0},

	    {-2, -2,  1}, {-1, -2,  1}, { 0, -2,  1}, { 1, -2,  1}, { 2, -2,  1},
	    {-2, -1,  1}, {-1, -1,  1}, { 0, -1,  1}, { 1, -1,  1}, { 2, -1,  1},
	    {-2,  0,  1}, {-1,  0,  1}, { 0,  0,  1}, { 1,  0,  1}, { 2,  0,  1},
	    {-2,  1,  1}, {-1,  1,  1}, { 0,  1,  1}, { 1, -2,  1}, { 2,  1,  1},
	    {-2,  2,  1}, {-1,  2,  1}, { 0,  2,  1}, { 1, -2,  1}, { 2,  2,  1},

	    {-2, -2,  2}, {-1, -2,  2}, { 0, -2,  2}, { 1, -2,  2}, { 2, -2,  2},
	    {-2, -1,  2}, {-1, -1,  2}, { 0, -1,  2}, { 1, -1,  2}, { 2, -1,  2},
	    {-2,  0,  2}, {-1,  0,  2}, { 0,  0,  2}, { 1,  0,  2}, { 2,  0,  2},
	    {-2,  1,  2}, {-1,  1,  2}, { 0,  1,  2}, { 1, -2,  2}, { 2,  1,  2},
	                  {-1,  2,  2}, { 0,  2,  2}, { 1, -2,  2}
	};
	const int num_neighbors = ARRAY_SIZE(neighbors);
	
	bool found_sample = false;
	for (; ctx->trial < max_trials; ++ctx->trial) {
		while (!BLI_ghashIterator_done(&ctx->iter)) {
			MeshSampleCell *cell = BLI_ghashIterator_getValue(&ctx->iter);
			BLI_ghashIterator_step(&ctx->iter);
			
			if (cell->sample != SAMPLE_INDEX_INVALID) {
				continue;
			}
			
			bool cell_valid = true;
			
			unsigned int sample_index = cell->sample_start + ctx->trial;
			const IndexedMeshSample *isample = &gen->uniform_samples[sample_index];
			/* Check if we ran out of sample candidates for this cell */
			if (sample_index >= (unsigned int)gen->num_uniform_samples ||
			    !cell_index_eq(isample->cell_index, cell->cell_index)) {

				cell_valid = false;
				// TODO remove from hash table?
				UNUSED_VARS(cell_valid);

			}
			else {

				/* Check the sample candidate */
				const int *idx = cell->cell_index;
				
				bool conflict = false;
				for (int i = 0; i < num_neighbors; ++i) {
					const int *idx_offset = neighbors[i];
					const int nidx[3] = {idx[0] + idx_offset[0], idx[1] + idx_offset[1], idx[2] + idx_offset[2]};
					const MeshSampleCell *ncell = BLI_ghash_lookup(gen->cell_table, nidx);
					if (ncell) {
						if (ncell->sample != SAMPLE_INDEX_INVALID) {
							const IndexedMeshSample *nsample = &gen->uniform_samples[ncell->sample];
							BLI_assert(cell_index_eq(nsample->cell_index, ncell->cell_index));
							if (len_squared_v3v3(isample->co, nsample->co) < gen->mindist_squared) {
								conflict = true;
								break;
							}
						}
					}
				}
				if (!conflict) {
					cell->sample = sample_index;
					
					memcpy(sample->orig_verts, isample->orig_verts, sizeof(sample->orig_verts));
					memcpy(sample->orig_weights, isample->orig_weights, sizeof(sample->orig_weights));
					memset(sample->orig_loops, 0, sizeof(sample->orig_loops));
					sample->orig_poly = 0;
					
					found_sample = true;
				}

			}
			
			if (found_sample) {
				break;
			}
		}
		
		if (found_sample) {
			break;
		}
		else {
			BLI_ghashIterator_init(&ctx->iter, gen->cell_table);
		}
	}
	
	return found_sample;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_poissondisk(unsigned int seed, float mindist, unsigned int max_samples,
                                                             MeshSampleVertexWeightFp vertex_weight_cb, void *userdata)
{
	MSurfaceSampleGenerator_PoissonDisk *gen;
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_PoissonDisk), "MSurfaceSampleGenerator_PoissonDisk");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_poissondisk_free,
	                      (GeneratorBindFp)generator_poissondisk_bind,
	                      (GeneratorUnbindFp) generator_poissondisk_unbind,
	                      (GeneratorThreadContextCreateFp)generator_poissondisk_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_poissondisk_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_poissondisk_make_sample,
	                      (GeneratorGetMaxSamplesFp)generator_poissondisk_get_max_samples);
	
	gen->uniform_gen = BKE_mesh_sample_gen_surface_random(seed, true, vertex_weight_cb, userdata);
	gen->max_samples = max_samples;
	gen->mindist_squared = mindist * mindist;
	gen->cellsize = mindist / SQRT_3;
	gen->grid_scale = SQRT_3 / mindist;
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

typedef struct MVolumeSampleGenerator_Random {
	MeshSampleGenerator base;
	
	unsigned int seed;
	float density;
	
	/* bind data */
	BVHTreeFromMesh bvhdata;
	float min[3], max[3], extent[3], volume;
	int max_samples_per_ray;
} MVolumeSampleGenerator_Random;

typedef struct MVolumeSampleGenerator_Random_ThreadContext {
	const BVHTreeFromMesh *bvhdata;
	RNG *rng;
	
	/* current ray intersections */
	BVHTreeRayHit *ray_hits;
	int tothits, allochits;
	
	/* current segment index and sample number */
	int cur_seg, cur_tot, cur_sample;
} MVolumeSampleGenerator_Random_ThreadContext;

static void generator_volume_random_free(MVolumeSampleGenerator_Random *gen)
{
	MEM_freeN(gen);
}

static void generator_volume_random_bind(MVolumeSampleGenerator_Random *gen)
{
	DerivedMesh *dm = gen->base.dm;
	
	memset(&gen->bvhdata, 0, sizeof(gen->bvhdata));
	
	DM_ensure_tessface(dm);
	
	if (dm->getNumTessFaces(dm) == 0)
		return;
	
	bvhtree_from_mesh_faces(&gen->bvhdata, dm, 0.0f, 4, 6);
	
	INIT_MINMAX(gen->min, gen->max);
	dm->getMinMax(dm, gen->min, gen->max);
	sub_v3_v3v3(gen->extent, gen->max, gen->min);
	gen->volume = gen->extent[0] * gen->extent[1] * gen->extent[2];
	gen->max_samples_per_ray = max_ii(1, (int)powf(gen->volume, 1.0f/3.0f)) >> 1;
}

static void generator_volume_random_unbind(MVolumeSampleGenerator_Random *gen)
{
	free_bvhtree_from_mesh(&gen->bvhdata);
}

BLI_INLINE unsigned int hibit(unsigned int n) {
	n |= (n >>  1);
	n |= (n >>  2);
	n |= (n >>  4);
	n |= (n >>  8);
	n |= (n >> 16);
	return n ^ (n >> 1);
}

static void generator_volume_hits_reserve(MVolumeSampleGenerator_Random_ThreadContext *ctx, int tothits)
{
	if (tothits > ctx->allochits) {
		ctx->allochits = (int)hibit((unsigned int)tothits) << 1;
		ctx->ray_hits = MEM_reallocN(ctx->ray_hits, (size_t)ctx->allochits * sizeof(BVHTreeRayHit));
	}
}

static void *generator_volume_random_thread_context_create(const MVolumeSampleGenerator_Random *gen, int start)
{
	MVolumeSampleGenerator_Random_ThreadContext *ctx = MEM_callocN(sizeof(*ctx), "thread context");
	
	ctx->bvhdata = &gen->bvhdata;
	
	ctx->rng = BLI_rng_new(gen->seed);
	// 11 RNG gets per sample
	BLI_rng_skip(ctx->rng, start * 11);
	
	generator_volume_hits_reserve(ctx, 64);
	
	return ctx;
}

static void generator_volume_random_thread_context_free(const MVolumeSampleGenerator_Random *UNUSED(gen), void *thread_ctx)
{
	MVolumeSampleGenerator_Random_ThreadContext *ctx = thread_ctx;
	BLI_rng_free(ctx->rng);
	
	if (ctx->ray_hits) {
		MEM_freeN(ctx->ray_hits);
	}
	
	MEM_freeN(ctx);
}

static void generator_volume_ray_cb(void *userdata, int index, const BVHTreeRay *ray, BVHTreeRayHit *hit)
{
	MVolumeSampleGenerator_Random_ThreadContext *ctx = userdata;
	
	ctx->bvhdata->raycast_callback((void *)ctx->bvhdata, index, ray, hit);
	
	if (hit->index >= 0) {
		++ctx->tothits;
		generator_volume_hits_reserve(ctx, ctx->tothits);
		
		memcpy(&ctx->ray_hits[ctx->tothits-1], hit, sizeof(BVHTreeRayHit));
	}
}

typedef struct Ray {
	float start[3];
	float end[3];
} Ray;

static void generator_volume_random_cast_ray(MVolumeSampleGenerator_Random_ThreadContext *ctx, const Ray* ray)
{
	float dir[3];
	
	sub_v3_v3v3(dir, ray->end, ray->start);
	normalize_v3(dir);
	
	ctx->tothits = 0;
	BLI_bvhtree_ray_cast_all(ctx->bvhdata->tree, ray->start, dir, 0.0f, BVH_RAYCAST_DIST_MAX,
	                         generator_volume_ray_cb, ctx);
	
	ctx->cur_seg = 0;
	ctx->cur_tot = 0;
	ctx->cur_sample = 0;
}

static void generator_volume_init_segment(const MVolumeSampleGenerator_Random *gen, MVolumeSampleGenerator_Random_ThreadContext *ctx)
{
	BVHTreeRayHit *a, *b;
	float length;
	
	BLI_assert(ctx->cur_seg + 1 < ctx->tothits);
	a = &ctx->ray_hits[ctx->cur_seg];
	b = &ctx->ray_hits[ctx->cur_seg + 1];
	
	length = len_v3v3(a->co, b->co);
	ctx->cur_tot = min_ii(gen->max_samples_per_ray, (int)ceilf(length * gen->density));
	ctx->cur_sample = 0;
}

static void generator_volume_get_ray(RNG *rng, Ray *ray)
{
	/* bounding box margin to get clean ray intersections */
	static const float margin = 0.01f;
	
	ray->start[0] = BLI_rng_get_float(rng);
	ray->start[1] = BLI_rng_get_float(rng);
	ray->start[2] = 0.0f;
	ray->end[0] = BLI_rng_get_float(rng);
	ray->end[1] = BLI_rng_get_float(rng);
	ray->end[2] = 1.0f;
	
	int axis = BLI_rng_get_int(rng) % 3;
	switch (axis) {
		case 0: break;
		case 1:
			SHIFT3(float, ray->start[0], ray->start[1], ray->start[2]);
			SHIFT3(float, ray->end[0], ray->end[1], ray->end[2]);
			break;
		case 2:
			SHIFT3(float, ray->start[2], ray->start[1], ray->start[0]);
			SHIFT3(float, ray->end[2], ray->end[1], ray->end[0]);
			break;
	}
	
	mul_v3_fl(ray->start, 1.0f + 2.0f*margin);
	add_v3_fl(ray->start, -margin);
	mul_v3_fl(ray->end, 1.0f + 2.0f*margin);
	add_v3_fl(ray->end, -margin);
}

static void generator_volume_ray_to_bbox(const MVolumeSampleGenerator_Random *gen, Ray *ray)
{
	madd_v3_v3v3v3(ray->start, gen->min, ray->start, gen->extent);
	madd_v3_v3v3v3(ray->end, gen->min, ray->end, gen->extent);
}

static bool generator_volume_random_make_sample(const MVolumeSampleGenerator_Random *gen, void *thread_ctx, MeshSample *sample)
{
	MVolumeSampleGenerator_Random_ThreadContext *ctx = thread_ctx;
	
	Ray ray1, ray2;
	// Do all RNG gets at the beggining for keeping consistent state
	generator_volume_get_ray(ctx->rng, &ray1);
	generator_volume_get_ray(ctx->rng, &ray2);
	float t = BLI_rng_get_float(ctx->rng);
	
	if (ctx->cur_seg + 1 >= ctx->tothits) {
		generator_volume_ray_to_bbox(gen, &ray1);
		generator_volume_random_cast_ray(ctx, &ray1);
		if (ctx->tothits < 2)
			return false;
	}
	
	if (ctx->cur_sample >= ctx->cur_tot) {
		ctx->cur_seg += 2;
		
		if (ctx->cur_seg + 1 >= ctx->tothits) {
			generator_volume_ray_to_bbox(gen, &ray2);
			generator_volume_random_cast_ray(ctx, &ray2);
			if (ctx->tothits < 2)
				return false;
		}
		
		generator_volume_init_segment(gen, ctx);
	}
	BVHTreeRayHit *a = &ctx->ray_hits[ctx->cur_seg];
	BVHTreeRayHit *b = &ctx->ray_hits[ctx->cur_seg + 1];
	
	if (ctx->cur_sample < ctx->cur_tot) {
		
		sample->orig_verts[0] = SAMPLE_INDEX_INVALID;
		sample->orig_verts[1] = SAMPLE_INDEX_INVALID;
		sample->orig_verts[2] = SAMPLE_INDEX_INVALID;
		
		interp_v3_v3v3(sample->orig_weights, a->co, b->co, t);
		
		ctx->cur_sample += 1;
		
		return true;
	}
	
	return false;
}

MeshSampleGenerator *BKE_mesh_sample_gen_volume_random_bbray(unsigned int seed, float density)
{
	MVolumeSampleGenerator_Random *gen;
	
	gen = MEM_callocN(sizeof(MVolumeSampleGenerator_Random), "MVolumeSampleGenerator_Random");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_volume_random_free,
	                      (GeneratorBindFp)generator_volume_random_bind,
	                      (GeneratorUnbindFp) generator_volume_random_unbind,
	                      (GeneratorThreadContextCreateFp)generator_volume_random_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_volume_random_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_volume_random_make_sample,
	                      NULL);
	
	gen->seed = seed;
	gen->density = density;
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

void BKE_mesh_sample_free_generator(MeshSampleGenerator *gen)
{
	if (gen->default_ctx) {
		if (gen->thread_context_free) {
			gen->thread_context_free(gen, gen->default_ctx);
		}
	}
	
	BKE_mesh_sample_generator_unbind(gen);
	
	gen->free(gen);
}


/* ==== Sampling ==== */

void BKE_mesh_sample_generator_bind(MeshSampleGenerator *gen, DerivedMesh *dm)
{
	BLI_assert(gen->dm == NULL && "Generator already bound");
	
	gen->dm = dm;
	if (gen->bind) {
		gen->bind(gen);
	}
}

void BKE_mesh_sample_generator_unbind(MeshSampleGenerator *gen)
{
	if (gen->dm) {
		if (gen->unbind) {
			gen->unbind(gen);
		}
		gen->dm = NULL;
	}
}

unsigned int BKE_mesh_sample_gen_get_max_samples(const MeshSampleGenerator *gen)
{
	if (gen->get_max_samples) {
		return gen->get_max_samples(gen);
	}
	return 0;
}

bool BKE_mesh_sample_generate(MeshSampleGenerator *gen, struct MeshSample *sample)
{
	if (!gen->default_ctx && gen->thread_context_create) {
		gen->default_ctx = gen->thread_context_create(gen, 0);
	}
	
	return gen->make_sample(gen, gen->default_ctx, sample);
}

typedef struct MeshSamplePoolData {
	const MeshSampleGenerator *gen;
	int output_stride;
} MeshSamplePoolData;

typedef struct MeshSampleTaskData {
	void *thread_ctx;
	void *output_buffer;
	int count;
	int result;
} MeshSampleTaskData;

static void mesh_sample_generate_task_run(TaskPool * __restrict pool, void *taskdata_, int UNUSED(threadid))
{
	MeshSamplePoolData *pooldata = BLI_task_pool_userdata(pool);
	const MeshSampleGenerator *gen = pooldata->gen;
	const int output_stride = pooldata->output_stride;
	
	GeneratorMakeSampleFp make_sample = gen->make_sample;
	MeshSampleTaskData *taskdata = taskdata_;
	void *thread_ctx = taskdata->thread_ctx;
	const int count = taskdata->count;
	MeshSample *sample = taskdata->output_buffer;
	
	int i = 0;
	for (; i < count; ++i, sample = (MeshSample *)((char *)sample + output_stride)) {
		if (!make_sample(gen, thread_ctx, sample)) {
			break;
		}
	}
	
	taskdata->result = i;
}

int BKE_mesh_sample_generate_batch_ex(MeshSampleGenerator *gen,
                                      void *output_buffer, int output_stride, int count,
                                      bool use_threads)
{
	if (use_threads) {
		TaskScheduler *scheduler = BLI_task_scheduler_get();
		
		MeshSamplePoolData pool_data;
		pool_data.gen = gen;
		pool_data.output_stride = output_stride;
		TaskPool *task_pool = BLI_task_pool_create(scheduler, &pool_data);
		
		const int num_tasks = (count + gen->task_size - 1) / gen->task_size;
		MeshSampleTaskData *task_data = MEM_callocN(sizeof(MeshSampleTaskData) * (unsigned int)num_tasks, "mesh sample task data");
		
		{
			MeshSampleTaskData *td = task_data;
			int start = 0;
			for (int i = 0; i < num_tasks; ++i, ++td, start += gen->task_size) {
				if (gen->thread_context_create) {
					td->thread_ctx = gen->thread_context_create(gen, start);
				}
				td->output_buffer = (char *)output_buffer + start * output_stride;
				td->count = min_ii(count - start, gen->task_size);
				
				BLI_task_pool_push(task_pool, mesh_sample_generate_task_run, td, false, TASK_PRIORITY_LOW);
			}
		}
		
		BLI_task_pool_work_and_wait(task_pool);
		BLI_task_pool_free(task_pool);
		
		int totresult = 0;
		{
			MeshSampleTaskData *td = task_data;
			for (int i = 0; i < num_tasks; ++i, ++td) {
				totresult += td->result;
			}
		}
		
		if (gen->thread_context_free) {
			MeshSampleTaskData *td = task_data;
			for (int i = 0; i < num_tasks; ++i, ++td) {
				if (td->thread_ctx) {
					gen->thread_context_free(gen, td->thread_ctx);
				}
			}
		}
		MEM_freeN(task_data);
		
		return totresult;
	}
	else {
		void *thread_ctx = NULL;
		if (gen->thread_context_create) {
			thread_ctx = gen->thread_context_create(gen, 0);
		}
		
		MeshSample *sample = output_buffer;
		int i = 0;
		for (; i < count; ++i, sample = (MeshSample *)((char *)sample + output_stride)) {
			if (!gen->make_sample(gen, thread_ctx, sample)) {
				break;
			}
		}
		
		if (thread_ctx && gen->thread_context_free) {
			gen->thread_context_free(gen, thread_ctx);
		}
		
		return i;
	}
}

int BKE_mesh_sample_generate_batch(MeshSampleGenerator *gen,
                                   MeshSample *output_buffer, int count)
{
	return BKE_mesh_sample_generate_batch_ex(gen, output_buffer, sizeof(MeshSample), count, true);
}

/* ==== Utilities ==== */

#include "DNA_particle_types.h"

#include "BKE_bvhutils.h"
#include "BKE_particle.h"

bool BKE_mesh_sample_from_particle(MeshSample *sample, ParticleSystem *psys, DerivedMesh *dm, ParticleData *pa)
{
	MVert *mverts;
	MFace *mface;
	float mapfw[4];
	int mapindex;
	float *co1 = NULL, *co2 = NULL, *co3 = NULL, *co4 = NULL;
	float vec[3];
	float w[4];
	
	if (!psys_get_index_on_dm(psys, dm, pa, &mapindex, mapfw))
		return false;
	
	mface = dm->getTessFaceData(dm, mapindex, CD_MFACE);
	mverts = dm->getVertDataArray(dm, CD_MVERT);
	
	co1 = mverts[mface->v1].co;
	co2 = mverts[mface->v2].co;
	co3 = mverts[mface->v3].co;
	
	if (mface->v4) {
		co4 = mverts[mface->v4].co;
		
		interp_v3_v3v3v3v3(vec, co1, co2, co3, co4, mapfw);
	}
	else {
		interp_v3_v3v3v3(vec, co1, co2, co3, mapfw);
	}
	
	/* test both triangles of the face */
	interp_weights_tri_v3(w, co1, co2, co3, vec);
	if (w[0] <= 1.0f && w[1] <= 1.0f && w[2] <= 1.0f) {
		sample->orig_verts[0] = mface->v1;
		sample->orig_verts[1] = mface->v2;
		sample->orig_verts[2] = mface->v3;
	
		copy_v3_v3(sample->orig_weights, w);
		return true;
	}
	else if (mface->v4) {
		interp_weights_tri_v3(w, co3, co4, co1, vec);
		sample->orig_verts[0] = mface->v3;
		sample->orig_verts[1] = mface->v4;
		sample->orig_verts[2] = mface->v1;
	
		copy_v3_v3(sample->orig_weights, w);
		return true;
	}
	else
		return false;
}

bool BKE_mesh_sample_to_particle(MeshSample *sample, ParticleSystem *UNUSED(psys), DerivedMesh *dm, BVHTreeFromMesh *bvhtree, ParticleData *pa)
{
	BVHTreeNearest nearest;
	float vec[3], nor[3], tang[3];
	
	BKE_mesh_sample_eval(dm, sample, vec, nor, tang);
	
	nearest.index = -1;
	nearest.dist_sq = FLT_MAX;
	BLI_bvhtree_find_nearest(bvhtree->tree, vec, &nearest, bvhtree->nearest_callback, bvhtree);
	if (nearest.index >= 0) {
		MFace *mface = dm->getTessFaceData(dm, nearest.index, CD_MFACE);
		MVert *mverts = dm->getVertDataArray(dm, CD_MVERT);
		
		float *co1 = mverts[mface->v1].co;
		float *co2 = mverts[mface->v2].co;
		float *co3 = mverts[mface->v3].co;
		float *co4 = mface->v4 ? mverts[mface->v4].co : NULL;
		
		pa->num = nearest.index;
		pa->num_dmcache = DMCACHE_NOTFOUND;
		
		interp_weights_quad_v3(pa->fuv, co1, co2, co3, co4, vec);
		pa->foffset = 0.0f; /* XXX any sensible way to reconstruct this? */
		
		return true;
	}
	else
		return false;
}
