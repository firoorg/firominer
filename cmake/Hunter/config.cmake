hunter_config(CURL VERSION ${HUNTER_CURL_VERSION} CMAKE_ARGS HTTP_ONLY=ON CMAKE_USE_OPENSSL=ON CMAKE_USE_LIBSSH2=OFF CURL_CA_PATH=none)
hunter_config(Boost VERSION 1.66.0)

hunter_config(ethash VERSION 1.0.0
    URL https://github.com/RavenCommunity/cpp-kawpow/archive/1.0.0.tar.gz
    SHA1 c7e925644fcd4ddff4ae0c29b42d9ef0248e70fc
)