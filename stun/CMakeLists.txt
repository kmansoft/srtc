cmake_minimum_required(VERSION 3.22.1)

project("stun")

add_library(stun STATIC
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

target_compile_definitions(stun
    PRIVATE
    HAVE_OPENSSL=1
)

target_include_directories(stun PRIVATE
        "${CMAKE_BINARY_DIR}/srtc/external/boringssl/install/include")

add_dependencies(stun boringssl)
