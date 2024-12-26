cmake_minimum_required(VERSION 3.22.1)

project("srtc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(srtc STATIC
        include/srtc/error.h
        include/srtc/extension_map.h
        include/srtc/peer_connection.h
        include/srtc/random_generator.h
        include/srtc/sdp_offer.h
        include/srtc/sdp_answer.h
        include/srtc/srtc.h
        include/srtc/track.h
        error.cpp
        extension_map.cpp
        peer_connection.cpp
        random_generator.cpp
        sdp_answer.cpp
        sdp_offer.cpp
        srtc.cpp
        track.cpp
)

target_include_directories(srtc PUBLIC "./include")

target_compile_options(srtc PRIVATE
        "-Wthread-safety")
target_compile_definitions(srtc PRIVATE
        "_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS")
