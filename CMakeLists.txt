cmake_minimum_required(VERSION 3.22.1)

include(ExternalProject)
include(FetchContent)

project("srtc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(srtc SHARED
        include/srtc/byte_buffer.h
        include/srtc/error.h
        include/srtc/extension_map.h
        include/srtc/peer_connection.h
        include/srtc/random_generator.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/srtc.h
        include/srtc/track.h
        include/srtc/x509_certificate.h
        byte_buffer.cpp
        error.cpp
        extension_map.cpp
        peer_connection.cpp
        random_generator.cpp
        sdp_answer.cpp
        sdp_offer.cpp
        srtc.cpp
        track.cpp
        x509_certificate.cpp
)

target_include_directories(srtc PUBLIC "./include")

target_compile_options(srtc PRIVATE
        "-Wthread-safety")
target_compile_definitions(srtc PRIVATE
        "_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS")

target_include_directories(srtc PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/external/boringssl/install/include")

target_link_libraries(
        srtc
        PRIVATE
        ssl
        crypto
        log
)

target_link_directories(srtc PUBLIC
        "${CMAKE_CURRENT_BINARY_DIR}/external/boringssl/install/lib")

ExternalProject_Add(
        boringssl
        GIT_REPOSITORY "https://github.com/google/boringssl.git"
        GIT_TAG "origin/master"
        SOURCE_DIR "external/boringssl"
        CMAKE_ARGS -DOPENSSL_SMALL=TRUE -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DANDROID_ABI=${ANDROID_ABI} -DANDROID_PLATFORM=android-29
)

add_dependencies(srtc boringssl)
