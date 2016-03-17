project(openhmd C CXX)
set(CMAKE_C_FLAGS "-std=c99")

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

if(WIN32)
	set(CMAKE_C_FLAGS ${CMAKE_C_FLAGS} -DOHMD_STATIC)
endif(WIN32)

#source files set just for Android
set(openhmd_source_files
	${CMAKE_CURRENT_LIST_DIR}/src/openhmd.c
	${CMAKE_CURRENT_LIST_DIR}/src/platform-win32.c
	${CMAKE_CURRENT_LIST_DIR}/src/drv_dummy/dummy.c
	${CMAKE_CURRENT_LIST_DIR}/src/omath.c
	${CMAKE_CURRENT_LIST_DIR}/src/platform-posix.c
	${CMAKE_CURRENT_LIST_DIR}/src/fusion.c
)   

OPTION(OPENHMD_DRIVER_OCULUS_RIFT "Oculus Rift DK1 and DK2" ON)
OPTION(OPENHMD_DRIVER_EXTERNAL "External sensor driver" OFF)
OPTION(OPENHMD_DRIVER_ANDROID "General Android driver" OFF)

if(OPENHMD_DRIVER_OCULUS_RIFT)
	set(openhmd_source_files ${openhmd_source_files} 
	${CMAKE_CURRENT_LIST_DIR}/src/drv_oculus_rift/rift.c
	${CMAKE_CURRENT_LIST_DIR}/src/drv_oculus_rift/packet.c
	)
	add_definitions(-DDRIVER_OCULUS_RIFT)

	find_package(HIDAPI REQUIRED)
	include_directories(${HIDAPI_INCLUDE_DIRS})
	set(LIBS ${LIBS} ${HIDAPI_LIBRARIES})
endif(OPENHMD_DRIVER_OCULUS_RIFT)

if (OPENHMD_DRIVER_EXTERNAL)
	set(openhmd_source_files ${openhmd_source_files} 
	${CMAKE_CURRENT_LIST_DIR}/src/drv_external/external.c
	)
	add_definitions(-DDRIVER_EXTERNAL)
endif(OPENHMD_DRIVER_EXTERNAL)

if (OPENHMD_DRIVER_ANDROID)
	set(openhmd_source_files ${openhmd_source_files} 
	${CMAKE_CURRENT_LIST_DIR}/src/drv_android/android.c
	)
	add_definitions(-DDRIVER_ANDROID)
endif(OPENHMD_DRIVER_ANDROID)

blender_add_lib(extern_openhmd "${openhmd_source_files}" "${openhmd_source_files}" "${LIBS}")
