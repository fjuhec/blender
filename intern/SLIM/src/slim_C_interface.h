//
//  slim_C_interface.hpp
//  Blender
//
//  Created by Aurel Gruber on 30.11.16.
//
//

#ifndef slim_C_interface_hpp
#define slim_C_interface_hpp

#include "matrix_transfer.h"

#ifdef __cplusplus
extern "C" {
#endif


void param_slim_C(matrix_transfer *mt, int nIterations, bool borderVerticesArePinned, bool skipInitialization);
void transfer_uvs_blended_C(matrix_transfer *mt, void* slim, int chartNr, float blend);
void param_slim_single_iteration_C(void *slim);
void* setup_slim_C(matrix_transfer *mt, int chartNr, bool fixBorder, bool skipInitialization);
void free_slim_data_C(void* slimDataPtr);

#ifdef __cplusplus
}
#endif

#endif /* slim_C_interface_hpp */
