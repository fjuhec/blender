#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_alloca.h"
#include "BLI_math.h"
#include "BLI_math_geom.h"
#include "BLI_task.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_editmesh.h"
#include "BKE_library_query.h"
#include "BKE_modifier.h"

#include "depsgraph_private.h"

#include "MEM_guardedalloc.h"

#include "MOD_util.h"

typedef struct SDefAdjacency {
	struct SDefAdjacency *next;
	int index;
} SDefAdjacency;

typedef struct SDefEdgePolys {
	int polys[2], num;
} SDefEdgePolys;

typedef struct SDefBindCalcData {
	BVHTreeFromMesh * const treeData;
	const SDefAdjacency ** const vert_edges;
	const SDefEdgePolys * const edge_polys;
	SDefVert * const bind_verts;
	const MLoopTri * const looptri;
	const MPoly * const mpoly;
	const MEdge * const medge;
	const MLoop * const mloop;
	const MVert * const mvert;
	float (* const vertexCos)[3];
	const float falloff;
	int success;
} SDefBindCalcData;

typedef struct SDefBindPoly {
	struct SDefBindPoly *next;
	float (*coords)[3];
	float (*coords_v2)[2];
	float point_v2[2];
	float weight_components[3]; /* indices: 0 = angular weight; 1 = projected point weight; 2 = actual point weights; */
	float weight;
	float scales[2];
	float centroid[3];
	float centroid_v2[2];
	float normal[3];
	float cent_edgemid_vecs_v2[2][2];
	float edgemid_angle;
	float point_edgemid_angles[2];
	float corner_edgemid_angles[2];
	float dominant_angle_weight;
	int index;
	int numverts;
	int loopstart;
	int edge_inds[2];
	int edge_vert_inds[2];
	int corner_ind;
	int dominant_edge;
	bool inside;
} SDefBindPoly;

typedef struct SDefBindWeightData {
	SDefBindPoly *bind_polys;
	int numbinds;
} SDefBindWeightData;

static void initData(ModifierData *md)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	smd->target	 = NULL;
	smd->verts	 = NULL;
	smd->flags   = 0;
	smd->falloff = 4.0f;
}

static void freeData(ModifierData *md)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	if (smd->verts) {
		int i, j;
		for (i = 0; i < smd->numverts; i++) {
			if (smd->verts[i].binds) {
				for (j = 0; j < smd->verts[i].numbinds; j++) {
					if (smd->verts[i].binds[j].vert_inds) {
						MEM_freeN(smd->verts[i].binds[j].vert_inds);
					}

					if (smd->verts[i].binds[j].vert_weights) {
						MEM_freeN(smd->verts[i].binds[j].vert_weights);
					}
				}

				MEM_freeN(smd->verts[i].binds);
			}
		}

		MEM_freeN(smd->verts);
		smd->verts = NULL;
	}
}

static void copyData(ModifierData *md, ModifierData *target)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	SurfaceDeformModifierData *tsmd = (SurfaceDeformModifierData *) target;

	*tsmd = *smd;

	if (smd->verts) {
		int i, j;

		tsmd->verts = MEM_dupallocN(smd->verts);

		for (i = 0; i < smd->numverts; i++) {
			if (smd->verts[i].binds) {
				tsmd->verts[i].binds = MEM_dupallocN(smd->verts[i].binds);

				for (j = 0; j < smd->verts[i].numbinds; j++) {
					if (smd->verts[i].binds[j].vert_inds) {
						tsmd->verts[i].binds[j].vert_inds = MEM_dupallocN(smd->verts[i].binds[j].vert_inds);
					}

					if (smd->verts[i].binds[j].vert_weights) {
						tsmd->verts[i].binds[j].vert_weights = MEM_dupallocN(smd->verts[i].binds[j].vert_weights);
					}
				}
			}
		}
	}
}

static void foreachObjectLink(ModifierData *md, Object *ob, ObjectWalkFunc walk, void *userData)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	walk(userData, ob, &smd->target, IDWALK_NOP);
}

static void updateDepgraph(ModifierData *md, DagForest *forest,
                           struct Main *UNUSED(bmain),
                           struct Scene *UNUSED(scene),
                           Object *UNUSED(ob),
                           DagNode *obNode)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	if (smd->target) {
		DagNode *curNode = dag_get_node(forest, smd->target);

		dag_add_relation(forest, curNode, obNode,
		                 DAG_RL_DATA_DATA | DAG_RL_OB_DATA | DAG_RL_DATA_OB | DAG_RL_OB_OB,
		                 "Surface Deform Modifier");
	}
}

static void updateDepsgraph(ModifierData *md,
                            struct Main *UNUSED(bmain),
                            struct Scene *UNUSED(scene),
                            Object *UNUSED(ob),
                            struct DepsNodeHandle *node)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *)md;
	if (smd->target != NULL) {
		DEG_add_object_relation(node, smd->target, DEG_OB_COMP_GEOMETRY, "Surface Deform Modifier");
	}
}

static void freeAdjacencyMap(SDefAdjacency ** const vert_edges, SDefEdgePolys * const edge_polys, const int numverts)
{
	SDefAdjacency *adj;
	int i;

	MEM_freeN(edge_polys);

	for (i = 0; i < numverts; i++) {
		for (adj = vert_edges[i]; adj; adj = vert_edges[i]) {
			vert_edges[i] = adj->next;

			MEM_freeN(adj);
		}
	}

	MEM_freeN(vert_edges);
}

static int buildAdjacencyMap(const MPoly *poly, const MEdge *edge, const MLoop * const mloop, const int numpoly, const int numedges,
                              SDefAdjacency ** const vert_edges, SDefEdgePolys * const edge_polys)
{
	const MLoop *loop;
	SDefAdjacency *adj;
	int i, j;

	/* Fing polygons adjacent to edges */
	for (i = 0; i < numpoly; i++, poly++) {
		loop = &mloop[poly->loopstart];

		for (j = 0; j < poly->totloop; j++, loop++) {
			if (edge_polys[loop->e].num == 0) {
				edge_polys[loop->e].polys[0] = i;
				edge_polys[loop->e].polys[1] = -1;
				edge_polys[loop->e].num++;
			}
			else if (edge_polys[loop->e].num == 1) {
				edge_polys[loop->e].polys[1] = i;
				edge_polys[loop->e].num++;
			}
			else {
				printf("Surface Deform: Target has edges with more than two polys\n");
				return -1;
			}
		}
	}

	/* Find edges adjacent to vertices */
	for (i = 0; i < numedges; i++, edge++) {
		adj = MEM_mallocN(sizeof(*adj), "SDefVertEdge");
		if (adj == NULL) {
			return 0;
		}

		adj->next = vert_edges[edge->v1];
		adj->index = i;
		vert_edges[edge->v1] = adj;

		adj = MEM_mallocN(sizeof(*adj), "SDefVertEdge");
		if (adj == NULL) {
			return 0;
		}

		adj->next = vert_edges[edge->v2];
		adj->index = i;
		vert_edges[edge->v2] = adj;
	}

	return 1;
}

BLI_INLINE void sortPolyVertsEdge(int *indices, const MLoop * const mloop, const int edge, const int num)
{
	int i;
	bool found = false;

	for (i = 0; i < num; i++) {
		if (mloop[i].e == edge) {
			found = true;
		}
		if (found) {
			*indices = mloop[i].v;
			indices++;
		}
	}

	/* Fill in remaining vertex indices that occur before the edge */
	for (i = 0; mloop[i].e != edge; i++) {
		*indices = mloop[i].v;
		indices++;
	}
}

BLI_INLINE void sortPolyVertsTri(int *indices, const MLoop * const mloop, const int loopstart, const int num)
{
	int i;

	for (i = loopstart; i < num; i++) {
		*indices = mloop[i].v;
		indices++;
	}

	for (i = 0; i < loopstart; i++) {
		*indices = mloop[i].v;
		indices++;
	}
}

BLI_INLINE void meanValueCoordinates(float w[], const float point[2], const float verts[][2], const int num)
{
	float vec_curr[2], vec_prev[2], vec_tmp[2];
	float tan_prev, tan_curr, mag_curr, mag_next;
	float tot_w = 0;
	int i, ind_curr;

	sub_v2_v2v2(vec_tmp, verts[num - 2], point);
	sub_v2_v2v2(vec_prev, verts[num - 1], point);

	mag_curr = normalize_v2(vec_prev);
	normalize_v2(vec_tmp);

	tan_prev = tan(saacos(dot_v2v2(vec_prev, vec_tmp)) / 2.0f);

	for (i = 0; i < num; i++) {
		sub_v2_v2v2(vec_curr, verts[i], point);
		mag_next = normalize_v2(vec_curr);

		tan_curr = tan(saacos(dot_v2v2(vec_curr, vec_prev)) / 2.0f);

		ind_curr = (i == 0) ? (num - 1) : (i - 1);

		if (mag_curr >= FLT_EPSILON) {
			w[ind_curr] = (tan_prev + tan_curr) / mag_curr;
			tot_w += w[ind_curr];
		}
		else {
			for (i = 0; i < num; i++) {
				w[i] = 0.0f;
			}
			w[ind_curr] = 1.0f;
			return;
		}

		mag_curr = mag_next;
		tan_prev = tan_curr;
		copy_v2_v2(vec_prev, vec_curr);
	}

	for (i = 0; i < num; i++) {
		w[i] /= tot_w;
	}
}

BLI_INLINE int nearestVert(SDefBindCalcData * const data, const float point_co[3])
{
	const MVert * const mvert = data->mvert;
	BVHTreeNearest nearest = {.dist_sq = FLT_MAX, .index = -1};
	const MPoly *poly;
	const MEdge *edge;
	const MLoop *loop;
	float max_dist = FLT_MAX;
	float dist;
	int index;
	int i;

	BLI_bvhtree_find_nearest(data->treeData->tree, point_co, &nearest, data->treeData->nearest_callback, data->treeData);

	poly = &data->mpoly[data->looptri[nearest.index].poly];
	loop = &data->mloop[poly->loopstart];

	for (i = 0; i < poly->totloop; i++, loop++) {
		edge = &data->medge[loop->e];
		dist = dist_squared_to_line_segment_v3(point_co, mvert[edge->v1].co, mvert[edge->v1].co);

		if (dist < max_dist) {
			max_dist = dist;
			index = loop->e;
		}
	}

	edge = &data->medge[index];
	if (len_squared_v3v3(point_co, mvert[edge->v1].co) < len_squared_v3v3(point_co, mvert[edge->v2].co)) {
		return edge->v1;
	}
	else {
		return edge->v2;
	}
}

BLI_INLINE bool isPolyValid(const float coords[][2], const int nr)
{
	float prev_co[2];
	float curr_vec[2], prev_vec[2];
	int i;

	if (!is_poly_convex_v2(coords, nr)) {
		printf("Surface Deform: Target containts concave polys\n");
		return false;
	}

	copy_v2_v2(prev_co, coords[nr - 1]);
	sub_v2_v2v2(prev_vec, prev_co, coords[nr - 2]);

	for (i = 0; i < nr; i++) {
		sub_v2_v2v2(curr_vec, coords[i], prev_co);

		if (len_v2(curr_vec) < FLT_EPSILON) {
			printf("Surface Deform: Target containts overlapping verts\n");
			return false;
		}

		if (1.0f - dot_v2v2(prev_vec, curr_vec) < FLT_EPSILON) {
			printf("Surface Deform: Target containts concave polys\n");
			return false;
		}

		copy_v2_v2(prev_co, coords[i]);
		copy_v2_v2(prev_vec, curr_vec);
	}

	return true;
}

static void freeBindData(SDefBindWeightData * const bwdata)
{
	SDefBindPoly *bpoly;

	for (bpoly = bwdata->bind_polys; bpoly; bpoly = bwdata->bind_polys) {
		bwdata->bind_polys = bpoly->next;

		if (bpoly->coords) {
			MEM_freeN(bpoly->coords);
		}

		if (bpoly->coords_v2) {
			MEM_freeN(bpoly->coords_v2);
		}

		MEM_freeN(bpoly);
	}

	MEM_freeN(bwdata);
}

BLI_INLINE SDefBindWeightData *computeBindWeights(SDefBindCalcData * const data, const float point_co[3])
{
	const int nearest = nearestVert(data, point_co);
	const SDefAdjacency * const vert_edges = data->vert_edges[nearest];
	const SDefEdgePolys * const edge_polys = data->edge_polys;

	const SDefAdjacency *vedge;
	const MPoly *poly;
	const MLoop *loop;

	SDefBindWeightData *bwdata;
	SDefBindPoly *bpoly;

	float world[3] = {0.0f, 0.0f, 1.0f};
	float avg_point_dist = 0.0f;
	float tot_weight = 0.0f;
	int inf_weight_flags = 0;
	int numpoly = 0;
	int i, j;

	bwdata = MEM_callocN(sizeof(*bwdata), "SDefBindWeightData");
	if (bwdata == NULL) {
		data->success = 0;
		return NULL;
	}

	/* Loop over all adjacent edges, and build the SDefBindPoly data for each poly adjacent to those */
	for (vedge = vert_edges; vedge; vedge = vedge->next) {
		int edge_ind = vedge->index;

		for (i = 0; i < edge_polys[edge_ind].num; i++) {
			for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
				if (bpoly->index == edge_polys[edge_ind].polys[i]) {
					break;
				}
			}

			/* Check if poly was already created by another edge or still has to be initialized */
			if (!bpoly || bpoly->index != edge_polys[edge_ind].polys[i]) {
				float angle;
				float axis[3];
				float tmp_vec_v2[2];

				/* SDefBindPoly initialization */
				bpoly = MEM_mallocN(sizeof(*bpoly), "SDefBindPoly");
				if (bpoly == NULL) {
					freeBindData(bwdata);
					data->success = 0;
					return NULL;
				}

				bpoly->next = bwdata->bind_polys;
				bpoly->index = edge_polys[edge_ind].polys[i];
				bpoly->coords = NULL;
				bpoly->coords_v2 = NULL;
				bwdata->bind_polys = bpoly;

				numpoly++;

				/* Copy poly data */
				poly = &data->mpoly[bpoly->index];
				loop = &data->mloop[poly->loopstart];

				bpoly->numverts = poly->totloop;
				bpoly->loopstart = poly->loopstart;

				bpoly->coords = MEM_mallocN(sizeof(*bpoly->coords) * poly->totloop, "SDefBindPolyCoords");
				if (bpoly->coords == NULL) {
					freeBindData(bwdata);
					data->success = 0;
					return NULL;
				}

				bpoly->coords_v2 = MEM_mallocN(sizeof(*bpoly->coords_v2) * poly->totloop, "SDefBindPolyCoords_v2");
				if (bpoly->coords_v2 == NULL) {
					freeBindData(bwdata);
					data->success = 0;
					return NULL;
				}

				for (j = 0; j < poly->totloop; j++, loop++) {
					copy_v3_v3(bpoly->coords[j], data->mvert[loop->v].co);

					/* Find corner and edge indices within poly loop array */
					if (loop->v == nearest) {
						bpoly->corner_ind = j;
						bpoly->edge_vert_inds[0] = (j == 0) ? (poly->totloop - 1) : (j - 1);
						bpoly->edge_vert_inds[1] = (j == poly->totloop - 1) ? (0) : (j + 1);

						bpoly->edge_inds[0] = data->mloop[poly->loopstart + bpoly->edge_vert_inds[0]].e;
						bpoly->edge_inds[1] = loop->e;
					}
				}

				/* Compute poly's parametric data */
				mid_v3_v3_array(bpoly->centroid, bpoly->coords, poly->totloop);
				normal_poly_v3(bpoly->normal, bpoly->coords, poly->totloop);

				/* Compute poly skew angle and axis */
				angle = saacos(dot_v3v3(bpoly->normal, world));

				cross_v3_v3v3(axis, bpoly->normal, world);
				normalize_v3(axis);

				/* Map coords onto 2d normal plane */
				map_to_plane_axis_angle_v2_v3v3fl(bpoly->point_v2, point_co, axis, angle);

				zero_v2(bpoly->centroid_v2);
				for (j = 0; j < poly->totloop; j++) {
					map_to_plane_axis_angle_v2_v3v3fl(bpoly->coords_v2[j], bpoly->coords[j], axis, angle);
					madd_v2_v2fl(bpoly->centroid_v2, bpoly->coords_v2[j], 1.0f / poly->totloop);
				}

				if (!isPolyValid(bpoly->coords_v2, poly->totloop)) {
					freeBindData(bwdata);
					data->success = -1;
					return NULL;
				}

				bpoly->inside = isect_point_poly_v2(bpoly->point_v2, bpoly->coords_v2, poly->totloop, false);

				/* Initialize weight components */
				bpoly->weight_components[0] = 1.0f;
				bpoly->weight_components[1] = len_v2v2(bpoly->centroid_v2, bpoly->point_v2);
				bpoly->weight_components[2] = len_v3v3(bpoly->centroid, point_co);

				avg_point_dist += bpoly->weight_components[2];

				/* Compute centroid to mid-edge vectors */
				mid_v2_v2v2(bpoly->cent_edgemid_vecs_v2[0],
				            bpoly->coords_v2[bpoly->edge_vert_inds[0]],
				            bpoly->coords_v2[bpoly->corner_ind]);

				mid_v2_v2v2(bpoly->cent_edgemid_vecs_v2[1],
				            bpoly->coords_v2[bpoly->edge_vert_inds[1]],
				            bpoly->coords_v2[bpoly->corner_ind]);

				sub_v2_v2(bpoly->cent_edgemid_vecs_v2[0], bpoly->centroid_v2);
				sub_v2_v2(bpoly->cent_edgemid_vecs_v2[1], bpoly->centroid_v2);

				/* Compute poly scales with respect to mid-edges, and normalize the vectors */
				bpoly->scales[0] = normalize_v2(bpoly->cent_edgemid_vecs_v2[0]);
				bpoly->scales[1] = normalize_v2(bpoly->cent_edgemid_vecs_v2[1]);

				/* Compute the required polygon angles */
				bpoly->edgemid_angle = saacos(dot_v2v2(bpoly->cent_edgemid_vecs_v2[0], bpoly->cent_edgemid_vecs_v2[1]));

				sub_v2_v2v2(tmp_vec_v2, bpoly->coords_v2[bpoly->corner_ind], bpoly->centroid_v2);
				normalize_v2(tmp_vec_v2);

				bpoly->corner_edgemid_angles[0] = saacos(dot_v2v2(tmp_vec_v2, bpoly->cent_edgemid_vecs_v2[0]));
				bpoly->corner_edgemid_angles[1] = saacos(dot_v2v2(tmp_vec_v2, bpoly->cent_edgemid_vecs_v2[1]));

				/* Check for inifnite weights, and compute angular data otherwise */
				if (bpoly->weight_components[2] < FLT_EPSILON) {
					inf_weight_flags |= 1 << 1;
					inf_weight_flags |= 1 << 2;
				}
				else if (bpoly->weight_components[1] < FLT_EPSILON) {
					inf_weight_flags |= 1 << 1;
				}
				else {
					float cent_point_vec[2];

					sub_v2_v2v2(cent_point_vec, bpoly->point_v2, bpoly->centroid_v2);
					normalize_v2(cent_point_vec);

					bpoly->point_edgemid_angles[0] = saacos(dot_v2v2(cent_point_vec, bpoly->cent_edgemid_vecs_v2[0]));
					bpoly->point_edgemid_angles[1] = saacos(dot_v2v2(cent_point_vec, bpoly->cent_edgemid_vecs_v2[1]));
				}
			}
		}
	}

	avg_point_dist /= numpoly;

	/* If weights 1 and 2 are not infinite, loop over all adjacent edges again,
	 * and build adjacency dependent angle data (depends on all polygons having been computed) */
	if (!inf_weight_flags) {
		for (vedge = vert_edges; vedge; vedge = vedge->next) {
			SDefBindPoly *bpolys[2];
			const SDefEdgePolys *epolys;
			float tmp1, tmp2;
			int edge_ind = vedge->index;
			int edge_on_poly[2];

			epolys = &edge_polys[edge_ind];

			/* Find bind polys corresponding to the edge's adjacent polys */
			for (bpoly = bwdata->bind_polys, i = 0; bpoly && i < epolys->num; bpoly = bpoly->next)
			{
				if (ELEM(bpoly->index, epolys->polys[0], epolys->polys[1])) {
					bpolys[i] = bpoly;

					if (bpoly->edge_inds[0] == edge_ind) {
						edge_on_poly[i] = 0;
					}
					else {
						edge_on_poly[i] = 1;
					}

					i++;
				}
			}

			/* Compute angular weight component */
			/* Attention! Same operations have to be done in both conditions below! */
			if (epolys->num == 1) {
				tmp1 = bpolys[0]->point_edgemid_angles[edge_on_poly[0]];
				tmp1 /= bpolys[0]->edgemid_angle;
				tmp1 *= M_PI_2;
				tmp1 = sinf(tmp1);

				bpolys[0]->weight_components[0] *= tmp1 * tmp1;
			}
			else if (epolys->num == 2) {
				tmp1 = bpolys[0]->point_edgemid_angles[edge_on_poly[0]];
				tmp2 = bpolys[1]->point_edgemid_angles[edge_on_poly[1]];
				tmp1 /= bpolys[0]->edgemid_angle;
				tmp2 /= bpolys[1]->edgemid_angle;
				tmp1 *= M_PI_2;
				tmp2 *= M_PI_2;
				tmp1 = sinf(tmp1);
				tmp2 = sinf(tmp2);

				bpolys[0]->weight_components[0] *= tmp1 * tmp2;
				bpolys[1]->weight_components[0] *= tmp1 * tmp2;
			}
		}
	}

	/* Compute scalings and falloff.
	 * Scale all weights if no infinite weight is found,
	 * scale only unprojected weight if projected weight is infinite,
	 * scale none if both are infinite. */
	if (!inf_weight_flags) {
		for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
			float tmp1 = bpoly->point_edgemid_angles[0] / bpoly->corner_edgemid_angles[0];
			float tmp2 = bpoly->point_edgemid_angles[1] / bpoly->corner_edgemid_angles[1];
			float scale_weight, sqr, inv_sqr;

			if (isnanf(tmp1) || isnanf(tmp2)) {
				freeBindData(bwdata);
				data->success = -1;
				/* I know this message is vague, but I could not think of a way
				 * to explain this whith a reasonably sized message.
				 * Though it shouldn't really matter all that much,
				 * because this is very unlikely to occur */
				printf("Surface Deform: Target contains invalid polys\n");
				return NULL;
			}

			/* Find which edge the point is closer to */
			if (tmp1 < tmp2) {
				bpoly->dominant_edge = 0;
				bpoly->dominant_angle_weight = tmp1;
			}
			else {
				bpoly->dominant_edge = 1;
				bpoly->dominant_angle_weight = tmp2;
			}

			bpoly->dominant_angle_weight = sinf(bpoly->dominant_angle_weight * M_PI_2);

			/* Compute quadratic angular scale interpolation weight */
			scale_weight = bpoly->point_edgemid_angles[bpoly->dominant_edge] / bpoly->edgemid_angle;
			scale_weight /= scale_weight + (bpoly->point_edgemid_angles[!bpoly->dominant_edge] / bpoly->edgemid_angle);

			sqr = scale_weight * scale_weight;
			inv_sqr = 1.0f - scale_weight;
			inv_sqr *= inv_sqr;
			scale_weight = sqr / (sqr + inv_sqr);

			/* Compute interpolated scale (no longer need the individual scales,
			 * so simply storing the result over the scale in index zero) */
			bpoly->scales[0] = bpoly->scales[bpoly->dominant_edge] * (1.0f - scale_weight) +
			                   bpoly->scales[!bpoly->dominant_edge] * scale_weight;

			/* Scale the point distance weights, and introduce falloff */
			bpoly->weight_components[1] /= bpoly->scales[0];
			bpoly->weight_components[1] = powf(bpoly->weight_components[1], data->falloff);

			bpoly->weight_components[2] /= avg_point_dist;
			bpoly->weight_components[2] = powf(bpoly->weight_components[2], data->falloff);

			/* Re-check for infinite weights, now that all scalings and interpolations are computed */
			if (bpoly->weight_components[2] < FLT_EPSILON) {
				inf_weight_flags |= 1 << 1;
				inf_weight_flags |= 1 << 2;
			}
			else if (bpoly->weight_components[1] < FLT_EPSILON) {
				inf_weight_flags |= 1 << 1;
			}
			else if (bpoly->weight_components[0] < FLT_EPSILON) {
				inf_weight_flags |= 1 << 0;
			}
		}
	}
	else if (!(inf_weight_flags & (1 << 2))) {
		for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
			/* Scale the point distance weight by average point distance, and introduce falloff */
			bpoly->weight_components[2] /= avg_point_dist;
			bpoly->weight_components[2] = powf(bpoly->weight_components[2], data->falloff);

			/* Re-check for infinite weights, now that all scalings and interpolations are computed */
			if (bpoly->weight_components[2] < FLT_EPSILON) {
				inf_weight_flags |= 1 << 2;
			}
		}
	}

	/* Final loop, to compute actual weights */
	for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
		/* Weight computation from components */
		if (inf_weight_flags & 1 << 2) {
			bpoly->weight = bpoly->weight_components[2] < FLT_EPSILON ? 1.0f : 0.0f;
		}
		else if (inf_weight_flags & 1 << 1) {
			bpoly->weight = bpoly->weight_components[1] < FLT_EPSILON ?
			                1.0f / bpoly->weight_components[2] : 0.0f;
		}
		else if (inf_weight_flags & 1 << 0) {
			bpoly->weight = bpoly->weight_components[0] < FLT_EPSILON ?
			                1.0f / bpoly->weight_components[1] / bpoly->weight_components[2] : 0.0f;
		}
		else {
			bpoly->weight = 1.0f / bpoly->weight_components[0] /
			                       bpoly->weight_components[1] /
			                       bpoly->weight_components[2];
		}

		tot_weight += bpoly->weight;
	}

	for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
		bpoly->weight /= tot_weight;

		/* Evaluate if this poly is relevant to bind */
		/* Even though the weights should add up to 1.0,
		 * the losses of weights smaller than epsilon here
		 * should be negligible... */
		if (bpoly->weight >= FLT_EPSILON) {
			if (bpoly->inside) {
				bwdata->numbinds += 1;
			}
			else {
				if (bpoly->dominant_angle_weight < FLT_EPSILON || 1.0f - bpoly->dominant_angle_weight < FLT_EPSILON) {
					bwdata->numbinds += 1;
				}
				else {
					bwdata->numbinds += 2;
				}
			}
		}
	}

	return bwdata;
}

BLI_INLINE float computeNormalDisplacement(const float point_co[3], const float point_co_proj[3], const float normal[3])
{
	float disp_vec[3];
	float normal_dist;

	sub_v3_v3v3(disp_vec, point_co, point_co_proj);
	normal_dist = len_v3(disp_vec);

	if (dot_v3v3(disp_vec, normal) < 0) {
		normal_dist *= -1;
	}

	return normal_dist;
}

static void bindVert(void *userdata, void *UNUSED(userdata_chunk), const int index, const int UNUSED(threadid))
{
	SDefBindCalcData * const data = (SDefBindCalcData *) userdata;
	float point_co[3];
	float point_co_proj[3];

	SDefBindWeightData *bwdata;
	SDefVert *sdvert = data->bind_verts + index;
	SDefBindPoly *bpoly;
	SDefBind *sdbind;

	int i;

	if (data->success != 1) {
		sdvert->binds = NULL;
		sdvert->numbinds = 0;
		return;
	}

	copy_v3_v3(point_co, data->vertexCos[index]);
	bwdata = computeBindWeights(data, point_co);

	if (bwdata == NULL) {
		sdvert->binds = NULL;
		sdvert->numbinds = 0;
		return;
	}

	sdvert->binds = MEM_callocN(sizeof(*sdvert->binds) * bwdata->numbinds, "SDefVertBindData");
	if (sdvert->binds == NULL) {
		data->success = 0;
		sdvert->numbinds = 0;
		return;
	}

	sdvert->numbinds = bwdata->numbinds;

	sdbind = sdvert->binds;

	for (bpoly = bwdata->bind_polys; bpoly; bpoly = bpoly->next) {
		if (bpoly->weight >= FLT_EPSILON) {
			if (bpoly->inside) {
				const MLoop *loop = &data->mloop[bpoly->loopstart];

				sdbind->influence = bpoly->weight;
				sdbind->numverts = bpoly->numverts;

				sdbind->mode = MOD_SDEF_MODE_NGON;
				sdbind->vert_weights = MEM_mallocN(sizeof(*sdbind->vert_weights) * bpoly->numverts, "SDefNgonVertWeights");
				if (sdbind->vert_weights == NULL) {
					data->success = 0;
					return;
				}

				sdbind->vert_inds = MEM_mallocN(sizeof(*sdbind->vert_inds) * bpoly->numverts, "SDefNgonVertInds");
				if (sdbind->vert_inds == NULL) {
					data->success = 0;
					return;
				}

				meanValueCoordinates(sdbind->vert_weights, bpoly->point_v2, bpoly->coords_v2, bpoly->numverts);

				/* Reproject vert based on weights and original poly verts, to reintroduce poly non-planarity */
				zero_v3(point_co_proj);
				for (i = 0; i < bpoly->numverts; i++, loop++) {
					madd_v3_v3fl(point_co_proj, bpoly->coords[i], sdbind->vert_weights[i]);
					sdbind->vert_inds[i] = loop->v;
				}

				sdbind->normal_dist = computeNormalDisplacement(point_co, point_co_proj, bpoly->normal);

				sdbind++;
			}
			else {
				float tmp_vec[3];
				float cent[3], norm[3];
				float v1[3], v2[3], v3[3];

				if (1.0f - bpoly->dominant_angle_weight >= FLT_EPSILON) {
					sdbind->influence = bpoly->weight * (1.0f - bpoly->dominant_angle_weight);
					sdbind->numverts = bpoly->numverts;

					sdbind->mode = MOD_SDEF_MODE_CENTROID;
					sdbind->vert_weights = MEM_mallocN(sizeof(*sdbind->vert_weights) * 3, "SDefCentVertWeights");
					if (sdbind->vert_weights == NULL) {
						data->success = 0;
						return;
					}

					sdbind->vert_inds = MEM_mallocN(sizeof(*sdbind->vert_inds) * bpoly->numverts, "SDefCentVertInds");
					if (sdbind->vert_inds == NULL) {
						data->success = 0;
						return;
					}

					sortPolyVertsEdge(sdbind->vert_inds, &data->mloop[bpoly->loopstart],
					                  bpoly->edge_inds[bpoly->dominant_edge], bpoly->numverts);

					copy_v3_v3(v1, data->mvert[sdbind->vert_inds[0]].co);
					copy_v3_v3(v2, data->mvert[sdbind->vert_inds[1]].co);
					copy_v3_v3(v3, bpoly->centroid);

					mid_v3_v3v3v3(cent, v1, v2, v3);
					normal_tri_v3(norm, v1, v2, v3);

					add_v3_v3v3(tmp_vec, point_co, bpoly->normal);

					isect_line_plane_v3(point_co_proj, point_co, tmp_vec, cent, norm);

					interp_weights_face_v3(sdbind->vert_weights, v1, v2, v3, NULL, point_co_proj);

					sdbind->normal_dist = computeNormalDisplacement(point_co, point_co_proj, bpoly->normal);

					sdbind++;
				}

				if (bpoly->dominant_angle_weight >= FLT_EPSILON) {
					sdbind->influence = bpoly->weight * bpoly->dominant_angle_weight;
					sdbind->numverts = bpoly->numverts;

					sdbind->mode = MOD_SDEF_MODE_LOOPTRI;
					sdbind->vert_weights = MEM_mallocN(sizeof(*sdbind->vert_weights) * 3, "SDefTriVertWeights");
					if (sdbind->vert_weights == NULL) {
						data->success = 0;
						return;
					}

					sdbind->vert_inds = MEM_mallocN(sizeof(*sdbind->vert_inds) * bpoly->numverts, "SDefTriVertInds");
					if (sdbind->vert_inds == NULL) {
						data->success = 0;
						return;
					}

					sortPolyVertsTri(sdbind->vert_inds, &data->mloop[bpoly->loopstart], bpoly->edge_vert_inds[0], bpoly->numverts);

					copy_v3_v3(v1, data->mvert[sdbind->vert_inds[0]].co);
					copy_v3_v3(v2, data->mvert[sdbind->vert_inds[1]].co);
					copy_v3_v3(v3, data->mvert[sdbind->vert_inds[2]].co);

					mid_v3_v3v3v3(cent, v1, v2, v3);
					normal_tri_v3(norm, v1, v2, v3);

					add_v3_v3v3(tmp_vec, point_co, bpoly->normal);

					isect_line_plane_v3(point_co_proj, point_co, tmp_vec, cent, norm);

					interp_weights_face_v3(sdbind->vert_weights, v1, v2, v3, NULL, point_co_proj);

					sdbind->normal_dist = computeNormalDisplacement(point_co, point_co_proj, bpoly->normal);

					sdbind++;
				}
			}
		}
	}

	freeBindData(bwdata);
}

static bool surfacedeformBind(SurfaceDeformModifierData *smd, float (*vertexCos)[3],
                              int numverts, int tnumpoly, DerivedMesh *tdm)
{
	BVHTreeFromMesh treeData = {NULL};
	const MPoly *mpoly = tdm->getPolyArray(tdm);
	const MEdge *medge = tdm->getEdgeArray(tdm);
	const MLoop *mloop = tdm->getLoopArray(tdm);
	int tnumedges = tdm->getNumEdges(tdm);
	int tnumverts = tdm->getNumVerts(tdm);
	int adj_result;
	SDefAdjacency **vert_edges;
	SDefEdgePolys *edge_polys;

	vert_edges = MEM_callocN(sizeof(*vert_edges) * tnumverts, "SDefVertEdgeMap");
	if (vert_edges == NULL) {
		printf("Surface Deform: Out of memory\n");
		return false;
	}

	edge_polys = MEM_callocN(sizeof(*edge_polys) * tnumedges, "SDefEdgeFaceMap");
	if (edge_polys == NULL) {
		printf("Surface Deform: Out of memory\n");
		MEM_freeN(vert_edges);
		return false;
	}

	smd->verts = MEM_mallocN(sizeof(*smd->verts) * numverts, "SDefBindVerts");
	if (smd->verts == NULL) {
		printf("Surface Deform: Out of memory\n");
		MEM_freeN(vert_edges);
		MEM_freeN(edge_polys);
		return false;
	}

	bvhtree_from_mesh_looptri(&treeData, tdm, 0.0, 2, 6);
	if (treeData.tree == NULL) {
		printf("Surface Deform: Out of memory\n");
		MEM_freeN(vert_edges);
		MEM_freeN(edge_polys);
		MEM_freeN(smd->verts);
		smd->verts = NULL;
		return false;
	}

	adj_result = buildAdjacencyMap(mpoly, medge, mloop, tnumpoly, tnumedges, vert_edges, edge_polys);

	if(adj_result == 0) {
		printf("Surface Deform: Out of memory\n");
		freeAdjacencyMap(vert_edges, edge_polys, tnumverts);
		free_bvhtree_from_mesh(&treeData);
		MEM_freeN(smd->verts);
		smd->verts = NULL;
		return false;
	}

	if (adj_result == -1) {
		printf("Surface Deform: Invalid target mesh\n");
		freeAdjacencyMap(vert_edges, edge_polys, tnumverts);
		free_bvhtree_from_mesh(&treeData);
		MEM_freeN(smd->verts);
		smd->verts = NULL;
		return false;
	}

	smd->numverts = numverts;
	smd->numpoly = tnumpoly;

	SDefBindCalcData data = {.treeData = &treeData,
		                     .vert_edges = vert_edges,
		                     .edge_polys = edge_polys,
		                     .mpoly = mpoly,
		                     .medge = medge,
		                     .mloop = mloop,
		                     .looptri = tdm->getLoopTriArray(tdm),
		                     .mvert = tdm->getVertArray(tdm),
		                     .bind_verts = smd->verts,
		                     .vertexCos = vertexCos,
		                     .falloff = smd->falloff,
		                     .success = 1};

	BLI_task_parallel_range_ex(0, numverts, &data, NULL, 0, bindVert,
	                           numverts > 10000, false);

	if (data.success == 0) {
		printf("Surface Deform: Out of memory\n");
		freeData((ModifierData *) smd);
	}
	else if (data.success == -1) {
		printf("Surface Deform: Invalid target mesh\n");
		freeData((ModifierData *) smd);
	}

	freeAdjacencyMap(vert_edges, edge_polys, tnumverts);
	free_bvhtree_from_mesh(&treeData);

	return data.success == 1;
}

static void surfacedeformModifier_do(ModifierData *md, float (*vertexCos)[3], int numverts)
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;
	DerivedMesh *tdm;
	int tnumpoly;
	SDefVert *sdvert;
	SDefBind *sdbind;
	const MVert *mvert;
	int i, j, k;
	float norm[3], temp[3];

	/* Exit function if bind flag is not set (free bind data if any) */
	if (!(smd->flags & MOD_SDEF_BIND)) {
		freeData(md);
		return;
	}

	/* Handle target mesh both in and out of edit mode */
	if (smd->target == md->scene->obedit) {
		BMEditMesh *em = BKE_editmesh_from_object(smd->target);
		tdm = em->derivedFinal;
	}
	else {
		tdm = smd->target->derivedFinal;
	}

	tnumpoly = tdm->getNumPolys(tdm);

	/* If not bound, execute bind */
	if (!(smd->verts)) {
		if (!surfacedeformBind(smd, vertexCos, numverts, tnumpoly, tdm)) {
			smd->flags &= ~MOD_SDEF_BIND;
			return;
		}
	}

	/* Poly count checks */
	if (smd->numverts != numverts) {
		modifier_setError(md, "Verts changed from %d to %d", smd->numverts, numverts);
		tdm->release(tdm);
		return;
	}
	else if (smd->numpoly != tnumpoly) {
		modifier_setError(md, "Target polygons changed from %d to %d", smd->numpoly, tnumpoly);
		tdm->release(tdm);
		return;
	}

	/* Actual vertex location update starts here */
	mvert = tdm->getVertArray(tdm);
	sdvert = smd->verts;

	for (i = 0; i < numverts; i++, sdvert++) {
		sdbind = sdvert->binds;

		zero_v3(vertexCos[i]);

		for (j = 0; j < sdvert->numbinds; j++, sdbind++) {
			/* Mode-generic operations (allocate poly coordinates) */
			float (*coords)[3] = MEM_mallocN(sizeof(*coords) * sdbind->numverts, "SDefDoPolyCoords");

			for (k = 0; k < sdbind->numverts; k++) {
				copy_v3_v3(coords[k], mvert[sdbind->vert_inds[k]].co);
			}

			normal_poly_v3(norm, coords, sdbind->numverts);
			zero_v3(temp);

			/* ---------- looptri mode ---------- */
			if (sdbind->mode == MOD_SDEF_MODE_LOOPTRI) {
				madd_v3_v3fl(temp, mvert[sdbind->vert_inds[0]].co, sdbind->vert_weights[0]);
				madd_v3_v3fl(temp, mvert[sdbind->vert_inds[1]].co, sdbind->vert_weights[1]);
				madd_v3_v3fl(temp, mvert[sdbind->vert_inds[2]].co, sdbind->vert_weights[2]);
			}
			else {
				/* ---------- ngon mode ---------- */
				if (sdbind->mode == MOD_SDEF_MODE_NGON) {
					for (k = 0; k < sdbind->numverts; k++) {
						madd_v3_v3fl(temp, coords[k], sdbind->vert_weights[k]);
					}
				}

				/* ---------- centroid mode ---------- */
				else if (sdbind->mode == MOD_SDEF_MODE_CENTROID) {
					float cent[3];
					mid_v3_v3_array(cent, coords, sdbind->numverts);

					madd_v3_v3fl(temp, mvert[sdbind->vert_inds[0]].co, sdbind->vert_weights[0]);
					madd_v3_v3fl(temp, mvert[sdbind->vert_inds[1]].co, sdbind->vert_weights[1]);
					madd_v3_v3fl(temp, cent, sdbind->vert_weights[2]);
				}
			}

			MEM_freeN(coords);

			/* Apply normal offset (generic for all modes) */
			madd_v3_v3fl(temp, norm, sdbind->normal_dist);

			madd_v3_v3fl(vertexCos[i], temp, sdbind->influence);
		}
	}

	tdm->release(tdm);
}

static void deformVerts(ModifierData *md, Object *UNUSED(ob),
                        DerivedMesh *UNUSED(derivedData),
                        float (*vertexCos)[3], int numVerts,
                        ModifierApplyFlag UNUSED(flag))
{
	surfacedeformModifier_do(md, vertexCos, numVerts);
}

static void deformVertsEM(ModifierData *md, Object *UNUSED(ob),
                          struct BMEditMesh *UNUSED(editData),
                          DerivedMesh *UNUSED(derivedData),
                          float (*vertexCos)[3], int numVerts)
{
	surfacedeformModifier_do(md, vertexCos, numVerts);
}

static bool isDisabled(ModifierData *md, int UNUSED(useRenderParams))
{
	SurfaceDeformModifierData *smd = (SurfaceDeformModifierData *) md;

	return !smd->target;
}

ModifierTypeInfo modifierType_SurfaceDeform = {
	/* name */              "Surface Deform",
	/* structName */        "SurfaceDeformModifierData",
	/* structSize */        sizeof(SurfaceDeformModifierData),
	/* type */              eModifierTypeType_OnlyDeform,
	/* flags */             eModifierTypeFlag_AcceptsMesh |
	                        eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       deformVerts,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     deformVertsEM,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          freeData,
	/* isDisabled */        isDisabled,
	/* updateDepgraph */    updateDepgraph,
	/* updateDepsgraph */   updateDepsgraph,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */  NULL,
	/* foreachObjectLink */ foreachObjectLink,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
