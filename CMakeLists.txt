cmake_minimum_required(VERSION 3.22.1)

include(ExternalProject)

project("srtc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(srtc SHARED
        include/srtc/byte_buffer.h
        include/srtc/error.h
        include/srtc/extension_map.h
        include/srtc/logging.h
        include/srtc/peer_connection.h
        include/srtc/random_generator.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/scheduler.h
        include/srtc/srtc.h
        include/srtc/track.h
        include/srtc/util.h
        include/srtc/x509_certificate.h
        include/srtc/x509_hash.h
        byte_buffer.cpp
        error.cpp
        extension_map.cpp
        logging.cpp
        peer_connection.cpp
        random_generator.cpp
        sdp_answer.cpp
        sdp_offer.cpp
        scheduler.cpp
        srtc.cpp
        track.cpp
        util.cpp
        x509_certificate.cpp
        x509_hash.cpp
)

target_include_directories(srtc PUBLIC "./include")

target_compile_options(srtc PRIVATE
        "-Wthread-safety")
target_compile_definitions(srtc PRIVATE
        "_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS")

target_link_libraries(
        srtc
        PRIVATE
        ssl
        crypto
        nice
        srtp2
        log
)

# BoringSSL

ExternalProject_Add(
        boringssl
        GIT_REPOSITORY "https://github.com/google/boringssl.git"
        GIT_TAG "origin/master"
        SOURCE_DIR "external/boringssl"
        CMAKE_ARGS -DOPENSSL_SMALL=TRUE -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DANDROID_ABI=${ANDROID_ABI} -DANDROID_PLATFORM=android-29
)

target_include_directories(srtc PRIVATE
        "${CMAKE_BINARY_DIR}/srtc/external/boringssl/install/include")

target_link_directories(srtc PUBLIC
        "${CMAKE_BINARY_DIR}/srtc/external/boringssl/install/lib")

add_dependencies(srtc boringssl)

# LibSRTP

ExternalProject_Add(
        srtp
        GIT_REPOSITORY "https://github.com/cisco/libsrtp.git"
        GIT_TAG "v2.6.0"
        SOURCE_DIR "external/libsrtp"
        INSTALL_COMMAND ""
        CMAKE_ARGS -DLIBSRTP_TEST_APPS=OFF -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DANDROID_ABI=${ANDROID_ABI} -DANDROID_PLATFORM=android-29
)

target_include_directories(srtc PRIVATE
        "${CMAKE_BINARY_DIR}/srtc/external/libsrtp/include")

target_link_directories(srtc PUBLIC
        "${CMAKE_BINARY_DIR}/srtc/srtp-prefix/src/srtp-build/")

add_dependencies(srtc srtp)

# STUN

add_subdirectory(stun)

add_dependencies(nice boringssl)
add_dependencies(srtc nice)

target_include_directories(srtc PRIVATE
    "stun")

target_link_directories(srtc PUBLIC
        "${CMAKE_BINARY_DIR}/srtc/stun")
