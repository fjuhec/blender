/* Apache License, Version 2.0 */

#include <iostream>
#include <fstream>
#include <sstream>

#include "testing/testing.h"

#include "BKE_mesh_test_util.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_rand.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_mesh.h"
#include "BKE_mesh_sample.h"
}

#define TEST_MESH_OUTPUT_FILE "mesh_dump_"

static const float verts[][3] = { {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                                  {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1} };
static const int faces[] = { 0, 1, 3, 2,
                             4, 5, 7, 6,
                             0, 1, 5, 4,
                             2, 3, 7, 6,
                             0, 2, 6, 4,
                             1, 3, 7, 5,
                           };
static const int face_lengths[] = { 4, 4, 4, 4, 4, 4 };

class MeshSampleTest : public ::testing::Test
{
public:
	static const unsigned int m_seed;
	static const int m_numsamples;
	
	std::string get_testname() const;
	
	void load_mesh(const float (*verts)[3], int numverts,
	               const int (*edges)[2], int numedges,
	               const int *faces, const int *face_lengths, int numfaces);
	void unload_mesh();
	
	void generate_samples_simple(struct MeshSampleGenerator *gen);
	void generate_samples_batch(struct MeshSampleGenerator *gen);
	void generate_samples_batch_threaded(struct MeshSampleGenerator *gen);
	void compare_samples(const struct MeshSample *ground_truth);
	void test_samples(struct MeshSampleGenerator *gen, const struct MeshSample *ground_truth, int count);
	
	void dump_samples();
	
protected:
	void SetUp() override;
	void TearDown() override;
	
protected:
	Mesh *m_mesh;
	DerivedMesh *m_dm;
	
	MeshSample *m_samples;
};

const unsigned int MeshSampleTest::m_seed = 8343;
const int MeshSampleTest::m_numsamples = 100000;

std::string MeshSampleTest::get_testname() const
{
	std::stringstream testname;
	testname << ::testing::UnitTest::GetInstance()->current_test_info()->name();
	return testname.str();
}

void MeshSampleTest::load_mesh(const float (*verts)[3], int numverts,
                               const int (*edges)[2], int numedges,
                               const int *faces, const int *face_lengths, int numfaces)
{
	m_mesh = BKE_mesh_test_from_data(verts, numverts, edges, numedges, faces, face_lengths, numfaces);
	m_dm = CDDM_from_mesh(m_mesh);
}

void MeshSampleTest::unload_mesh()
{
	if (m_dm) {
		m_dm->release(m_dm);
		m_dm = NULL;
	}
	if (m_mesh) {
		BKE_mesh_free(m_mesh);
		MEM_freeN(m_mesh);
		m_mesh = NULL;
	}
}

void MeshSampleTest::SetUp()
{
	int numverts = ARRAY_SIZE(verts);
	int numfaces = ARRAY_SIZE(face_lengths);
	load_mesh(verts, numverts, NULL, 0, faces, face_lengths, numfaces);
	
	m_samples = (MeshSample *)MEM_mallocN(sizeof(MeshSample) * m_numsamples, "mesh samples");
}

void MeshSampleTest::TearDown()
{
	if (m_samples) {
		MEM_freeN(m_samples);
		m_samples = NULL;
	}
	
	unload_mesh();
}


void MeshSampleTest::dump_samples()
{
#ifdef TEST_MESH_OUTPUT_FILE
	int numverts = m_mesh->totvert;
	
	int dbg_numverts = numverts + m_numsamples;
	float (*dbg_verts)[3] = (float (*)[3])MEM_mallocN(sizeof(float[3]) * dbg_numverts, "vertices");
	for (int i = 0; i < numverts; ++i) {
		copy_v3_v3(dbg_verts[i], m_mesh->mvert[i].co);
	}
	for (int i = 0; i < m_numsamples; ++i) {
		float nor[3], tang[3];
		BKE_mesh_sample_eval(m_dm, &m_samples[i], dbg_verts[numverts + i], nor, tang);
	}
	int *dbg_faces = (int *)MEM_mallocN(sizeof(int) * m_mesh->totloop, "faces");
	int *dbg_face_lengths = (int *)MEM_mallocN(sizeof(int) * m_mesh->totpoly, "face_lengths");
	int loopstart = 0;
	for (int i = 0; i < m_mesh->totpoly; ++i) {
		const MPoly *mp = &m_mesh->mpoly[i];
		dbg_face_lengths[i] = mp->totloop;
		for (int k = 0; k < mp->totloop; ++k) {
			dbg_faces[loopstart + k] = m_mesh->mloop[mp->loopstart + k].v;
		}
		loopstart += mp->totloop;
	}
	Mesh *dbg_mesh = BKE_mesh_test_from_data(dbg_verts, dbg_numverts, NULL, 0, dbg_faces, dbg_face_lengths, m_mesh->totpoly);
	MEM_freeN(dbg_verts);
	MEM_freeN(dbg_faces);
	MEM_freeN(dbg_face_lengths);
	
	std::stringstream filename;
	filename << TEST_MESH_OUTPUT_FILE << get_testname() << ".py";
	std::fstream s(filename.str(), s.trunc | s.out);
	
	BKE_mesh_test_dump_mesh(dbg_mesh, get_testname().c_str(), s);
	
	BKE_mesh_free(dbg_mesh);
	MEM_freeN(dbg_mesh);
#endif
}

void MeshSampleTest::compare_samples(const MeshSample *ground_truth)
{
	for (int i = 0; i < m_numsamples; ++i) {
		EXPECT_EQ(ground_truth[i].orig_verts[0], m_samples[i].orig_verts[0]);
		EXPECT_EQ(ground_truth[i].orig_verts[1], m_samples[i].orig_verts[1]);
		EXPECT_EQ(ground_truth[i].orig_verts[2], m_samples[i].orig_verts[2]);
		
		EXPECT_EQ(ground_truth[i].orig_weights[0], m_samples[i].orig_weights[0]);
		EXPECT_EQ(ground_truth[i].orig_weights[1], m_samples[i].orig_weights[1]);
		EXPECT_EQ(ground_truth[i].orig_weights[2], m_samples[i].orig_weights[2]);
	}
}

void MeshSampleTest::generate_samples_simple(MeshSampleGenerator *gen)
{
	for (int i = 0; i < m_numsamples; ++i) {
		BKE_mesh_sample_generate(gen, &m_samples[i]);
	}
}

void MeshSampleTest::generate_samples_batch(MeshSampleGenerator *gen)
{
	BKE_mesh_sample_generate_batch_ex(gen, m_samples, sizeof(MeshSample), m_numsamples, false);
}

void MeshSampleTest::generate_samples_batch_threaded(MeshSampleGenerator *gen)
{
	BKE_mesh_sample_generate_batch_ex(gen, m_samples, sizeof(MeshSample), m_numsamples, true);
}

void MeshSampleTest::test_samples(MeshSampleGenerator *gen, const MeshSample *ground_truth, int count)
{
	if (ground_truth) {
		EXPECT_EQ(count, m_numsamples) << "Ground truth size does not match number of samples";
		if (count != m_numsamples) {
			return;
		}
		
		generate_samples_simple(gen);
		compare_samples(ground_truth);
	}
	else {
		generate_samples_simple(gen);
		// Use simple sample generation as ground truth if not provided explicitly
		ground_truth = m_samples;
	}
	
	generate_samples_batch(gen);
	compare_samples(ground_truth);
	
	generate_samples_batch_threaded(gen);
	compare_samples(ground_truth);
}

TEST_F(MeshSampleTest, SurfaceVertices)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_vertices(m_dm);
	ASSERT_TRUE(gen != NULL) << "No generator created";
	
	test_samples(gen, NULL, 0);
	dump_samples();
	
	BKE_mesh_sample_free_generator(gen);
}

TEST_F(MeshSampleTest, SurfaceRandom)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_random(m_dm, m_seed, NULL, NULL);
	ASSERT_TRUE(gen != NULL) << "No generator created";
	
	test_samples(gen, NULL, 0);
	dump_samples();
	
	BKE_mesh_sample_free_generator(gen);
}

const float poisson_disk_mindist = 0.01f;

TEST_F(MeshSampleTest, SurfacePoissonDisk)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_poissondisk(m_dm, m_seed, poisson_disk_mindist, 10000000, NULL, NULL);
	ASSERT_TRUE(gen != NULL) << "No generator created";
	
	test_samples(gen, NULL, 0);
	dump_samples();
	
	BKE_mesh_sample_free_generator(gen);
}

extern "C" {

static const unsigned int raycast_seed = 85344;
static const float raycast_radius = 100.0f;

static void* raycast_thread_context_create(void *UNUSED(userdata), int start)
{
	RNG *rng = BLI_rng_new(raycast_seed);
	BLI_rng_skip(rng, start * 2);
	return rng;
}

static void raycast_thread_context_free(void *UNUSED(userdata), void *thread_ctx)
{
	BLI_rng_free((RNG *)thread_ctx);
}

static bool raycast_ray(void *UNUSED(userdata), void *thread_ctx, float ray_start[3], float ray_end[3])
{
	RNG *rng = (RNG *)thread_ctx;
	float v[3];
	{
		float r;
		v[2] = (2.0f * BLI_rng_get_float(rng)) - 1.0f;
		float a = (float)(M_PI * 2.0) * BLI_rng_get_float(rng);
		if ((r = 1.0f - (v[2] * v[2])) > 0.0f) {
			r = sqrtf(r);
			v[0] = r * cosf(a);
			v[1] = r * sinf(a);
		}
		else {
			v[2] = 1.0f;
		}
	}
	
	mul_v3_fl(v, raycast_radius);
	copy_v3_v3(ray_start, v);
	negate_v3_v3(ray_end, v);
	return true;
}

} /*extern "C"*/

TEST_F(MeshSampleTest, SurfaceRaycast)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_surface_raycast(
	                               m_dm, raycast_thread_context_create, raycast_thread_context_free, raycast_ray, NULL);
	ASSERT_TRUE(gen != NULL) << "No generator created";
	
	test_samples(gen, NULL, 0);
	dump_samples();
	
	BKE_mesh_sample_free_generator(gen);
}

static const float volume_bbray_density = 0.1f;

TEST_F(MeshSampleTest, VolumeBBRay)
{
	MeshSampleGenerator *gen = BKE_mesh_sample_gen_volume_random_bbray(m_dm, m_seed, volume_bbray_density);
	ASSERT_TRUE(gen != NULL) << "No generator created";
	
	test_samples(gen, NULL, 0);
	dump_samples();
	
	BKE_mesh_sample_free_generator(gen);
}
