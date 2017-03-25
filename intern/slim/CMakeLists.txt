cmake_minimum_required(VERSION 2.6)

set(INC
  ./
  ext/libigl_extract
  ../../source/blender/editors/uvedit
  ../../source/blender/makesdna
  ../../source/blender/blenlib
)

set(INC_SYS
  ${EIGEN3_INCLUDE_DIRS}
)

set(SRC
  slim_matrix_transfer.h
  slim_capi.h

  intern/area_compensation.cpp
  intern/area_compensation.h
  intern/geometry_data_retrieval.cpp
  intern/geometry_data_retrieval.h
  intern/least_squares_relocator.cpp
  intern/least_squares_relocator.h
  intern/slim.cpp
  intern/slim.h
  intern/slim_capi.cpp
  intern/slim_parametrizer.cpp
  intern/slim_parametrizer.h
  intern/uv_initializer.cpp
  intern/uv_initializer.h


  ${ADDITIONAL_SOURCES}
)

blender_add_lib(bf_intern_slim "${SRC}" "${INC}" "${INC_SYS}")
target_link_libraries(bf_intern_slim ${ADDITIONAL_LIBRARIES})
