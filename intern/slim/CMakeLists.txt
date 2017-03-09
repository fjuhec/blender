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
  matrix_transfer.h
  slim.h
  slim_parametrizer.h
  uv_initializer.h
  slim_c_interface.h
  area_compensation.h
  least_squares_relocator.h
  geometry_data_retrieval.h

  src/slim.cpp
  src/slim_parametrizer.cpp
  src/uv_initializer.cpp
  src/slim_c_interface.cpp
  src/area_compensation.cpp
  src/least_squares_relocator.cpp
  src/geometry_data_retrieval.cpp


  ${ADDITIONAL_SOURCES}
)

blender_add_lib(bf_intern_slim "${SRC}" "${INC}" "${INC_SYS}")
target_link_libraries(bf_intern_slim ${ADDITIONAL_LIBRARIES})
