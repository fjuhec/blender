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

#pragma once

#include "abc_customdata.h"
#include "abc_object.h"

struct DerivedMesh;
struct Mesh;
struct ModifierData;

/* ************************************************************************** */

class AbcMeshWriter : public AbcObjectWriter {
	Alembic::AbcGeom::OPolyMeshSchema m_mesh_schema;
	Alembic::AbcGeom::OPolyMeshSchema::Sample m_mesh_sample;

	Alembic::AbcGeom::OSubDSchema m_subdiv_schema;
	Alembic::AbcGeom::OSubDSchema::Sample m_subdiv_sample;

	bool m_has_per_face_materials;
	Alembic::AbcGeom::OFaceSet m_face_set;
	Alembic::Abc::OArrayProperty m_mat_indices;

	bool m_is_animated;
	ModifierData *m_subsurf_mod;

	CDStreamConfig m_custom_data_config;

	bool m_is_liquid;
	bool m_is_subd;

public:
	AbcMeshWriter(Scene *scene,
	              Object *ob,
	              AbcTransformWriter *parent,
	              uint32_t time_sampling,
	              ExportSettings &settings);

	~AbcMeshWriter();

private:
	virtual void do_write();

	bool isAnimated() const;

	void writeMesh(DerivedMesh *dm);
	void writeSubD(DerivedMesh *dm);

	void getMeshInfo(DerivedMesh *dm, std::vector<float> &points,
	                 std::vector<int32_t> &facePoints,
	                 std::vector<int32_t> &faceCounts,
	                 std::vector<int32_t> &creaseIndices,
	                 std::vector<int32_t> &creaseLengths,
	                 std::vector<float> &creaseSharpness);

	DerivedMesh *getFinalMesh();
	void freeMesh(DerivedMesh *dm);

	void getMaterialIndices(DerivedMesh *dm, std::vector<int32_t> &indices);

	void writeArbGeoParams(DerivedMesh *dm);
	void getGeoGroups(DerivedMesh *dm, std::map<std::string, std::vector<int32_t> > &geoGroups);
	
	/* fluid surfaces support */
	void getVelocities(DerivedMesh *dm, std::vector<Imath::V3f> &vels);

	template <typename Schema>
	void writeCommonData(DerivedMesh *dm, Schema &schema);
};

/* ************************************************************************** */

class AbcMeshReader : public AbcObjectReader {
	Alembic::AbcGeom::IPolyMeshSchema m_schema;
	Alembic::AbcGeom::ISubDSchema m_subd_schema;

public:
	AbcMeshReader(const Alembic::Abc::IObject &object, ImportSettings &settings, bool is_subd);

	bool valid() const;

	void readObjectData(Main *bmain, Scene *scene, float time);

private:
	void readFaceSetsSample(Main *bmain, Mesh *mesh, size_t poly_start,
	                        const Alembic::AbcGeom::ISampleSelector &sample_sel);

	void readPolyDataSample(Mesh *mesh,
	                        const Alembic::AbcGeom::Int32ArraySamplePtr &face_indices,
	                        const Alembic::AbcGeom::Int32ArraySamplePtr &face_counts,
	                        const Alembic::AbcGeom::N3fArraySamplePtr &normals);

	void readVertexDataSample(Mesh *mesh,
	                          const Alembic::AbcGeom::P3fArraySamplePtr &positions,
	                          const Alembic::AbcGeom::N3fArraySamplePtr &normals);
};

/* ************************************************************************** */

struct MLoop;
struct MLoopUV;
struct MPoly;
struct MVert;

void read_mverts(MVert *mverts,
                 const Alembic::AbcGeom::P3fArraySamplePtr &positions,
                 const Alembic::AbcGeom::N3fArraySamplePtr &normals);

void read_mpolys(MPoly *mpolys, MLoop *mloops, MLoopUV *mloopuvs, CustomData *ldata,
                 const Alembic::AbcGeom::Int32ArraySamplePtr &face_indices,
                 const Alembic::AbcGeom::Int32ArraySamplePtr &face_counts,
                 const Alembic::AbcGeom::V2fArraySamplePtr &uvs,
                 const Alembic::AbcGeom::UInt32ArraySamplePtr &uvs_indices,
                 const Alembic::AbcGeom::N3fArraySamplePtr &normals);

namespace utils {

void mesh_add_verts(struct Mesh *mesh, size_t len);

}
