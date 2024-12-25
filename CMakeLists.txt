cmake_minimum_required(VERSION 3.22.1)

project("srtc")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

add_library(srtc STATIC
        include/srtc/error.h
        include/srtc/peer_connection.h
        include/srtc/random_generator.h
        include/srtc/sdp_offer.h
        include/srtc/srtc.h
        error.cpp
        peer_connection.cpp
        random_generator.cpp
        sdp_offer.cpp
        srtc.cpp
)

target_include_directories(srtc PUBLIC "./include")
