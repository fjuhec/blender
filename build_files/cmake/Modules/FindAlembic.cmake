# - Find Alembic library
# Find the native Alembic includes and libraries
# This module defines
#  ALEMBIC_INCLUDE_DIRS, where to find samplerate.h, Set when
#                        ALEMBIC_INCLUDE_DIR is found.
#  ALEMBIC_LIBRARIES, libraries to link against to use Samplerate.
#  ALEMBIC_ROOT_DIR, The base directory to search for Samplerate.
#                    This can also be an environment variable.
#  ALEMBIC_FOUND, If false, do not try to use Samplerate.
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

# If ALEMBIC_ROOT_DIR was defined in the environment, use it.
IF(NOT ALEMBIC_ROOT_DIR AND NOT $ENV{ALEMBIC_ROOT_DIR} STREQUAL "")
  SET(ALEMBIC_ROOT_DIR $ENV{ALEMBIC_ROOT_DIR})
ENDIF()

SET(_alembic_SEARCH_DIRS
  ${ALEMBIC_ROOT_DIR}
  /usr/local
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt/lib/alembic
)

SET(_alembic_FIND_COMPONENTS
  AlembicAbc
  AlembicAbcGeom
  AlembicAbcCoreAbstract
  AlembicUtil
  AlembicAbcCoreHDF5
  AlembicOgawa
  AlembicAbcCoreOgawa
  AlembicAbcMaterial
  AlembicAbcCoreFactory
)

FIND_PATH(ALEMBIC_INCLUDE_DIR
  NAMES
    Alembic/Abc/All.h
  HINTS
    ${_alembic_SEARCH_DIRS}
  PATH_SUFFIXES
    include
)

SET(_alembic_LIBRARIES)
FOREACH(COMPONENT ${_alembic_FIND_COMPONENTS})
  STRING(TOUPPER ${COMPONENT} UPPERCOMPONENT)

  FIND_LIBRARY(${UPPERCOMPONENT}_LIBRARY
    NAMES
      ${COMPONENT}
    HINTS
      ${_alembic_SEARCH_DIRS}
    PATH_SUFFIXES
      lib64 lib lib/static
    )
  MARK_AS_ADVANCED(${UPPERCOMPONENT}_LIBRARY)
  LIST(APPEND _alembic_LIBRARIES "${${UPPERCOMPONENT}_LIBRARY}")
ENDFOREACH()

# handle the QUIETLY and REQUIRED arguments and set ALEMBIC_FOUND to TRUE if 
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ALEMBIC DEFAULT_MSG _alembic_LIBRARIES ALEMBIC_INCLUDE_DIR)

IF(ALEMBIC_FOUND)
  SET(ALEMBIC_LIBRARIES ${_alembic_LIBRARIES})
  SET(ALEMBIC_INCLUDE_DIRS ${ALEMBIC_INCLUDE_DIR})
ENDIF(ALEMBIC_FOUND)

MARK_AS_ADVANCED(
  ALEMBIC_INCLUDE_DIR
  ALEMBIC_LIBRARY
)

UNSET(COMPONENT)
UNSET(UPPERCOMPONENT)
UNSET(_alembic_LIBRARIES)
