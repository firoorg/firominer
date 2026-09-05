file(GLOB sources "*.cpp")
file(GLOB headers "*.h")

add_library(ethash-cpu ${sources} ${headers})
target_link_libraries(ethash-cpu ethcore crypto Threads::Threads)
target_include_directories(ethash-cpu PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR})
