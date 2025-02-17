cmake_minimum_required(VERSION 3.22.1)

project("srtc")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)

include(ExternalProject)

# srtc

add_library(srtc
        include/srtc/byte_buffer.h
        include/srtc/error.h
        include/srtc/extension_map.h
        include/srtc/h264.h
        include/srtc/ice_agent.h
        include/srtc/logging.h
        include/srtc/optional.h
        include/srtc/packetizer.h
        include/srtc/packetizer_h264.h
        include/srtc/packetizer_opus.h
        include/srtc/peer_candidate.h
        include/srtc/peer_candidate_listener.h
        include/srtc/peer_connection.h
        include/srtc/random_generator.h
        include/srtc/rtp_packet.h
        include/srtc/rtp_packet_source.h
        include/srtc/scheduler.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/send_history.h
        include/srtc/socket.h
        include/srtc/srtc.h
        include/srtc/srtp_connection.h
        include/srtc/track.h
        include/srtc/util.h
        include/srtc/x509_certificate.h
        include/srtc/x509_hash.h
        byte_buffer.cpp
        error.cpp
        extension_map.cpp
        h264.cpp
        ice_agent.cpp
        logging.cpp
        packetizer.cpp
        packetizer_h264.cpp
        packetizer_opus.cpp
        peer_candidate.cpp
        peer_connection.cpp
        random_generator.cpp
        rtp_packet.cpp
        rtp_packet_source.cpp
        sdp_answer.cpp
        sdp_offer.cpp
        scheduler.cpp
        send_history.cpp
        socket.cpp
        srtc.cpp
        srtp_connection.cpp
        track.cpp
        util.cpp
        x509_certificate.cpp
        x509_hash.cpp)

target_include_directories(srtc PUBLIC "./include")

target_link_libraries(
        srtc
        PRIVATE
        stun
        srtp3
)

# Android specific

if(ANDROID)
    target_compile_options(srtc PRIVATE
            "-Wthread-safety")
    target_compile_definitions(srtc PRIVATE
            "_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS")

    target_link_libraries(srtc
            PRIVATE
            log
            )
endif()


# OpenSSL

find_package(OpenSSL REQUIRED)

target_include_directories(srtc
        PRIVATE
        ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(srtc
        PRIVATE
        OpenSSL::Crypto
        OpenSSL::SSL
)

# LibSRTP

list(APPEND LIBSRTP_CMAKE_ARGS
        "-DLIBSRTP_TEST_APPS=OFF"
        "-DENABLE_OPENSSL=ON"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
)

if(ANDROID)
    list(APPEND LIBSRTP_CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=android-29
        -DCMAKE_FIND_ROOT_PATH=${OPENSSL_ROOT_DIR}
    )
endif()

ExternalProject_Add(
        srtp
        GIT_REPOSITORY "https://github.com/cisco/libsrtp.git"
        GIT_TAG "cb9e8920e42fd31c5fab9a385e69e3dfcbc7f3be"
        GIT_SHALLOW ON
        SOURCE_DIR "external/libsrtp"
        INSTALL_COMMAND ""
        UPDATE_DISCONNECTED ON
        CMAKE_ARGS ${LIBSRTP_CMAKE_ARGS}
)

target_include_directories(srtc PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/external/libsrtp/include")

target_link_directories(srtc PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/srtp-prefix/src/srtp-build")

add_dependencies(srtc srtp)

# STUN

add_subdirectory(stun)

add_dependencies(srtc stun)

target_include_directories(srtc PRIVATE
        "stun")

target_link_directories(srtc PUBLIC
        "${CMAKE_BINARY_DIR}/srtc/stun")
