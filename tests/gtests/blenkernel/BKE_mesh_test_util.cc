/* Apache License, Version 2.0 */

#include <fstream>
#include <iostream>
#include <iomanip>

#include "BKE_mesh_test_util.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "BLI_math.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_customdata.h"
#include "BKE_mesh.h"
}

static void mesh_update(Mesh *mesh, int calc_edges, int calc_tessface)
{
	bool tessface_input = false;

	if (mesh->totface > 0 && mesh->totpoly == 0) {
		BKE_mesh_convert_mfaces_to_mpolys(mesh);

		/* would only be converting back again, don't bother */
		tessface_input = true;
	}

	if (calc_edges || ((mesh->totpoly || mesh->totface) && mesh->totedge == 0))
		BKE_mesh_calc_edges(mesh, calc_edges, true);

	if (calc_tessface) {
		if (tessface_input == false) {
			BKE_mesh_tessface_calc(mesh);
		}
	}
	else {
		/* default state is not to have tessface's so make sure this is the case */
		BKE_mesh_tessface_clear(mesh);
	}

	BKE_mesh_calc_normals(mesh);
}

static void mesh_add_verts(Mesh *mesh, int len)
{
	CustomData vdata;

	if (len == 0)
		return;

	int totvert = mesh->totvert + len;
	CustomData_copy(&mesh->vdata, &vdata, CD_MASK_MESH, CD_DEFAULT, totvert);
	CustomData_copy_data(&mesh->vdata, &vdata, 0, 0, mesh->totvert);

	if (!CustomData_has_layer(&vdata, CD_MVERT))
		CustomData_add_layer(&vdata, CD_MVERT, CD_CALLOC, NULL, totvert);

	CustomData_free(&mesh->vdata, mesh->totvert);
	mesh->vdata = vdata;
	BKE_mesh_update_customdata_pointers(mesh, false);

	/* scan the input list and insert the new vertices */

	/* set final vertex list size */
	mesh->totvert = totvert;
}

static void mesh_add_edges(Mesh *mesh, int len)
{
	CustomData edata;
	MEdge *medge;
	int i, totedge;

	if (len == 0)
		return;

	totedge = mesh->totedge + len;

	/* update customdata  */
	CustomData_copy(&mesh->edata, &edata, CD_MASK_MESH, CD_DEFAULT, totedge);
	CustomData_copy_data(&mesh->edata, &edata, 0, 0, mesh->totedge);

	if (!CustomData_has_layer(&edata, CD_MEDGE))
		CustomData_add_layer(&edata, CD_MEDGE, CD_CALLOC, NULL, totedge);

	CustomData_free(&mesh->edata, mesh->totedge);
	mesh->edata = edata;
	BKE_mesh_update_customdata_pointers(mesh, false); /* new edges don't change tessellation */

	/* set default flags */
	medge = &mesh->medge[mesh->totedge];
	for (i = 0; i < len; i++, medge++)
		medge->flag = ME_EDGEDRAW | ME_EDGERENDER;

	mesh->totedge = totedge;
}

static void mesh_add_loops(Mesh *mesh, int len)
{
	CustomData ldata;
	int totloop;

	if (len == 0)
		return;

	totloop = mesh->totloop + len;   /* new face count */

	/* update customdata */
	CustomData_copy(&mesh->ldata, &ldata, CD_MASK_MESH, CD_DEFAULT, totloop);
	CustomData_copy_data(&mesh->ldata, &ldata, 0, 0, mesh->totloop);

	if (!CustomData_has_layer(&ldata, CD_MLOOP))
		CustomData_add_layer(&ldata, CD_MLOOP, CD_CALLOC, NULL, totloop);

	CustomData_free(&mesh->ldata, mesh->totloop);
	mesh->ldata = ldata;
	BKE_mesh_update_customdata_pointers(mesh, true);

	mesh->totloop = totloop;
}

static void mesh_add_polys(Mesh *mesh, int len)
{
	CustomData pdata;
	MPoly *mpoly;
	int i, totpoly;

	if (len == 0)
		return;

	totpoly = mesh->totpoly + len;   /* new face count */

	/* update customdata */
	CustomData_copy(&mesh->pdata, &pdata, CD_MASK_MESH, CD_DEFAULT, totpoly);
	CustomData_copy_data(&mesh->pdata, &pdata, 0, 0, mesh->totpoly);

	if (!CustomData_has_layer(&pdata, CD_MPOLY))
		CustomData_add_layer(&pdata, CD_MPOLY, CD_CALLOC, NULL, totpoly);

	CustomData_free(&mesh->pdata, mesh->totpoly);
	mesh->pdata = pdata;
	BKE_mesh_update_customdata_pointers(mesh, true);

	/* set default flags */
	mpoly = &mesh->mpoly[mesh->totpoly];
	for (i = 0; i < len; i++, mpoly++)
		mpoly->flag = ME_FACE_SEL;

	mesh->totpoly = totpoly;
}

Mesh* BKE_mesh_test_from_data(
        const float (*verts)[3], int numverts,
        const int (*edges)[2], int numedges,
        const int *loops, const int *face_lengths, int numfaces)
{
	Mesh *me = (Mesh *)MEM_callocN(sizeof(Mesh), "Mesh");
	
	BKE_mesh_init(me);
	
	int numloops = 0;
	for (int i = 0; i < numfaces; ++i) {
		numloops += face_lengths[i];
	}
	
	mesh_add_verts(me, numverts);
	mesh_add_edges(me, numedges);
	mesh_add_loops(me, numloops);
	mesh_add_polys(me, numfaces);
	
	{
		MVert *v = me->mvert;
		for (int i = 0; i < numverts; ++i, ++v) {
			copy_v3_v3(v->co, verts[i]);
		}
	}
	
	{
		MEdge *e = me->medge;
		for (int i = 0; i < numedges; ++i, ++e) {
			e->v1 = edges[i][0];
			e->v2 = edges[i][1];
		}
	}
	
	{
		MLoop *l = me->mloop;
		for (int i = 0; i < numloops; ++i, ++l) {
			l->v = loops[i];
		}
	}
	
	{
		MPoly *p = me->mpoly;
		int loopstart = 0;
		for (int i = 0; i < numfaces; ++i, ++p) {
			int totloop = face_lengths[i];
			p->loopstart = loopstart;
			p->totloop = totloop;
			
			loopstart += totloop;
		}
	}
	
	if (numfaces > 0 && numedges == 0) {
		mesh_update(me, true, false);
	}
	
	return me;
}

Mesh* BKE_mesh_test_from_csv(const char *filename)
{
	std::ifstream ifs (filename, std::ifstream::in);
	
	char delim;
	
	int numverts, numloops, numfaces;
	float (*verts)[3] = NULL;
	int *loops = NULL;
	int *face_lengths = NULL;
	
	ifs >> numverts;
	ifs >> delim;
	verts = (float (*)[3])MEM_mallocN(sizeof(float[3]) * numverts, "verts");
	for (int i = 0; i < numverts; ++i) {
		ifs >> verts[i][0];
		ifs >> delim;
		ifs >> verts[i][1];
		ifs >> delim;
		ifs >> verts[i][2];
		ifs >> delim;
	}
	
	ifs >> numloops;
	ifs >> delim;
	loops = (int *)MEM_mallocN(sizeof(int) * numloops, "loops");
	for (int i = 0; i < numloops; ++i) {
		ifs >> loops[i];
		ifs >> delim;
	}
	
	ifs >> numfaces;
	ifs >> delim;
	face_lengths = (int *)MEM_mallocN(sizeof(int) * numfaces, "face_lengths");
	for (int i = 0; i < numfaces; ++i) {
		ifs >> face_lengths[i];
		ifs >> delim;
	}
	
	return BKE_mesh_test_from_data(verts, numverts, NULL, 0, loops, face_lengths, numfaces);
}

void BKE_mesh_test_dump_verts(Mesh *me, std::ostream &str)
{
	int numverts = me->totvert;
	
	str << std::setprecision(5);
	
	MVert *v = me->mvert;
	str << "[";
	for (int i = 0; i < numverts; ++i, ++v) {
		str << "(" << v->co[0] << ", " << v->co[1] << ", " << v->co[2] << "), ";
	}
	str << "]";
}

void BKE_mesh_test_dump_edges(Mesh *me, std::ostream &str)
{
	int numedges = me->totedge;
	
	MEdge *e = me->medge;
	str << "[";
	for (int i = 0; i < numedges; ++i, ++e) {
		str << "(" << e->v1 << ", " << e->v2 << "), ";
	}
	str << "]";
}

void BKE_mesh_test_dump_faces(Mesh *me, std::ostream &str)
{
	int numpolys = me->totpoly;
	
	MPoly *p = me->mpoly;
	str << "[";
	for (int i = 0; i < numpolys; ++i, ++p) {
		int totloop = p->totloop;
		MLoop *l = me->mloop + p->loopstart;
		
		str << "(";
		for (int k = 0; k < totloop; ++k, ++l) {
			str << l->v << ", ";
		}
		str << "), ";
	}
	str << "]";
}

void BKE_mesh_test_dump_mesh(Mesh *me, const char *name, std::ostream &str)
{
	str << "import bpy\n";
	str << "from bpy_extras.object_utils import object_data_add\n";
	str << "mesh = bpy.data.meshes.new(name=\"" << name << "\")\n";
	
	str << "mesh.from_pydata(";
	str << "vertices=";
	BKE_mesh_test_dump_verts(me, str);
	str << ", edges=";
	BKE_mesh_test_dump_edges(me, str);
	str << ", faces=";
	BKE_mesh_test_dump_faces(me, str);
	str << ")\n";
	
	str << "object_data_add(bpy.context, mesh)\n";
}
