# Hunter's OpenCL 2.1-p3 loader predates Windows adapter-based ICD discovery.
if(CMAKE_VERSION VERSION_LESS 3.16)
    message(FATAL_ERROR "Building the OpenCL loader requires CMake 3.16 or newer")
endif()
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)
FetchContent_Declare(firominer_opencl_headers
    URL https://github.com/KhronosGroup/OpenCL-Headers/archive/refs/tags/v2026.05.29.tar.gz
    URL_HASH SHA256=d9e6c48357de5002da11ce45de600e0c3ffe6ab4f628a3b9fe2b38603161658a
)
FetchContent_Declare(firominer_opencl_loader
    URL https://github.com/KhronosGroup/OpenCL-ICD-Loader/archive/refs/tags/v2026.05.29.tar.gz
    URL_HASH SHA256=48fd0c5181db7cd046f4f731d5955694892e10998d49d09ee0d997e7e04fd939
)

# Preserve self-contained packages; update this pin when loader fixes are needed.
set(OPENCL_ICD_LOADER_BUILD_SHARED_LIBS OFF)
set(ENABLE_OPENCL_LAYERS OFF)
set(OPENCL_ICD_LOADER_BUILD_TESTING OFF)
set(OPENCL_HEADERS_BUILD_TESTING OFF)
FetchContent_MakeAvailable(firominer_opencl_headers firominer_opencl_loader)

# The upstream Windows sources rely on declarations omitted by these miner flags.
if(MSVC)
    get_directory_property(opencl_definitions DIRECTORY ${firominer_opencl_loader_SOURCE_DIR}
        COMPILE_DEFINITIONS)
    list(REMOVE_ITEM opencl_definitions WIN32_LEAN_AND_MEAN VC_EXTRALEAN)
    set_property(DIRECTORY ${firominer_opencl_loader_SOURCE_DIR}
        PROPERTY COMPILE_DEFINITIONS ${opencl_definitions})
endif()

# Build linked dependencies, but do not package their SDK headers and libraries.
set_property(DIRECTORY ${firominer_opencl_headers_SOURCE_DIR}
    PROPERTY EXCLUDE_FROM_ALL TRUE)
set_property(DIRECTORY ${firominer_opencl_loader_SOURCE_DIR}
    PROPERTY EXCLUDE_FROM_ALL TRUE)

install(FILES ${firominer_opencl_headers_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer/licenses RENAME OpenCL-Headers-LICENSE)
install(FILES ${firominer_opencl_loader_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer/licenses RENAME OpenCL-ICD-Loader-LICENSE)
