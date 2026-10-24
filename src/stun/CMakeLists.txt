cmake_minimum_required(VERSION 3.22.1)

project("stun")

add_library(srtc_stun STATIC
        constants.h
        debug.cpp
        debug.h
        rand.cpp
        rand.h
        readme.txt
        stun5389.cpp
        stun5389.h
        stuncrc32.cpp
        stuncrc32.h
        stunhmac.cpp
        stunhmac.h
        stunmessage.cpp
        stunmessage.h
        utils.cpp
        utils.h
        win32_common.h
)

if(UNIX)
    target_compile_options(srtc_stun
        PRIVATE
        "-ffunction-sections")
endif()

# Same crypto library as the rest of srtc, the parent has already found it
if(SRTC_CRYPTO_LIB STREQUAL "MBEDTLS")
    target_compile_definitions(srtc_stun
        PRIVATE
        HAVE_MBEDTLS=1
    )
    target_link_libraries(srtc_stun
        PRIVATE
        MbedTLS::mbedcrypto
    )
else()
    target_compile_definitions(srtc_stun
        PRIVATE
        HAVE_OPENSSL=1
    )
    find_package(OpenSSL REQUIRED)
    target_include_directories(srtc_stun
        PRIVATE
        ${OPENSSL_INCLUDE_DIR}
    )
    target_link_libraries(srtc_stun
        PRIVATE
        OpenSSL::Crypto
    )
endif()
