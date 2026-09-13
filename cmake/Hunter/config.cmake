hunter_config(CURL VERSION ${HUNTER_CURL_VERSION} CMAKE_ARGS HTTP_ONLY=ON CMAKE_USE_OPENSSL=ON CMAKE_USE_LIBSSH2=OFF CURL_CA_PATH=none)
hunter_config(
    Boost
    URL "https://archives.boost.io/release/1.76.0/source/boost_1_76_0.tar.bz2"
    SHA1 "8064156508312dde1d834fec3dca9b11006555b6"
)

#hunter_config(
#    ethash VERSION 1.0.0
#    URL https://github.com/RavenCommunity/cpp-kawpow/archive/1.1.0.tar.gz
#    SHA1 fff78f555a43900b6726c131305a71be769ef769
#)

hunter_config(
    intx VERSION 0.5.1
    URL https://github.com/chfast/intx/archive/v0.5.1.tar.gz
    SHA1 743c46a82750143bd302a4394b7008a2112fc97b
)
