hunter_config(CURL VERSION ${HUNTER_CURL_VERSION} CMAKE_ARGS HTTP_ONLY=ON CMAKE_USE_OPENSSL=OFF CMAKE_USE_LIBSSH2=OFF CURL_CA_PATH=none)
hunter_config(Boost VERSION 1.66.0)

hunter_config(ethash VERSION 0.5.1
    URL https://github.com/leerproject/cpp-ethash/archive/0.5.1-alpha.1.1.tar.gz
    SHA1 1f02418e00ba65b8d191aee93cb7aaccb54a29fd
)
