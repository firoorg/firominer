cmake_policy(SET CMP0015 NEW)

aux_source_directory(. SRC_LIST)

include_directories(BEFORE ..)

set(EXECUTABLE firominer)

file(GLOB HEADERS "*.h")

add_executable(${EXECUTABLE} ${SRC_LIST} ${HEADERS})
if(MSVC)
	target_sources(${EXECUTABLE} PRIVATE firominer.rc firominer.manifest)
endif()

hunter_add_package(CLI11)
find_package(CLI11 CONFIG REQUIRED)

target_link_libraries(firominer PRIVATE ethcore poolprotocols devcore firominer-buildinfo CLI11::CLI11 Boost::filesystem Boost::system)

if(ETHDBUS)
	find_package(PkgConfig)
	set( ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/lib/x86_64-linux-gnu/pkgconfig" )
	pkg_check_modules(DBUS dbus-1)
	include_directories(${DBUS_INCLUDE_DIRS})
	link_directories(${DBUS_LIBRARY_DIRS})
	target_link_libraries(firominer PRIVATE ${DBUS_LIBRARIES})
endif()

if(APICORE)
	target_link_libraries(firominer PRIVATE apicore)
endif()

include(GNUInstallDirs)
install(TARGETS firominer DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    set(GUI_LAUNCHER firominer-gui.pyw)
else()
    set(GUI_LAUNCHER firominer-gui.py)
endif()
install(PROGRAMS ../gui/firominer_gui.py DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME ${GUI_LAUNCHER})
install(DIRECTORY ../patches/ DESTINATION ${CMAKE_INSTALL_DATADIR}/firominer/patches)
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:firominer> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()
