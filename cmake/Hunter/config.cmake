hunter_config(CURL VERSION ${HUNTER_CURL_VERSION} CMAKE_ARGS HTTP_ONLY=ON CMAKE_USE_OPENSSL=ON CMAKE_USE_LIBSSH2=OFF CURL_CA_PATH=none)
hunter_config(
    OpenSSL VERSION 3.5.8
    URL "https://github.com/openssl/openssl/releases/download/openssl-3.5.8/openssl-3.5.8.tar.gz"
    SHA1 "aff8430711a8724804c7ef39232732a1b17c5cf4"
)

hunter_config(
    jsoncpp VERSION 1.9.7
    URL "https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/1.9.7.tar.gz"
    SHA1 "f5649ee5ff302dfcfd54c24b4580fcb50ba244a1"
    CMAKE_ARGS BUILD_SHARED_LIBS=OFF BUILD_STATIC_LIBS=ON BUILD_OBJECT_LIBS=OFF
)

hunter_config(
    CLI11 VERSION 2.6.2
    URL "https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.6.2.tar.gz"
    SHA1 "d43c249c47f280f72540dea74a326171a7bfac6f"
)

hunter_config(
    intx VERSION 0.5.1
    URL https://github.com/chfast/intx/archive/v0.5.1.tar.gz
    SHA1 743c46a82750143bd302a4394b7008a2112fc97b
)
