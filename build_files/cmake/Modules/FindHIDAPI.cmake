# - Find HIDAPI library from http://www.signal11.us/oss/hidapi/
# Find the native HIDAPI includes and library
# This module defines
#  HIDAPI_INCLUDE_DIRS, where to find hidapi.h, Set when
#                       HIDAPI_INCLUDE_DIR is found.
#  HIDAPI_LIBRARIES, libraries to link against to use HIDAPI.
#  HIDAPI_ROOT_DIR, The base directory to search for HIDAPI.
#                   This can also be an environment variable.
#  HIDAPI_FOUND, If false, do not try to use HIDAPI.
#
# also defined, but not for general use are
#  HIDAPI_LIBRARY, where to find the HIDAPI library.

#=============================================================================
# Copyright 2016 Blender Foundation.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================

# If HIDAPI_ROOT_DIR was defined in the environment, use it.
IF(NOT HIDAPI_ROOT_DIR AND NOT $ENV{HIDAPI_ROOT_DIR} STREQUAL "")
  SET(HIDAPI_ROOT_DIR $ENV{HIDAPI_ROOT_DIR})
ENDIF()

SET(_hidapi_SEARCH_DIRS
  ${HIDAPI_ROOT_DIR}
  /usr/local
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt/lib/hidapi
)

FIND_PATH(HIDAPI_INCLUDE_DIR
  NAMES
    hidapi.h
  HINTS
    ${_hidapi_SEARCH_DIRS}
  PATH_SUFFIXES
    include/hidapi
)

FIND_LIBRARY(HIDAPI_LIBRARY
  NAMES
    hidapi hidapi-libusb
  HINTS
    ${_hidapi_SEARCH_DIRS}
  PATH_SUFFIXES
    lib64 lib
  )

# Handle the QUIETLY and REQUIRED arguments and set HIDAPI_FOUND to TRUE if
# all listed variables are TRUE
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(HIDAPI DEFAULT_MSG
    HIDAPI_LIBRARY HIDAPI_INCLUDE_DIR)

IF(HIDAPI_FOUND)
  SET(HIDAPI_LIBRARIES ${HIDAPI_LIBRARY})
  SET(HIDAPI_INCLUDE_DIRS ${HIDAPI_INCLUDE_DIR})
ENDIF(HIDAPI_FOUND)

MARK_AS_ADVANCED(
  HIDAPI_INCLUDE_DIR
  HIDAPI_LIBRARY
)
