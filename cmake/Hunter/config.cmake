hunter_config(CURL VERSION ${HUNTER_CURL_VERSION} CMAKE_ARGS HTTP_ONLY=ON CMAKE_USE_OPENSSL=ON CMAKE_USE_LIBSSH2=OFF CURL_CA_PATH=none)
hunter_config(Boost VERSION 1.66.0)

hunter_config(ethash VERSION 0.6.4
    URL https://github.com/blondfrogs/cpp-ethash/archive/0.6.4.tar.gz
    SHA1 6d9bda5f21c958ad443dec1a29399369e52cab91
)