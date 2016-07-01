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

#include "abc_curves.h"

#include <cstdio>

#include "abc_transform.h"
#include "abc_util.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_curve_types.h"
#include "DNA_object_types.h"

#include "BLI_listbase.h"

#include "BKE_curve.h"
#include "BKE_object.h"

#include "ED_curve.h"
}

using Alembic::Abc::IInt32ArrayProperty;
using Alembic::Abc::Int32ArraySamplePtr;
using Alembic::Abc::P3fArraySamplePtr;

using Alembic::AbcGeom::ICurves;
using Alembic::AbcGeom::ICurvesSchema;
using Alembic::AbcGeom::ISampleSelector;
using Alembic::AbcGeom::kWrapExisting;

using Alembic::AbcGeom::OCurves;
using Alembic::AbcGeom::OCurvesSchema;
using Alembic::AbcGeom::ON3fGeomParam;
using Alembic::AbcGeom::OV2fGeomParam;

/* ************************************************************************** */

AbcCurveWriter::AbcCurveWriter(Scene *scene,
                               Object *ob,
                               AbcTransformWriter *parent,
                               uint32_t time_sampling,
                               ExportSettings &settings)
    : AbcObjectWriter(scene, ob, time_sampling, settings, parent)
{
	OCurves curves(parent->alembicXform(), m_name, m_time_sampling);
	m_schema = curves.getSchema();
}

void AbcCurveWriter::do_write()
{
	Curve *curve = static_cast<Curve *>(m_object->data);

	std::vector<Imath::V3f> verts;
	std::vector<int32_t> vert_counts;
	std::vector<float> widths;
	Imath::V3f temp_vert;

	Alembic::AbcGeom::BasisType curve_basis;
	Alembic::AbcGeom::CurveType curve_type;
	Alembic::AbcGeom::CurvePeriodicity periodicity;

	Nurb *nurbs = static_cast<Nurb *>(curve->nurb.first);
	for (; nurbs; nurbs = nurbs->next) {
		if ((nurbs->flagu & CU_NURB_ENDPOINT) != 0) {
			periodicity = Alembic::AbcGeom::kNonPeriodic;
		}
		else if ((nurbs->flagu & CU_NURB_CYCLIC) != 0) {
			periodicity = Alembic::AbcGeom::kPeriodic;
		}

		if (nurbs->bp) {
			curve_basis = Alembic::AbcGeom::kNoBasis;
			curve_type = Alembic::AbcGeom::kLinear;

			const int totpoint = nurbs->pntsu * nurbs->pntsv;
			vert_counts.push_back(totpoint);

			const BPoint *point = nurbs->bp;

			for (int i = 0; i < totpoint; ++i, ++point) {
				copy_zup_yup(temp_vert.getValue(), point->vec);
				verts.push_back(temp_vert);
				widths.push_back(point->radius);
			}
		}
		else if (nurbs->bezt) {
			curve_basis = Alembic::AbcGeom::kBezierBasis;
			curve_type = Alembic::AbcGeom::kCubic;

			const int totpoint = nurbs->pntsu;
			vert_counts.push_back(totpoint);

			const BezTriple *bezier = nurbs->bezt;

			/* TODO(kevin): store info about handles, Alembic doesn't have this. */
			for (int i = 0; i < totpoint; ++i, ++bezier) {
				copy_zup_yup(temp_vert.getValue(), bezier->vec[1]);
				verts.push_back(temp_vert);
			}
		}
	}

	Alembic::Abc::P3fArraySample pos(verts);
	m_sample = OCurvesSchema::Sample(pos, vert_counts);
	m_sample.setBasis(curve_basis);
	m_sample.setType(curve_type);
	m_sample.setWrap(periodicity);

	if (!widths.empty()) {
		Alembic::AbcGeom::OFloatGeomParam::Sample width_sample;
		width_sample.setVals(widths);

		m_sample.setWidths(width_sample);
	}

	m_sample.setSelfBounds(bounds());
	m_schema.set(m_sample);
}

/* ************************************************************************** */

AbcCurveReader::AbcCurveReader(const Alembic::Abc::IObject &object, ImportSettings &settings)
    : AbcObjectReader(object, settings)
{
	ICurves abc_curves(object, kWrapExisting);
	m_curves_schema = abc_curves.getSchema();

	get_min_max_time(m_curves_schema, m_min_time, m_max_time);
}

bool AbcCurveReader::valid() const
{
	return m_curves_schema.valid();
}

void AbcCurveReader::readObjectData(Main *bmain, Scene *scene, float time)
{
	Curve *cu = BKE_curve_add(bmain, m_data_name.c_str(), OB_CURVE);

	cu->flag |= CU_DEFORM_FILL;
	cu->actvert = CU_ACT_NONE;

	const ISampleSelector sample_sel(time);

	const ICurvesSchema::Sample smp = m_curves_schema.getValue(sample_sel);
	const Int32ArraySamplePtr hvertices = smp.getCurvesNumVertices();
	const P3fArraySamplePtr positions = smp.getPositions();

	m_object = BKE_object_add(bmain, scene, OB_CURVE, m_object_name.c_str());
	m_object->data = cu;

	size_t idx = 0;
	for (size_t i = 0; i < hvertices->size(); ++i) {
		const int steps = (*hvertices)[i];

		Nurb *nu = static_cast<Nurb *>(MEM_callocN(sizeof(Nurb), "abc_getnurb"));
		nu->bp = static_cast<BPoint *>(MEM_callocN(sizeof(BPoint) * steps, "abc_getnurb"));
		nu->type = CU_NURBS;
		nu->resolu = cu->resolu;
		nu->resolv = cu->resolv;
		nu->pntsu = steps;
		nu->pntsv = 1;
		nu->orderu = steps;
		nu->flag |= CU_SMOOTH;
		nu->flagu |= CU_NURB_ENDPOINT;

		BPoint *bp = nu->bp;

		for (int j = 0; j < steps; ++j, ++bp) {
			Imath::V3f pos = (*positions)[idx++];

			copy_yup_zup(bp->vec, pos.getValue());
			bp->vec[3] = 1.0f;

			bp->radius = bp->weight = 1.0f;
		}

		BKE_nurb_knot_calc_u(nu);

		BLI_addtail(BKE_curve_nurbs_get(cu), nu);
	}

	if (m_settings->is_sequence || !m_curves_schema.isConstant()) {
		addDefaultModifier(bmain);
	}
}
