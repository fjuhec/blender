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

#include "../ABC_alembic.h"

#include <Alembic/AbcCoreHDF5/All.h>
#include <Alembic/AbcCoreOgawa/All.h>
#include <Alembic/AbcMaterial/IMaterial.h>

#include "abc_camera.h"
#include "abc_curves.h"
#include "abc_hair.h"
#include "abc_mesh.h"
#include "abc_nurbs.h"
#include "abc_points.h"
#include "abc_transform.h"
#include "abc_util.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_cachefile_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_cachefile.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_scene.h"

/* SpaceType struct has a member called 'new' which obviously conflicts with C++
 * so temporarily redefining the new keyword to make it compile. */
#define new extern_new
#include "BKE_screen.h"
#undef new

#include "BLI_fileops.h"
#include "BLI_ghash.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "WM_api.h"
#include "WM_types.h"
}

using Alembic::Abc::ObjectHeader;

using Alembic::AbcGeom::ErrorHandler;
using Alembic::AbcGeom::Exception;
using Alembic::AbcGeom::MetaData;
using Alembic::AbcGeom::P3fArraySamplePtr;
using Alembic::AbcGeom::kWrapExisting;

using Alembic::AbcGeom::IArchive;
using Alembic::AbcGeom::ICamera;
using Alembic::AbcGeom::ICurves;
using Alembic::AbcGeom::ICurvesSchema;
using Alembic::AbcGeom::IFaceSet;
using Alembic::AbcGeom::ILight;
using Alembic::AbcGeom::INuPatch;
using Alembic::AbcGeom::IObject;
using Alembic::AbcGeom::IPoints;
using Alembic::AbcGeom::IPointsSchema;
using Alembic::AbcGeom::IPolyMesh;
using Alembic::AbcGeom::IPolyMeshSchema;
using Alembic::AbcGeom::ISampleSelector;
using Alembic::AbcGeom::ISubD;
using Alembic::AbcGeom::IV2fGeomParam;
using Alembic::AbcGeom::IXform;
using Alembic::AbcGeom::IXformSchema;
using Alembic::AbcGeom::N3fArraySamplePtr;
using Alembic::AbcGeom::XformSample;
using Alembic::AbcGeom::ICompoundProperty;
using Alembic::AbcGeom::IN3fArrayProperty;
using Alembic::AbcGeom::IN3fGeomParam;

using Alembic::AbcMaterial::IMaterial;

struct AbcArchiveHandle {
	int unused;
};

ABC_INLINE IArchive *archive_from_handle(AbcArchiveHandle *handle)
{
	return reinterpret_cast<IArchive *>(handle);
}

ABC_INLINE AbcArchiveHandle *handle_from_archive(IArchive *archive)
{
	return reinterpret_cast<AbcArchiveHandle *>(archive);
}

static IArchive *open_archive(const std::string &filename)
{
	Alembic::AbcCoreAbstract::ReadArraySampleCachePtr cache_ptr;
	IArchive *archive;

	try {
		archive = new IArchive(Alembic::AbcCoreHDF5::ReadArchive(),
		                       filename.c_str(), ErrorHandler::kThrowPolicy,
		                       cache_ptr);
	}
	catch (const Exception &) {
		try {
			archive = new IArchive(Alembic::AbcCoreOgawa::ReadArchive(),
			                       filename.c_str(), ErrorHandler::kThrowPolicy,
			                       cache_ptr);
		}
		catch (const Exception &e) {
			std::cerr << e.what() << '\n';
			return NULL;
		}
	}

	return archive;
}

AbcArchiveHandle *ABC_create_handle(const char *filename)
{
	return handle_from_archive(open_archive(filename));
}

void ABC_free_handle(AbcArchiveHandle *handle)
{
	delete archive_from_handle(handle);
}

int ABC_get_version()
{
	return ALEMBIC_LIBRARY_VERSION;
}

static void find_iobject(const IObject &object, IObject &ret,
                         const std::string &path)
{
	if (!object.valid()) {
		return;
	}

	std::vector<std::string> tokens;
	split(path, '/', tokens);

	IObject tmp = object;

	std::vector<std::string>::iterator iter;
	for (iter = tokens.begin(); iter != tokens.end(); ++iter) {
		IObject child = tmp.getChild(*iter);
		tmp = child;
	}

	ret = tmp;
}

struct ExportJobData {
	Scene *scene;
	Main *bmain;

	char filename[1024];
	ExportSettings settings;

	short *stop;
	short *do_update;
	float *progress;

	bool was_canceled;
};

static void export_startjob(void *customdata, short *stop, short *do_update, float *progress)
{
	ExportJobData *data = static_cast<ExportJobData *>(customdata);

	data->stop = stop;
	data->do_update = do_update;
	data->progress = progress;

	/* XXX annoying hack: needed to prevent data corruption when changing
	 * scene frame in separate threads
	 */
	G.is_rendering = true;
	BKE_spacedata_draw_locks(true);

	G.is_break = false;

	try {
		Scene *scene = data->scene;
		AbcExporter exporter(scene, data->filename, data->settings);

		const int orig_frame = CFRA;

		data->was_canceled = false;
		exporter(data->bmain, *data->progress, data->was_canceled);

		if (CFRA != orig_frame) {
			CFRA = orig_frame;

			BKE_scene_update_for_newframe(data->bmain->eval_ctx, data->bmain,
			                              scene, scene->lay);
		}
	}
	catch (const std::exception &e) {
		std::cerr << "Abc Export error: " << e.what() << '\n';
	}
	catch (...) {
		std::cerr << "Abc Export error\n";
	}
}

static void export_endjob(void *customdata)
{
	ExportJobData *data = static_cast<ExportJobData *>(customdata);

	if (data->was_canceled && BLI_exists(data->filename)) {
		BLI_delete(data->filename, false, false);
	}

	G.is_rendering = false;
	BKE_spacedata_draw_locks(false);
}

void ABC_export(
        Scene *scene,
        bContext *C,
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
        const float global_scale)
{
	ExportJobData *job = static_cast<ExportJobData *>(MEM_mallocN(sizeof(ExportJobData), "ExportJobData"));
	job->scene = scene;
	job->bmain = CTX_data_main(C);
	BLI_strncpy(job->filename, filepath, 1024);

	job->settings.scene = job->scene;
	job->settings.startframe = start;
	job->settings.endframe = end;
	job->settings.xform_frame_step = xformstep;
	job->settings.shape_frame_step = geomstep;
	job->settings.shutter_open = shutter_open;
	job->settings.shutter_close = shutter_close;
	job->settings.selected_only = selected_only;
	job->settings.export_face_sets = facesets;
	job->settings.export_normals = normals;
	job->settings.export_uvs = uvs;
	job->settings.export_vcols = vcolors;
	job->settings.apply_subdiv = apply_subdiv;
	job->settings.flatten_hierarchy = flatten_hierarchy;
	job->settings.visible_layers_only = vislayers;
	job->settings.renderable_only = renderable;
	job->settings.use_subdiv_schema = use_subdiv_schema;
	job->settings.export_ogawa = (compression == ABC_ARCHIVE_OGAWA);
	job->settings.pack_uv = packuv;
	job->settings.global_scale = global_scale;

	if (job->settings.startframe > job->settings.endframe) {
		std::swap(job->settings.startframe, job->settings.endframe);
	}

	wmJob *wm_job = WM_jobs_get(CTX_wm_manager(C),
	                            CTX_wm_window(C),
	                            job->scene,
	                            "Alembic Export",
	                            WM_JOB_PROGRESS,
	                            WM_JOB_TYPE_ALEMBIC);

	/* setup job */
	WM_jobs_customdata_set(wm_job, job, MEM_freeN);
	WM_jobs_timer(wm_job, 0.1, NC_SCENE | ND_FRAME, NC_SCENE | ND_FRAME);
	WM_jobs_callbacks(wm_job, export_startjob, NULL, NULL, export_endjob);

	WM_jobs_start(CTX_wm_manager(C), wm_job);
}

/* ********************** Import file ********************** */

static void visit_object(const IObject &object,
                         std::vector<AbcObjectReader *> &readers,
                         GHash *parent_map,
                         ImportSettings &settings)
{
	if (!object.valid()) {
		return;
	}

	for (int i = 0; i < object.getNumChildren(); ++i) {
		IObject child = object.getChild(i);

		if (!child.valid()) {
			continue;
		}

		AbcObjectReader *reader = NULL;

		const MetaData &md = child.getMetaData();

		if (IXform::matches(md)) {
			bool create_xform = false;

			/* Check whether or not this object is a Maya locator, which is
			 * similar to empties used as parent object in Blender. */
			if (has_property(child.getProperties(), "locator")) {
				create_xform = true;
			}
			else {
				/* Avoid creating an empty object if the child of this transform
				 * is not a transform (that is an empty). */
				if (child.getNumChildren() == 1) {
					if (IXform::matches(child.getChild(0).getMetaData())) {
						create_xform = true;
					}
#if 0
					else {
						std::cerr << "Skipping " << child.getFullName() << '\n';
					}
#endif
				}
				else {
					create_xform = true;
				}
			}

			if (create_xform) {
				reader = new AbcEmptyReader(child, settings);
			}
		}
		else if (IPolyMesh::matches(md)) {
			reader = new AbcMeshReader(child, settings, false);
		}
		else if (ISubD::matches(md)) {
			reader = new AbcMeshReader(child, settings, true);
		}
		else if (INuPatch::matches(md)) {
			reader = new AbcNurbsReader(child, settings);
		}
		else if (ICamera::matches(md)) {
			reader = new AbcCameraReader(child, settings);
		}
		else if (IPoints::matches(md)) {
			reader = new AbcPointsReader(child, settings);
		}
		else if (IMaterial::matches(md)) {
			/* Pass for now. */
		}
		else if (ILight::matches(md)) {
			/* Pass for now. */
		}
		else if (IFaceSet::matches(md)) {
			/* Pass, those are handled in the mesh reader. */
		}
		else if (ICurves::matches(md)) {
			reader = new AbcCurveReader(child, settings);
		}
		else {
			assert(false);
		}

		if (reader) {
			readers.push_back(reader);

			/* Cast to `void *` explicitly to avoid compiler errors because it
			 * is a `const char *` which the compiler cast to `const void *`
			 * instead of the expected `void *`. */
			BLI_ghash_insert(parent_map, (void *)child.getFullName().c_str(), reader);
		}

		visit_object(child, readers, parent_map, settings);
	}
}

struct ImportJobData {
	Main *bmain;
	Scene *scene;

	char filename[1024];
	ImportSettings settings;

	GHash *parent_map;
	std::vector<AbcObjectReader *> readers;

	short *stop;
	short *do_update;
	float *progress;
};

static void import_startjob(void *user_data, short *stop, short *do_update, float *progress)
{
	ImportJobData *data = static_cast<ImportJobData *>(user_data);

	data->stop = stop;
	data->do_update = do_update;
	data->progress = progress;

	IArchive *archive = open_archive(data->filename);

	if (!archive || !archive->valid()) {
		return;
	}

	CacheFile *cache_file = static_cast<CacheFile *>(BKE_cachefile_add(data->bmain, BLI_path_basename(data->filename)));

	/* Decrement the ID ref-count because it is going to be incremented for each
	 * modifier and constraint that it will be attached to, so since currently
	 * it is not used by anyone, its use count will off by one. */
	id_us_min(&cache_file->id);

	cache_file->is_sequence = data->settings.is_sequence;
	cache_file->scale = data->settings.scale;
	cache_file->handle = handle_from_archive(archive);
	BLI_strncpy(cache_file->filepath, data->filename, 1024);

	data->settings.cache_file = cache_file;

	*data->do_update = true;
	*data->progress = 0.05f;

	data->parent_map = BLI_ghash_str_new("alembic parent ghash");

	/* Parse Alembic Archive. */

	visit_object(archive->getTop(), data->readers, data->parent_map, data->settings);

	if (G.is_break) {
		return;
	}

	*data->do_update = true;
	*data->progress = 0.1f;

	/* Create objects and set scene frame range. */

	const float size = static_cast<float>(data->readers.size());
	size_t i = 0;

	Scene *scene = data->scene;

	chrono_t min_time = std::numeric_limits<chrono_t>::max();
	chrono_t max_time = std::numeric_limits<chrono_t>::min();

	std::vector<AbcObjectReader *>::iterator iter;
	for (iter = data->readers.begin(); iter != data->readers.end(); ++iter) {
		AbcObjectReader *reader = *iter;

		if (reader->valid()) {
			reader->readObjectData(data->bmain, scene, 0.0f);
			reader->readObjectMatrix(0.0f);

			min_time = std::min(min_time, reader->minTime());
			max_time = std::max(max_time, reader->maxTime());
		}

		*data->progress = 0.1f + 0.6f * (++i / size);

		if (G.is_break) {
			return;
		}
	}

	if (data->settings.set_frame_range) {
		if (data->settings.is_sequence) {
			SFRA = data->settings.offset;
			EFRA = SFRA + (data->settings.sequence_len - 1);
			CFRA = SFRA;
		}
		else if (min_time < max_time) {
			SFRA = min_time * FPS;
			EFRA = max_time * FPS;
			CFRA = SFRA;
		}
	}

	/* Setup parentship. */

	i = 0;
	for (iter = data->readers.begin(); iter != data->readers.end(); ++iter) {
		const AbcObjectReader *reader = *iter;
		const AbcObjectReader *parent_reader = NULL;
		const IObject &iobject = reader->iobject();

		if (IXform::matches(iobject.getHeader())) {
			parent_reader = reinterpret_cast<AbcObjectReader *>(
			                    BLI_ghash_lookup(data->parent_map,
			                                     iobject.getParent().getFullName().c_str()));
		}
		else {
			/* In the case of an non XForm node, the parent is the transform
			 * matrix of the data itself, so skip it. */
			parent_reader = reinterpret_cast<AbcObjectReader *>(
			                    BLI_ghash_lookup(data->parent_map,
			                                     iobject.getParent().getParent().getFullName().c_str()));
		}

		if (parent_reader) {
			Object *parent = parent_reader->object();

			if (parent != NULL && reader->object() != parent) {
				Object *ob = reader->object();
				ob->parent = parent;

				DAG_id_tag_update(&ob->id, OB_RECALC_OB);
				DAG_relations_tag_update(data->bmain);
				WM_main_add_notifier(NC_OBJECT | ND_PARENT, ob);
			}
		}

		*data->progress = 0.7f + 0.3f * (++i / size);

		if (G.is_break) {
			return;
		}
	}
}

static void import_endjob(void *user_data)
{
	ImportJobData *data = static_cast<ImportJobData *>(user_data);

	/* TODO(kevin): remove objects from the scene on cancelation. */

	std::vector<AbcObjectReader *>::iterator iter;
	for (iter = data->readers.begin(); iter != data->readers.end(); ++iter) {
		delete *iter;
	}

	BLI_ghash_free(data->parent_map, NULL, NULL);

	WM_main_add_notifier(NC_SCENE | ND_FRAME, data->scene);
}

static void import_freejob(void *user_data)
{
	ImportJobData *data = static_cast<ImportJobData *>(user_data);
	delete data;
}

void ABC_import(bContext *C, const char *filepath, float scale, bool is_sequence, bool set_frame_range, int sequence_len, int offset)
{
	/* Using new here since MEM_* funcs do not call ctor to properly initialize
	 * data. */
	ImportJobData *job = new ImportJobData();
	job->bmain = CTX_data_main(C);
	job->scene = CTX_data_scene(C);
	BLI_strncpy(job->filename, filepath, 1024);

	job->settings.scale = scale;
	job->settings.is_sequence = is_sequence;
	job->settings.set_frame_range = set_frame_range;
	job->settings.sequence_len = sequence_len;
	job->settings.offset = offset;

	G.is_break = false;

	wmJob *wm_job = WM_jobs_get(CTX_wm_manager(C),
	                            CTX_wm_window(C),
	                            job->scene,
	                            "Alembic Import",
	                            WM_JOB_PROGRESS,
	                            WM_JOB_TYPE_ALEMBIC);

	/* setup job */
	WM_jobs_customdata_set(wm_job, job, import_freejob);
	WM_jobs_timer(wm_job, 0.1, NC_SCENE | ND_FRAME, NC_SCENE | ND_FRAME);
	WM_jobs_callbacks(wm_job, import_startjob, NULL, NULL, import_endjob);

	WM_jobs_start(CTX_wm_manager(C), wm_job);
}

/* ******************************* */

void ABC_get_transform(AbcArchiveHandle *handle, Object *ob, const char *object_path, float r_mat[4][4], float time, float scale)
{
	IArchive *archive = archive_from_handle(handle);

	if (!archive || !archive->valid()) {
		return;
	}

	IObject tmp;
	find_iobject(archive->getTop(), tmp, object_path);

	IXform ixform;

	if (IXform::matches(tmp.getHeader())) {
		ixform = IXform(tmp, kWrapExisting);
	}
	else {
		ixform = IXform(tmp.getParent(), kWrapExisting);
	}

	IXformSchema schema = ixform.getSchema();

	if (!schema.valid()) {
		return;
	}

	ISampleSelector sample_sel(time);

	create_input_transform(sample_sel, ixform, ob, r_mat, scale);
}

/* ***************************************** */

static void *add_customdata_cb(void *user_data, const char *name, int data_type)
{
	DerivedMesh *dm = static_cast<DerivedMesh *>(user_data);
	CustomDataType cd_data_type = static_cast<CustomDataType>(data_type);
	void *cd_ptr = NULL;

	if (ELEM(cd_data_type, CD_MLOOPUV, CD_MLOOPCOL)) {
		cd_ptr = CustomData_get_layer_named(dm->getLoopDataLayout(dm), cd_data_type, name);

		if (cd_ptr == NULL) {
			cd_ptr = CustomData_add_layer_named(dm->getLoopDataLayout(dm),
			                                    cd_data_type,
			                                    CD_DEFAULT,
			                                    NULL,
			                                    dm->getNumLoops(dm),
			                                    name);
		}
	}

	return cd_ptr;
}

static DerivedMesh *read_mesh_sample(DerivedMesh *dm, const IObject &iobject, const float time)
{
	IPolyMesh mesh(iobject, kWrapExisting);
	IPolyMeshSchema schema = mesh.getSchema();
	ISampleSelector sample_sel(time);
	const IPolyMeshSchema::Sample sample = schema.getValue(sample_sel);

	const P3fArraySamplePtr &positions = sample.getPositions();
	const Alembic::Abc::Int32ArraySamplePtr &face_indices = sample.getFaceIndices();
	const Alembic::Abc::Int32ArraySamplePtr &face_counts = sample.getFaceCounts();

	bool new_dm = false;
	if (dm->getNumVerts(dm) != positions->size()) {
		dm = CDDM_new(positions->size(), 0, 0, face_indices->size(), face_counts->size());
		new_dm = true;
	}

	MVert *mverts = dm->getVertArray(dm);
	MPoly *mpolys = dm->getPolyArray(dm);
	MLoop *mloops = dm->getLoopArray(dm);
	MLoopUV *mloopuvs = NULL;
	CustomData *ldata = dm->getLoopDataLayout(dm);

	const IV2fGeomParam uv = schema.getUVsParam();
	Alembic::Abc::V2fArraySamplePtr uvs;
	Alembic::Abc::UInt32ArraySamplePtr uvs_indices;

	if (uv.valid()) {
		IV2fGeomParam::Sample uvsamp;
		uv.getIndexed(uvsamp, sample_sel);
		uvs = uvsamp.getVals();
		uvs_indices = uvsamp.getIndices();

		if (uvs_indices->size() != dm->getNumLoops(dm)) {
			uvs = Alembic::Abc::V2fArraySamplePtr();
			uvs_indices = Alembic::Abc::UInt32ArraySamplePtr();
		}
		else {
			std::string name = Alembic::Abc::GetSourceName(uv.getMetaData());

			/* According to the convention, primary UVs should have had their name
			 * set using Alembic::Abc::SetSourceName, but you can't expect everyone
			 * to follow it! :) */
			if (name.empty()) {
				name = uv.getName();
			}

			void *ptr = add_customdata_cb(dm, name.c_str(), CD_MLOOPUV);
			mloopuvs = static_cast<MLoopUV *>(ptr);

			dm->dirty = static_cast<DMDirtyFlag>(static_cast<int>(dm->dirty) | static_cast<int>(DM_DIRTY_TESS_CDLAYERS));
		}
	}

	N3fArraySamplePtr vertex_normals, poly_normals;
	const IN3fGeomParam normals = schema.getNormalsParam();

	if (normals.valid()) {
		IN3fGeomParam::Sample normsamp = normals.getExpandedValue(sample_sel);

		if (normals.getScope() == Alembic::AbcGeom::kFacevaryingScope) {
			poly_normals = normsamp.getVals();
		}
		else if ((normals.getScope() == Alembic::AbcGeom::kVertexScope) ||
		         (normals.getScope() == Alembic::AbcGeom::kVaryingScope))
		{
			vertex_normals = normsamp.getVals();
		}

		dm->dirty = static_cast<DMDirtyFlag>(static_cast<int>(dm->dirty) | static_cast<int>(DM_DIRTY_NORMALS));
	}

	read_mverts(mverts, positions, vertex_normals);
	read_mpolys(mpolys, mloops, mloopuvs, ldata, face_indices, face_counts, uvs, uvs_indices, poly_normals);

	CDStreamConfig config;
	config.user_data = dm;
	config.mloop = dm->getLoopArray(dm);
	config.mpoly = dm->getPolyArray(dm);
	config.totloop = dm->getNumLoops(dm);
	config.totpoly = dm->getNumPolys(dm);
	config.add_customdata_cb = add_customdata_cb;

	read_custom_data(schema.getArbGeomParams(), config, sample_sel);

	if (new_dm) {
		CDDM_calc_edges(dm);
	}

	return dm;
}

static DerivedMesh *read_points_sample(DerivedMesh *dm, const IObject &iobject, const float time)
{
	IPoints points(iobject, kWrapExisting);
	IPointsSchema schema = points.getSchema();
	ISampleSelector sample_sel(time);
	const IPointsSchema::Sample sample = schema.getValue(sample_sel);

	const P3fArraySamplePtr &positions = sample.getPositions();

	if (dm->getNumVerts(dm) != positions->size()) {
		dm = CDDM_new(positions->size(), 0, 0, 0, 0);
	}

	ICompoundProperty prop = schema.getArbGeomParams();
	N3fArraySamplePtr vnormals;

	if (has_property(prop, "N")) {
		const IN3fArrayProperty &normals_prop = IN3fArrayProperty(prop, "N", 0);

		if (normals_prop) {
			vnormals = normals_prop.getValue(sample_sel);
		}
	}

	MVert *mverts = dm->getVertArray(dm);

	read_mverts(mverts, positions, vnormals);

	return dm;
}

static DerivedMesh *read_curves_sample(DerivedMesh *dm, const IObject &iobject, const float time)
{
	ICurves points(iobject, kWrapExisting);
	ICurvesSchema schema = points.getSchema();
	ISampleSelector sample_sel(time);
	const ICurvesSchema::Sample sample = schema.getValue(sample_sel);

	const P3fArraySamplePtr &positions = sample.getPositions();

	read_mverts(dm->getVertArray(dm), positions, N3fArraySamplePtr());

	return dm;
}

DerivedMesh *ABC_read_mesh(AbcArchiveHandle *handle, DerivedMesh *dm, const char *object_path, const float time)
{
	IArchive *archive = archive_from_handle(handle);

	if (!archive || !archive->valid()) {
		return dm;
	}

	IObject iobject;
	find_iobject(archive->getTop(), iobject, object_path);

	if (!iobject.valid()) {
		return NULL;
	}

	const ObjectHeader &header = iobject.getHeader();

	if (IPolyMesh::matches(header)) {
		return read_mesh_sample(dm, iobject, time);
	}
	else if (IPoints::matches(header)) {
		return read_points_sample(dm, iobject, time);
	}
	else if (ICurves::matches(header)) {
		return read_curves_sample(dm, iobject, time);
	}

	return NULL;
}
