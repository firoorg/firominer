# Use Boost's CMake release: Hunter's b2 recipe rejects newer header-only libraries.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

function(firominer_add_boost)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BOOST_INCLUDE_LIBRARIES filesystem process system)
    set(BOOST_SKIP_INSTALL_RULES ON)
    FetchContent_Declare(firominer_boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz
        URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
    )
    FetchContent_MakeAvailable(firominer_boost)

    # Match installed Boost::headers for header-only uses beyond the built libraries.
    file(GLOB boost_header_dirs
        "${firominer_boost_SOURCE_DIR}/libs/*/include"
        "${firominer_boost_SOURCE_DIR}/libs/numeric/*/include")
    target_include_directories(boost_headers INTERFACE ${boost_header_dirs})
    target_compile_definitions(boost_headers INTERFACE BOOST_ALL_NO_LIB)
    set_property(DIRECTORY ${firominer_boost_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)

    get_directory_property(boost_version DIRECTORY ${firominer_boost_SOURCE_DIR}
        DEFINITION Boost_VERSION)
    set(Boost_VERSION_STRING "${boost_version}" PARENT_SCOPE)
    install(FILES "${firominer_boost_SOURCE_DIR}/LICENSE_1_0.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer/licenses RENAME Boost-LICENSE)
endfunction()
firominer_add_boost()
