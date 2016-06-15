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
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2016 Kévin Dietrich.
 * All rights reserved.
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

#include "abc_customdata.h"

#include <Alembic/AbcGeom/All.h>
#include <algorithm>

extern "C" {
#include "DNA_meshdata_types.h"

#include "BKE_customdata.h"
}

using Alembic::AbcGeom::kFacevaryingScope;

using Alembic::Abc::C4fArraySample;
using Alembic::Abc::UInt32ArraySample;
using Alembic::Abc::V2fArraySample;

using Alembic::AbcGeom::OV2fGeomParam;
using Alembic::AbcGeom::OC4fGeomParam;

static void get_uvs(const CDWriterConfig &config,
                    std::vector<Imath::V2f> &uvs,
                    std::vector<uint32_t> &uvidx,
                    void *cd_data)
{
	MLoopUV *mloopuv_array = static_cast<MLoopUV *>(cd_data);

	if (!mloopuv_array) {
		return;
	}

	const int num_poly = config.totpoly;
	MPoly *polygons = config.mpoly;

	if (!config.pack_uvs) {
		int cnt = 0;
		for (int i = 0; i < num_poly; ++i) {
			MPoly &current_poly = polygons[i];
			MLoopUV *loopuvpoly = mloopuv_array + current_poly.loopstart + current_poly.totloop;

			for (int j = 0; j < current_poly.totloop; ++j) {
				loopuvpoly--;
				uvidx.push_back(cnt++);
				Imath::V2f uv(loopuvpoly->uv[0], loopuvpoly->uv[1]);
				uvs.push_back(uv);
			}
		}
	}
	else {
		for (int i = 0; i < num_poly; ++i) {
			MPoly &current_poly = polygons[i];
			MLoopUV *loopuvpoly = mloopuv_array + current_poly.loopstart + current_poly.totloop;

			for (int j = 0; j < current_poly.totloop; ++j) {
				loopuvpoly--;
				Imath::V2f uv(loopuvpoly->uv[0], loopuvpoly->uv[1]);

				std::vector<Imath::V2f>::iterator it = std::find(uvs.begin(), uvs.end(), uv);

				if (it == uvs.end()) {
					uvidx.push_back(uvs.size());
					uvs.push_back(uv);
				}
				else {
					uvidx.push_back(std::distance(uvs.begin(), it));
				}
			}
		}
	}
}

void get_uv_sample(UVSample &sample, const CDWriterConfig &config, CustomData *data)
{
	const int active_uvlayer = CustomData_get_active_layer(data, CD_MLOOPUV);

	if (active_uvlayer < 0) {
		return;
	}

	void *cd_data = CustomData_get_layer_n(data, CD_MLOOPUV, active_uvlayer);

	get_uvs(config, sample.uvs, sample.indices, cd_data);
}

static void write_uv(const OCompoundProperty &prop, const CDWriterConfig &config, void *data, const char *name)
{
	std::vector<uint32_t> indices;
	std::vector<Imath::V2f> uvs;

	get_uvs(config, uvs, indices, data);

	if (indices.empty() || uvs.empty()) {
		return;
	}

	OV2fGeomParam param(prop, name, true, kFacevaryingScope, 1);

	OV2fGeomParam::Sample sample(
		V2fArraySample((const Imath::V2f *)&uvs.front(), uvs.size()),
		UInt32ArraySample((const uint32_t *)&indices.front(), indices.size()),
		kFacevaryingScope);

	param.set(sample);
}

static void write_mcol(const OCompoundProperty &prop, const CDWriterConfig &config, void *data, const char *name)
{
	const float cscale = 1.0f / 255.0f;
	MPoly *polys = config.mpoly;
	MCol *cfaces = static_cast<MCol *>(data);

	std::vector<float> buffer;

	for (int i = 0; i < config.totpoly; ++i) {
		MPoly *p = &polys[i];
		MCol *cface = &cfaces[p->loopstart + p->totloop];

		for (int j = 0; j < p->totloop; ++j) {
			cface--;
			buffer.push_back(cface->b * cscale);
			buffer.push_back(cface->g * cscale);
			buffer.push_back(cface->r * cscale);
			buffer.push_back(cface->a * cscale);
		}
	}

	OC4fGeomParam param(prop, name, true, kFacevaryingScope, 1);

	OC4fGeomParam::Sample sample(
		C4fArraySample((const Imath::C4f *)&buffer.front(), buffer.size() / 4),
		kFacevaryingScope);

	param.set(sample);
}

void write_custom_data(const OCompoundProperty &prop, const CDWriterConfig &config, CustomData *data, int data_type)
{
	CustomDataType cd_data_type = static_cast<CustomDataType>(data_type);

	if (!CustomData_has_layer(data, cd_data_type)) {
		return;
	}

	const int active_layer = CustomData_get_active_layer(data, cd_data_type);
	const int tot_layers = CustomData_number_of_layers(data, cd_data_type);

	for (int i = 0; i < tot_layers; ++i) {
		void *cd_data = CustomData_get_layer_n(data, cd_data_type, i);
		const char *name = CustomData_get_layer_name(data, cd_data_type, i);

		if (cd_data_type == CD_MLOOPUV) {
			/* Already exported. */
			if (i == active_layer) {
				continue;
			}

			write_uv(prop, config, cd_data, name);
		}
		else if (cd_data_type == CD_MLOOPCOL) {
			write_mcol(prop, config, cd_data, name);
		}
	}
}
