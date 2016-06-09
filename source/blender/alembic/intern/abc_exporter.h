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

#include <map>
#include <vector>

#include "abc_object.h"
#include "abc_transform.h"

class EvaluationContext;

class AbcExporter {
	ExportSettings &m_settings;

	const char *m_filename;

	Alembic::Abc::OArchive m_archive;
	unsigned int m_trans_sampling_index, m_shape_sampling_index;

	Scene *m_scene;

	std::map<std::string, AbcTransformWriter *> m_xforms;
	std::vector<AbcObjectWriter *> m_shapes;

public:
	AbcExporter(Scene *scene, const char *filename, ExportSettings &settings);
	~AbcExporter();

	void operator()(Main *bmain, float &progress);

private:
	void getShutterSamples(double step, bool time_relative, std::vector<double> &samples);

	Alembic::Abc::TimeSamplingPtr createTimeSampling(double step);

	void getFrameSet(double step, std::set<double> &frames);

	void createTransformWritersHierarchy(EvaluationContext *eval_ctx);
	void createTransformWritersFlat();
	void createTransformWriter(Object *ob,  Object *parent, Object *dupliObParent);
	void exploreTransform(EvaluationContext *eval_ctx, Object *ob, Object *parent, Object *dupliObParent = NULL);
	void exploreObject(EvaluationContext *eval_ctx, Object *ob, Object *dupliObParent);
	void createShapeWriters(EvaluationContext *eval_ctx);
	void createShapeWriter(Object *ob, Object *dupliObParent);

	AbcTransformWriter *getXForm(const std::string &name);

	bool objectIsShape(Object *ob);
	bool objectIsSmokeSim(Object *ob);

	void setCurrentFrame(Main *bmain, double t);
};
