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
  slim.h
  slim_parametrizer.h
  uv_initializer.h
  slim_capi.h
  area_compensation.h
  least_squares_relocator.h
  geometry_data_retrieval.h

  intern/slim.cpp
  intern/slim_parametrizer.cpp
  intern/uv_initializer.cpp
  intern/slim_capi.cpp
  intern/area_compensation.cpp
  intern/least_squares_relocator.cpp
  intern/geometry_data_retrieval.cpp


  ${ADDITIONAL_SOURCES}
)

blender_add_lib(bf_intern_slim "${SRC}" "${INC}" "${INC_SYS}")
target_link_libraries(bf_intern_slim ${ADDITIONAL_LIBRARIES})
