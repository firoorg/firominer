file(GLOB HEADERS "*.h")
file(GLOB SOURCES "*.cpp")

find_package(Threads)

add_library(devcore ${SOURCES} ${HEADERS})
target_link_libraries(devcore PUBLIC Boost::headers Boost::system)
if(WIN32)
	target_link_libraries(devcore PUBLIC ws2_32 mswsock)
endif()
target_link_libraries(devcore PRIVATE Threads::Threads)

