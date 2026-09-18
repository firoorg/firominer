# Package an installed GUI using the Qt kit that built it. This is a CI packaging
# step, not part of ordinary system installation (which uses distribution Qt).
cmake_minimum_required(VERSION 3.19)
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Linux GUI deployment must run on Linux")
endif()
get_filename_component(PACKAGE_DIR "${PACKAGE_DIR}" ABSOLUTE)
set(gui "${PACKAGE_DIR}/bin/firominer-gui")
set(libraries "${PACKAGE_DIR}/lib/firominer-gui")
if(VERIFY_ONLY)
    set(QT_PLUGIN_DIR "${libraries}/plugins")
endif()
if(NOT EXISTS "${gui}" OR NOT IS_DIRECTORY "${QT_PLUGIN_DIR}")
    message(FATAL_ERROR "Provide PACKAGE_DIR with an installed GUI and QT_PLUGIN_DIR from qmake6")
endif()
set(plugins platforms/libqxcb.so platforms/libqoffscreen.so imageformats/libqico.so)
set(plugin_files)
foreach(plugin IN LISTS plugins)
    list(APPEND plugin_files "${QT_PLUGIN_DIR}/${plugin}")
endforeach()

# Leave the platform C/C++ runtime and graphics-driver stack to the host.
# In particular, an older bundled libstdc++ can break a newer GPU driver.
set(platform_libraries
    "^ld-linux.*" "^lib(c|m|pthread|dl|rt|resolv|util|anl)\\.so.*"
    "^lib(stdc\\+\\+|gcc_s)\\.so.*"
    "^lib(GL|EGL|GLX|OpenGL|GLdispatch|vulkan|drm|gbm|nvidia).*\\.so.*")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${gui}" MODULES ${plugin_files}
    PRE_EXCLUDE_REGEXES ${platform_libraries}
    RESOLVED_DEPENDENCIES_VAR dependencies)
if(VERIFY_ONLY)
    foreach(dependency IN LISTS dependencies)
        file(REAL_PATH "${dependency}" resolved)
        file(RELATIVE_PATH relative "${libraries}" "${resolved}")
        if(relative MATCHES "^\\.\\./")
            message(FATAL_ERROR "GUI dependency escaped the package: ${dependency}")
        endif()
    endforeach()
    return()
endif()
find_program(patchelf patchelf REQUIRED)
foreach(dependency IN LISTS dependencies)
    file(INSTALL DESTINATION "${libraries}" TYPE SHARED_LIBRARY
        FOLLOW_SYMLINK_CHAIN FILES "${dependency}")
endforeach()
foreach(plugin IN LISTS plugins)
    get_filename_component(category "${plugin}" DIRECTORY)
    file(INSTALL DESTINATION "${libraries}/plugins/${category}" TYPE MODULE
        FILES "${QT_PLUGIN_DIR}/${plugin}")
endforeach()
file(WRITE "${PACKAGE_DIR}/bin/qt.conf"
    "[Paths]\nPrefix=..\nLibraries=lib/firominer-gui\nPlugins=lib/firominer-gui/plugins\n")

# RUNPATH is not transitive. Each copied library and plugin needs its own path.
file(GLOB_RECURSE bundled_files "${libraries}/*")
foreach(binary IN LISTS bundled_files)
    if(NOT IS_SYMLINK "${binary}")
        get_filename_component(directory "${binary}" DIRECTORY)
        file(RELATIVE_PATH relative "${directory}" "${libraries}")
        execute_process(COMMAND "${patchelf}" --set-rpath "$ORIGIN/${relative}" "${binary}"
            COMMAND_ERROR_IS_FATAL ANY)
    endif()
endforeach()
execute_process(COMMAND "${patchelf}" --set-rpath "$ORIGIN/../lib/firominer-gui" "${gui}"
    COMMAND_ERROR_IS_FATAL ANY)

# The source collector uses original paths, including the dynamically loaded plugins.
list(APPEND dependencies ${plugin_files})
list(REMOVE_DUPLICATES dependencies)
list(SORT dependencies)
string(REPLACE ";" "\n" manifest "${dependencies}")
file(WRITE "${PACKAGE_DIR}/share/firominer-gui/bundled-libraries.txt" "${manifest}\n")
