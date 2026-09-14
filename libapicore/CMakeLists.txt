set(SOURCES
    ApiServer.h ApiServer.cpp
)

add_library(apicore ${SOURCES})
target_link_libraries(apicore PUBLIC ethcore poolprotocols PRIVATE firominer-buildinfo Boost::filesystem)
target_link_libraries(apicore PUBLIC JsonCpp::JsonCpp)
target_include_directories(apicore PRIVATE ..)
