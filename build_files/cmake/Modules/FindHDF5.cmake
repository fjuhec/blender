# - Find Alembic library
# Find the native Alembic includes and libraries
# This module defines
#  HDF5_INCLUDE_DIRS, where to find samplerate.h, Set when
#                        HDF5_INCLUDE_DIR is found.
#  HDF5_LIBRARIES, libraries to link against to use Samplerate.
#  HDF5_ROOT_DIR, The base directory to search for Samplerate.
#                    This can also be an environment variable.
#  HDF5_FOUND, If false, do not try to use Samplerate.
#

#=============================================================================
# Copyright 2011 Blender Foundation.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

# If HDF5_ROOT_DIR was defined in the environment, use it.
IF(NOT HDF5_ROOT_DIR AND NOT $ENV{HDF5_ROOT_DIR} STREQUAL "")
  SET(HDF5_ROOT_DIR $ENV{HDF5_ROOT_DIR})
ENDIF()

SET(_hdf5_SEARCH_DIRS
  ${HDF5_ROOT_DIR}
  /usr/local
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt/lib/hdf5
)

SET(_hdf5_FIND_COMPONENTS
  hdf5
  hdf5_hl
)

FIND_PATH(HDF5_INCLUDE_DIR
  NAMES
    hdf5.h
  HINTS
    ${_hdf5_SEARCH_DIRS}
  PATH_SUFFIXES
    include
)

SET(_hdf5_LIBRARIES)
FOREACH(COMPONENT ${_hdf5_FIND_COMPONENTS})
  STRING(TOUPPER ${COMPONENT} UPPERCOMPONENT)

  FIND_LIBRARY(${UPPERCOMPONENT}_LIBRARY
    NAMES
      ${COMPONENT}
    HINTS
      ${_hdf5_SEARCH_DIRS}
    PATH_SUFFIXES
      lib64 lib
    )
  MARK_AS_ADVANCED(${UPPERCOMPONENT}_LIBRARY)
  LIST(APPEND _hdf5_LIBRARIES "${${UPPERCOMPONENT}_LIBRARY}")
ENDFOREACH()

# handle the QUIETLY and REQUIRED arguments and set HDF5_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(HDF5 DEFAULT_MSG _hdf5_LIBRARIES HDF5_INCLUDE_DIR)

IF(HDF5_FOUND)
  SET(HDF5_LIBRARIES ${_hdf5_LIBRARIES})
  SET(HDF5_INCLUDE_DIRS ${HDF5_INCLUDE_DIR})
ENDIF(HDF5_FOUND)

MARK_AS_ADVANCED(
  HDF5_INCLUDE_DIR
  HDF5_LIBRARY
)

UNSET(COMPONENT)
UNSET(UPPERCOMPONENT)
UNSET(_hdf5_LIBRARIES)
