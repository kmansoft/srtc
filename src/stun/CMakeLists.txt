cmake_minimum_required(VERSION 3.22.1)

project("stun")

add_library(srtc_stun STATIC
        constants.h
        debug.c
        debug.h
        rand.c
        rand.h
        readme.txt
        stun5389.c
        stun5389.h
        stuncrc32.c
        stuncrc32.h
        stunhmac.c
        stunhmac.h
        stunmessage.c
        stunmessage.h
        utils.c
        utils.h
        win32_common.h
)

target_compile_definitions(srtc_stun
    PRIVATE
    HAVE_OPENSSL=1
)

if(UNIX)
    target_compile_options(srtc_stun
        PRIVATE
        "-ffunction-sections")
endif()

find_package(OpenSSL REQUIRED)

target_include_directories(srtc_stun
    PRIVATE
    ${OPENSSL_INCLUDE_DIR}
)
