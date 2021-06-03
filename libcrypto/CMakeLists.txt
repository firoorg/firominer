file(GLOB_RECURSE CRYPTO_SRC CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.c" "*.h")
add_library(crypto ${CRYPTO_SRC})
target_include_directories(crypto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crypto PUBLIC intx::intx)