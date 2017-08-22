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

#include "MEM_guardedalloc.h"

#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_rand.h"
#include "BLI_task.h"

#include "BKE_bvhutils.h"
#include "BKE_mesh_sample.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"

#include "BLI_strict_flags.h"

#define DEFAULT_TASK_SIZE 1024

/* ==== Evaluate ==== */

bool BKE_mesh_sample_is_volume_sample(const MeshSample *sample)
{
	return sample->orig_verts[0] == 0 && sample->orig_verts[1] == 0;
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


/* ==== Sampling Utilities ==== */

BLI_INLINE void mesh_sample_weights_from_loc(MeshSample *sample, DerivedMesh *dm, int face_index, const float loc[3])
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

/* ==== Sampling ==== */

typedef void (*GeneratorFreeFp)(struct MeshSampleGenerator *gen);
typedef void* (*GeneratorThreadContextCreateFp)(const struct MeshSampleGenerator *gen, int start);
typedef void (*GeneratorThreadContextFreeFp)(const struct MeshSampleGenerator *gen, void *thread_ctx);
typedef bool (*GeneratorMakeSampleFp)(const struct MeshSampleGenerator *gen, void *thread_ctx, struct MeshSample *sample);

typedef struct MeshSampleGenerator
{
	GeneratorFreeFp free;
	GeneratorThreadContextCreateFp thread_context_create;
	GeneratorThreadContextFreeFp thread_context_free;
	GeneratorMakeSampleFp make_sample;
	
	void *default_ctx;
	int task_size;
} MeshSampleGenerator;

static void sample_generator_init(MeshSampleGenerator *gen,
                                  GeneratorFreeFp free,
                                  GeneratorThreadContextCreateFp thread_context_create,
                                  GeneratorThreadContextFreeFp thread_context_free,
                                  GeneratorMakeSampleFp make_sample)
{
	gen->free = free;
	gen->thread_context_create = thread_context_create;
	gen->thread_context_free = thread_context_free;
	gen->make_sample = make_sample;
	
	gen->default_ctx = NULL;
	gen->task_size = DEFAULT_TASK_SIZE;
}

/* ------------------------------------------------------------------------- */

typedef struct MSurfaceSampleGenerator_Vertices {
	MeshSampleGenerator base;
	
	DerivedMesh *dm;
	int (*vert_loop_map)[3];
} MSurfaceSampleGenerator_Vertices;

static void generator_vertices_free(MSurfaceSampleGenerator_Vertices *gen)
{
	if (gen->vert_loop_map) {
		MEM_freeN(gen->vert_loop_map);
	}
	MEM_freeN(gen);
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

static bool generator_vertices_make_sample(const MSurfaceSampleGenerator_Vertices *gen, void *thread_ctx, MeshSample *sample)
{
	DerivedMesh *dm = gen->dm;
	const int num_verts = dm->getNumVerts(dm);
	const MLoop *mloops = dm->getLoopArray(dm);
	
	int cur_vert = *(int *)thread_ctx;
	bool found_vert = false;
	while (cur_vert < num_verts) {
		++cur_vert;
		
		const int *loops = gen->vert_loop_map[cur_vert];
		if (loops[0] >= 0) {
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

			found_vert = true;
			break;
		}
	}
	
	*(int *)thread_ctx = cur_vert;
	return found_vert;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_vertices(DerivedMesh *dm)
{
	MSurfaceSampleGenerator_Vertices *gen;
	
	DM_ensure_normals(dm);
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_Vertices), "MSurfaceSampleGenerator_Vertices");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_vertices_free,
	                      (GeneratorThreadContextCreateFp)generator_vertices_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_vertices_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_vertices_make_sample);
	
	gen->dm = dm;
	
	{
		const int num_verts = dm->getNumVerts(dm);
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
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

//#define USE_DEBUG_COUNT

typedef struct MSurfaceSampleGenerator_Random {
	MeshSampleGenerator base;
	
	DerivedMesh *dm;
	unsigned int seed;
	float *tri_weights;
	float *vertex_weights;
	
#ifdef USE_DEBUG_COUNT
	int *debug_count;
#endif
} MSurfaceSampleGenerator_Random;

static void generator_random_free(MSurfaceSampleGenerator_Random *gen)
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
	MEM_freeN(gen);
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
	DerivedMesh *dm = gen->dm;
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

BLI_INLINE float triangle_weight(DerivedMesh *dm, const MLoopTri *tri, MeshSampleVertexWeightFp vertex_weight_cb, void *userdata)
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
	
	if (vertex_weight_cb) {
		float w1 = vertex_weight_cb(dm, v1, index1, userdata);
		float w2 = vertex_weight_cb(dm, v2, index2, userdata);
		float w3 = vertex_weight_cb(dm, v3, index3, userdata);
		
		weight *= (w1 + w2 + w3) / 3.0f;
	}
	
	return weight;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_random_ex(DerivedMesh *dm, unsigned int seed,
                                                           MeshSampleVertexWeightFp vertex_weight_cb, void *userdata, bool use_facearea)
{
	MSurfaceSampleGenerator_Random *gen;
	
	DM_ensure_normals(dm);
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_Random), "MSurfaceSampleGenerator_Random");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_random_free,
	                      (GeneratorThreadContextCreateFp)generator_random_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_random_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_random_make_sample);
	
	gen->dm = dm;
	gen->seed = seed;
	
	if (use_facearea) {
		int numtris = dm->getNumLoopTri(dm);
		int numweights = numtris;
		const MLoopTri *mtris = dm->getLoopTriArray(dm);
		const MLoopTri *mt;
		int i;
		float totweight;
		
		gen->tri_weights = MEM_mallocN(sizeof(float) * (size_t)numweights, "mesh sample triangle weights");
		
		/* accumulate weights */
		totweight = 0.0f;
		for (i = 0, mt = mtris; i < numtris; ++i, ++mt) {
			float weight = triangle_weight(dm, mt, vertex_weight_cb, userdata);
			gen->tri_weights[i] = totweight;
			totweight += weight;
		}
		
		/* normalize */
		if (totweight > 0.0f) {
			float norm = 1.0f / totweight;
			for (i = 0, mt = mtris; i < numtris; ++i, ++mt) {
				gen->tri_weights[i] *= norm;
			}
		}
		else {
			/* invalid weights, remove to avoid invalid binary search */
			MEM_freeN(gen->tri_weights);
			gen->tri_weights = NULL;
		}
		
#ifdef USE_DEBUG_COUNT
		gen->debug_count = MEM_callocN(sizeof(int) * (size_t)numweights, "surface sample debug counts");
#endif
	}
	
	return &gen->base;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_random(DerivedMesh *dm, unsigned int seed)
{
	return BKE_mesh_sample_gen_surface_random_ex(dm, seed, NULL, NULL, true);
}

/* ------------------------------------------------------------------------- */

typedef struct MSurfaceSampleGenerator_RayCast {
	MeshSampleGenerator base;
	
	DerivedMesh *dm;
	BVHTreeFromMesh bvhdata;
	
	MeshSampleRayFp ray_cb;
	MeshSampleThreadContextCreateFp thread_context_create_cb;
	MeshSampleThreadContextFreeFp thread_context_free_cb;
	void *userdata;
} MSurfaceSampleGenerator_RayCast;

static void generator_raycast_free(MSurfaceSampleGenerator_RayCast *gen)
{
	free_bvhtree_from_mesh(&gen->bvhdata);
	MEM_freeN(gen);
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
		
		mesh_sample_weights_from_loc(sample, gen->dm, hit.index, hit.co);
		
		return true;
	}
	else
		return false;
}

MeshSampleGenerator *BKE_mesh_sample_gen_surface_raycast(
        DerivedMesh *dm, 
        MeshSampleThreadContextCreateFp thread_context_create_cb,
        MeshSampleThreadContextFreeFp thread_context_free_cb,
        MeshSampleRayFp ray_cb,
        void *userdata)
{
	MSurfaceSampleGenerator_RayCast *gen;
	BVHTreeFromMesh bvhdata;
	
	DM_ensure_tessface(dm);
	
	if (dm->getNumTessFaces(dm) == 0)
		return NULL;
	
	memset(&bvhdata, 0, sizeof(BVHTreeFromMesh));
	bvhtree_from_mesh_faces(&bvhdata, dm, 0.0f, 4, 6);
	if (!bvhdata.tree)
		return NULL;
	
	gen = MEM_callocN(sizeof(MSurfaceSampleGenerator_RayCast), "MSurfaceSampleGenerator_RayCast");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_raycast_free,
	                      (GeneratorThreadContextCreateFp)generator_raycast_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_raycast_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_raycast_make_sample);
	
	gen->dm = dm;
	memcpy(&gen->bvhdata, &bvhdata, sizeof(gen->bvhdata));
	gen->thread_context_create_cb = thread_context_create_cb;
	gen->thread_context_free_cb = thread_context_free_cb;
	gen->ray_cb = ray_cb;
	gen->userdata = userdata;
	
	return &gen->base;
}

/* ------------------------------------------------------------------------- */

typedef struct MVolumeSampleGenerator_Random {
	MeshSampleGenerator base;
	
	DerivedMesh *dm;
	BVHTreeFromMesh bvhdata;
	unsigned int seed;
	float min[3], max[3], extent[3], volume;
	float density;
	int max_samples_per_ray;
	
	/* current ray intersections */
	BVHTreeRayHit *ray_hits;
	int tothits, allochits;
	
	/* current segment index and sample number */
	int cur_seg, cur_tot, cur_sample;
} MVolumeSampleGenerator_Random;

static void generator_volume_random_free(MVolumeSampleGenerator_Random *gen)
{
	free_bvhtree_from_mesh(&gen->bvhdata);
	
	if (gen->ray_hits) {
		MEM_freeN(gen->ray_hits);
	}
	
	MEM_freeN(gen);
}

static void *generator_volume_random_thread_context_create(MVolumeSampleGenerator_Random *gen, int start)
{
	RNG *rng = BLI_rng_new(gen->seed);
	// 11 RNG gets per sample
	BLI_rng_skip(rng, start * 11);
	return rng;
}

static void generator_volume_random_thread_context_free(MVolumeSampleGenerator_Random *UNUSED(gen), void *thread_ctx)
{
	BLI_rng_free(thread_ctx);
}

BLI_INLINE unsigned int hibit(unsigned int n) {
	n |= (n >>  1);
	n |= (n >>  2);
	n |= (n >>  4);
	n |= (n >>  8);
	n |= (n >> 16);
	return n ^ (n >> 1);
}

static void generator_volume_hits_reserve(MVolumeSampleGenerator_Random *gen, int tothits)
{
	if (tothits > gen->allochits) {
		gen->allochits = (int)hibit((unsigned int)tothits) << 1;
		gen->ray_hits = MEM_reallocN(gen->ray_hits, (size_t)gen->allochits * sizeof(BVHTreeRayHit));
	}
}

static void generator_volume_ray_cb(void *userdata, int index, const BVHTreeRay *ray, BVHTreeRayHit *hit)
{
	MVolumeSampleGenerator_Random *gen = userdata;
	
	gen->bvhdata.raycast_callback(&gen->bvhdata, index, ray, hit);
	
	if (hit->index >= 0) {
		++gen->tothits;
		generator_volume_hits_reserve(gen, gen->tothits);
		
		memcpy(&gen->ray_hits[gen->tothits-1], hit, sizeof(BVHTreeRayHit));
	}
}

typedef struct Ray {
	float start[3];
	float end[3];
} Ray;

static void generator_volume_random_cast_ray(MVolumeSampleGenerator_Random *gen, const Ray* ray)
{
	Ray wray;
	float dir[3];
	
	madd_v3_v3v3v3(wray.start, gen->min, ray->start, gen->extent);
	madd_v3_v3v3v3(wray.end, gen->min, ray->end, gen->extent);
	
	sub_v3_v3v3(dir, wray.end, wray.start);
	
	gen->tothits = 0;
	BLI_bvhtree_ray_cast_all(gen->bvhdata.tree, wray.start, dir, 0.0f, BVH_RAYCAST_DIST_MAX,
	                         generator_volume_ray_cb, gen);
	
	gen->cur_seg = 0;
	gen->cur_tot = 0;
	gen->cur_sample = 0;
}

static void generator_volume_init_segment(MVolumeSampleGenerator_Random *gen)
{
	BVHTreeRayHit *a, *b;
	float length;
	
	BLI_assert(gen->cur_seg + 1 < gen->tothits);
	a = &gen->ray_hits[gen->cur_seg];
	b = &gen->ray_hits[gen->cur_seg + 1];
	
	length = len_v3v3(a->co, b->co);
	gen->cur_tot = min_ii(gen->max_samples_per_ray, (int)ceilf(length * gen->density));
	gen->cur_sample = 0;
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

static bool generator_volume_random_make_sample(MVolumeSampleGenerator_Random *gen, void *thread_ctx, MeshSample *sample)
{
	RNG *rng = thread_ctx;
	
	Ray ray1, ray2;
	// Do all RNG gets at the beggining for keeping consistent state
	generator_volume_get_ray(rng, &ray1);
	generator_volume_get_ray(rng, &ray2);
	float t = BLI_rng_get_float(rng);
	
	if (gen->cur_seg + 1 >= gen->tothits) {
		generator_volume_random_cast_ray(gen, &ray1);
		if (gen->tothits < 2)
			return false;
	}
	
	if (gen->cur_sample >= gen->cur_tot) {
		gen->cur_seg += 2;
		
		if (gen->cur_seg + 1 >= gen->tothits) {
			generator_volume_random_cast_ray(gen, &ray2);
			if (gen->tothits < 2)
				return false;
		}
		
		generator_volume_init_segment(gen);
	}
	BVHTreeRayHit *a = &gen->ray_hits[gen->cur_seg];
	BVHTreeRayHit *b = &gen->ray_hits[gen->cur_seg + 1];
	
	if (gen->cur_sample < gen->cur_tot) {
		
		sample->orig_verts[0] = 0;
		sample->orig_verts[1] = 0;
		sample->orig_verts[2] = 0;
		
		interp_v3_v3v3(sample->orig_weights, a->co, b->co, t);
		
		gen->cur_sample += 1;
		
		return true;
	}
	
	return false;
}

MeshSampleGenerator *BKE_mesh_sample_gen_volume_random_bbray(DerivedMesh *dm, unsigned int seed, float density)
{
	MVolumeSampleGenerator_Random *gen;
	BVHTreeFromMesh bvhdata;
	
	gen = MEM_callocN(sizeof(MVolumeSampleGenerator_Random), "MVolumeSampleGenerator_Random");
	sample_generator_init(&gen->base,
	                      (GeneratorFreeFp)generator_volume_random_free,
	                      (GeneratorThreadContextCreateFp)generator_volume_random_thread_context_create,
	                      (GeneratorThreadContextFreeFp)generator_volume_random_thread_context_free,
	                      (GeneratorMakeSampleFp)generator_volume_random_make_sample);
	
	DM_ensure_tessface(dm);
	
	if (dm->getNumTessFaces(dm) == 0)
		return NULL;
	
	memset(&bvhdata, 0, sizeof(BVHTreeFromMesh));
	bvhtree_from_mesh_faces(&bvhdata, dm, 0.0f, 4, 6);
	if (!bvhdata.tree)
		return NULL;
	
	gen->dm = dm;
	memcpy(&gen->bvhdata, &bvhdata, sizeof(gen->bvhdata));
	gen->seed = seed;
	
	INIT_MINMAX(gen->min, gen->max);
	dm->getMinMax(dm, gen->min, gen->max);
	sub_v3_v3v3(gen->extent, gen->max, gen->min);
	gen->volume = gen->extent[0] * gen->extent[1] * gen->extent[2];
	gen->density = density;
	gen->max_samples_per_ray = max_ii(1, (int)powf(gen->volume, 1.0f/3.0f)) >> 1;
	
	generator_volume_hits_reserve(gen, 64);
	
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
	
	gen->free(gen);
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
