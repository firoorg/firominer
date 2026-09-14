# A custom command and target to turn the OpenCL kernel into a byte array header
# The normal build depends on it properly and if the kernel file is changed, then
# a rebuild of libethash-cl should be triggered

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/CLMiner_kernel.h
	COMMAND ${CMAKE_COMMAND} ARGS
	"-DTXT2STR_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/CLMiner_kernel.cl"
	-DTXT2STR_VARIABLE_NAME=CLMiner_kernel
	"-DTXT2STR_HEADER_FILE=${CMAKE_CURRENT_BINARY_DIR}/CLMiner_kernel.h"
	-P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/txt2str.cmake"
	COMMENT "Generating OpenCL Kernel"
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/CLMiner_kernel.cl
		${CMAKE_CURRENT_SOURCE_DIR}/../cmake/txt2str.cmake
	VERBATIM
)
add_custom_target(cl_kernel DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/CLMiner_kernel.h ${CMAKE_CURRENT_SOURCE_DIR}/CLMiner_kernel.cl)

if(BUILD_TESTING AND FIROMINER_PYTHON3 AND FIROMINER_PYTHON_VERSION_CHECK EQUAL 0)
	add_test(NAME opencl-kernel-embedding COMMAND ${FIROMINER_PYTHON3}
		${CMAKE_CURRENT_SOURCE_DIR}/../test/opencl-embedding.py
		${CMAKE_COMMAND}
		${CMAKE_CURRENT_SOURCE_DIR}/../cmake/txt2str.cmake
		${CMAKE_CURRENT_SOURCE_DIR}/CLMiner_kernel.cl)
endif()

set(SOURCES
	CLMiner.h CLMiner.cpp
	${CMAKE_CURRENT_BINARY_DIR}/CLMiner_kernel.h
)

if(APPLE OR NOT HUNTER_ENABLED)
	# Use the system library on macOS and for unmanaged dependency builds.
	find_package(OpenCL REQUIRED)
	if(NOT TARGET OpenCL::OpenCL)
		# FindOpenCL only provides variables before CMake 3.7.
		add_library(OpenCL::OpenCL INTERFACE IMPORTED)
		set_target_properties(OpenCL::OpenCL PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${OpenCL_INCLUDE_DIRS}"
			INTERFACE_LINK_LIBRARIES "${OpenCL_LIBRARIES}")
	endif()
else()
	include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/OpenCL.cmake)
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(..)

add_library(ethash-cl ${SOURCES})
target_link_libraries(ethash-cl PUBLIC ethcore crypto)
target_link_libraries(ethash-cl PUBLIC OpenCL::OpenCL)
target_link_libraries(ethash-cl PRIVATE Boost::filesystem Threads::Threads)

if(WIN32 AND BUILD_TESTING)
	add_executable(opencl-platform-discovery-test ../test/opencl-platform-discovery.cpp)
	target_link_libraries(opencl-platform-discovery-test PRIVATE OpenCL::OpenCL)
	add_test(NAME opencl-platform-discovery COMMAND opencl-platform-discovery-test)
	set_tests_properties(opencl-platform-discovery PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
endif()
