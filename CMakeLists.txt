cmake_minimum_required(VERSION 3.22.1)

project("srtc")

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif ()

option(SRTC_BUILD_TESTS "Build tests with Google Test" ON)
option(SRTC_BUILD_TOOLS "Build the command line tools" ON)
option(SRTC_BUILD_TOOL_PUBLISH "Build the publish command line tool" ON)
option(SRTC_BUILD_TOOL_SUBSCRIBE "Build the subscribe command line tool" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED YES)

include(ExternalProject)
include(FetchContent)

# Static link runtime on Windows, unless building tests (link errors)

if (WIN32 AND NOT SRTC_BUILD_TESTS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# The main srtc library

add_library(srtc
        # Headers
        include/srtc/byte_buffer.h
        include/srtc/depacketizer.h
        include/srtc/depacketizer_av1.h
        include/srtc/depacketizer_h264.h
        include/srtc/depacketizer_h265.h
        include/srtc/depacketizer_opus.h
        include/srtc/depacketizer_video.h
        include/srtc/depacketizer_vp8.h
        include/srtc/error.h
        include/srtc/event_loop.h
        include/srtc/extension_map.h
        include/srtc/extended_value.h
        include/srtc/codec_av1.h
        include/srtc/codec_h264.h
        include/srtc/codec_h265.h
        include/srtc/ice_agent.h
        include/srtc/jitter_buffer_item.h
        include/srtc/jitter_buffer.h
        include/srtc/logging.h
        include/srtc/packetizer.h
        include/srtc/packetizer_av1.h
        include/srtc/packetizer_h264.h
        include/srtc/packetizer_h265.h
        include/srtc/packetizer_opus.h
        include/srtc/packetizer_video.h
        include/srtc/packetizer_vp8.h
        include/srtc/peer_candidate.h
        include/srtc/peer_candidate_listener.h
        include/srtc/peer_connection.h
        include/srtc/pool_allocator.h
        include/srtc/random_generator.h
        include/srtc/replay_protection.h
        include/srtc/rtcp_packet.h
        include/srtc/rtcp_packet_source.h
        include/srtc/rtp_extension.h
        include/srtc/rtp_extension_builder.h
        include/srtc/rtp_extension_source_simulcast.h
        include/srtc/rtp_extension_source_twcc.h
        include/srtc/rtp_extension_source.h
        include/srtc/rtp_packet.h
        include/srtc/rtp_packet_source.h
        include/srtc/rtp_responder_twcc.h
        include/srtc/rtp_std_extensions.h
        include/srtc/rtp_time_source.h
        include/srtc/scheduler.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/sender_report.h
        include/srtc/sender_reports_history.h
        include/srtc/send_rtp_history.h
        include/srtc/send_pacer.h
        include/srtc/simulcast_layer.h
        include/srtc/socket.h
        include/srtc/srtc.h
        include/srtc/srtp_connection.h
        include/srtc/srtp_crypto.h
        include/srtc/srtp_hmac_sha1.h
        include/srtc/srtp_openssl.h
        include/srtc/srtp_util.h
        include/srtc/track.h
        include/srtc/track_selector.h
        include/srtc/track_stats.h
        include/srtc/twcc_common.h
        include/srtc/twcc_publish.h
        include/srtc/twcc_subscribe.h
        include/srtc/util.h
        include/srtc/x509_certificate.h
        include/srtc/x509_hash.h
        # Sources
        src/byte_buffer.cpp
        src/codec_av1.cpp
        src/codec_h264.cpp
        src/codec_h265.cpp
        src/depacketizer.cpp
        src/depacketizer_av1.cpp
        src/depacketizer_h264.cpp
        src/depacketizer_h265.cpp
        src/depacketizer_opus.cpp
        src/depacketizer_video.cpp
        src/depacketizer_vp8.cpp
        src/error.cpp
        src/extension_map.cpp
        src/extended_value.cpp
        src/ice_agent.cpp
        src/jitter_buffer.cpp
        src/logging.cpp
        src/packetizer.cpp
        src/packetizer_av1.cpp
        src/packetizer_h264.cpp
        src/packetizer_h265.cpp
        src/packetizer_opus.cpp
        src/packetizer_video.cpp
        src/packetizer_vp8.cpp
        src/peer_candidate.cpp
        src/peer_connection.cpp
        src/pool_allocator.cpp
        src/random_generator.cpp
        src/replay_protection.cpp
        src/replay_protection.cpp
        src/rtcp_packet.cpp
        src/rtcp_packet_source.cpp
        src/rtp_extension.cpp
        src/rtp_extension_builder.cpp
        src/rtp_extension_source_simulcast.cpp
        src/rtp_extension_source_twcc.cpp
        src/rtp_extension_source.cpp
        src/rtp_packet.cpp
        src/rtp_packet_source.cpp
        src/rtp_responder_twcc.cpp
        src/rtp_std_extensions.cpp
        src/rtp_time_source.cpp
        src/sdp_answer.cpp
        src/sdp_offer.cpp
        src/scheduler.cpp
        src/sender_reports_history.cpp
        src/send_rtp_history.cpp
        src/send_pacer.cpp
        src/simulcast_layer.cpp
        src/socket.cpp
        src/srtc.cpp
        src/srtp_connection.cpp
        src/srtp_crypto.cpp
        src/srtp_hmac_sha1.cpp
        src/srtp_openssl.cpp
        src/srtp_util.cpp
        src/track.cpp
        src/track_selector.cpp
        src/track_stats.cpp
        src/twcc_publish.cpp
        src/twcc_subscribe.cpp
        src/util.cpp
        src/x509_certificate.cpp
        src/x509_hash.cpp
)

if (APPLE)
    target_sources(srtc
            PRIVATE
            include/srtc/event_loop_macos.h
            src/event_loop_macos.cpp
    )
elseif (WIN32)
    target_sources(srtc
            PRIVATE
            include/srtc/event_loop_win.h
            src/event_loop_win.cpp
    )
elseif (UNIX)
    target_sources(srtc
            PRIVATE
            include/srtc/event_loop_linux.h
            src/event_loop_linux.cpp
    )
endif ()

target_include_directories(srtc PUBLIC "./include")

if (UNIX)
    target_compile_options(srtc
            PRIVATE
            "-ffunction-sections"
            "-Wall"
            "-Werror"
    )
endif ()
if (WIN32)
    target_compile_options(srtc
            PRIVATE
            "-DNOMINMAX"
    )
endif ()

if (ANDROID)
    target_compile_options(srtc PRIVATE
            "-Wthread-safety")
    target_compile_definitions(srtc PRIVATE
            "_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS")

    target_link_libraries(srtc
            PRIVATE
            log
    )
endif ()

target_link_libraries(
        srtc
        PRIVATE
        srtc_stun
)

# OpenSSL

if (WIN32)
    # OpenSSL does not provide binary releases, and building from source takes about 10 minutes -
    # so we build it ourselves somewhere else and upload to a server, then fetch from there.
    set(OPENSSL_USE_STATIC_LIBS ON)
    set(OPENSSL_VERSION "3.5.0")
    set(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/openssl-bin-${OPENSSL_VERSION}")

    if (NOT EXISTS "${OPENSSL_ROOT_DIR}")
        message(STATUS "Downloading OpenSSL binaries...")
        set(OPENSSL_ZIP_FILE "${CMAKE_BINARY_DIR}/openssl-bin-${OPENSSL_VERSION}.zip")
        file(DOWNLOAD "https://kman.mobi/download/openssl-bin-${OPENSSL_VERSION}.zip" "${OPENSSL_ZIP_FILE}")
        file(ARCHIVE_EXTRACT INPUT "${OPENSSL_ZIP_FILE}" DESTINATION "${OPENSSL_ROOT_DIR}")
    endif ()
endif ()
if (APPLE)
    # OpenSSL comes from Homebrew which users may not have installed, so we want static linking
    set(OPENSSL_USE_STATIC_LIBS ON)
endif ()

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

add_subdirectory(src/stun)

add_dependencies(srtc srtc_stun)

target_include_directories(srtc PRIVATE
        src/stun
)

target_link_directories(srtc PRIVATE
        "${CMAKE_BINARY_DIR}/src/stun"
)

# Tests - only when not cross-compiling e.g. Android, and when enabled

if (NOT CMAKE_CROSSCOMPILING AND SRTC_BUILD_TESTS)

    # Google Test

    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/04ee1b4f2aefdffb0135d7cf2a2c519fe50dabe4.zip
    )

    set(INSTALL_GTEST OFF)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)

    # LibSRTP - only for tests

    list(APPEND LIBSRTP_CMAKE_ARGS
            "-DLIBSRTP_TEST_APPS=OFF"
            "-DENABLE_OPENSSL=ON"
            "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    )

    if (WIN32)
        list(APPEND LIBSRTP_CMAKE_ARGS
                -DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}
        )
    endif ()
    if (ANDROID)
        list(APPEND LIBSRTP_CMAKE_ARGS
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                -DANDROID_ABI=${ANDROID_ABI}
                -DANDROID_PLATFORM=android-29
                -DCMAKE_FIND_ROOT_PATH=${OPENSSL_ROOT_DIR}
        )
    endif ()

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
            test/test_google_vla.cpp
            test/test_extended_value.cpp
            test/test_replay_protection.cpp
            test/test_rtp_packet.cpp
            test/test_srtp_key_derivation.cpp
            test/test_srtp_crypto.cpp
            test/test_util.cpp
            test/test_allocator.cpp
            test/test_subscribe_twcc.cpp
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

    if (WIN32)
        target_link_libraries(
                srtc_test
                ws2_32
        )
    endif ()

    include(GoogleTest)
    gtest_discover_tests(srtc_test)
endif ()

# Tools

if (NOT CMAKE_CROSSCOMPILING AND SRTC_BUILD_TOOLS)

    # Publish tool

    if (SRTC_BUILD_TOOL_PUBLISH)
        add_executable(srtc_publish
                tools/srtc_publish.cpp
                tools/http_whip_whep.h
                tools/media_reader.h
                tools/media_reader.cpp
                tools/media_reader_av1.h
                tools/media_reader_av1.cpp
                tools/media_reader_h264.h
                tools/media_reader_h264.cpp
                tools/media_reader_h265.h
                tools/media_reader_h265.cpp
                tools/media_reader_vp8.h
                tools/media_reader_vp8.cpp
                tools/media_reader_webm.h
                tools/media_reader_webm.cpp
                tools/media_reader_webm.cpp
        )

        add_dependencies(srtc_publish srtc)

        if (WIN32)
            target_sources(srtc_publish
                    PRIVATE
                    tools/http_whip_whep_win.cpp
            )

            target_link_libraries(
                    srtc_publish
                    PRIVATE
                    srtc
                    winmm
            )
        else ()
            find_package(CURL REQUIRED)

            target_sources(srtc_publish
                    PRIVATE
                    tools/http_whip_whep_curl.cpp
            )

            target_include_directories(srtc_publish
                    PRIVATE
                    ${CURL_INCLUDE_DIRS}
            )

            target_link_libraries(
                    srtc_publish
                    PRIVATE
                    srtc
                    CURL::libcurl
            )
        endif ()
    endif ()

    # Subscribe tool

    if (SRTC_BUILD_TOOL_SUBSCRIBE)
        add_executable(srtc_subscribe
                tools/srtc_subscribe.cpp
                tools/http_whip_whep.h
                tools/media_writer.h
                tools/media_writer.cpp
                tools/media_writer_h26x.h
                tools/media_writer_h26x.cpp
                tools/media_writer_ogg.h
                tools/media_writer_ogg.cpp
                tools/media_writer_vp8.h
                tools/media_writer_vp8.cpp
                tools/media_writer_webm.h
                tools/media_writer_webm.cpp
        )

        add_subdirectory(tools/libogg)

        add_dependencies(srtc_subscribe srtc ogg)

        target_link_libraries(srtc_subscribe PRIVATE ogg)

        if (WIN32)
            target_sources(srtc_subscribe
                    PRIVATE
                    tools/http_whip_whep_win.cpp
            )

            target_link_libraries(
                    srtc_subscribe
                    PRIVATE
                    srtc
                    winmm
            )
        else ()
            find_package(CURL REQUIRED)

            target_sources(srtc_subscribe
                    PRIVATE
                    tools/http_whip_whep_curl.cpp
            )

            target_include_directories(srtc_subscribe
                    PRIVATE
                    ${CURL_INCLUDE_DIRS}
            )

            target_link_libraries(
                    srtc_subscribe
                    PRIVATE
                    srtc
                    CURL::libcurl
            )
        endif ()
    endif ()

endif ()

# Install

file(GLOB SRTC_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include/srtc/*.h")

install(FILES
        ${SRTC_INCLUDES}
        DESTINATION include/srtc
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libsrtc.a
        ${CMAKE_CURRENT_BINARY_DIR}/stun/libsrtc_stun.a
        DESTINATION lib
)
