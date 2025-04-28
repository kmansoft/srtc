cmake_minimum_required(VERSION 3.22.1)

project("srtc")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)

include(ExternalProject)
include(FetchContent)

# srtc

add_library(srtc
        include/srtc/byte_buffer.h
        include/srtc/error.h
        include/srtc/event_loop.h
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
        include/srtc/replay_protection.h
        include/srtc/rtcp_packet.h
        include/srtc/rtcp_packet_source.h
        include/srtc/rtp_extension.h
        include/srtc/rtp_extension_builder.h
        include/srtc/rtp_packet.h
        include/srtc/rtp_packet_source.h
        include/srtc/rtp_std_extensions.h
        include/srtc/rtp_time_source.h
        include/srtc/scheduler.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/send_history.h
        include/srtc/simulcast_layer.h
        include/srtc/socket.h
        include/srtc/srtc.h
        include/srtc/srtp_connection.h
        include/srtc/srtp_crypto.h
        include/srtc/srtp_hmac_sha1.h
        include/srtc/srtp_openssl.h
        include/srtc/srtp_util.h
        include/srtc/track.h
        include/srtc/track_stats.h
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
        replay_protection.cpp
        replay_protection.cpp
        rtcp_packet.cpp
        rtcp_packet_source.cpp
        rtp_extension.cpp
        rtp_extension_builder.cpp
        rtp_packet.cpp
        rtp_packet_source.cpp
        rtp_std_extensions.cpp
        rtp_time_source.cpp
        sdp_answer.cpp
        sdp_offer.cpp
        scheduler.cpp
        send_history.cpp
        socket.cpp
        srtc.cpp
        srtp_connection.cpp
        srtp_crypto.cpp
        srtp_hmac_sha1.cpp
        srtp_openssl.cpp
        srtp_util.cpp
        track.cpp
        track_stats.cpp
        util.cpp
        x509_certificate.cpp
        x509_hash.cpp)

if(APPLE)
	target_sources(srtc
        PRIVATE
        include/srtc/event_loop_linux.h
        event_loop_macos.cpp
	)
elseif(UNIX)
    target_sources(srtc
        PRIVATE
        include/srtc/event_loop_linux.h
        event_loop_linux.cpp
    )
endif()

target_include_directories(srtc PUBLIC "./include")

#target_compile_options(srtc
#    PRIVATE
#    "-Wno-deprecated-declarations"
#)

target_link_libraries(
        srtc
        PRIVATE
        stun
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

# STUN

add_subdirectory(stun)

add_dependencies(srtc stun)

target_include_directories(srtc PRIVATE
        "stun")

target_link_directories(srtc PUBLIC
        "${CMAKE_BINARY_DIR}/stun")

# Google Test

FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# LibSRTP - only for tests

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
        GIT_TAG "76f23aa78124381a9681b7b1a14c52c6cd40ea21"
        SOURCE_DIR "external/libsrtp"
        INSTALL_COMMAND ""
        UPDATE_DISCONNECTED ON
        CMAKE_ARGS ${LIBSRTP_CMAKE_ARGS}
)

set(SRTP_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/libsrtp/include")
set(SRTP_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/srtp-prefix/src/srtp-build")

# Google test executable

add_executable(
        srtc_test
        EXCLUDE_FROM_ALL
        test_google_vla.cpp
        test_replay_protection.cpp
        test_srtp_key_derivation.cpp
        test_srtp_crypto.cpp
)

target_include_directories(
        srtc_test
        PRIVATE
        "${SRTP_INCLUDE_DIR}"
        "${OPENSSL_INCLUDE_DIR}"
)

target_link_directories(
        srtc_test
        PRIVATE
        "${SRTP_LIBRARY_DIR}"
)

add_dependencies(srtc_test srtc srtp)

target_link_libraries(
        srtc_test
        GTest::gtest_main
        srtc
        srtp3
        OpenSSL::Crypto
        OpenSSL::SSL
)

if(NOT CMAKE_CROSSCOMPILING)
    include(GoogleTest)
    gtest_discover_tests(srtc_test)
endif()

if(UNIX AND NOT ANDROID)
    # Command line demo

    add_executable(cmdline_demo
            cmdline_demo.cpp
            )

    add_dependencies(cmdline_demo srtc)

    find_package(CURL REQUIRED)

    target_include_directories(cmdline_demo
            PRIVATE
            ${CURL_INCLUDE_DIRS}
            )

    target_link_libraries(
            cmdline_demo
            PRIVATE
            srtc
            CURL::libcurl
    )
endif()
