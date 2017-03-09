cmake_minimum_required(VERSION 2.6)

set(INC
  ext/libigl_extract
  ../../source/blender/editors/uvedit
  ../../source/blender/makesdna
  ../../source/blender/blenlib
)

set(INC_SYS
  ${EIGEN3_INCLUDE_DIRS}
)

set(SRC
  src/slim.cpp
  src/slim_parametrizer.cpp
  src/UVInitializer.cpp
  src/slim_C_interface.cpp
  src/area_compensation.cpp
  src/least_squares_relocator.cpp
  src/geometry_data_retrieval.cpp

  src/matrix_transfer.h
  src/slim.h
  src/slim_parametrizer.h
  src/UVInitializer.h
  src/slim_C_interface.h
  src/area_compensation.h
  src/least_squares_relocator.h
  src/geometry_data_retrieval.h

  ${ADDITIONAL_SOURCES}
)

blender_add_lib(bf_intern_slim "${SRC}" "${INC}" "${INC_SYS}")
target_link_libraries(bf_intern_slim ${ADDITIONAL_LIBRARIES})
