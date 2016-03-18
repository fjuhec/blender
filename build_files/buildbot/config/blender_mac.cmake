# ######## Global feature set settings ########

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/config/blender_full.cmake")

# Set clang omp compiler
set(CMAKE_C_COMPILER         "${CMAKE_SOURCE_DIR}/../../lib/darwin-9.x.universal/clang-omp-3.5/bin/clang"    CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER       "${CMAKE_SOURCE_DIR}/../../lib/darwin-9.x.universal/clang-omp-3.5/bin/clang++"  CACHE STRING "" FORCE)
