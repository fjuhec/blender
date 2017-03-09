//
//  areaCompensator.cpp
//  Blender
//
//  Created by Aurel Gruber on 05.12.16.
//
//

#include "slim.h"

#include <doublearea.h>
#include <Eigen/Dense>

using namespace Eigen;
using namespace igl;

namespace areacomp {
	void correctMapSurfaceAreaIfNecessary(SLIMData *slimData);
	void correctMeshSurfaceAreaIfNecessary(SLIMData *slimData, double relative_scale = 1.0);
}
