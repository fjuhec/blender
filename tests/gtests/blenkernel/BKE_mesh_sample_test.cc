/* Apache License, Version 2.0 */

#include <iostream>
#include <fstream>
#include <sstream>

#include "testing/testing.h"

#include "BKE_mesh_test_util.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_mesh.h"
#include "BKE_mesh_sample.h"
}

//#define TEST_MESH_OUTPUT_FILE "mesh_dump_"

static const float verts[][3] = { {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                                  {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1} };
static const int faces[] = { 0, 1, 3, 2,
                             4, 5, 7, 6,
                             0, 1, 5, 4,
                             2, 3, 7, 6,
                             0, 2, 6, 4,
                             1, 3, 7, 5,
                           };
static const int face_sizes[] = { 4, 4, 4, 4, 4, 4 };

static void dump_samples(const char *testname, const Mesh *mesh, const MeshSample *samples, int numsamples)
{
#ifdef TEST_MESH_OUTPUT_FILE
	int numverts = mesh->totvert;
	
	int dbg_numverts = numverts + numsamples;
	float (*dbg_verts)[3] = (float (*)[3])MEM_mallocN(sizeof(float[3]) * dbg_numverts, "vertices");
	for (int i = 0; i < numverts; ++i) {
		copy_v3_v3(dbg_verts[i], mesh->mvert[i].co);
	}
	for (int i = 0; i < numsamples; ++i) {
		float nor[3], tang[3];
		BKE_mesh_sample_eval(dm, &samples[i], dbg_verts[numverts + i], nor, tang);
	}
	Mesh *dbg_mesh = BKE_mesh_test_from_data(dbg_verts, dbg_numverts, NULL, 0, faces, face_sizes, numfaces);
	MEM_freeN(dbg_verts);
	
	std::stringstream filename;
	filename << TEST_MESH_OUTPUT_FILE << testname << ".py";
	std::fstream s(filename.str(), s.trunc | s.out);
	
	BKE_mesh_test_dump_mesh(dbg_mesh, testname, s);
	
	BKE_mesh_free(dbg_mesh);
	MEM_freeN(dbg_mesh);
#else
	UNUSED_VARS(testname, mesh, samples, numsamples);
#endif
}

static void compare_samples(const MeshSample *ground_truth, const MeshSample *samples, int count)
{
	for (int i = 0; i < count; ++i) {
		EXPECT_EQ(ground_truth[i].orig_verts[0], samples[i].orig_verts[0]);
		EXPECT_EQ(ground_truth[i].orig_verts[1], samples[i].orig_verts[1]);
		EXPECT_EQ(ground_truth[i].orig_verts[2], samples[i].orig_verts[2]);
		
		EXPECT_EQ(ground_truth[i].orig_weights[0], samples[i].orig_weights[0]);
		EXPECT_EQ(ground_truth[i].orig_weights[1], samples[i].orig_weights[1]);
		EXPECT_EQ(ground_truth[i].orig_weights[2], samples[i].orig_weights[2]);
	}
}

static void generate_samples_simple(MeshSampleGenerator *gen, MeshSample *samples, int count)
{
	for (int i = 0; i < count; ++i) {
		BKE_mesh_sample_generate(gen, &samples[i]);
	}
}

static void generate_samples_batch(MeshSampleGenerator *gen, MeshSample *samples, int count)
{
	BKE_mesh_sample_generate_batch_ex(gen, samples, sizeof(MeshSample), count, false);
}

static void generate_samples_batch_threaded(MeshSampleGenerator *gen, MeshSample *samples, int count)
{
	BKE_mesh_sample_generate_batch_ex(gen, samples, sizeof(MeshSample), count, true);
}

static void test_samples(MeshSampleGenerator *gen, const MeshSample *ground_truth, MeshSample *samples, int count)
{
	generate_samples_simple(gen, samples, count);
	if (ground_truth) {
		compare_samples(ground_truth, samples, count);
	}
	else {
		// Use simple sample generation as ground truth if not provided explicitly
		ground_truth = samples;
	}
	
	generate_samples_batch(gen, samples, count);
	compare_samples(ground_truth, samples, count);
	
	generate_samples_batch_threaded(gen, samples, count);
	compare_samples(ground_truth, samples, count);
}

TEST(mesh_sample, SurfaceRandom)
{
	int numverts = ARRAY_SIZE(verts);
	int numfaces = ARRAY_SIZE(face_sizes);
	Mesh *mesh = BKE_mesh_test_from_data(verts, numverts, NULL, 0, faces, face_sizes, numfaces);
	DerivedMesh *dm = CDDM_from_mesh(mesh);
	
	const unsigned int seed = 8343;
	const int numsamples = 100000;
	MeshSample *samples = (MeshSample *)MEM_mallocN(sizeof(MeshSample) * numsamples, "mesh samples");
	
	std::stringstream testname;
	testname << ::testing::UnitTest::GetInstance()->current_test_info()->name() << "_" << seed;
	
	{
		MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_random(dm, seed);
		ASSERT_TRUE(gen != NULL) << "No generator created";
		
		test_samples(gen, NULL, samples, numsamples);
		dump_samples(testname.str().c_str(), mesh, samples, numsamples);
		
		BKE_mesh_sample_free_generator(gen);
	}
	
	MEM_freeN(samples);
	
	dm->release(dm);
	BKE_mesh_free(mesh);
	MEM_freeN(mesh);
}
