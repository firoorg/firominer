cmake_minimum_required(VERSION 3.18)

# Standalone builds need only Qt, not the miner's GPU toolkits or dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(firominer-gui LANGUAGES CXX)
    include(CTest)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../CMakeLists.txt" version_line
        REGEX "^set\\(PROJECT_VERSION ")
    string(REGEX REPLACE "^set\\(PROJECT_VERSION ([^)]+)\\)$" "\\1"
        PROJECT_VERSION "${version_line}")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network)
include(GNUInstallDirs)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(firominer-gui WIN32
    main.cpp mainwindow.cpp mainwindow.h
    minercontroller.cpp minercontroller.h resources.qrc)
target_link_libraries(firominer-gui PRIVATE Qt6::Widgets Qt6::Network)
target_compile_definitions(firominer-gui PRIVATE FIROMINER_GUI_VERSION="${PROJECT_VERSION}")
if(WIN32)
    enable_language(RC)
    target_sources(firominer-gui PRIVATE firominer-gui.rc)
    target_include_directories(firominer-gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../firominer")
endif()

install(TARGETS firominer-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../docs/GUI.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer-gui)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer-gui)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../docs/LGPL-3.0.txt"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer-gui/licenses)

if(WIN32)
    get_target_property(qmake_executable Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(qt_bin_dir "${qmake_executable}" DIRECTORY)
    get_filename_component(qt_prefix "${qt_bin_dir}" DIRECTORY)
    foreach(qt_licenses "${qt_prefix}/LICENSES" "${qt_prefix}/share/licenses/qt6-base")
        if(IS_DIRECTORY "${qt_licenses}")
            install(DIRECTORY "${qt_licenses}/"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer-gui/licenses/qt)
            break()
        endif()
    endforeach()
    find_program(FIROMINER_WINDEPLOYQT NAMES windeployqt HINTS "${qt_bin_dir}" REQUIRED)
    set(qt_compiler_runtime --compiler-runtime)
    if(MSVC)
        set(qt_compiler_runtime --no-compiler-runtime)
    endif()
    install(CODE "
        execute_process(COMMAND \"${FIROMINER_WINDEPLOYQT}\"
            --$<IF:$<CONFIG:Debug>,debug,release> --no-plugins --no-translations --no-opengl-sw ${qt_compiler_runtime}
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:firominer-gui>\"
            RESULT_VARIABLE deploy_result)
        if(NOT deploy_result EQUAL 0)
            message(FATAL_ERROR \"windeployqt failed: \${deploy_result}\")
        endif()")
    # Deploy only qtbase plugins, matching the corresponding source in the CI bundle.
    install(FILES "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/platforms)
    install(FILES "$<TARGET_FILE:Qt6::QICOPlugin>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/imageformats)
    foreach(qt_style QModernWindowsStylePlugin QWindowsVistaStylePlugin)
        if(TARGET Qt6::${qt_style})
            install(FILES "$<TARGET_FILE:Qt6::${qt_style}>"
                DESTINATION ${CMAKE_INSTALL_BINDIR}/styles)
        endif()
    endforeach()
    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
        include(InstallRequiredSystemLibraries)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    add_executable(gui-test-miner test_miner.cpp)
    target_link_libraries(gui-test-miner PRIVATE Qt6::Core Qt6::Network)
    add_executable(test-controller test_controller.cpp minercontroller.cpp minercontroller.h)
    target_link_libraries(test-controller PRIVATE Qt6::Network Qt6::Test)
    add_dependencies(test-controller gui-test-miner)
    add_test(NAME gui-controller COMMAND test-controller $<TARGET_FILE:gui-test-miner>)
    add_executable(test-window test_window.cpp mainwindow.cpp mainwindow.h
        minercontroller.cpp minercontroller.h resources.qrc)
    target_link_libraries(test-window PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
    add_dependencies(test-window gui-test-miner)
    target_compile_definitions(test-window PRIVATE FIROMINER_GUI_VERSION="${PROJECT_VERSION}")
    add_test(NAME gui-window COMMAND test-window)
    set_tests_properties(gui-controller gui-window PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
    set_tests_properties(gui-controller PROPERTIES TIMEOUT 120)
    set_tests_properties(gui-window PROPERTIES TIMEOUT 60)
    foreach(scale 1.25 1.5 2)
        add_test(NAME gui-layout-${scale} COMMAND test-window layoutAtDifferentSizes)
        set_tests_properties(gui-layout-${scale} PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;QT_SCALE_FACTOR=${scale}")
    endforeach()
endif()
